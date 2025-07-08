#pragma once
#include "Order.hpp"
#include <vector>
#include <mutex>
#include <algorithm>

class OrderBook {
private:
    std::vector<Order> orders;
    mutable std::mutex mtx;

public:
    void insert(const Order& o) {
        std::lock_guard<std::mutex> lock(mtx);
        orders.push_back(o);
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mtx);
        return orders.size();
    }

    std::vector<Order> snapshot() const {
        std::lock_guard<std::mutex> lock(mtx);
        return orders; // return copy for read-only use
    }

    void sort_by_price_desc() {
        std::lock_guard<std::mutex> lock(mtx);
        std::sort(orders.begin(), orders.end(),
                  [](const Order& a, const Order& b) { return a.price > b.price; });
    }
};
