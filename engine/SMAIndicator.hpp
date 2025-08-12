#pragma once

#include "../core/EngineConfig.hpp"
#include "../security/SecurityAwareLogger.hpp"
#include "../engine/IIndicator.hpp"
#include "../core/Candlestick.hpp"
#include "../utils/FixedWindow.hpp"

#include <string>
#include <cmath>
#include <sstream>

namespace engine {

    class SMAIndicator : public IIndicator {
        public:
            explicit SMAIndicator(std::size_t period)
                : window(period), sum(0.0), last_sma(0.0) {}

            void update(const engine::Candlestick& candle) override {
                double close = candle.close;

                if (window.size() == window.capacity()) {
                    sum -= window.at(0);  // evict oldest
                }

                window.push(close);
                sum += close;

                if (window.size() == window.capacity()) {
                    last_sma = sum / static_cast<double>(window.capacity());
                } else {
                    last_sma = sum / static_cast<double>(window.size());
                }

                security::SecurityAwareLogger::instance().log(
                    security::SecurityAwareLogger::Level::Info,
                    "[SMA Update] Close={} SMA={}",
                    close, last_sma);
            }

            std::string signal() const override {
                if (window.size() < 2) return "hold";
                double prev = window.at(window.size() - 2);
                double curr = window.at(window.size() - 1);

                std::string sig;
                if (curr > last_sma && prev <= last_sma) sig = "buy";
                else if (curr < last_sma && prev >= last_sma) sig = "sell";
                else sig = "hold";

                security::SecurityAwareLogger::instance().log(
                    security::SecurityAwareLogger::Level::Info,
                    "[SMA Signal] SMA={} Prev={} Curr={} → Signal={}",
                    last_sma, prev, curr, sig);

                return sig;
            }

            double value() const override {
                return last_sma;
            }

        private:
            engine::FixedWindow<double> window;
            double sum;
            double last_sma;
        };

} // namespace engine