#pragma once

#include "core/Order.hpp"
#include "utils/SimdSort.hpp"
#include "utils/SimdSortAvx.hpp"
#include "utils/logger.h"
#include <vector>
#include <random>
#include <chrono>

namespace benchmark {

inline void benchmark_simd_sort(std::size_t count = 10000) {
    std::vector<engine::Order> data;
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> dist_price(90.0, 110.0);
    std::uniform_real_distribution<double> dist_amt(0.1, 5.0);

    for (std::size_t i = 0; i < count; ++i) {
        data.emplace_back(dist_price(rng), dist_amt(rng), 1'725'000'000 + i);
    }

    auto ref = data;
    auto avx = data;

    auto start1 = std::chrono::high_resolution_clock::now();
    utils::simd_sort_desc(ref);
    auto end1 = std::chrono::high_resolution_clock::now();

    auto start2 = std::chrono::high_resolution_clock::now();
    utils::simd_sort_desc_avx(avx);
    auto end2 = std::chrono::high_resolution_clock::now();

    auto dur1 = std::chrono::duration_cast<std::chrono::microseconds>(end1 - start1).count();
    auto dur2 = std::chrono::duration_cast<std::chrono::microseconds>(end2 - start2).count();

    SAFE_LOG(INFO) << "[Benchmark] simd_sort_desc = " << dur1 << "us | simd_sort_desc_avx = " << dur2 << "us";

    for (std::size_t i = 0; i < count; ++i) {
        if (ref[i].price != avx[i].price) {
            SAFE_LOG(ERROR) << "[Mismatch] index=" << i << " | ref=" << ref[i].price << " | avx=" << avx[i].price;
            break;
        }
    }
}

} // namespace benchmark