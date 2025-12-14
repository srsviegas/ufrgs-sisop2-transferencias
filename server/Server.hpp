#ifndef SERVER_HPP
#define SERVER_HPP

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <netinet/in.h>
#include "../client/request.hpp"
#include <atomic>
#include <chrono>

struct Cliente {
    std::string ip;
    uint32_t saldo;
    int last_req;

    uint16_t failover_port;   // porta do cliente para failover
    uint16_t server_port;     // opcional: porta usada pelo cliente para comunicação normal
};


struct Transacao {
    std::string ipRemetente;
    std::string ipDestino;
    u_int32_t valor;
    int req_id;
};

enum class Role { PRIMARY, BACKUP };

class Server {
private:
    // Banco de dados dos clientes e histórico de transações
    std::map<std::string, Cliente> ClientesDB;
    std::vector<Transacao> historicoTransacoes;

    std::mutex saldosMutex;

    // Estatísticas
    int num_transactions;
    int total_transferred;
    int total_balance;

    // Função auxiliar privada
    void processarRequisicao(u_int32_t value, std::string ipRemetente, std::string ipDestino);

    Role serverRole;  // Role do servidor (PRIMARY ou BACKUP)
    uint32_t serverID; // Identificador do servidor
    std::vector<std::pair<std::string,uint16_t>> backupServers; // lista de backups (IP + porta)
    std::atomic<bool> primarioAtivo; // monitorar se primário está ativo

    std::chrono::steady_clock::time_point ultimoHeartbeat;
    int discoverySocketGlobal = -1;
    uint16_t server_port;

public:
    Server(uint32_t id = 0); // construtor

    // Métodos principais
    void handle_discovery(int discoverySocket, struct sockaddr_in clientAddr, uint32_t seqn, uint16_t packetType);
    void handle_pix(int serviceSocket, struct sockaddr_in clientAddr, packet_t requisicao);
    
    void imprimirStatus();

    void descobrirOutrosServidores(int discoverySocket, int discoveryPort);
    uint32_t getID() const { return serverID; }
    bool isPrimary() const;
    void replicaEstado(int sock);
    void aplicarEstado(const packet_t& pacote);
    
    void verificarPrimario();
    void assumirComoPrimario();
    void heartbeatLoop(int sock);
    void registrarBackup(const std::string& ip, uint16_t port);
    void anunciarNovoPrimario();

};

#endif
