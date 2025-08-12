#pragma once

#include "../core/Order.hpp"
#include "../core/AuthFlags.hpp"
#include "../core/EngineConfig.hpp"
#include "../security/SecurityAwareLogger.hpp"

namespace feed {

    class OrderParser {
        public:
            virtual ~OrderParser() = default;

            // Attempts to parse a line and return a valid Order
            virtual std::pair<engine::Order, engine::AuthFlags> parse(const std::string& line) = 0;

            // Optional: identifier for logs / tracing
            virtual std::string source() const = 0;
        };

} // namespace feed 