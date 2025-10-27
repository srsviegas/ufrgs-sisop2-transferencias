#include <iostream>
#include <string>
#include <map>
#include <cstring>
#include <algorithm>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h> 
#include <thread> 
#include <mutex>  

#include "../client/request.hpp"
#include "util.hpp"

using namespace std;

struct Cliente {
    string ip;
    u_int32_t saldo;
    int last_req;
};

int num_transactions = 0;
int total_transferred = 0;
int total_balance = 0;

map<string, Cliente> ClientesDB; //Banco de dados dos clientes (mudar)
mutex saldosMutex; //Mutex para proteger o acesso aos dados dos clientes    

void processarRequisicao(u_int32_t value, string ipRemetente, string ipDestino){

    string resposta;
    lock_guard<mutex> lock(saldosMutex);
    if(value == 0){
        cout << ">>> [Thread] Consultando saldo para: " << ipDestino << endl;
        if(ClientesDB.find(ipDestino) == ClientesDB.end()){
            resposta = "ERRO: Conta " + ipDestino + " nao encontrada.";
            cout << ">>> [Thread] " << resposta << endl;
            return;
        }else{
            string saldoStr = to_string(ClientesDB[ipDestino].saldo);
            resposta = "SALDO de " + ipDestino + ": R$ " + saldoStr;
            cout << ">>> [Thread] " << resposta << endl;
        }
    }
    //Transação Pix
    else{
        if(ClientesDB.find(ipDestino) == ClientesDB.end()){
            resposta = "ERRO: O destinatario nao e um cliente valido.";
            cout << ">>> [Thread] " << resposta << endl;
            return;
        }else if(ClientesDB[ipRemetente].saldo < value){
            resposta = "ERRO: Saldo insuficiente.";
            cout << ">>> [Thread] " << resposta << endl;
            return;
        }else if(ipRemetente == ipDestino){
            resposta = "ERRO: Nao suportamos self-pix.";
            cout << ">>> [Thread] " << resposta << endl;
            return;
        }else{
            ClientesDB[ipRemetente].saldo -= value;
            ClientesDB[ipDestino].saldo += value;
            ClientesDB[ipRemetente].last_req += 1;
            total_transferred += value;
            for(const auto& cliente : ClientesDB){
                total_balance += cliente.second.saldo;
            }
            num_transactions += 1;
            cout << "[Thread] Transacao processada: " << ipRemetente << " -> " << ipDestino << "(R$ " << value << ")" << endl;
        }
    }    
}

//Função executada pela thread de descoberta
//Registra clientes novos e responde com a porta do serviço
void handle_discovery(int discoverySocket, struct sockaddr_in clientAddr, uint32_t seqn){
    packet_t resposta{};
    resposta.seqn = seqn;
    resposta.type = PACKET_TYPE_DISCOVERY_ACK;

    string ipCliente = inet_ntoa(clientAddr.sin_addr);
    {
        lock_guard<mutex> lock(saldosMutex);
        if(ClientesDB.find(ipCliente) == ClientesDB.end()){
            Cliente novoCliente;
            novoCliente.ip = ipCliente;
            novoCliente.saldo = 100; //Saldo inicial
            novoCliente.last_req = 0;
            ClientesDB[ipCliente] = novoCliente;
            cout << "[Thread] Novo cliente registrado: " << ipCliente << " com saldo inicial R$ " << novoCliente.saldo << endl;
        }else{
            cout << "[Thread] Cliente ja registrado: " << ipCliente << endl;
        }
    }
    for(const auto& cliente : ClientesDB){
        resposta.data.ack.new_balance += cliente.second.saldo;
    }
    sendto(discoverySocket, &resposta, sizeof(resposta), 0, (struct sockaddr *)&clientAddr, sizeof(clientAddr));
}

