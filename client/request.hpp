#include <cstdint>
#include <cstddef>


/* Request structures and packet definition */

#define PACKET_TYPE_DISCOVERY      0
#define PACKET_TYPE_REQUEST       1
#define PACKET_TYPE_DISCOVERY_ACK  2
#define PACKET_TYPE_REQUEST_ACK   3

#define DISCOVERY_TIMEOUT_MS 5000
#define REQUEST_ACK_TIMEOUT_MS 5000
#define REQUEST_MAX_RETRIES 3
#define REQUEST_RETRY_DELAY_MS 1000

struct request {
    uint32_t dest_addr;     // Destination address
    uint32_t value;         // Value to be transferred
};

struct request_ack {
    uint32_t seqn;          // Sequence number of the request
    uint32_t new_balance;   // New balance after the transfer
};

typedef struct __packet {
    uint32_t seqn;          // Sequence number of the request
    uint16_t type;          // Type of the packet (DESC | REQ | DESC_ACK | REQ_ACK)
    union {
        struct request req;          // Request data
        struct request_ack ack;      // Request ack data
    } data;
} packet_t;


/* Request functions */

bool send_exit_request(int socket, uint32_t server_ip, uint16_t server_port);
bool send_request_with_retry(int socket, uint32_t server_ip, uint16_t server_port, const request& req, request_ack& ack_out);
uint32_t send_request(int socket, uint32_t server_ip, uint16_t server_port, const request& req);
bool receive_request_ack(int socket, uint32_t expected_seqn, request_ack& ack);