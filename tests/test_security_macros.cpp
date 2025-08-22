#include <gtest/gtest.h>
#include <gtest/gtest-spi.h>   // for EXPECT_NONFATAL_FAILURE / EXPECT_FATAL_FAILURE
#include "security_macros.hpp"
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>

TEST(SecurityMacros, NoMemoryLeakPass) {
    EXPECT_NO_MEMORY_LEAK({
        auto ptr = std::make_unique<int[]>(1024 * 1024);
        (void)ptr;
    });
}

TEST(SecurityMacros, ThreadSafePass) {
    auto safe = []() -> int {
        int local = 0;
        for (int i = 0; i < 1000; ++i) {
            ++local;
        }
        return local;
    };
    EXPECT_THREAD_SAFE(safe(), 8);
}

TEST(SecurityMacros, DetectMemoryLeak) {
    EXPECT_NONFATAL_FAILURE(
        [&]{
            EXPECT_NO_MEMORY_LEAK({
                // ... existing body ...
                auto *p = new int[4];
                (void)p; // intentionally leaked
            });
        }(),
        "Memory leak detected"
    );
}

TEST(SecurityMacros, DetectRace) {
    // Minimal 'race' workload for the macro to call.
    auto race = []() -> int {
        static std::atomic<int> x{0};
        // do something very small to be copyable/returnable
        return ++x;
    };

    EXPECT_NONFATAL_FAILURE(
        [&]{
            EXPECT_THREAD_SAFE(race(), 8);
        }(),
        "Race condition detected"
    );
}