//Geerencia as requisições PIX e envia respostas
void handle_pix(int serviceSocket, struct sockaddr_in clientAddr, packet_t requisicao){    
    packet_t resposta{};
    if(requisicao.type == PACKET_TYPE_REQUEST){
        string ipRemetente = inet_ntoa(clientAddr.sin_addr);
        struct in_addr addr;
        addr.s_addr = htonl(requisicao.data.req.dest_addr);
        string ipDestino = inet_ntoa(addr);

        processarRequisicao(requisicao.data.req.value, ipRemetente, ipDestino);

        // Prepara a resposta
        resposta.seqn = requisicao.seqn;
        resposta.type = PACKET_TYPE_REQUEST_ACK;
        resposta.data.ack.seqn = ClientesDB[ipRemetente].last_req;
        if(requisicao.data.req.value == 0){
            resposta.data.ack.new_balance = ClientesDB[ipDestino].saldo;
        }else{
            resposta.data.ack.new_balance = ClientesDB[ipRemetente].saldo;
        }
        
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));
    sendto(serviceSocket, &resposta, sizeof(resposta), 0, (struct sockaddr *)&clientAddr, sizeof(clientAddr));
    if (requisicao.data.req.dest_addr == 1) {
        // EXIT request
        cout << "[Thread] Received EXIT request from "
             << inet_ntoa(clientAddr.sin_addr) << endl;
        return;
    }
}

int main(int argc, char *argv[]){
    if(argc != 2){
        cerr << "Uso: " << argv[0] << " <porta_descoberta>" << endl;
        return 1;
    }
    int portaDescoberta = stoi(argv[1]);

    Cliente c1;
    c1.ip = "192.168.15.20";
    c1.saldo = 100;
    c1.last_req = 0;
    ClientesDB[c1.ip] = c1;

    int discoverySocket;
    struct sockaddr_in infoDiscovery, infoCliente;

    //Configuração dos sockets de descoberta 
    discoverySocket = socket(AF_INET, SOCK_DGRAM, 0);
    infoDiscovery.sin_family = AF_INET;
    infoDiscovery.sin_addr.s_addr = INADDR_ANY;
    infoDiscovery.sin_port = htons(portaDescoberta);
    bind(discoverySocket,(struct sockaddr *)&infoDiscovery, sizeof(infoDiscovery));

    printf("%s num_transactions %d total_transfered %d total_balance %d \n", 
        timestamp().c_str(), 
        num_transactions,
        total_transferred, 
        total_balance 
    );

    fd_set readfds;
    int max_sd = discoverySocket;

    while(true){
        FD_ZERO(&readfds);
        FD_SET(discoverySocket, &readfds);

        select(max_sd + 1, &readfds, NULL, NULL, NULL); 

        //Socket único para descoberta e requisições
        if(FD_ISSET(discoverySocket, &readfds)){
            socklen_t len = sizeof(infoCliente);
            packet_t pacote{};
            ssize_t bytes = recvfrom(discoverySocket, &pacote, sizeof(pacote), 0,(struct sockaddr *)&infoCliente, &len);
            if(bytes >= (ssize_t)sizeof(packet_t)){
                if (pacote.type == PACKET_TYPE_DISCOVERY) {
                    cout << "\n--- PACOTE DE DESCOBERTA RECEBIDO ---" << std::endl;
                    struct sockaddr_in clientAddr_copy = infoCliente;
                    thread(handle_discovery, discoverySocket, clientAddr_copy, pacote.seqn).detach();
                } else if (pacote.type == PACKET_TYPE_REQUEST) {
                    cout << "\n--- REQUISICAO PIX RECEBIDA ---" << std::endl;
                    struct sockaddr_in clientAddr_copy = infoCliente;
                    thread t(handle_pix, discoverySocket, clientAddr_copy, pacote);
                    t.join();    
                    printf("%s num_transactions %d total_transfered %d total_balance %d \n", 
                    timestamp().c_str(), 
                    num_transactions,
                    total_transferred, 
                    total_balance 
                    );
                }
            }
        }
        
    }
    close(discoverySocket);
    return 0;
}