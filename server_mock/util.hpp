#pragma once

#include <string>
#include <signal.h>
#include <ctime>
#include <atomic>

extern std::atomic<bool> running;

std::string timestamp();
void signal_handler(int signum);
uint32_t ip_from_string(const std::string& ip_str);