#include <gtest/gtest.h>
#include "../utils/FixedWindow.hpp"
#include <thread>
#include <vector>
#include <random>
#include <numeric>
#include <chrono>

using engine::FixedWindow;

TEST(FixedWindowTest, ZeroCapacityThrows) {
    EXPECT_THROW(FixedWindow<int> fw(0), std::invalid_argument);
}

TEST(FixedWindowTest, PushAndRetrieve) {
    FixedWindow<int> fw(3);
    fw.push(10);
    fw.push(20);
    fw.push(30);
    EXPECT_EQ(fw.size(), 3);
    EXPECT_EQ(fw.at(0), 10);
    EXPECT_EQ(fw.at(1), 20);
    EXPECT_EQ(fw.at(2), 30);
}

TEST(FixedWindowTest, EvictsOldest) {
    FixedWindow<int> fw(2);
    fw.push(1);
    fw.push(2);
    fw.push(3);  // evicts 1
    EXPECT_EQ(fw.size(), 2);
    EXPECT_EQ(fw.at(0), 2);
    EXPECT_EQ(fw.at(1), 3);
}

TEST(FixedWindowTest, OutOfBoundsThrows) {
    FixedWindow<int> fw(2);
    fw.push(1);
    EXPECT_THROW(fw.at(1), std::out_of_range);
}

TEST(FixedWindowTest, CapacityAndSizeCorrect) {
    FixedWindow<int> fw(4);
    EXPECT_EQ(fw.capacity(), 4);
    EXPECT_EQ(fw.size(), 0);
}

TEST(FixedWindowTest, CapacityOneEviction) {
    FixedWindow<int> fw(1);
    fw.push(5);
    fw.push(6);  // evicts 5
    EXPECT_EQ(fw.size(), 1);
    EXPECT_EQ(fw.at(0), 6);
}

TEST(FixedWindowTest, WrapAroundLogic) {
    FixedWindow<int> fw(3);
    for (int i = 1; i <= 6; ++i) fw.push(i);  // Final state: 4, 5, 6
    EXPECT_EQ(fw.at(0), 4);
    EXPECT_EQ(fw.at(1), 5);
    EXPECT_EQ(fw.at(2), 6);
}

TEST(FixedWindowTest, ThreadSafetyUnderLoad) {
    FixedWindow<int> fw(100);
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&fw, t]() {
            for (int i = 0; i < 1000; ++i) {
                fw.push(t * 1000 + i);
            }
        });
    }
    for (auto& th : threads) th.join();
    EXPECT_EQ(fw.size(), 100);  // only last 100 survive
    // Not testing exact values, just stability
}

TEST(FixedWindowTest, FuzzPushVsAtNeverCrashes) {
    FixedWindow<int> fw(10);
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(1, 1000);

    for (int i = 0; i < 50; ++i) fw.push(dist(rng));

    ASSERT_NO_THROW({
        for (std::size_t i = 0; i < fw.size(); ++i)
            (void)fw.at(i);
    });
}

TEST(FixedWindowTest, ValuesRemainCorrectAfterMultiplePushes) {
    FixedWindow<std::string> fw(3);
    fw.push("alpha");
    fw.push("beta");
    fw.push("gamma");
    fw.push("delta"); 

    EXPECT_EQ(fw.at(0), "beta");
    EXPECT_EQ(fw.at(1), "gamma");
    EXPECT_EQ(fw.at(2), "delta");
}

TEST(FixedWindowTest, PushPerformanceUnderHighVolume) {
    FixedWindow<int> fw(10'000);
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 1'000'000; ++i) fw.push(i);

    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    EXPECT_LT(ms, 500);  // Should not exceed 500ms even under heavy load
}

TEST(FixedWindowTest, HandlesLargeNumberOfElements) {
    FixedWindow<int> fw(1000);
    for (int i = 0; i < 1000; ++i) fw.push(i);
    EXPECT_EQ(fw.size(), 1000);
    for (int i = 0; i < 1000; ++i) {
        EXPECT_EQ(fw.at(i), i);
    }
}

TEST(FixedWindowTest, ThreadSafetyUnderConcurrentPush) {
    FixedWindow<int> fw(100);
    std::vector<std::thread> threads;

    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&fw, i]() {
            for (int j = 0; j < 10; ++j) fw.push(i * 10 + j);
        });
    }

    for (auto& t : threads) t.join();

    EXPECT_EQ(fw.size(), 100);
}