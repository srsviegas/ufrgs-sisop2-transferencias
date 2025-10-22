#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h> 

using namespace std;

const string DISCOVERY_MSG = "PIX_SERVER_DISCOVERY_REQUEST";

int main(int argc, char* argv[]){
    if(argc != 2){
        cerr << "Uso: " << argv[0] << " <porta_descoberta>" << endl;
        return 1;
    }
    int portaDescoberta = stoi(argv[1]);

    int clienteSocket;
    struct sockaddr_in broadcastAddr, servAddr;
    char buffer[1024];

    clienteSocket = socket(AF_INET, SOCK_DGRAM, 0);

    //DESCOBERTA DO SERVIDOR

    // Broadcast permission
    int broadcastPermission = 1;
    setsockopt(clienteSocket, SOL_SOCKET, SO_BROADCAST,(void *)&broadcastPermission, sizeof(broadcastPermission));

    //Timeout de 5 segundos
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(clienteSocket, SOL_SOCKET, SO_RCVTIMEO,(const char*)&tv, sizeof tv);

    //Configuração do endereço de broadcast
    memset(&broadcastAddr, 0, sizeof(broadcastAddr));
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(portaDescoberta);
    broadcastAddr.sin_addr.s_addr = INADDR_BROADCAST; 

    cout << "Buscando servidor Pix na rede(porta " << portaDescoberta << ")..." << endl;

    //Envia mesnagem de descoberta e espera resposta
    sendto(clienteSocket, DISCOVERY_MSG.c_str(), DISCOVERY_MSG.length(), 0,(struct sockaddr *)&broadcastAddr, sizeof(broadcastAddr));
    socklen_t servLen = sizeof(servAddr);
    memset(buffer, 0, 1024);
    ssize_t bytesRecebidos = recvfrom(clienteSocket, buffer, 1024, 0,(struct sockaddr *)&servAddr, &servLen);

    if(bytesRecebidos < 0){
        cerr << "Ninguem respondeu(timeout)." << endl;
        close(clienteSocket);
        return 1;
    }

    //Processa a resposta do servidor(IP e porta de serviço)
    buffer[bytesRecebidos] = '\0';
    int portaServico = stoi(string(buffer));
    
    char ipServidor[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &servAddr.sin_addr, ipServidor, INET_ADDRSTRLEN);

    cout << "Servidor encontrado em " << ipServidor << ":" << portaServico << endl;
    cout << "-----------------------------------------------" << endl;

    servAddr.sin_port = htons(portaServico);

    //REQUISIÇÕES
    string linha, ipDestino, valorStr;
    while(true){
        cout << "\nDigite o comando(Formato: IP_DESTINO VALOR, ou 'sair' para fechar)\n> ";
        getline(cin, linha); //Lê a linha
        if(linha == "sair") break;

        //Divide em IP_DESTINO e VALOR
        size_t pos = linha.find(' ');
        if(pos == string::npos){
            cerr << "Formato invalido" << endl;
            continue;
        }
        ipDestino = linha.substr(0, pos);
        valorStr = linha.substr(pos + 1);

        //Monta e envia a mensagem
        string mensagem = ipDestino + ":" + valorStr;
        
        if(sendto(clienteSocket, mensagem.c_str(), mensagem.length(), 0,(struct sockaddr *)&servAddr, sizeof(servAddr)) == -1){
            cerr << "sendto(Pix) falhou." << endl;
            continue;
        }

        //Aguarda a resposta do servidor
        memset(buffer, 0, 1024);
        bytesRecebidos = recvfrom(clienteSocket, buffer, 1024, 0, NULL, NULL); 

        if(bytesRecebidos < 0){
            cerr << "Servidor nao respondeu a requisicao." << endl;
        } else{
            buffer[bytesRecebidos] = '\0';
            cout << "Resposta do Servidor: " << buffer << endl;
        }
    }

    cout << "Encerrando cliente." << endl;
    close(clienteSocket);
    return 0;
}