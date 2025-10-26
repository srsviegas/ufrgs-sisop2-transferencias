#include <iostream>
#include <arpa/inet.h>
#include <sstream>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "request.hpp"
#include "discovery.hpp"
#include "util.hpp"


int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <server_port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int server_port = atoi(argv[1]);

    uint32_t server_ip = discover_server(server_port);
    char server_ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &server_ip, server_ip_str, INET_ADDRSTRLEN);

    if (server_ip == 0) {
        printf("%s No server found in UDP port %d.\n", timestamp().c_str(), server_port);
        return EXIT_FAILURE;
    }

    int request_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (request_socket < 0) {
        fprintf(stderr, "%s Failed to create request socket.\n", timestamp().c_str());
        return EXIT_FAILURE;
    }

    std::queue<request> request_queue;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::thread input_thread([&]() {
        while (running.load()) {
            std::string line;
            if (!std::getline(std::cin, line)) {
                running.store(false);
                queue_cv.notify_all();
                break;
            }

            if (line.empty()) {
                continue;
            }

            std::istringstream iss(line);
            std::string ip_str;
            std::string value_str;

            if (!(iss >> ip_str >> value_str)) {
                std::cerr << timestamp() << " Invalid input format. Use: <destination_ip> <value>" << std::endl;
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

    std::thread request_thread([&]() {
        while (running.load()) {
            request req;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                queue_cv.wait(lock, [&]() { return !request_queue.empty() || !running.load(); });

                if (!running.load() && request_queue.empty()) {
                    break;
                }

                req = request_queue.front();
                request_queue.pop();
            }

            request_ack ack{};
            if (send_request_with_retry(request_socket, server_ip, server_port, req, ack)) {
                char dest_ip_str[INET_ADDRSTRLEN];
                uint32_t dest_ip_network = htonl(req.dest_addr);
                inet_ntop(AF_INET, &dest_ip_network, dest_ip_str, INET_ADDRSTRLEN);

                printf("%s server %s id_req %u dest %s value %u new_balance %u\n", 
                    timestamp().c_str(), 
                    server_ip_str, 
                    ack.seqn, 
                    dest_ip_str, 
                    req.value, 
                    ack.new_balance);
            } else {
                std::cerr << timestamp() << " Request to server " << server_ip_str 
                          << " for destination " << req.dest_addr << " failed after "
                          << REQUEST_MAX_RETRIES << " attempts." << std::endl;
            }
        }
    });

    input_thread.join();
    running.store(false);
    queue_cv.notify_all();
    request_thread.join();
    close(request_socket);

    return 0;
}