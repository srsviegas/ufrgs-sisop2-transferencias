#include <iostream>
#include <string>
#include <cstring>

// Cabeçalhos padrão para Sockets em Linux
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

const int TAMANHO_BUFFER = 1024;

int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr << "Uso: " << argv[0] << " <IP_SERVIDOR> <PORTA> <IP_DESTINO_PIX> <VALOR>" << std::endl;
        return 1;
    }

    const char* ipServidor = argv[1];
    int porta = std::stoi(argv[2]);
    const char* ipDestinoPix = argv[3];
    const char* valorStr = argv[4];

    int clienteSocket;
    struct sockaddr_in infoServidor;
    char buffer[TAMANHO_BUFFER];

    // 1. Criar o socket UDP
    if ((clienteSocket = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        std::cerr << "Nao foi possivel criar o socket." << std::endl;
        return 1;
    }

    // 2. Configurar informações do servidor
    infoServidor.sin_family = AF_INET;
    infoServidor.sin_port = htons(porta);
    inet_pton(AF_INET, ipServidor, &infoServidor.sin_addr);

    // 3. Criar a mensagem a ser enviada
    std::string mensagem = std::string(ipDestinoPix) + ":" + std::string(valorStr);

    // 4. Enviar a mensagem para o servidor
    if (sendto(clienteSocket, mensagem.c_str(), mensagem.length(), 0, (struct sockaddr *)&infoServidor, sizeof(infoServidor)) == -1) {
        std::cerr << "sendto falhou." << std::endl;
        close(clienteSocket);
        return 1;
    }

    std::cout << "Requisicao de Pix enviada ao servidor: " << mensagem << std::endl;
    std::cout << "Aguardando resposta..." << std::endl;
    
    // 5. Receber a resposta do servidor
    struct sockaddr_in infoRemoto;
    socklen_t tamanhoInfoRemoto = sizeof(infoRemoto);
    ssize_t bytesRecebidos = recvfrom(clienteSocket, buffer, TAMANHO_BUFFER, 0, (struct sockaddr *)&infoRemoto, &tamanhoInfoRemoto);
    
    if (bytesRecebidos == -1) {
        std::cerr << "recvfrom falhou ou o tempo de espera esgotou." << std::endl;
    } else {
        buffer[bytesRecebidos] = '\0';
        std::cout << "\nResposta do Servidor: " << buffer << std::endl;
    }

    close(clienteSocket);
    return 0;
}