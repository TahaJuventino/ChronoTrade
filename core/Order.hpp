#pragma once

#include <iostream>
#include <string>
#include <stdexcept>
#include <iomanip>
#include <cstdint>
#include <cmath>
#include <sstream>

#include "../security/SecurityAwareLogger.hpp"

namespace engine {

    class Order {
        public:
            static constexpr double MIN_PRICE = 0.0001;
            static constexpr double MAX_PRICE = 1e6;
            static constexpr double MIN_AMOUNT = 0.0001;
            static constexpr double MAX_AMOUNT = 1e5;
            static constexpr std::int64_t MIN_TIMESTAMP = 1'000'000'000;
            static constexpr std::int64_t MAX_TIMESTAMP = 2'000'000'000;

            double price;
            double amount;
            std::int64_t timestamp;

            Order(double p, double a, std::int64_t ts) {
                if (!std::isfinite(p)) throw std::invalid_argument("Order::price not finite");
                if (!std::isfinite(a)) throw std::invalid_argument("Order::amount not finite");
                if (p < MIN_PRICE || p > MAX_PRICE)
                    throw std::invalid_argument("Order::price out of bounds");
                if (a < MIN_AMOUNT || a > MAX_AMOUNT)
                    throw std::invalid_argument("Order::amount out of bounds");
                if (ts < MIN_TIMESTAMP || ts > MAX_TIMESTAMP)
                    throw std::invalid_argument("Order::timestamp out of bounds");

                price = p;
                amount = a;
                timestamp = ts;

                security::SecurityAwareLogger::instance().log(
                    security::SecurityAwareLogger::Level::Info,
                    "[Order Created] Price={:.6f} Amount={:.6f} Timestamp={}",
                    price, amount, timestamp);
            }

            std::string to_string() const {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(6)
                    << price << "," << amount << "," << timestamp;
                return oss.str();
            }

            friend std::ostream& operator<<(std::ostream& os, const Order& o) {
                os << std::fixed << std::setprecision(2)
                   << "[Order] Price: " << o.price
                   << ", Amount: " << o.amount
                   << ", Timestamp: " << o.timestamp;
                return os;
            }
    };

} // namespace engine