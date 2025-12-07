#include "Server.hpp"
#include "util.hpp"
#include <iostream>
#include <cstring>
#include <thread>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;

Server::Server(){
    num_transactions = 0;
    total_transferred = 0;
    total_balance = 0;
}

void Server::processarRequisicao(u_int32_t value, string ipRemetente, string ipDestino){
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
            // Atualiza saldos
            ClientesDB[ipRemetente].saldo -= value;
            ClientesDB[ipDestino].saldo += value;
            ClientesDB[ipRemetente].last_req += 1;
            
            // Atualiza stats
            total_transferred += value;
            total_balance = 0;
            for(const auto& cliente : ClientesDB){
                total_balance += cliente.second.saldo;
            }
            num_transactions += 1;

            // Log
            Transacao t;
            t.ipRemetente = ipRemetente;
            t.ipDestino = ipDestino;
            t.valor = value;
            t.req_id = ClientesDB[ipRemetente].last_req;
            historicoTransacoes.push_back(t);
            cout << "[Thread] Transacao processada: " << ipRemetente << " -> " << ipDestino << "(R$ " << value << ")" << endl;
        }
    }   
}

void Server::handle_discovery(int discoverySocket, struct sockaddr_in clientAddr, uint32_t seqn){
    packet_t resposta{};
    resposta.seqn = seqn;
    resposta.type = PACKET_TYPE_DISCOVERY_ACK;

    string ipCliente = inet_ntoa(clientAddr.sin_addr);
   {
        lock_guard<mutex> lock(saldosMutex);
        if(ClientesDB.find(ipCliente) == ClientesDB.end()){
            Cliente novoCliente;
            novoCliente.ip = ipCliente;
            novoCliente.saldo = 100;
            novoCliente.last_req = 0;
            ClientesDB[ipCliente] = novoCliente;
            cout << "[Thread] Novo cliente registrado: " << ipCliente << " com saldo inicial R$ " << novoCliente.saldo << endl;
        }else{
            cout << "[Thread] Cliente ja registrado: " << ipCliente << endl;
        }
        
        // Calcula saldo total para resposta dentro do lock
        for(const auto& cliente : ClientesDB){
            resposta.data.ack.new_balance += cliente.second.saldo;
        }
    }
    
    sendto(discoverySocket, &resposta, sizeof(resposta), 0, (struct sockaddr *)&clientAddr, sizeof(clientAddr));
}

void Server::handle_pix(int serviceSocket, struct sockaddr_in clientAddr, packet_t requisicao){
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
       {
             lock_guard<mutex> lock(saldosMutex);
             resposta.data.ack.seqn = ClientesDB[ipRemetente].last_req;
             if(requisicao.data.req.value == 0){
                 resposta.data.ack.new_balance = ClientesDB[ipDestino].saldo;
             }else{
                 resposta.data.ack.new_balance = ClientesDB[ipRemetente].saldo;
             }
        }
    }

    sendto(serviceSocket, &resposta, sizeof(resposta), 0, (struct sockaddr *)&clientAddr, sizeof(clientAddr));
    
    if (requisicao.data.req.dest_addr == 1){
        cout << "[Thread] Received EXIT request from " << inet_ntoa(clientAddr.sin_addr) << endl;
    }
}

void Server::imprimirStatus(){
    printf("%s num_transactions %d total_transfered %d total_balance %d \n", 
        timestamp().c_str(), 
        num_transactions,
        total_transferred, 
        total_balance 
    );
}