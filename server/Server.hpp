#ifndef SERVER_HPP
#define SERVER_HPP

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <netinet/in.h>
#include "../client/request.hpp"

struct Cliente {
    std::string ip;
    u_int32_t saldo;
    int last_req;
};

struct Transacao {
    std::string ipRemetente;
    std::string ipDestino;
    u_int32_t valor;
    int req_id;
};

class Server {
private:
    //Banco de dados dos clientes e histórico de transações
    std::map<std::string, Cliente> ClientesDB;
    std::vector<Transacao> historicoTransacoes;

    std::mutex saldosMutex;

    //Estatísticas que antes eram globais
    int num_transactions;
    int total_transferred;
    int total_balance;

    //Função auxiliar então é privada
    void processarRequisicao(u_int32_t value, std::string ipRemetente, std::string ipDestino);

public:
    Server();

    // Métodos principais
    void handle_discovery(int discoverySocket, struct sockaddr_in clientAddr, uint32_t seqn);
    void handle_pix(int serviceSocket, struct sockaddr_in clientAddr, packet_t requisicao);
    
    void imprimirStatus();
};

#endif