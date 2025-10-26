#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>

#include "discovery.hpp"
#include "request.hpp"
#include "util.hpp"


uint32_t discover_server(uint16_t port) {
    int broadcast = 1;
    int discovery_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (discovery_socket < 0) {
        perror("socket");
        return 0;
    }
    if (setsockopt(discovery_socket, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        perror("setsockopt");
        close(discovery_socket);
        return 0;
    }

    sockaddr_in broadcast_addr{};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(port);
    broadcast_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    packet_t packet{};
    packet.type = PACKET_TYPE_DISCOVERY;

    sendto(discovery_socket, &packet, sizeof(packet), 0,
           (sockaddr*)&broadcast_addr, sizeof(broadcast_addr));

    packet_t response{};
    sockaddr_in from_addr{};
    socklen_t from_len = sizeof(from_addr);

    struct timeval tv;
    tv.tv_sec = DISCOVERY_TIMEOUT_MS / 1000;
    tv.tv_usec = (DISCOVERY_TIMEOUT_MS % 1000) * 1000;
    if (setsockopt(discovery_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1) {
        perror("setsockopt failed");
        close(discovery_socket);
        return 0;
    }

    ssize_t recv_len = recvfrom(discovery_socket, &response, sizeof(response), 0,
                                (sockaddr*)&from_addr, &from_len);

    uint32_t server_addr = 0;
    if (recv_len > 0 && response.type == PACKET_TYPE_DISCOVERY_ACK) {
        server_addr = from_addr.sin_addr.s_addr;
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &server_addr, ip_str, sizeof(ip_str));
        printf("%s server addr %s\n", timestamp().c_str(), ip_str);
    }

    close(discovery_socket);
    return server_addr;
}