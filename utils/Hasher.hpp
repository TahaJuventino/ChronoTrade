#pragma once
#include "../core/Order.hpp"
#include <string>
#include <sstream>
#include <vector>
#include "picosha2.h"

inline std::string hash_orders(const std::vector<Order>& orders) {
    std::ostringstream oss;
    for (const auto& o : orders) {
        oss << o.price << "," << o.amount << "," << o.timestamp << ";";
    }
    return picosha2::hash256_hex_string(oss.str());
}
