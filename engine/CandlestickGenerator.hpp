#pragma once
#include "../core/Order.hpp"
#include "../core/Candlestick.hpp"
#include <vector>
#include <mutex>
#include <optional>

class CandlestickGenerator {
private:
    std::mutex mtx;
    std::vector<Order> window;
    std::int64_t window_start;
    const std::int64_t window_duration; // in seconds

public:
    explicit CandlestickGenerator(std::int64_t duration)
        : window_start(0), window_duration(duration) {}

    void insert(const Order& order) {
        std::lock_guard<std::mutex> lock(mtx);

        if (window.empty()) {
            window_start = order.timestamp;
        }

        if (order.timestamp < window_start + window_duration) {
            window.push_back(order);
        }
        // else: too late for current window → drop silently (future: stats)
    }

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
                volume += o.amount;
            }

            Candlestick candle(
                open, high, low, close, volume,
                window_start, window_start + window_duration
            );

            window.clear();
            return std::make_optional(std::move(candle));
        }

        return std::nullopt;
    }
};
