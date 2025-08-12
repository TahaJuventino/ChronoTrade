#pragma once

#include "../core/EngineConfig.hpp"
#include "../security/SecurityAwareLogger.hpp"
#include "../engine/IIndicator.hpp"
#include "../core/Candlestick.hpp"
#include "../utils/FixedWindow.hpp"

#include <cmath>
#include <string>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <cassert>

namespace engine {

    class MACDIndicator : public IIndicator {
        public:
            MACDIndicator(std::size_t fast = 12, std::size_t slow = 26, std::size_t signal = 9);

            void update(const Candlestick& candle) override;
            std::string signal() const override;
            double value() const override;

            double get_fast_ema() const { return fastEMA; }
            double get_slow_ema() const { return slowEMA; }
            double get_macd_line() const { return macdLine; }
            double get_signal_line() const { return signalLine; }
            double get_histogram() const { return histogram; }
            std::optional<std::string> get_last_crossover() const { return lastCrossover; }

        private:
            double computeEMA(double price, double prev_ema, double period) const;

            double fastEMA = 0.0;
            double slowEMA = 0.0;
            double signalEMA = 0.0;

            double macdLine = 0.0;
            double signalLine = 0.0;
            double histogram = 0.0;

            std::size_t fastPeriod;
            std::size_t slowPeriod;
            std::size_t signalPeriod;
            std::size_t steps = 0;

            double lastHistogram = 0.0;
            double lastClose = -1.0;
            std::optional<std::string> lastCrossover = std::nullopt;
        };

        MACDIndicator::MACDIndicator(std::size_t fast, std::size_t slow, std::size_t signal)
            : fastPeriod(fast), slowPeriod(slow), signalPeriod(signal) {
            if (fast == 0 || slow == 0 || signal == 0) {
                throw std::invalid_argument("MACD periods must be > 0");
            }
        }

        double MACDIndicator::computeEMA(double price, double prev_ema, double period) const {
            assert(period > 0);
            double multiplier = 2.0 / (period + 1);
            return (price - prev_ema) * multiplier + prev_ema;
        }

        void MACDIndicator::update(const Candlestick& candle) {
            double close = candle.close;

            if (lastClose < 0.0) {
                fastEMA = slowEMA = signalEMA = close;
                lastClose = close;
                ++steps;
                return;
            }

            fastEMA = computeEMA(close, fastEMA, fastPeriod);
            slowEMA = computeEMA(close, slowEMA, slowPeriod);
            macdLine = fastEMA - slowEMA;

            signalEMA = computeEMA(macdLine, signalEMA, signalPeriod);
            signalLine = signalEMA;

            lastHistogram = histogram;
            histogram = macdLine - signalLine;

            if (steps >= slowPeriod + signalPeriod) {
                double threshold = std::max(1e-6, std::abs(histogram) * 0.001);

                if (lastHistogram <= 0 && histogram > threshold) {
                    lastCrossover = "buy";
                    security::SecurityAwareLogger::instance().log(
                        security::SecurityAwareLogger::Level::Info,
                        "[MACD Crossover] Histogram flipped + @ step={}",
                        steps);
                } else if (lastHistogram >= 0 && histogram < -threshold) {
                    lastCrossover = "sell";
                    security::SecurityAwareLogger::instance().log(
                        security::SecurityAwareLogger::Level::Info,
                        "[MACD Crossover] Histogram flipped - @ step={}",
                        steps);
                }
            }

            security::SecurityAwareLogger::instance().log(
                security::SecurityAwareLogger::Level::Info,
                "[MACD Update] close={} fast={} slow={} MACD={} Signal={} Hist={}",
                close, fastEMA, slowEMA, macdLine, signalLine, histogram);

            lastClose = close;
            ++steps;
        }

        std::string MACDIndicator::signal() const {
            if (steps < slowPeriod + signalPeriod)
                return "hold";
            if (lastCrossover.has_value())
                return *lastCrossover;
            return "hold";
        }

        double MACDIndicator::value() const {
            return histogram;
        }

} // namespace engine
