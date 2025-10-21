#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <cstring> // Para memset

// Cabeçalhos padrão para Sockets em Linux
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h> // Para a função close()

const int TAMANHO_BUFFER = 1024;

using namespace std;

int main(int argc, char *argv[]){
    // Valida se a porta foi passada como argumento
    if(argc != 2){
        cerr << "Uso: " << argv[0] << " <porta>" << endl;
        return 1;
    }

    int porta;
    try{
        porta = stoi(argv[1]);
        if(porta <= 1024 || porta > 65535){
             cerr << "Aviso: A porta deve ser um numero entre 1025 e 65535. Portas baixas requerem privilegios de root." << endl;
        }
    }catch(const invalid_argument& e){
        cerr << "Erro: Porta invalida. Forneca um numero." << endl;
        return 1;
    }

    // Simulação de um banco de dados de clientes(IP -> Saldo)
    map<string, double> saldosClientes;
    saldosClientes["127.0.0.1"] = 1000.00; // Cliente local 1
    saldosClientes["192.168.0.10"] = 500.50;  // Exemplo de outro cliente na rede
    saldosClientes["192.168.0.11"] = 250.00;  // Exemplo de outro cliente na rede

    int servidorSocket;
    struct sockaddr_in infoServidor, infoCliente;
    char buffer[TAMANHO_BUFFER];

    // 1. Criar o socket UDP
    if((servidorSocket = socket(AF_INET, SOCK_DGRAM, 0)) == -1){
        cerr << "Nao foi possivel criar o socket." << endl;
        return 1;
    }
    cout << "Socket UDP criado com sucesso." << endl;

    // 2. Preparar a estrutura sockaddr_in
    infoServidor.sin_family = AF_INET;
    infoServidor.sin_addr.s_addr = INADDR_ANY; // Aceita conexões de qualquer IP
    infoServidor.sin_port = htons(porta);

    // 3. Fazer o bind do socket à porta e endereço
    if(bind(servidorSocket,(struct sockaddr *)&infoServidor, sizeof(infoServidor)) == -1){
        cerr << "Bind falhou." << endl;
        close(servidorSocket);
        return 1;
    }
    cout << "Bind realizado na porta " << porta << endl;
    cout << "Servidor aguardando transacoes..." << endl;

    // 4. Loop principal para receber mensagens
    while(true){
        socklen_t tamanhoInfoCliente = sizeof(infoCliente);
        ssize_t bytesRecebidos;

        // Limpa o buffer
        memset(buffer, 0, TAMANHO_BUFFER);

        // Recebe dados do cliente(chamada bloqueante)
        bytesRecebidos = recvfrom(servidorSocket, buffer, TAMANHO_BUFFER, 0,(struct sockaddr *)&infoCliente, &tamanhoInfoCliente);
        if(bytesRecebidos == -1){
            cerr << "recvfrom falhou." << endl;
            continue;
        }

        // Obtém o IP do remetente
        char ipRemetente[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &infoCliente.sin_addr, ipRemetente, INET_ADDRSTRLEN);
        string ipRemetenteStr(ipRemetente);

        buffer[bytesRecebidos] = '\0'; // Adiciona terminador nulo para segurança
        cout << "\nRecebido de " << ipRemetenteStr << ": " << buffer << endl;

        // Processa a mensagem: "IP_DESTINO:VALOR"
        string mensagem(buffer);
        string resposta;
        size_t pos = mensagem.find(':');

        if(pos == string::npos){
            resposta = "ERRO: Formato da mensagem invalido. Use IP_DESTINO:VALOR";
        }else{
            string ipDestino = mensagem.substr(0, pos);
            try{
                double valor = stod(mensagem.substr(pos + 1));

                // Validação da transação
                if(saldosClientes.find(ipRemetenteStr) == saldosClientes.end()){
                    resposta = "ERRO: Voce nao e um cliente registrado.";
                }else if(saldosClientes.find(ipDestino) == saldosClientes.end()){
                    resposta = "ERRO: O destinatario nao e um cliente valido.";
                }else if(valor <= 0){
                    resposta = "ERRO: O valor da transferencia deve ser positivo.";
                }else if(saldosClientes[ipRemetenteStr] < valor){
                    resposta = "ERRO: Saldo insuficiente.";
                }else if(ipRemetenteStr == ipDestino){
                    resposta = "ERRO: Nao e possivel enviar Pix para si mesmo.";
                }else{
                    // Transação Válida!
                    saldosClientes[ipRemetenteStr] -= valor;
                    saldosClientes[ipDestino] += valor;
                    resposta = "SUCESSO: Transferencia realizada. Seu novo saldo: " + to_string(saldosClientes[ipRemetenteStr]);
                    
                    cout << ">>> Transacao processada: " << ipRemetenteStr << " -> " << ipDestino << "(R$ " << valor << ")" << endl;
                    cout << "    Novo saldo de " << ipRemetenteStr << ": R$ " << saldosClientes[ipRemetenteStr] << endl;
                    cout << "    Novo saldo de " << ipDestino << ": R$ " << saldosClientes[ipDestino] << endl;
                }
            }catch(const invalid_argument& e){
                resposta = "ERRO: Valor da transferencia invalido.";
            }
        }

        // Envia a resposta de volta ao cliente
        sendto(servidorSocket, resposta.c_str(), resposta.length(), 0,(struct sockaddr *)&infoCliente, tamanhoInfoCliente);
    }

    close(servidorSocket);
    return 0;
}