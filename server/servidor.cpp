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

int main(int argc, char *argv[]){
    if(argc != 2){
        cerr << "Uso: " << argv[0] << " <porta_descoberta>" << endl;
        return 1;
    }
    int portaDescoberta = stoi(argv[1]);

    Server server;

    int discoverySocket;
    struct sockaddr_in infoDiscovery, infoCliente;

    //Configuração dos sockets
    discoverySocket = socket(AF_INET, SOCK_DGRAM, 0);
    infoDiscovery.sin_family = AF_INET;
    infoDiscovery.sin_addr.s_addr = INADDR_ANY;
    infoDiscovery.sin_port = htons(portaDescoberta);
    bind(discoverySocket,(struct sockaddr *)&infoDiscovery, sizeof(infoDiscovery));

    server.imprimirStatus();

    fd_set readfds;
    int max_sd = discoverySocket;

    while(true){
        FD_ZERO(&readfds);
        FD_SET(discoverySocket, &readfds);

        select(max_sd + 1, &readfds, NULL, NULL, NULL); 

        if(FD_ISSET(discoverySocket, &readfds)){
            socklen_t len = sizeof(infoCliente);
            packet_t pacote{};
            ssize_t bytes = recvfrom(discoverySocket, &pacote, sizeof(pacote), 0,(struct sockaddr *)&infoCliente, &len);
            
            if(bytes >= (ssize_t)sizeof(packet_t)){
                struct sockaddr_in clientAddr_copy = infoCliente;

                if(pacote.type == PACKET_TYPE_DISCOVERY){
                    cout << "\n--- PACOTE DE DESCOBERTA RECEBIDO ---" << std::endl;
                    thread(&Server::handle_discovery, &server, discoverySocket, clientAddr_copy, pacote.seqn).detach();
                
                }else if(pacote.type == PACKET_TYPE_REQUEST){
                    cout << "\n--- REQUISICAO PIX RECEBIDA ---" << std::endl;
                    thread t(&Server::handle_pix, &server, discoverySocket, clientAddr_copy, pacote);
                    t.join();
                    
                    server.imprimirStatus();
                }
            }
        }
    }
    close(discoverySocket);
    return 0;
}