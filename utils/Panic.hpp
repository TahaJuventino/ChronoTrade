#pragma once

#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>

namespace utils {

inline void write_crash_dump(const std::string &msg, const char *file, int line) {
    std::ofstream dump("crash.dump");
    time_t now = time(nullptr);
    dump << "Timestamp: " << std::ctime(&now);
    dump << "Panic at: " << file << ':' << line << '\n';
    dump << "Reason: " << msg << '\n';
}

} // namespace utils

#define PANIC(msg)                                                                  \
    do {                                                                            \
        std::cerr << "[FATAL] " << msg << " at " << __FILE__ << ":" << __LINE__   \
                  << std::endl;                                                     \
        std::cerr << ">> System halted. Creating crash.dump...\n";                  \
        utils::write_crash_dump(msg, __FILE__, __LINE__);                           \
        std::exit(EXIT_FAILURE);                                                    \
    } while (0)

