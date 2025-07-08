#pragma once
#include <iostream>
#include <fstream>
#include <ctime>
#include <string>
#include <sstream>

inline std::string log_ring[10];
inline int log_index = 0;

inline void log_impl(const std::string& level, const char* file, int line) {
    std::ostringstream out;
    out << "[" << level << "] " << file << ":" << line;
    log_ring[log_index % 10] = out.str();
    std::cerr << log_ring[log_index % 10] << std::endl;
    log_index++;
}

#define LOG(level) log_impl(#level, __FILE__, __LINE__)

#define PANIC(msg) \
    do { \
        std::cerr << "[FATAL] " << msg << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::cerr << ">> System halted. Creating crash.dump...\n"; \
        std::ofstream dump("crash.dump"); \
        time_t now = time(nullptr); \
        dump << "Timestamp: " << std::ctime(&now); \
        dump << "Panic at: " << __FILE__ << ":" << __LINE__ << "\n"; \
        dump << "Reason: " << msg << "\n"; \
        dump << "Log history:\n"; \
        for (int i = 0; i < 10; ++i) dump << "- " << log_ring[i] << "\n"; \
        dump.close(); \
        std::exit(EXIT_FAILURE); \
    } while (0)
