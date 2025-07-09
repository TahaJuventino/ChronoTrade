#pragma once
#include "../core/Order.hpp"
#include "../core/Candlestick.hpp"
#include "../utils/Hasher.hpp"
#include "../utils/logger.h"
#include <vector>
#include <mutex>
#include <optional>
#include <cfloat>
#include <sstream>
#include <cmath>

// Optional toggle at top of file (or move to config.hpp)
#define ENABLE_LOGS 0

class CandlestickGenerator {
private:
    std::mutex mtx;
    std::vector<Order> window;
    std::int64_t window_start;
    const std::int64_t window_duration; // in seconds

    int accepted_orders = 0;
    int late_orders = 0;
    int dropped_orders = 0;

public:
    explicit CandlestickGenerator(std::int64_t duration)
        : window_start(0), window_duration(duration) {}

    std::optional<Candlestick> flush_if_ready(std::int64_t current_time) {
        std::lock_guard<std::mutex> lock(mtx);
        if (window.empty()) return std::nullopt;

        if (current_time >= window_start + window_duration) {
            const double open = window.front().price;
            const double close = window.back().price;
            double high = open, low = open, volume = 0.0;

            for (const auto& o : window) {
                if (o.price > high) high = o.price;
                if (o.price < low)  low  = o.price;

                double tmp = volume + o.amount;
                if (!std::isfinite(tmp)) {
                    SAFE_LOG(ERROR) << "[Overflow Detected] volume=" << volume
                                    << " + amount=" << o.amount;
                    throw std::overflow_error("Volume accumulation overflow");
                }
                volume = tmp;
            }

            Candlestick candle(
                open, high, low, close, volume,
                window_start, window_start + window_duration
            );

            dropped_orders += static_cast<int>(window.size());

            std::ostringstream oss_trace;
            oss_trace << "[Flush Trace] SHA256 = " << hash_orders(window);
            SAFE_LOG(INFO) << oss_trace.str();

            std::ostringstream oss_summary;
            oss_summary << "[Flush] Window Start=" << window_start
                        << " | Accepted=" << accepted_orders
                        << " | Late=" << late_orders
                        << " | Dropped=" << dropped_orders;
            SAFE_LOG(INFO) << oss_summary.str();

            window.clear();
            accepted_orders = 0;
            late_orders = 0;
            dropped_orders = 0;

            return std::make_optional(std::move(candle));
        }

        return std::nullopt;
    }

    void insert(const Order& order) {
        std::lock_guard<std::mutex> lock(mtx);

        if (window.empty()) {
            window_start = order.timestamp;
        }

        if (order.timestamp < window_start + window_duration) {
            window.push_back(order);
            accepted_orders++;

            std::ostringstream oss_accept;
            oss_accept << "[Order Accepted] " << order;
            SAFE_LOG(INFO) << oss_accept.str();
        } else {
            late_orders++;
            std::ostringstream oss;
            oss << "[Late Order Dropped] " << order;
            SAFE_LOG(WARN) << oss.str();
        }
    }
};
