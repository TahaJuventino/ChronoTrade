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

    class RSIIndicator : public IIndicator {
        public:
            explicit RSIIndicator(std::size_t period);

            void update(const Candlestick& candle) override;
            std::string signal() const override;
            double value() const override;

        private:
            std::size_t period;
            FixedWindow<double> gains;
            FixedWindow<double> losses;
            double last_close = -1.0;
            double rsi = 50.0;
        };

        RSIIndicator::RSIIndicator(std::size_t p)
            : period(p), gains(p), losses(p) {}

        void RSIIndicator::update(const Candlestick& candle) {
            if (last_close < 0) {
                last_close = candle.close;
                return;
            }

            double delta = candle.close - last_close;
            double gain = delta > 0 ? delta : 0.0;
            double loss = delta < 0 ? -delta : 0.0;

            if (gains.size() == gains.capacity()) {
                gains.at(0);  // trigger eviction
                losses.at(0);
            }

            gains.push(gain);
            losses.push(loss);

            double avg_gain = 0.0;
            double avg_loss = 0.0;

            for (std::size_t i = 0; i < gains.size(); ++i) avg_gain += gains.at(i);
            for (std::size_t i = 0; i < losses.size(); ++i) avg_loss += losses.at(i);

            avg_gain /= gains.size();
            avg_loss /= losses.size();

            if (avg_loss == 0) {
                rsi = 100.0;
            } else {
                double rs = avg_gain / avg_loss;
                rsi = 100.0 - (100.0 / (1.0 + rs));
            }

            last_close = candle.close;

            security::SecurityAwareLogger::instance().log(
                security::SecurityAwareLogger::Level::Info,
                "[RSI Update] Close={} Gain={} Loss={} RSI={}",
                candle.close, gain, loss, rsi);
        }

        double RSIIndicator::value() const {
            return rsi;
        }

        std::string RSIIndicator::signal() const {
            std::string s;
            if (rsi >= 70.0) s = "sell";
            else if (rsi <= 30.0) s = "buy";
            else s = "hold";

            security::SecurityAwareLogger::instance().log(
                security::SecurityAwareLogger::Level::Info,
                "[RSI Signal] RSI={} → Signal={}",
                rsi, s);
            return s;
        }

} // namespace engine