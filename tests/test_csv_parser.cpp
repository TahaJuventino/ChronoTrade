#include <gtest/gtest.h>
#include "../feed/CSVOrderParser.hpp"

using feed::CSVOrderParser;
using engine::Order;
using engine::AuthFlags;

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

TEST(CSVOrderParserTest, ExtraFieldsIgnored) {
    CSVOrderParser parser;
    auto [_, flag] = parser.parse("100.0,1.0,1725000000,EXTRA,IGNORED");
    EXPECT_EQ(flag, AuthFlags::MALFORMED);
}

TEST(CSVOrderParserTest, MissingFieldRejected) {
    CSVOrderParser parser;
    auto [_, flag] = parser.parse("100.0,1725000000"); // missing one field
    EXPECT_EQ(flag, AuthFlags::MALFORMED);
}

TEST(CSVOrderParserTest, ZeroAmountOrPriceRejected) {
    CSVOrderParser parser;
    auto [_, f1] = parser.parse("0,1,1725000000");
    auto [__, f2] = parser.parse("1,0,1725000000");
    EXPECT_EQ(f1, AuthFlags::SUSPICIOUS);
    EXPECT_EQ(f2, AuthFlags::SUSPICIOUS);
}

TEST(CSVOrderParserTest, SubtleDecimalPoisoning) {
    CSVOrderParser parser;
    auto [_, flag] = parser.parse("not_a_number,2.0,1725000000");
    EXPECT_EQ(flag, AuthFlags::MALFORMED);
}

TEST(CSVOrderParserTest, HighPrecisionValidLine) {
    CSVOrderParser parser;
    auto [order, flag] = parser.parse("100.0000001,0.0001001,1725000000");

    EXPECT_EQ(flag, AuthFlags::TRUSTED);
    EXPECT_NEAR(order.price, 100.0000001, 1e-8);  // increased threshold
    EXPECT_NEAR(order.amount, 0.0001001, 1e-8);
}