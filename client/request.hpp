#include <cstdint>
#include <cstddef>


/* Request structures and packet definition */

#define PACKET_TYPE_CLIENT_DISCOVERY   1
#define PACKET_TYPE_CLIENT_ACK         2

#define PACKET_TYPE_REQUEST            3
#define PACKET_TYPE_REQUEST_ACK        4

#define PACKET_TYPE_SERVER_DISCOVERY   5
#define PACKET_TYPE_STATE_UPDATE       6
#define PACKET_TYPE_HEARTBEAT           7
#define PACKET_TYPE_SERVER_ACK          8
#define PACKET_TYPE_PRIMARY_ANNOUNCE   9


#define DISCOVERY_TIMEOUT_MS 5000
#define REQUEST_ACK_TIMEOUT_MS 5000
#define REQUEST_MAX_RETRIES 3
#define REQUEST_RETRY_DELAY_MS 1000

#define MAX_CLIENTES 128
#define MAX_TRANSACOES 256

struct cliente_state {
    uint32_t ip;
    uint32_t saldo;
    uint32_t last_req;
};

struct transacao_state {
    uint32_t ipRemetente;
    uint32_t ipDestino;
    uint32_t valor;
    uint32_t req_id;
};

struct state_update {
    uint32_t num_clientes;
    uint32_t num_transacoes;

    cliente_state clientes[MAX_CLIENTES];
    transacao_state transacoes[MAX_TRANSACOES];

    uint32_t num_transactions;
    uint32_t total_transferred;
    uint32_t total_balance;
};


struct request {
    uint32_t dest_addr;     // Destination address
    uint32_t value;         // Value to be transferred
};

struct request_ack {
    uint32_t seqn;          // Sequence number of the request
    uint32_t new_balance;   // New balance after the transfer
};

struct primary_announce {
    uint16_t server_port;  // porta do novo PRIMARY
};

typedef struct __packet {
    uint32_t seqn;
    uint16_t type;
    union {
        struct request req;
        struct request_ack ack;
        struct state_update state;
        struct primary_announce primary_announce; // <- adicionado
    } data;
} packet_t;


/* Request functions */

bool send_exit_request(int socket, uint32_t server_ip, uint16_t server_port);
bool send_request_with_retry(int socket, uint32_t server_ip, uint16_t server_port, const request& req, request_ack& ack_out);
uint32_t send_request(int socket, uint32_t server_ip, uint16_t server_port, const request& req);
bool receive_request_ack(int socket, uint32_t expected_seqn, request_ack& ack);