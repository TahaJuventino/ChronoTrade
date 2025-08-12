#pragma once

#include <gtest/gtest.h>
#include <unistd.h>
#include <fstream>
#include <thread>
#include <vector>

inline std::size_t getCurrentRSS() {
    std::ifstream statm("/proc/self/statm");
    std::size_t total = 0, resident = 0;
    statm >> total >> resident;
    return resident * static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
}

#define EXPECT_NO_MEMORY_LEAK(code)                                                   \
    do {                                                                               \
        std::size_t __before = getCurrentRSS();                                       \
        { code; }                                                                     \
        std::size_t __after = getCurrentRSS();                                        \
        EXPECT_LE(__after, __before) << "Memory leak detected";                      \
    } while (0)

#define EXPECT_THREAD_SAFE(code, num_threads)                                         \
    do {                                                                               \
        auto __expected = code;                                                       \
        std::vector<decltype(__expected)> __results(num_threads);                     \
        std::vector<std::thread> __threads;                                           \
        __threads.reserve(num_threads);                                               \
        for (int __i = 0; __i < (num_threads); ++__i) {                               \
            __threads.emplace_back([&, __i]() { __results[__i] = code; });            \
        }                                                                             \
        for (auto& __t : __threads) {                                                 \
            __t.join();                                                               \
        }                                                                             \
        for (const auto& __r : __results) {                                           \
            EXPECT_EQ(__r, __expected) << "Race condition detected";                 \
        }                                                                             \
    } while (0)

