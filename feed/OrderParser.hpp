#pragma once
#include "../core/Order.hpp"
#include "../core/AuthFlags.hpp"
#include "../utils/logger.h"

// Local logging toggle for future use (currently no logs here)
#define ENABLE_LOGS 0

#if ENABLE_LOGS
#define SAFE_LOG(level) LOG(level)
#else
#define SAFE_LOG(level) if (false) LOG(level)
#endif

class OrderParser {
public:
    virtual ~OrderParser() = default;

    // Attempts to parse a line and return a valid Order
    virtual std::pair<Order, AuthFlags> parse(const std::string& line) = 0;

    // Optional: identifier for logs / tracing
    virtual std::string source() const = 0;
};
