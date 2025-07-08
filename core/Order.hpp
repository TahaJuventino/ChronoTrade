#pragma once
#include <iostream>
#include <string>
#include <stdexcept>
#include <iomanip>
#include <cstdint>

class Order {
public:
    static constexpr double MIN_PRICE = 0.0001;
    static constexpr double MAX_PRICE = 1e6;
    static constexpr double MIN_AMOUNT = 0.0001;
    static constexpr double MAX_AMOUNT = 1e5;
    static constexpr std::int64_t MIN_TIMESTAMP = 1'000'000'000; // after year ~2001
    static constexpr std::int64_t MAX_TIMESTAMP = 2'000'000'000; // before ~2033

    double price;
    double amount;
    std::int64_t timestamp;

    Order(double p, double a, std::int64_t ts) {
        if (p < MIN_PRICE || p > MAX_PRICE)
            throw std::invalid_argument("Order::price out of bounds");
        if (a < MIN_AMOUNT || a > MAX_AMOUNT)
            throw std::invalid_argument("Order::amount out of bounds");
        if (ts < MIN_TIMESTAMP || ts > MAX_TIMESTAMP)
            throw std::invalid_argument("Order::timestamp out of bounds");

        price = p;
        amount = a;
        timestamp = ts;
    }

    friend std::ostream& operator<<(std::ostream& os, const Order& o) {
        os << std::fixed << std::setprecision(2)
           << "[Order] Price: " << o.price
           << ", Amount: " << o.amount
           << ", Timestamp: " << o.timestamp;
        return os;
    }
};
