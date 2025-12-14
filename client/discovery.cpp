#include "request.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

using namespace std;

// Agora recebe também a porta de escuta do cliente
uint32_t discover_server(uint16_t server_port, uint16_t client_listen_port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 0;
    }

    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    sockaddr_in broadcastAddr{};
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(server_port);
    broadcastAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    // Monta pacote de discovery, incluindo porta do cliente
    packet_t packet{};
    packet.type = PACKET_TYPE_CLIENT_DISCOVERY;
    packet.seqn = 1; // qualquer valor != 0
    packet.data.req.value = client_listen_port; // porta dinâmica enviada ao servidor

    sendto(sock, &packet, sizeof(packet), 0,
           (sockaddr*)&broadcastAddr, sizeof(broadcastAddr));

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);

    timeval tv{};
    tv.tv_sec = 2;
    tv.tv_usec = 0;

    if (select(sock + 1, &readfds, nullptr, nullptr, &tv) > 0) {
        packet_t response{};
        sockaddr_in from{};
        socklen_t len = sizeof(from);

        ssize_t recv_len = recvfrom(
            sock,
            &response,
            sizeof(response),
            0,
            (sockaddr*)&from,
            &len
        );

        if (recv_len > 0 && response.type == PACKET_TYPE_CLIENT_ACK) {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));

            cout << "[CLIENT] PRIMARY encontrado em " << ip
                 << " (ID=" << response.seqn << ")\n";

            close(sock);
            return from.sin_addr.s_addr;
        }
    }

    cout << "[CLIENT] Nenhum PRIMARY encontrado\n";
    close(sock);
    return 0;
}
