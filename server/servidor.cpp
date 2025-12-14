#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <thread>
#include "util.hpp"
#include "Server.hpp"

using namespace std;

int main(int argc, char *argv[]) {
    if (argc != 2) {
        cerr << "Uso: " << argv[0] << " <porta_descoberta>" << endl;
        return 1;
    }

    int portaDescoberta = stoi(argv[1]);

    Server server;

    // 🔹 1. Cria UM ÚNICO socket UDP
    int discoverySocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (discoverySocket < 0) {
        cerr << "Erro ao criar socket UDP" << endl;
        return 1;
    }

    sockaddr_in infoDiscovery{};
    infoDiscovery.sin_family = AF_INET;
    infoDiscovery.sin_addr.s_addr = INADDR_ANY;
    infoDiscovery.sin_port = htons(portaDescoberta);

    if (bind(discoverySocket, (sockaddr*)&infoDiscovery, sizeof(infoDiscovery)) < 0) {
        cerr << "Erro ao bindar socket UDP" << endl;
        return 1;
    }

    // 🔥 2. Discovery acontece COM SOCKET JÁ BINDADO
    server.descobrirOutrosServidores(discoverySocket, portaDescoberta);

    server.imprimirStatus();

    fd_set readfds;

    while (true) {
        FD_ZERO(&readfds);
        FD_SET(discoverySocket, &readfds);

        select(discoverySocket + 1, &readfds, nullptr, nullptr, nullptr);

        if (FD_ISSET(discoverySocket, &readfds)) {
            sockaddr_in infoCliente{};
            socklen_t len = sizeof(infoCliente);
            packet_t pacote{};

            ssize_t bytes = recvfrom(
                discoverySocket,
                &pacote,
                sizeof(pacote),
                0,
                (sockaddr*)&infoCliente,
                &len
            );

            if (bytes < (ssize_t)sizeof(packet_t))
                continue;

            sockaddr_in clientAddr_copy = infoCliente;

            if (pacote.type == PACKET_TYPE_SERVER_DISCOVERY) {
                // 🔹 Responde ACK do primário
                if (server.isPrimary()) {
                    packet_t resp{};
                    resp.type = PACKET_TYPE_SERVER_ACK; // ✅ corrigido
                    resp.seqn = server.getID();
                    sendto(discoverySocket, &resp, sizeof(resp), 0,
                        (sockaddr*)&infoCliente, sizeof(infoCliente));

                    // Registrar backup
                    char ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &infoCliente.sin_addr, ip, sizeof(ip));
                    server.registrarBackup(ip, ntohs(infoCliente.sin_port));
                }
            }


            else if (pacote.type == PACKET_TYPE_CLIENT_DISCOVERY) {

                // 🔒 BACKUP NÃO responde cliente
                if (!server.isPrimary()) {
                    cout << "[Backup] Ignorando discovery de cliente\n";
                    continue;
                }

                cout << "\n--- PACOTE DE DESCOBERTA RECEBIDO (CLIENTE) ---\n";

                thread([&server, discoverySocket, infoCliente]() {
                    packet_t resposta{};
                    resposta.type = PACKET_TYPE_CLIENT_ACK;
                    resposta.seqn = server.getID();

                    sendto(discoverySocket, &resposta, sizeof(resposta), 0,
                        (sockaddr*)&infoCliente, sizeof(infoCliente));

                    server.handle_discovery(
                        discoverySocket, 
                        infoCliente, 
                        resposta.seqn,
                        PACKET_TYPE_CLIENT_DISCOVERY  // ✅ aqui
                    );
                }).detach();

            }

            else if (pacote.type == PACKET_TYPE_REQUEST) {
                if (!server.isPrimary()) {
                    cout << "[Backup] Ignorando PIX (somente PRIMARY atende)\n";
                    continue;
                }
                cout << "\n--- REQUISICAO PIX RECEBIDA ---\n";

                thread t(
                    &Server::handle_pix,
                    &server,
                    discoverySocket,
                    clientAddr_copy,
                    pacote
                );
                t.join();

                server.imprimirStatus();
            }
            else if (pacote.type == PACKET_TYPE_STATE_UPDATE) {
                server.aplicarEstado(pacote);
            }
            else if (pacote.type == PACKET_TYPE_HEARTBEAT) {
                if (!server.isPrimary()) {
                    server.aplicarEstado(pacote); // só atualiza timestamp
                }
            }


        }
    }

    close(discoverySocket);
    return 0;
}