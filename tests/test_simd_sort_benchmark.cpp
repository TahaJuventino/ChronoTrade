#include <gtest/gtest.h>
#include "../core/Order.hpp"
#include "../utils/SimdSortAVX.hpp"
#include <vector>
#include <random>
#include <algorithm>
#include <chrono>
#include <iostream>

using namespace engine;
using namespace utils;

TEST(SimdSortBenchmark, CompareStdSortAndSimdSort) {
    constexpr int N = 100000;
    std::vector<Order> data;
    data.reserve(N);

    std::mt19937 rng(42);
    std::uniform_real_distribution<double> price_dist(1.0, 1000.0);   // valid
    std::uniform_real_distribution<double> amount_dist(1.0, 100.0);   // valid
    std::uniform_int_distribution<int64_t> ts_dist(1'700'000'000, 1'700'100'000); // safe range

    for (int i = 0; i < N; ++i) {
        double price = price_dist(rng);
        double amount = amount_dist(rng);
        int64_t ts = ts_dist(rng);  // 🔒 ensure valid timestamp

        data.emplace_back(price, amount, ts);
    }

    std::vector<Order> baseline = data;
    std::vector<Order> simd_data = data;

    auto t1 = std::chrono::high_resolution_clock::now();
    std::sort(baseline.begin(), baseline.end(), [](const Order& a, const Order& b) {
        return a.price > b.price;
    });
    auto t2 = std::chrono::high_resolution_clock::now();

    auto t3 = std::chrono::high_resolution_clock::now();
    simd_sort_desc_avx(simd_data);
    auto t4 = std::chrono::high_resolution_clock::now();

    auto std_time = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    auto simd_time = std::chrono::duration_cast<std::chrono::milliseconds>(t4 - t3).count();

    std::cout << "[std::sort] " << std_time << " ms\n";
    std::cout << "[SIMD sort] " << simd_time << " ms\n";

    for (int i = 0; i < N; ++i) {
        EXPECT_DOUBLE_EQ(simd_data[i].price, baseline[i].price);
    }
}