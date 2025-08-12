#include <gtest/gtest.h>
#include "security_macros.hpp"
#include <memory>
#include <mutex>
#include <thread>

TEST(SecurityMacros, NoMemoryLeakPass) {
    EXPECT_NO_MEMORY_LEAK({
        auto ptr = std::make_unique<int[]>(1024 * 1024);
        (void)ptr;
    });
}

TEST(SecurityMacros, DetectMemoryLeak) {
    EXPECT_NONFATAL_FAILURE({
        EXPECT_NO_MEMORY_LEAK({
            int* leak = new int[1024 * 1024];
            (void)leak;
        });
    }, "Memory leak detected");
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

TEST(SecurityMacros, DetectRace) {
    int counter = 0;
    auto race = [&]() -> int {
        int local = counter;
        for (int i = 0; i < 1000; ++i) {
            ++local;
        }
        counter = local;
        return counter;
    };
    EXPECT_NONFATAL_FAILURE({
        EXPECT_THREAD_SAFE(race(), 8);
    }, "Race condition detected");
}

