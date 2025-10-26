#include <iostream>
#include <arpa/inet.h>
#include <sstream>

#include "request.hpp"
#include "discovery.hpp"
#include "util.hpp"


int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <UDP_port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int udp_port = atoi(argv[1]);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    uint32_t server_ip = discover_server(udp_port);
    char server_ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &server_ip, server_ip_str, INET_ADDRSTRLEN);

    if (server_ip == 0) {
        printf("%s No server found in UDP port %d.\n", timestamp().c_str(), udp_port);
        return EXIT_FAILURE;
    }

    while (true) {
        std::string line;
        if (!std::getline(std::cin, line)) {
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

        request req { 
            ip_from_string(ip_str), 
            std::stoul(value_str)
        };

        uint32_t seqn = send_request(server_ip, udp_port, req);
        if (seqn == 0) {
            // TODO: retry
            std::cerr << timestamp() << " Failed to send request." << std::endl;
            continue;
        }

        request_ack ack{};
        if (!receive_request_ack(seqn, ack)) {
            // TODO: retry
            std::cerr << timestamp() << " Failed to receive request acknowledgment." << std::endl;
            continue;
        }

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
    }

    return 0;
}