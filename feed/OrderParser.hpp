#pragma once
#include "../core/Order.hpp"
#include "../core/AuthFlags.hpp"

class OrderParser {
public:
    virtual ~OrderParser() = default;

    // Attempts to parse a line and return a valid Order
    virtual std::pair<Order, AuthFlags> parse(const std::string& line) = 0;

    // Optional: identifier for logs / tracing
    virtual std::string source() const = 0;
};
