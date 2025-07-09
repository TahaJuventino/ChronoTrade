#pragma once
#include "Order.hpp"
#include <vector>
#include <mutex>
#include <algorithm>
#include <unordered_set>
#include "EngineConfig.hpp"
#include "../utils/logger.h"

// Local or global log toggle
#define ENABLE_LOGS 0

class OrderBook {
private:
    std::vector<Order> orders;
    mutable std::mutex mtx;
    std::unordered_set<std::int64_t> seen_timestamps;

public:
    void insert(const Order& o) {
        std::lock_guard<std::mutex> lock(mtx);

        if (seen_timestamps.contains(o.timestamp)) {
            SAFE_LOG(WARN) << "[Duplicate Timestamp] " << o;

            if (!EngineConfig::allow_duplicate_timestamps) {
                return;
            }
        }

        seen_timestamps.insert(o.timestamp);
        orders.push_back(o);
        SAFE_LOG(INFO) << "[Order Inserted] " << o;
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mtx);
        return orders.size();
    }

    std::vector<Order> snapshot() const {
        std::lock_guard<std::mutex> lock(mtx);
        return orders;
    }

    void sort_by_price_desc() {
        std::lock_guard<std::mutex> lock(mtx);
        std::sort(orders.begin(), orders.end(),
                  [](const Order& a, const Order& b) { return a.price > b.price; });
    }
};
