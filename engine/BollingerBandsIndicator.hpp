#pragma once

#include "../core/EngineConfig.hpp"
#include "../utils/logger.h"

#include "../engine/IIndicator.hpp"
#include "../core/Candlestick.hpp"
#include "../utils/FixedWindow.hpp"

#include <cmath>
#include <string>
#include <limits>
#include <sstream>
#include <optional>
#include <thread>

#define INFO 1
#define WARN 2
#define ERROR 3

namespace engine {

    class BollingerBandsIndicator : public IIndicator {
        public:
            explicit BollingerBandsIndicator(std::size_t period = 20, double k = 2.0);

            void update(const Candlestick& candle) override;
            std::string signal() const override;
            double value() const override;

            // Debug/Introspection
            double get_sma() const { 
                return sma; 
            }
            
            double get_upper_band() const { 
                return upper; 
            }
            
            double get_lower_band() const { 
                return lower; 
            }
            
            double get_band_width() const { 
                return upper - lower; 
            }
            
            double get_stddev() const { 
                return stddev; 
            }
            
            double get_band_distance() const;

            std::pair<double, double> band_values() const {
                return {lower, upper};
            }

            std::optional<std::string> get_last_signal() const { 
                return lastSignal; 
            }

            std::string trace() const;

        private:
            FixedWindow<double> window;
            std::size_t period;
            double multiplier;

            double sma = 0.0;
            double stddev = 0.0;
            double upper = 0.0;
            double lower = 0.0;
            double last_close = -1.0;

            std::optional<std::string> lastSignal = std::nullopt;
        };

        BollingerBandsIndicator::BollingerBandsIndicator(std::size_t p, double k)
            : window(p), period(p), multiplier(k) {}

        void BollingerBandsIndicator::update(const Candlestick& candle) {
            last_close = candle.close;
            if (std::isnan(last_close) || !std::isfinite(last_close))
                throw std::invalid_argument("Invalid close value");

            if (window.size() == window.capacity())
                window.at(0);  // force eviction
            window.push(last_close);

            sma = 0.0;
            for (std::size_t i = 0; i < window.size(); ++i)
                sma += window.at(i);
            sma /= window.size();

            stddev = 0.0;
            for (std::size_t i = 0; i < window.size(); ++i)
                stddev += std::pow(window.at(i) - sma, 2);
            stddev = std::sqrt(stddev / window.size());

            if (std::isnan(stddev) || std::isinf(stddev) || stddev < 1e-10) {
                SAFE_LOG(WARN) << "[Bollinger STDDEV Anomaly] stddev=" << stddev;
                stddev = 0.0;
            }

            upper = sma + multiplier * stddev;
            lower = sma - multiplier * stddev;

            if (window.size() < window.capacity()) {
                lastSignal = std::nullopt;
                return;
            }

            if (last_close > upper)
                lastSignal = "sell";
            else if (last_close < lower)
                lastSignal = "buy";
            else
                lastSignal = "hold";

            SAFE_LOG(INFO) << "[BollingerSignal] "
                        << "version=1.0 "
                        << "signal=" << lastSignal.value()
                        << " thread=" << std::this_thread::get_id()
                        << " time=" << candle.end_time;

            SAFE_LOG(INFO) << trace();
        }

        std::string BollingerBandsIndicator::signal() const {
            return lastSignal.value_or("hold");
        }

        double BollingerBandsIndicator::value() const {
            return sma;
        }

        double BollingerBandsIndicator::get_band_distance() const {
            if (lastSignal == "buy")
                return lower - last_close;
            if (lastSignal == "sell")
                return last_close - upper;
            return std::min(last_close - lower, upper - last_close);
        }

        std::string BollingerBandsIndicator::trace() const {
            std::ostringstream oss;
            oss << "[BollingerTrace] "
                << "SMA: " << sma
                << ", STD: " << stddev
                << ", Upper: " << upper
                << ", Lower: " << lower
                << ", Close: " << last_close
                << ", Signal: " << signal()
                << ", Distance: " << get_band_distance();
            return oss.str();
        }

}  // namespace engine