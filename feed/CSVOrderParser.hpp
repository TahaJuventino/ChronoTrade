#pragma once

#include "OrderParser.hpp"
#include "../core/EngineConfig.hpp"
#include "../security/SecurityAwareLogger.hpp"

#include <sstream>
#include <vector>

namespace feed {

    class CSVOrderParser : public OrderParser {
        public:
            std::pair<engine::Order, engine::AuthFlags> parse(const std::string& line) override {
                std::stringstream ss(line);
                std::string token;
                std::vector<std::string> tokens;

                while (std::getline(ss, token, ',')) {
                    tokens.push_back(token);
                }

                if (tokens.size() != 3) {
                    security::SecurityAwareLogger::instance().log(
                        security::SecurityAwareLogger::Level::Warn,
                        "[Malformed CSV] Incorrect number of fields: {}",
                        line);
                    return { engine::Order(1.0, 1.0, 1'725'000'000), engine::AuthFlags::MALFORMED };
                }

                try {
                    double price = std::stod(tokens[0]);
                    double amount = std::stod(tokens[1]);
                    std::int64_t timestamp = std::stoll(tokens[2]);

                    if (price < engine::Order::MIN_PRICE || price > engine::Order::MAX_PRICE) {
                        security::SecurityAwareLogger::instance().log(
                            security::SecurityAwareLogger::Level::Warn,
                            "[Suspicious Price] {}",
                            price);
                        return { engine::Order(1.0, 1.0, 1'725'000'000), engine::AuthFlags::SUSPICIOUS };
                    }

                    if (amount < engine::Order::MIN_AMOUNT || amount > engine::Order::MAX_AMOUNT) {
                        security::SecurityAwareLogger::instance().log(
                            security::SecurityAwareLogger::Level::Warn,
                            "[Suspicious Amount] {}",
                            amount);
                        return { engine::Order(1.0, 1.0, 1'725'000'000), engine::AuthFlags::SUSPICIOUS };
                    }

                    if (timestamp < engine::Order::MIN_TIMESTAMP || timestamp > engine::Order::MAX_TIMESTAMP) {
                        security::SecurityAwareLogger::instance().log(
                            security::SecurityAwareLogger::Level::Warn,
                            "[Suspicious Timestamp] {}",
                            timestamp);
                        return { engine::Order(1.0, 1.0, 1'725'000'000), engine::AuthFlags::SUSPICIOUS };
                    }

                    return { engine::Order(price, amount, timestamp), engine::AuthFlags::TRUSTED };

                } catch (...) {
                    security::SecurityAwareLogger::instance().log(
                        security::SecurityAwareLogger::Level::Error,
                        "[Malformed CSV] Failed to parse fields: {}",
                        line);
                    return { engine::Order(1.0, 1.0, 1'725'000'000), engine::AuthFlags::MALFORMED };
                }
            }

            std::string source() const override {
                return "CSV";
            }
        };

}  // namespace feed
