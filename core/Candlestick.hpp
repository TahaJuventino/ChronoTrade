#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <sstream>
#include <iomanip>

#include "../security/SecurityAwareLogger.hpp"

namespace engine {

    class Candlestick {
        public:
            double open;
            double high;
            double low;
            double close;
            double volume;
            std::int64_t start_time;
            std::int64_t end_time;

            Candlestick(double o, double h, double l, double c, double v,
                        std::int64_t start, std::int64_t end)
                : open(o), high(h), low(l), close(c), volume(v),
                start_time(start), end_time(end) {

                if (!(low <= open && open <= high))
                    throw std::invalid_argument("Invariant failed: low ≤ open ≤ high");
                if (!(low <= close && close <= high))
                    throw std::invalid_argument("Invariant failed: low ≤ close ≤ high");
                if (start >= end)
                    throw std::invalid_argument("Invalid time window: start >= end");
                if (volume < 0)
                    throw std::invalid_argument("Negative volume");

                security::SecurityAwareLogger::instance().log(
                    security::SecurityAwareLogger::Level::Info,
                    "[Candle Created] {}",
                    to_string());
            }

            std::string to_string() const {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(2)
                    << "[Candle] O: " << open << " H: " << high << " L: " << low
                    << " C: " << close << " V: " << volume
                    << " T: [" << start_time << " → " << end_time << "]";
                return oss.str();
            }
        };

} // namespace engine
