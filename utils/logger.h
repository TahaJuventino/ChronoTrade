#pragma once
#include <iostream>
#include <fstream>
#include <ctime>
#include <string>
#include <sstream>

// ===== Optional log suppression =====
#define ENABLE_LOGS 0

#if ENABLE_LOGS
#define SAFE_LOG(level) LOG(level)
#else
#define SAFE_LOG(level) if (false) LOG(level)
#endif


inline std::string log_ring[10];
inline int log_index = 0;

#define LOG(level) \
    std::cerr << "[" << #level << "] " << __FILE__ << ":" << __LINE__ << " "

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

