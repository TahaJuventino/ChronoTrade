#include <gtest/gtest.h>
#include "../core/Order.hpp"
#include <cmath>
#include <limits>
using engine::Order;

TEST(OrderTest, ValidOrder) {
    Order o(120.5, 10.0, 1'725'000'000);
    EXPECT_DOUBLE_EQ(o.price, 120.5);
    EXPECT_DOUBLE_EQ(o.amount, 10.0);
    EXPECT_EQ(o.timestamp, 1'725'000'000);
}

TEST(OrderTest, InvalidPriceThrows) {
    EXPECT_THROW(Order(-1.0, 10.0, 1'725'000'000), std::invalid_argument);
    EXPECT_THROW(Order(0.0, 10.0, 1'725'000'000), std::invalid_argument);
}

TEST(OrderTest, InvalidAmountThrows) {
    EXPECT_THROW(Order(100.0, 0.0, 1'725'000'000), std::invalid_argument);
    EXPECT_THROW(Order(100.0, -1.0, 1'725'000'000), std::invalid_argument);
}

TEST(OrderTest, InvalidTimestampThrows) {
    EXPECT_THROW(Order(100.0, 10.0, 100), std::invalid_argument);
    EXPECT_THROW(Order(100.0, 10.0, -1), std::invalid_argument);
}

TEST(OrderTest, HighPrecisionOrderAccepted) {
    EXPECT_NO_THROW(Order(100.000001, 0.0001, 1'725'000'000));
}

TEST(OrderTest, UpperBoundsPass) {
    EXPECT_NO_THROW(Order(1e6, 1e5, 1'999'999'999));
}

TEST(OrderTest, LowerBoundsPass) {
    EXPECT_NO_THROW(Order(0.0001, 0.0001, 1'000'000'000));
}

TEST(OrderTest, PriceBoundsValidation) {
    EXPECT_NO_THROW(Order(Order::MIN_PRICE, 1.0, 1'500'000'000));
    EXPECT_NO_THROW(Order(Order::MAX_PRICE, 1.0, 1'500'000'000));
    EXPECT_THROW(Order(0.0, 1.0, 1'500'000'000), std::invalid_argument);
    EXPECT_THROW(Order(Order::MAX_PRICE + 1.0, 1.0, 1'500'000'000), std::invalid_argument);
}

TEST(OrderTest, RejectsNaNOrInfinity) {
    double nan = std::numeric_limits<double>::quiet_NaN();
    double inf = std::numeric_limits<double>::infinity();
    EXPECT_THROW(Order(nan, 1.0, 1'500'000'000), std::invalid_argument);
    EXPECT_THROW(Order(1.0, nan, 1'500'000'000), std::invalid_argument);
    EXPECT_THROW(Order(inf, 1.0, 1'500'000'000), std::invalid_argument);
    EXPECT_THROW(Order(1.0, inf, 1'500'000'000), std::invalid_argument);
}

/* TODO: Add more tests for edge cases, such as:

OrderTest.SignatureAcceptsValidHMAC()
OrderTest.RejectsTamperedFields()

*/