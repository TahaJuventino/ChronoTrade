#pragma once

#include "../core/Order.hpp"
#include "../utils/logger.h"
#include <vector>
#include <algorithm>
#include <immintrin.h>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace utils {

    inline void simd_sort_desc(std::vector<engine::Order>& orders) {
        using namespace std::chrono;
        auto start = high_resolution_clock::now();

#if defined(__AVX2__)
        // Placeholder for actual AVX2 sort
        std::sort(orders.begin(), orders.end(), [](const auto& a, const auto& b) {
            return a.price > b.price;
        });
#else
        std::sort(orders.begin(), orders.end(), [](const auto& a, const auto& b) {
            return a.price > b.price;
        });
#endif

        auto end = high_resolution_clock::now();
        auto duration_us = duration_cast<microseconds>(end - start).count();

        std::ostringstream oss;
        oss << "[SIMD Sort] Sorted " << orders.size() << " orders in "
            << std::fixed << std::setprecision(3) << duration_us << " µs.";
        SAFE_LOG(INFO) << oss.str();
    }

}