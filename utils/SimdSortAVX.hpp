#pragma once

#include "../core/Order.hpp"

#include <vector>
#include <algorithm>
#include <immintrin.h>
#include "../security/SecurityAwareLogger.hpp"

namespace utils {

inline void simd_sort_desc_avx(std::vector<engine::Order>& orders) {
#if defined(__AVX2__)
    auto start = std::chrono::high_resolution_clock::now();

    // For now, fallback to std::sort while AVX logic is designed (Phase 4)
    std::sort(orders.begin(), orders.end(), [](const engine::Order& a, const engine::Order& b) {
        return a.price > b.price;
    });

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    security::SecurityAwareLogger::instance().log(
        security::SecurityAwareLogger::Level::Info,
        "[SIMD Sort AVX] Used AVX2 fallback | count={} | time={}us",
        orders.size(), duration.count());
#else
    security::SecurityAwareLogger::instance().log(
        security::SecurityAwareLogger::Level::Warn,
        "[SIMD Sort AVX] AVX2 not supported. Falling back to std::sort.");
    std::sort(orders.begin(), orders.end(), [](const engine::Order& a, const engine::Order& b) {
        return a.price > b.price;
    });
#endif
}

} // namespace utils
