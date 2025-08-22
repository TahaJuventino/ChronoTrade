#pragma once

#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <stdexcept>

namespace utils {
    inline void write_crash_dump(const std::string& msg, const char* file, int line) {
        std::ofstream dump("crash.dump", std::ios::out | std::ios::trunc);
        time_t now = time(nullptr);
        dump << "Timestamp: " << std::ctime(&now);
        dump << "Panic at: " << file << ':' << line << '\n';
        dump << "Reason: " << msg << '\n';
        dump.flush();
    }
} // namespace utils

#if defined(PANIC_THROWS_IN_TESTS)
#define PANIC(msg) do {                                                                \
    std::cerr << "[FATAL] " << msg << " at " << __FILE__ << ":" << __LINE__ << '\n';   \
    std::cerr << ">> System halted. Creating crash.dump...\n" << std::flush;           \
    utils::write_crash_dump((msg), __FILE__, __LINE__);                                \
    throw std::runtime_error(std::string("PANIC: ") + (msg));                          \
} while (0)
#else
#define PANIC(msg) do {                                                                \
    std::cerr << "[FATAL] " << msg << " at " << __FILE__ << ":" << __LINE__ << '\n';   \
    std::cerr << ">> System halted. Creating crash.dump...\n" << std::flush;           \
    utils::write_crash_dump((msg), __FILE__, __LINE__);                                \
    std::exit(EXIT_FAILURE);                                                           \
} while (0)
#endif
