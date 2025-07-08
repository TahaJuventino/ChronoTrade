#pragma once
#include "OrderParser.hpp"
#include <sstream>

class CSVOrderParser : public OrderParser {
public:
    std::pair<Order, AuthFlags> parse(const std::string& line) override {
        std::stringstream ss(line);
        std::string token;
        double price = 0, amount = 0;
        std::int64_t timestamp = 0;

        try {
            std::getline(ss, token, ',');
            price = std::stod(token);

            std::getline(ss, token, ',');
            amount = std::stod(token);

            std::getline(ss, token, ',');
            timestamp = std::stoll(token);
        } catch (...) {
            return { Order(1.0, 1.0, 1'725'000'000), AuthFlags::MALFORMED }; 
        }

        try {
            Order o(price, amount, timestamp);
            return { o, AuthFlags::TRUSTED };
        } catch (...) {
            return { Order(1.0, 1.0, 1'725'000'000), AuthFlags::SUSPICIOUS };
        }
    }

    std::string source() const override {
        return "CSV";
    }
};
