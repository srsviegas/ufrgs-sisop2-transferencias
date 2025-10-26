
#include <arpa/inet.h>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <thread>

#include "request.hpp"
#include "util.hpp"

bool send_exit_request(int socket, uint32_t server_ip, uint16_t server_port) {
    request exit_request{ 1, 0 };  // dest_addr = 1 for EXIT
    request_ack ack{};

    return send_request_with_retry(socket, server_ip, server_port, exit_request, ack);
}

bool send_request_with_retry(int socket, uint32_t server_ip, uint16_t server_port, const request& req, request_ack& ack_out) {
    for (int attempt = 0; attempt < REQUEST_MAX_RETRIES; attempt++) {
        uint32_t seqn = send_request(socket, server_ip, server_port, req);
        if (seqn == 0) {
            continue;
        }

        if (receive_request_ack(socket, seqn, ack_out)) {
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(REQUEST_RETRY_DELAY_MS));
    }

    return false;
}

uint32_t send_request(int socket, uint32_t server_ip, uint16_t server_port, const request& req) {
    packet_t packet{};
    packet.type = PACKET_TYPE_REQUEST;
    packet.data.req = req;
    packet.seqn = static_cast<uint32_t>(time(nullptr));

    struct sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = server_ip;

    ssize_t bytes_sent = sendto(socket, &packet, sizeof(packet), 0,
                                (struct sockaddr*)&server_addr, sizeof(server_addr));

    if (bytes_sent == -1) {
        fprintf(stderr, "%s Failed to send request.\n", timestamp().c_str());
        return 0;
    }

    if (bytes_sent != sizeof(packet)) {
        fprintf(stderr, "%s Incomplete packet sent: %zd bytes of %zu\n", timestamp().c_str(), bytes_sent, sizeof(packet));
        return 0;
    }

    return packet.seqn;
}

bool receive_request_ack(int socket, uint32_t expected_seqn, request_ack& ack) {
    struct timeval tv;
    tv.tv_sec = REQUEST_ACK_TIMEOUT_MS / 1000;
    tv.tv_usec = (REQUEST_ACK_TIMEOUT_MS % 1000) * 1000;
    if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1) {
        fprintf(stderr, "%s Failed to set socket options.\n", timestamp().c_str());
        return false;
    }

    packet_t packet{};
    sockaddr_in from{};
    socklen_t fromlen = sizeof(from);

    ssize_t bytes_received = recvfrom(socket, &packet, sizeof(packet), 0,
                                      (struct sockaddr*)&from, &fromlen);

    if (bytes_received == -1) {
        fprintf(stderr, "%s Failed to receive request acknowledgment.\n", timestamp().c_str());
        return false;
    }

    if (bytes_received != sizeof(packet_t)) {
        fprintf(stderr, "%s Incomplete packet received: %zd bytes of %zu\n", timestamp().c_str(), bytes_received, sizeof(packet_t));
        return false;
    }

    if (packet.type == PACKET_TYPE_REQUEST_ACK && packet.seqn == expected_seqn) {
        ack = packet.data.ack;
        return true;
    }

    return false;
}