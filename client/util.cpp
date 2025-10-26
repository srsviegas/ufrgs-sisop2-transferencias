#include <string>
#include <signal.h>
#include <ctime>
#include <arpa/inet.h>

#include "util.hpp"


std::string timestamp() {
    char buf[64];
    time_t now = time(nullptr);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return buf;
}

void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        // TODO: send EXIT to server before exiting
        exit(EXIT_SUCCESS);
    }
}

uint32_t ip_from_string(const std::string& ip_str) {
    struct in_addr addr;
    if (inet_pton(AF_INET, ip_str.c_str(), &addr) == 0) {
        return 0;
    }
    return ntohl(addr.s_addr);
}