#include <gtest/gtest.h>
#include "../core/Order.hpp"

TEST(OrderTest, ValidOrder) {
    Order o(120.5, 10.0, 1'725'000'000);
    EXPECT_DOUBLE_EQ(o.price, 120.5);
    EXPECT_DOUBLE_EQ(o.amount, 10.0);
    EXPECT_EQ(o.timestamp, 1'725'000'000);
}

TEST(OrderTest, InvalidPriceThrows) {
    EXPECT_THROW(Order(-1.0, 10.0, 1'725'000'000), std::invalid_argument);
}

TEST(OrderTest, InvalidAmountThrows) {
    EXPECT_THROW(Order(100.0, 0.0, 1'725'000'000), std::invalid_argument);
}

TEST(OrderTest, InvalidTimestampThrows) {
    EXPECT_THROW(Order(100.0, 10.0, 100), std::invalid_argument);
}


