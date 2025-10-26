#include <string>
#include <signal.h>
#include <ctime>


std::string timestamp();
void signal_handler(int signum);
uint32_t ip_from_string(const std::string& ip_str);