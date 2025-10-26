
#include <arpa/inet.h>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

#include "request.hpp"
#include "util.hpp"

static int g_request_socket = -1;

static bool ensure_socket_created() {
    if (g_request_socket >= 0) return true;
    g_request_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_request_socket < 0) {
        perror("socket");
        return false;
    }
    return true;
}

uint32_t send_request(uint32_t server_ip, uint16_t server_port, const request& req) {
    if (!ensure_socket_created()) return 0;

    packet_t packet{};
    packet.type = PACKET_TYPE_REQUEST;
    packet.data.req = req;
    packet.seqn = static_cast<uint32_t>(time(nullptr));

    struct sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = server_ip;

    ssize_t bytes_sent = sendto(g_request_socket, &packet, sizeof(packet), 0,
                                (struct sockaddr*)&server_addr, sizeof(server_addr));

    if (bytes_sent == -1) {
        perror("sendto failed");
        return 0;
    }

    if (bytes_sent != sizeof(packet)) {
        fprintf(stderr, "Incomplete packet sent: %zd bytes of %zu\n", bytes_sent, sizeof(packet));
        return 0;
    }

    return packet.seqn;
}

bool receive_request_ack(uint32_t expected_seqn, request_ack& ack) {
    if (!ensure_socket_created()) return false;
    
    struct timeval tv;
    tv.tv_sec = REQUEST_ACK_TIMEOUT_MS / 1000;
    tv.tv_usec = (REQUEST_ACK_TIMEOUT_MS % 1000) * 1000;
    if (setsockopt(g_request_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1) {
        perror("setsockopt failed");
        return false;
    }

    packet_t packet{};
    sockaddr_in from{};
    socklen_t fromlen = sizeof(from);

    ssize_t bytes_received = recvfrom(g_request_socket, &packet, sizeof(packet), 0,
                                      (struct sockaddr*)&from, &fromlen);

    if (bytes_received == -1) {
        perror("recvfrom failed");
        return false;
    }

    if (bytes_received != sizeof(packet_t)) {
        fprintf(stderr, "Incomplete packet received: %zd bytes of %zu\n", bytes_received, sizeof(packet_t));
        return false;
    }

    if (packet.type == PACKET_TYPE_REQUEST_ACK && packet.seqn == expected_seqn) {
        ack = packet.data.ack;
        return true;
    }

    return false;
}