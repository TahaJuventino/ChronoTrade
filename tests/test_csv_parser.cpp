#include <gtest/gtest.h>
#include "../feed/CSVOrderParser.hpp"

TEST(CSVOrderParserTest, ValidCSVLine) {
    CSVOrderParser parser;
    auto [order, flag] = parser.parse("100.5,2.0,1725000000");

    EXPECT_DOUBLE_EQ(order.price, 100.5);
    EXPECT_DOUBLE_EQ(order.amount, 2.0);
    EXPECT_EQ(order.timestamp, 1725000000);
    EXPECT_EQ(flag, AuthFlags::TRUSTED);
}

TEST(CSVOrderParserTest, MalformedCSVLine) {
    CSVOrderParser parser;
    auto [_, flag] = parser.parse("bad,input,line");
    EXPECT_EQ(flag, AuthFlags::MALFORMED);
}

TEST(CSVOrderParserTest, InvalidOrderData) {
    CSVOrderParser parser;
    auto [_, flag] = parser.parse("-1.0,0.0,1725000000");
    EXPECT_EQ(flag, AuthFlags::SUSPICIOUS);
}
