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

#define portaServico 9999 //Porta fixa do serviço PIX

using namespace std;

const int TAMANHO_BUFFER = 1024;

map<string, double> saldoClientes; //Banco de dados dos clientes (mudar)
mutex saldosMutex; //Mutex para proteger o acesso aos dados dos clientes    

string processarRequisicao(string ipRemetente, string mensagem){

    lock_guard<mutex> lock(saldosMutex);

    string resposta;
    size_t pos = mensagem.find(':');

    if(pos == string::npos){
        resposta = "ERRO: Formato da mensagem invalido. Use <IP_DESTINO> <VALOR>";
    }else{
        string ipDestino = mensagem.substr(0, pos);
        try{
            double valor = stod(mensagem.substr(pos + 1));
            //Consulta de Saldo
            if(valor == 0){
                cout << ">>> [Thread] Consultando saldo para: " << ipDestino << endl;
                if(saldoClientes.find(ipDestino) == saldoClientes.end()){
                    resposta = "ERRO: Conta " + ipDestino + " nao encontrada.";
                }else{
                    string saldoStr = to_string(saldoClientes[ipDestino]);
                    resposta = "SALDO de " + ipDestino + ": R$ " + saldoStr.substr(0, saldoStr.find('.') + 3);
                }
            }
            //Transação Pix
            else{
                if(saldoClientes.find(ipRemetente) == saldoClientes.end()){
                    resposta = "ERRO: Conta do remetente nao foi encontrada.";
                }else if(saldoClientes.find(ipDestino) == saldoClientes.end()){
                    resposta = "ERRO: O destinatario nao e um cliente valido.";
                }else if(valor < 0){
                    resposta = "ERRO: O valor da transferencia deve ser positivo.";
                }else if(saldoClientes[ipRemetente] < valor){
                    resposta = "ERRO: Saldo insuficiente.";
                }else if(ipRemetente == ipDestino){
                    resposta = "ERRO: Nao suportamos self-pix.";
                }else{
                    saldoClientes[ipRemetente] -= valor;
                    saldoClientes[ipDestino] += valor;
                    resposta = "Transferencia realizada. Seu novo saldo: " + to_string(saldoClientes[ipRemetente]);
                    cout << ">>> [Thread] Transacao processada: " << ipRemetente << " -> " << ipDestino << "(R$ " << valor << ")" << endl;
                }
            }
        }catch(const invalid_argument& e){
            resposta = "ERRO: Valor invalido.";
        }
    }
    return resposta;
}

//Função executada pela thread de descoberta
//Registra clientes novos e responde com a porta do serviço
void handle_discovery(int discoverySocket, struct sockaddr_in clientAddr, uint32_t seqn){
    packet_t resposta{};
    resposta.seqn = seqn;
    resposta.type = PACKET_TYPE_DISCOVERY_ACK;
    sendto(discoverySocket, &resposta, sizeof(resposta), 0, (struct sockaddr *)&clientAddr, sizeof(clientAddr));
}

//Geerencia as requisições PIX e envia respostas
void handle_pix(int serviceSocket, struct sockaddr_in clientAddr, packet_t requisicao){
    packet_t resposta{};
    resposta.seqn = requisicao.seqn;
    resposta.type = PACKET_TYPE_REQUEST_ACK;
    resposta.data.ack.seqn = requisicao.seqn;
    resposta.data.ack.new_balance = 100000; // mock: e.g., 1000.00 in cents
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

    int discoverySocket;
    struct sockaddr_in infoServico, infoDiscovery, infoCliente;
    char buffer[TAMANHO_BUFFER];

    //Configuração dos sockets de descoberta 
    discoverySocket = socket(AF_INET, SOCK_DGRAM, 0);
    infoDiscovery.sin_family = AF_INET;
    infoDiscovery.sin_addr.s_addr = INADDR_ANY;
    infoDiscovery.sin_port = htons(portaDescoberta);
    bind(discoverySocket,(struct sockaddr *)&infoDiscovery, sizeof(infoDiscovery));

    cout << "[Main] Servidor de Descoberta rodando na porta " << portaDescoberta << endl;
    cout << "[Main] Servidor PIX aguardando requisicoes na porta " << portaDescoberta << endl;

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
                    struct sockaddr_in clientAddr_copy = infoCliente;
                    thread(handle_discovery, discoverySocket, clientAddr_copy, pacote.seqn).detach();
                } else if (pacote.type == PACKET_TYPE_REQUEST) {
                    struct sockaddr_in clientAddr_copy = infoCliente;
                    thread(handle_pix, discoverySocket, clientAddr_copy, pacote).detach();
                }
            }
        }
    }
    close(discoverySocket);
    return 0;
}