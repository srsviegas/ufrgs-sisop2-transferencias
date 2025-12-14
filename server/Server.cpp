#include "Server.hpp"
#include "util.hpp"
#include <iostream>
#include <cstring>
#include <thread>
#include <arpa/inet.h>
#include <unistd.h>
#include <algorithm>

using namespace std;

Server::Server(uint32_t id) {
    num_transactions = 0;
    total_transferred = 0;
    total_balance = 0;

    serverID = id;
    serverRole = Role::PRIMARY;
    primarioAtivo.store(true);
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
        } else {
            string saldoStr = to_string(ClientesDB[ipDestino].saldo);
            resposta = "SALDO de " + ipDestino + ": R$ " + saldoStr;
            cout << ">>> [Thread] " << resposta << endl;
        }
    } else {
        if(ClientesDB.find(ipDestino) == ClientesDB.end()){
            resposta = "ERRO: O destinatario nao e um cliente valido.";
            cout << ">>> [Thread] " << resposta << endl;
            return;
        } else if(ClientesDB[ipRemetente].saldo < value){
            resposta = "ERRO: Saldo insuficiente.";
            cout << ">>> [Thread] " << resposta << endl;
            return;
        } else if(ipRemetente == ipDestino){
            resposta = "ERRO: Nao suportamos self-pix.";
            cout << ">>> [Thread] " << resposta << endl;
            return;
        } else {
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

void Server::handle_discovery(int discoverySocket, sockaddr_in clientAddr, uint32_t seqn, uint16_t packetType) {
    char ipStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
    uint16_t port = ntohs(clientAddr.sin_port);

    if (packetType == PACKET_TYPE_CLIENT_DISCOVERY) {
        if (!isPrimary()) {
            cout << "[Backup] Ignorando discovery de cliente: " << ipStr << endl;
            return;
        }

        lock_guard<mutex> lock(saldosMutex);
        if (ClientesDB.find(ipStr) == ClientesDB.end()) {
            Cliente novoCliente;
            novoCliente.ip = ipStr;
            novoCliente.saldo = 100;
            novoCliente.last_req = 0;
            novoCliente.failover_port = port; // porta do cliente para failover
            ClientesDB[ipStr] = novoCliente;
            cout << "[PRIMARY] Novo cliente registrado: " << ipStr << " porta " << port << endl;
        } else {
            cout << "[PRIMARY] Cliente já registrado: " << ipStr << endl;
        }

        packet_t resposta{};
        resposta.type = PACKET_TYPE_CLIENT_ACK;
        resposta.seqn = serverID;
        sendto(discoverySocket, &resposta, sizeof(resposta), 0,
               (sockaddr*)&clientAddr, sizeof(clientAddr));

    } else if (packetType == PACKET_TYPE_SERVER_DISCOVERY) {
        packet_t resposta{};
        resposta.type = PACKET_TYPE_SERVER_ACK;
        resposta.seqn = serverID;
        sendto(discoverySocket, &resposta, sizeof(resposta), 0,
               (sockaddr*)&clientAddr, sizeof(clientAddr));

        if (isPrimary()) {
            registrarBackup(ipStr, port);
        }
    }
}

void Server::handle_pix(int serviceSocket, struct sockaddr_in clientAddr, packet_t requisicao){
    packet_t resposta{};
    
    if(requisicao.type == PACKET_TYPE_REQUEST){
        string ipRemetente = inet_ntoa(clientAddr.sin_addr);
        struct in_addr addr;
        addr.s_addr = htonl(requisicao.data.req.dest_addr);
        string ipDestino = inet_ntoa(addr);

        processarRequisicao(requisicao.data.req.value, ipRemetente, ipDestino);

        resposta.seqn = requisicao.seqn;
        resposta.type = PACKET_TYPE_REQUEST_ACK;
        {
            lock_guard<mutex> lock(saldosMutex);
            resposta.data.ack.seqn = ClientesDB[ipRemetente].last_req;
            if(requisicao.data.req.value == 0){
                resposta.data.ack.new_balance = ClientesDB[ipDestino].saldo;
            } else {
                resposta.data.ack.new_balance = ClientesDB[ipRemetente].saldo;
            }
        }
    }

    sendto(serviceSocket, &resposta, sizeof(resposta), 0, (struct sockaddr *)&clientAddr, sizeof(clientAddr));

    if (requisicao.data.req.dest_addr == 1){
        cout << "[Thread] Received EXIT request from " << inet_ntoa(clientAddr.sin_addr) << endl;
    }

    if (isPrimary()) {
        replicaEstado(serviceSocket);
    }
}

void Server::imprimirStatus() {
    string roleStr = (serverRole == Role::PRIMARY) ? "PRIMARY" : "BACKUP";
    printf("%s Role: %s | num_transactions: %d total_transferred: %d total_balance: %d\n",
           timestamp().c_str(),
           roleStr.c_str(),
           num_transactions,
           total_transferred,
           total_balance);
}

void Server::descobrirOutrosServidores(int discoverySocket, int discoveryPort) {
    discoverySocketGlobal = discoverySocket;

    int broadcast = 1;
    setsockopt(discoverySocket, SOL_SOCKET, SO_BROADCAST,
               &broadcast, sizeof(broadcast));

    sockaddr_in broadcastAddr{};
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(discoveryPort);
    broadcastAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    packet_t pacote{};
    pacote.type = PACKET_TYPE_SERVER_DISCOVERY;
    pacote.seqn = serverID;

    const int tentativas = 5;
    const int timeout_ms = 500;

    bool encontrouPrimario = false;
    uint32_t maiorID = 0;

    for (int i = 0; i < tentativas; i++) {
        sendto(discoverySocket, &pacote, sizeof(pacote), 0,
               (sockaddr*)&broadcastAddr, sizeof(broadcastAddr));

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(discoverySocket, &readfds);

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = timeout_ms * 1000;

        int ret = select(discoverySocket + 1, &readfds, nullptr, nullptr, &tv);

        if (ret > 0 && FD_ISSET(discoverySocket, &readfds)) {
            sockaddr_in from{};
            socklen_t len = sizeof(from);
            packet_t resp{};
            ssize_t bytes = recvfrom(discoverySocket, &resp, sizeof(resp), 0,
                                     (sockaddr*)&from, &len);

            if (bytes < (ssize_t)sizeof(packet_t)) continue;

            if (resp.type == PACKET_TYPE_SERVER_ACK) {
                encontrouPrimario = true;
                maiorID = std::max(maiorID, resp.seqn);
            }
        }

        if (encontrouPrimario) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!encontrouPrimario) {
        serverID = 1;
        serverRole = Role::PRIMARY;
        primarioAtivo.store(true);

        cout << "[Discovery] Nenhum PRIMARY encontrado → assumindo PRIMARY ID=1\n";
        thread(&Server::heartbeatLoop, this, discoverySocket).detach();
    } else {
        serverID = maiorID + 1;
        serverRole = Role::BACKUP;
        primarioAtivo.store(false);

        cout << "[Discovery] PRIMARY encontrado → BACKUP ID=" << serverID << "\n";
        ultimoHeartbeat = chrono::steady_clock::now();
        thread(&Server::verificarPrimario, this).detach();
    }
}

bool Server::isPrimary() const {
    return serverRole == Role::PRIMARY;
}

void Server::replicaEstado(int sock) {
    if (!isPrimary()) return;

    packet_t pacote{};
    pacote.type = PACKET_TYPE_STATE_UPDATE;

    lock_guard<mutex> lock(saldosMutex);

    pacote.data.state.num_clientes = ClientesDB.size();
    int i = 0;
    for (const auto& [ip, cliente] : ClientesDB) {
        if (i >= MAX_CLIENTES) break;
        pacote.data.state.clientes[i].ip = inet_addr(ip.c_str());
        pacote.data.state.clientes[i].saldo = cliente.saldo;
        pacote.data.state.clientes[i].last_req = cliente.last_req;
        i++;
    }

    pacote.data.state.num_transacoes = historicoTransacoes.size();
    for (size_t j = 0; j < historicoTransacoes.size() && j < MAX_TRANSACOES; j++) {
        pacote.data.state.transacoes[j].ipRemetente = inet_addr(historicoTransacoes[j].ipRemetente.c_str());
        pacote.data.state.transacoes[j].ipDestino = inet_addr(historicoTransacoes[j].ipDestino.c_str());
        pacote.data.state.transacoes[j].valor = historicoTransacoes[j].valor;
        pacote.data.state.transacoes[j].req_id = historicoTransacoes[j].req_id;
    }

    pacote.data.state.num_transactions = num_transactions;
    pacote.data.state.total_transferred = total_transferred;
    pacote.data.state.total_balance = total_balance;

    for (auto& [ip, port] : backupServers) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
        sendto(sock, &pacote, sizeof(pacote), 0, (sockaddr*)&addr, sizeof(addr));
    }

    cout << "[PRIMARY] Estado replicado para backups\n";
}

void Server::aplicarEstado(const packet_t& pacote) {
    if (pacote.type == PACKET_TYPE_HEARTBEAT) {
        ultimoHeartbeat = chrono::steady_clock::now();
        return;
    }

    if (serverRole != Role::BACKUP) return;

    lock_guard<mutex> lock(saldosMutex);
    ClientesDB.clear();
    historicoTransacoes.clear();
    ultimoHeartbeat = chrono::steady_clock::now();

    for (uint32_t i = 0; i < pacote.data.state.num_clientes; i++) {
        in_addr addr; addr.s_addr = pacote.data.state.clientes[i].ip;
        string ip = inet_ntoa(addr);

        Cliente c;
        c.ip = ip;
        c.saldo = pacote.data.state.clientes[i].saldo;
        c.last_req = pacote.data.state.clientes[i].last_req;
        c.failover_port = 4000; // fallback caso precise
        ClientesDB[ip] = c;
    }

    for (uint32_t i = 0; i < pacote.data.state.num_transacoes; i++) {
        Transacao t;
        in_addr a, b;
        a.s_addr = pacote.data.state.transacoes[i].ipRemetente;
        b.s_addr = pacote.data.state.transacoes[i].ipDestino;
        t.ipRemetente = inet_ntoa(a);
        t.ipDestino = inet_ntoa(b);
        t.valor = pacote.data.state.transacoes[i].valor;
        t.req_id = pacote.data.state.transacoes[i].req_id;
        historicoTransacoes.push_back(t);
    }

    num_transactions = pacote.data.state.num_transactions;
    total_transferred = pacote.data.state.total_transferred;
    total_balance = pacote.data.state.total_balance;

    cout << "[BACKUP] Estado atualizado do PRIMARY\n";
}

void Server::verificarPrimario() {
    while (!isPrimary()) {
        auto agora = chrono::steady_clock::now();
        auto diff = chrono::duration_cast<chrono::seconds>(agora - ultimoHeartbeat).count();
        if (diff > 3) {
            cout << "[FAILOVER] PRIMARY inativo\n";
            assumirComoPrimario();
            anunciarNovoPrimario();
            break;
        }
        sleep(1);
    }
}

void Server::assumirComoPrimario() {
    serverRole = Role::PRIMARY;
    primarioAtivo.store(true);

    std::cout << "[FAILOVER] BACKUP agora é PRIMARY\n";

    backupServers.clear();

    {
        std::lock_guard<mutex> lock(saldosMutex);

        packet_t anuncio{};
        anuncio.type = PACKET_TYPE_PRIMARY_ANNOUNCE;
        anuncio.seqn = serverID;

        for (const auto& [ip, cliente] : ClientesDB) {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(cliente.failover_port); // porta do cliente
            inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

            sendto(discoverySocketGlobal, &anuncio, sizeof(anuncio), 0,
                   (sockaddr*)&addr, sizeof(addr));
            std::cout << "[PRIMARY] Anúncio enviado para cliente " << ip << ":" << cliente.failover_port << std::endl;
        }
    }

    if (discoverySocketGlobal != -1) {
        std::thread(&Server::heartbeatLoop, this, discoverySocketGlobal).detach();
    } else {
        std::cerr << "[PRIMARY] Socket para heartbeat não está disponível!\n";
    }
}

void Server::heartbeatLoop(int sock) {
    packet_t hb{};
    hb.type = PACKET_TYPE_HEARTBEAT;

    while (isPrimary()) {
        for (auto& [ip, port] : backupServers) {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
            sendto(sock, &hb, sizeof(hb), 0, (sockaddr*)&addr, sizeof(addr));
        }
        sleep(1);
    }
}

void Server::registrarBackup(const string& ip, uint16_t port) {
    for (auto& [existingIp, existingPort] : backupServers) {
        if (existingIp == ip) return;
    }
    backupServers.push_back({ip, port});
    cout << "[PRIMARY] Backup registrado: " << ip << ":" << port << endl;
}

void Server::anunciarNovoPrimario() {
    if (!isPrimary()) return;

    packet_t anuncio{};
    anuncio.type = PACKET_TYPE_PRIMARY_ANNOUNCE;
    anuncio.seqn = serverID;
    anuncio.data.primary_announce.server_port = server_port;

    lock_guard<mutex> lock(saldosMutex);

    for (const auto& [ip, cliente] : ClientesDB) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(cliente.failover_port); // porta correta
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

        sendto(discoverySocketGlobal, &anuncio, sizeof(anuncio), 0,
               (sockaddr*)&addr, sizeof(addr));
    }

    cout << "[PRIMARY] Novo primário anunciado para clientes\n";
}
