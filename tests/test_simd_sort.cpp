#include <gtest/gtest.h>
#include "../core/Order.hpp"
#include "../utils/SimdSort.hpp"
#include "../utils/SimdSortAvx.hpp"

#include <random>
#include <chrono>

using namespace engine;
using namespace utils;

TEST(SimdSortTest, SortsDescendingCorrectly) {
    std::vector<Order> orders = {
        {100.0, 1.0, 1725000001},
        {99.0, 1.0, 1725000002},
        {105.0, 1.0, 1725000003}
    };

    simd_sort_desc(orders);
    ASSERT_EQ(orders[0].price, 105.0);
    ASSERT_EQ(orders[1].price, 100.0);
    ASSERT_EQ(orders[2].price, 99.0);
}

using engine::Order;

TEST(SimdSortTest, CorrectnessBetweenFallbackAndAVX) {
    std::vector<Order> input;
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> price_dist(90.0, 110.0);
    std::uniform_real_distribution<double> amt_dist(0.1, 5.0);

    for (int i = 0; i < 1000; ++i) {
        input.emplace_back(price_dist(rng), amt_dist(rng), 1'725'000'000 + i);
    }

    auto fallback = input;
    auto avx = input;

    utils::simd_sort_desc(fallback);
    utils::simd_sort_desc_avx(avx);

    for (int i = 0; i < static_cast<int>(input.size()); ++i) {
        ASSERT_DOUBLE_EQ(fallback[i].price, avx[i].price) << "Mismatch at index " << i;
    }
}

TEST(SimdSortTest, BenchmarkSimdVsAvx) {
    std::vector<Order> data;
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> price_dist(50.0, 200.0);
    std::uniform_real_distribution<double> amt_dist(0.01, 3.0);

    for (int i = 0; i < 10000; ++i) {
        data.emplace_back(price_dist(rng), amt_dist(rng), 1'725'100'000 + i);
    }

    auto base = data;
    auto avx = data;

    auto t1 = std::chrono::high_resolution_clock::now();
    utils::simd_sort_desc(base);
    auto t2 = std::chrono::high_resolution_clock::now();

    auto t3 = std::chrono::high_resolution_clock::now();
    utils::simd_sort_desc_avx(avx);
    auto t4 = std::chrono::high_resolution_clock::now();

    auto dur_base = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    auto dur_avx = std::chrono::duration_cast<std::chrono::microseconds>(t4 - t3).count();

    std::cout << "[Benchmark] fallback: " << dur_base << "us | AVX: " << dur_avx << "us\n";
}