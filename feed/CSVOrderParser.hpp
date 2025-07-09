#pragma once
#include "OrderParser.hpp"
#include "../utils/logger.h"
#include <sstream>
#include <vector>

// Toggle logs locally
#define ENABLE_LOGS 0

#if ENABLE_LOGS
#define SAFE_LOG(level) LOG(level)
#else
#define SAFE_LOG(level) if (false) LOG(level)
#endif

class CSVOrderParser : public OrderParser {
public:
    std::pair<Order, AuthFlags> parse(const std::string& line) override {
        std::stringstream ss(line);
        std::string token;
        std::vector<std::string> tokens;

        while (std::getline(ss, token, ',')) {
            tokens.push_back(token);
        }

        if (tokens.size() != 3) {
            SAFE_LOG(WARN) << "[Malformed CSV] Incorrect number of fields: " << line;
            return { Order(1.0, 1.0, 1'725'000'000), AuthFlags::MALFORMED };
        }

        try {
            double price = std::stod(tokens[0]);
            double amount = std::stod(tokens[1]);
            std::int64_t timestamp = std::stoll(tokens[2]);

            if (price < Order::MIN_PRICE || price > Order::MAX_PRICE) {
                SAFE_LOG(WARN) << "[Suspicious Price] " << price;
                return { Order(1.0, 1.0, 1'725'000'000), AuthFlags::SUSPICIOUS };
            }
            if (amount < Order::MIN_AMOUNT || amount > Order::MAX_AMOUNT) {
                SAFE_LOG(WARN) << "[Suspicious Amount] " << amount;
                return { Order(1.0, 1.0, 1'725'000'000), AuthFlags::SUSPICIOUS };
            }
            if (timestamp < Order::MIN_TIMESTAMP || timestamp > Order::MAX_TIMESTAMP) {
                SAFE_LOG(WARN) << "[Suspicious Timestamp] " << timestamp;
                return { Order(1.0, 1.0, 1'725'000'000), AuthFlags::SUSPICIOUS };
            }

            SAFE_LOG(INFO) << "[Parsed Order] Price=" << price
                           << " Amount=" << amount
                           << " Timestamp=" << timestamp;

            return { Order(price, amount, timestamp), AuthFlags::TRUSTED };

        } catch (...) {
            SAFE_LOG(ERROR) << "[Malformed CSV] Failed to parse fields: " << line;
            return { Order(1.0, 1.0, 1'725'000'000), AuthFlags::MALFORMED };
        }
    }

    std::string source() const override {
        return "CSV";
    }
};
