#pragma once
#include "../core/Order.hpp"
#include "../core/Candlestick.hpp"
#include "../utils/Hasher.hpp"
#include "../core/EngineConfig.hpp"
#include "../threads/ThreadPool.hpp"
#include "../engine/IndicatorRegistry.hpp"
#include "../utils/logger.h"
#include <vector>
#include <mutex>
#include <optional>
#include <cfloat>
#include <sstream>
#include <cmath>

namespace engine {  

    class CandlestickGenerator {
    private:
        std::mutex mtx;
        std::vector<Order> window;
        std::int64_t window_start;
        const std::int64_t window_duration;

        int accepted_orders = 0;
        int late_orders = 0;
        int dropped_orders = 0;

        engine::IndicatorRegistry* bound_registry = nullptr;
        threads::ThreadPool* bound_pool = nullptr;

    public:
        explicit CandlestickGenerator(std::int64_t duration)
            : window_start(0), window_duration(duration) {}

        void bind_registry(engine::IndicatorRegistry* r) { bound_registry = r; }
       
        void bind_thread_pool(threads::ThreadPool* p) { bound_pool = p; }

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

                SAFE_LOG(INFO) << "[Flush Trace] SHA256 = " << hash_orders(window);
                SAFE_LOG(INFO) << "[Flush] Window Start=" << window_start
                               << " | Accepted=" << accepted_orders
                               << " | Late=" << late_orders
                               << " | Dropped=" << dropped_orders;

                window.clear();
                accepted_orders = 0;
                late_orders = 0;
                dropped_orders = 0;

                if (bound_registry && bound_pool) {
                    auto copy = candle;  // copy before moving
                    bound_pool->submit([reg = bound_registry, c = std::move(copy)] {
                        reg->update_all(c);
                    });
                } else {
                    SAFE_LOG(WARN) << "[CandlestickGenerator] No registry or thread pool bound.";
                }
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
                SAFE_LOG(INFO) << "[Order Accepted] " << order;
            } else {
                late_orders++;
                SAFE_LOG(WARN) << "[Late Order Dropped] " << order;
            }
        }
    };

} // namespace engine