#pragma once

#include "../core/EngineConfig.hpp"
#include "../security/SecurityAwareLogger.hpp"

namespace engine {

    class Candlestick;

    class IIndicator {
        public:
            virtual void update(const Candlestick& candle) = 0;
            virtual std::string signal() const = 0;
            virtual double value() const = 0;
            virtual ~IIndicator() = default;
        };

} // namespace engine