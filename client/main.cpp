#include <iostream>
#include <arpa/inet.h>
#include <sstream>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <csignal>
#include <unistd.h>

#include "request.hpp"
#include "discovery.hpp"
#include "util.hpp"

// Variáveis globais atômicas para failover
std::atomic<uint32_t> server_ip_atomic{0};
std::atomic<uint16_t> server_port_atomic{0};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <server_port>\n";
        return EXIT_FAILURE;
    }

    int server_port = atoi(argv[1]);
    uint16_t listening_port = 0;  // porta dinâmica do cliente

    // ---------------- Thread de escuta de failover ----------------
    std::atomic<bool> listening{true};
    std::thread listen_thread([&]() {
        int listen_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (listen_sock < 0) { 
            std::cerr << "Erro ao criar socket de escuta\n"; 
            return; 
        }

        sockaddr_in localAddr{};
        localAddr.sin_family = AF_INET;
        localAddr.sin_port = htons(0); // porta livre
        localAddr.sin_addr.s_addr = INADDR_ANY;

        if (bind(listen_sock, (sockaddr*)&localAddr, sizeof(localAddr)) < 0) {
            std::cerr << "Erro ao bindar socket de escuta\n";
            close(listen_sock);
            return;
        }

        socklen_t addr_len = sizeof(localAddr);
        getsockname(listen_sock, (sockaddr*)&localAddr, &addr_len);
        listening_port = ntohs(localAddr.sin_port);
        std::cout << "[CLIENT] Socket de escuta criado na porta " << listening_port << std::endl;

        while (listening.load()) {
            packet_t pacote{};
            sockaddr_in from{};
            socklen_t fromlen = sizeof(from);
            ssize_t bytes = recvfrom(listen_sock, &pacote, sizeof(pacote), 0,
                                     (sockaddr*)&from, &fromlen);
            if (bytes < (ssize_t)sizeof(packet_t)) continue;

            if (pacote.type == PACKET_TYPE_PRIMARY_ANNOUNCE) {
                char new_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &from.sin_addr, new_ip, INET_ADDRSTRLEN);
                uint16_t new_port = pacote.data.primary_announce.server_port;

                server_ip_atomic.store(ip_from_string(new_ip), std::memory_order_relaxed);
                server_port_atomic.store(new_port, std::memory_order_relaxed);

                std::cout << "[CLIENT] Novo PRIMARY anunciado: "
                          << new_ip << ":" << new_port << std::endl;
            }
        }

        close(listen_sock);
    });

    // ---------------- Descoberta do servidor ----------------
    uint32_t discovered_ip = discover_server(server_port, listening_port);
    if (discovered_ip == 0) {
        std::cout << timestamp() << " No server found in UDP port " << server_port << "\n";
        listening.store(false);
        listen_thread.join();
        return EXIT_FAILURE;
    }
    server_ip_atomic.store(discovered_ip, std::memory_order_relaxed);
    server_port_atomic.store(server_port, std::memory_order_relaxed);

    char server_ip_str[INET_ADDRSTRLEN];
    uint32_t server_ip_network = server_ip_atomic.load();
    inet_ntop(AF_INET, &server_ip_network, server_ip_str, INET_ADDRSTRLEN);
    std::cout << timestamp() << " PRIMARY encontrado em " << server_ip_str 
              << ":" << server_port << std::endl;

    int request_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (request_socket < 0) {
        std::cerr << timestamp() << " Failed to create request socket.\n";
        listening.store(false);
        listen_thread.join();
        return EXIT_FAILURE;
    }

    std::queue<request> request_queue;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // ---------------- Thread de input do usuário ----------------
    std::thread input_thread([&]() {
        std::string line;
        while (running.load() && std::getline(std::cin, line)) {
            if (line.empty()) continue;

            std::istringstream iss(line);
            std::string ip_str, value_str;
            if (!(iss >> ip_str >> value_str)) {
                std::cerr << timestamp() << " Invalid input format. Use: <destination_ip> <value>\n";
                continue;
            }

            request req { ip_from_string(ip_str), static_cast<uint32_t>(std::stoul(value_str)) };
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                request_queue.push(req);
            }
            queue_cv.notify_one();
        }
    });
    
    // ---------------- Thread de envio de requests ----------------
    std::thread request_thread([&]() {
        while (running.load() || !request_queue.empty()) {
            request req;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                queue_cv.wait(lock, [&]() { return !request_queue.empty() || !running.load(); });

                if (!running.load() && request_queue.empty()) break;

                req = request_queue.front();
                request_queue.pop();
            }

            // pega IP/porta atuais do PRIMARY
            uint32_t current_ip = server_ip_atomic.load(std::memory_order_relaxed);
            uint16_t current_port = server_port_atomic.load(std::memory_order_relaxed);

            request_ack ack{};
            if (!send_request_with_retry(request_socket, current_ip, current_port, req, ack)) {
                std::cerr << timestamp() << " Request failed, recolocando na fila\n";
                std::lock_guard<std::mutex> lock(queue_mutex);
                request_queue.push(req);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } else {
                char dest_ip_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &req.dest_addr, dest_ip_str, INET_ADDRSTRLEN);

                char server_ip_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &current_ip, server_ip_str, INET_ADDRSTRLEN);

                std::cout << timestamp() << " server " << server_ip_str
                        << " id_req " << ack.seqn 
                        << " dest " << dest_ip_str 
                        << " value " << req.value
                        << " new_balance " << ack.new_balance << std::endl;
            }
        }
    });


    // ---------------- Join threads e cleanup ----------------
    input_thread.join();
    running.store(false);
    queue_cv.notify_all();
    request_thread.join();

    listening.store(false);
    listen_thread.join();

    uint32_t final_ip = server_ip_atomic.load(std::memory_order_relaxed);
    uint16_t final_port = server_port_atomic.load(std::memory_order_relaxed);
    send_exit_request(request_socket, final_ip, final_port);
    close(request_socket);

    return 0;
}
