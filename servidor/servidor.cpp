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
#include <iomanip>
#include <sstream>

#define portaServico 9999 //Porta fixa do serviço PIX

using namespace std;

const int TAMANHO_BUFFER = 1024;
const string DISCOVERY_MSG = "PIX_SERVER_DISCOVERY_REQUEST";

map<string, double> saldoClientes; //Banco de dados dos clientes (mudar)
map<string, int> requisicoesPorCliente;

mutex saldosMutex; //Mutex para proteger o acesso aos dados dos clientes    
int numTransactions = 0;
double totalTransferred = 0.0;
double totalBalance = 0.0;
mutex statsMutex;

double calcularSaldoTotalBanco() {
    double soma = 0.0;
    for (auto &par : saldoClientes) soma += par.second;
    return soma;
}

string getDataHoraAtual() {
    time_t agora = time(nullptr);
    char bufferTempo[20];
    strftime(bufferTempo, sizeof(bufferTempo), "%Y-%m-%d %H:%M:%S", localtime(&agora));
    return string(bufferTempo);
}

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
                    //resposta = "Transferencia realizada. Seu novo saldo: " + to_string(saldoClientes[ipRemetente]);
                    //cout << ">>> [Thread] Transacao processada: " << ipRemetente << " -> " << ipDestino << "(R$ " << valor << ")" << endl;
                    int reqCliente;
                    {
                        lock_guard<mutex> lock2(statsMutex);
                        requisicoesPorCliente[ipRemetente]++;
                        reqCliente = requisicoesPorCliente[ipRemetente];
                        numTransactions++;
                        totalTransferred += valor;
                        totalBalance = calcularSaldoTotalBanco();
                    }

                    {
                        lock_guard<mutex> lockPrint(statsMutex);
                        cout << getDataHoraAtual()
                             << " client " << ipRemetente
                             << " id_req " << reqCliente
                             << " dest " << ipDestino
                             << " value " << fixed << setprecision(2) << valor << endl
                             << "num_transactions " << numTransactions << endl
                             << "total_transferred " << fixed << setprecision(2) << totalTransferred
                             << " total_balance " << fixed << setprecision(2) << totalBalance
                             << endl;
                    }

                    ostringstream string_builder;
                    string_builder << fixed << setprecision(2);
                    string_builder << "new balance " << saldoClientes[ipRemetente];
                    resposta = string_builder.str();
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
void handle_discovery(int discoverySocket, struct sockaddr_in clientAddr, string clientIP, int servicePort){
    cout << ">>> [Thread] Mensagem de descoberta recebida de: " << clientIP << endl;

   {
        lock_guard<mutex> lock(saldosMutex);
        if(saldoClientes.find(clientIP) == saldoClientes.end()){
            saldoClientes[clientIP] = 100.00;
            cout << "    [Thread] Novo cliente registrado! Saldo inicial: R$ 100.00" << endl;
        }
    }

    //Responde com a porta de serviço
    string portaResposta = to_string(servicePort);
    sendto(discoverySocket, portaResposta.c_str(), portaResposta.length(), 0,(struct sockaddr *)&clientAddr, sizeof(clientAddr)); 
}

//Geerencia as requisições PIX e envia respostas
void handle_pix(int serviceSocket, struct sockaddr_in clientAddr, string clientIP, string message){
    //cout << "\n>>> [Thread] Recebida requisicao PIX de " << clientIP << ": " << message << endl;
    //Chama a função de processamento(que já tem seu próprio mutex)
    string resposta = processarRequisicao(clientIP, message);
    //Envia a resposta de volta ao cliente
    sendto(serviceSocket, resposta.c_str(), resposta.length(), 0,(struct sockaddr *)&clientAddr, sizeof(clientAddr));
}

int main(int argc, char *argv[]){
    if(argc != 2){
        cerr << "Uso: " << argv[0] << " <porta_descoberta>" << endl;
        return 1;
    }
    int portaDescoberta = stoi(argv[1]);

    int serviceSocket, discoverySocket;
    struct sockaddr_in infoServico, infoDiscovery, infoCliente;
    char buffer[TAMANHO_BUFFER];

    //Configuração dos sockets de descoberta 
    discoverySocket = socket(AF_INET, SOCK_DGRAM, 0);
    infoDiscovery.sin_family = AF_INET;
    infoDiscovery.sin_addr.s_addr = INADDR_ANY;
    infoDiscovery.sin_port = htons(portaDescoberta);
    bind(discoverySocket,(struct sockaddr *)&infoDiscovery, sizeof(infoDiscovery));

    //Configuração do socket de serviço
    serviceSocket = socket(AF_INET, SOCK_DGRAM, 0);
    infoServico.sin_family = AF_INET;
    infoServico.sin_addr.s_addr = INADDR_ANY;
    infoServico.sin_port = htons(portaServico);
    bind(serviceSocket,(struct sockaddr *)&infoServico, sizeof(infoServico));

    cout << "[Main] Servidor de Descoberta rodando na porta " << portaDescoberta << endl;
    cout << "[Main] Servidor PIX aguardando requisicoes na porta " << portaServico << endl;

    cout << getDataHoraAtual() 
         << " num transactions 0 total transferred 0 total balance 0" 
         << endl;

    fd_set readfds;
    int max_sd = max(serviceSocket, discoverySocket);

    while(true){
        FD_ZERO(&readfds);
        FD_SET(serviceSocket, &readfds);
        FD_SET(discoverySocket, &readfds);

        select(max_sd + 1, &readfds, NULL, NULL, NULL); 

        //Socket de descoberta
        if(FD_ISSET(discoverySocket, &readfds)){
            socklen_t len = sizeof(infoCliente);
            memset(buffer, 0, TAMANHO_BUFFER);
            ssize_t bytes = recvfrom(discoverySocket, buffer, TAMANHO_BUFFER, 0,(struct sockaddr *)&infoCliente, &len);
            
            if(bytes > 0 && string(buffer) == DISCOVERY_MSG){
                //Copia as informações para a thread
                struct sockaddr_in clientAddr_copy = infoCliente; 
                char ipCliente[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &infoCliente.sin_addr, ipCliente, INET_ADDRSTRLEN);
                string ipClienteStr(ipCliente);

                //Cria thread para tratar da descoberta
                thread(handle_discovery, discoverySocket, clientAddr_copy, ipClienteStr, portaServico).detach();
            }
        }
        //Socket de serviços
        if(FD_ISSET(serviceSocket, &readfds)){
            socklen_t len = sizeof(infoCliente);
            memset(buffer, 0, TAMANHO_BUFFER);
            ssize_t bytes = recvfrom(serviceSocket, buffer, TAMANHO_BUFFER, 0,(struct sockaddr *)&infoCliente, &len);

            if(bytes > 0){
                //Copia as informações para a thread
                struct sockaddr_in clientAddr_copy = infoCliente;
                char ipRemetente[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &infoCliente.sin_addr, ipRemetente, INET_ADDRSTRLEN);
                string ipRemetenteStr(ipRemetente);
                string msg(buffer);

                //Cria thread para tratar da requisição PIX
                thread(handle_pix, serviceSocket, clientAddr_copy, ipRemetenteStr, msg).detach();
            }
        }
    }
    close(serviceSocket);
    close(discoverySocket);
    return 0;
}