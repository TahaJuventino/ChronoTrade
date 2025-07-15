#include <gtest/gtest.h>
#include "../core/Candlestick.hpp"
#include "../core/Order.hpp"
#include "../utils/Hasher.hpp"

using engine::Candlestick;
using engine::Order;
using engine::hash_orders;

TEST(CandlestickTest, ValidCandle) {
    Candlestick c(100.0, 110.0, 95.0, 105.0, 500.0, 1725000000, 1725000600);
    EXPECT_DOUBLE_EQ(c.open, 100.0);
    EXPECT_DOUBLE_EQ(c.high, 110.0);
    EXPECT_DOUBLE_EQ(c.low, 95.0);
    EXPECT_DOUBLE_EQ(c.close, 105.0);
    EXPECT_DOUBLE_EQ(c.volume, 500.0);
    EXPECT_EQ(c.start_time, 1725000000);
    EXPECT_EQ(c.end_time, 1725000600);
}

TEST(CandlestickTest, InvalidPriceOrderLowHigh) {
    EXPECT_THROW(Candlestick(100.0, 90.0, 95.0, 105.0, 100.0, 1, 2), std::invalid_argument);
}

TEST(CandlestickTest, InvalidPriceOrderCloseOutOfBounds) {
    EXPECT_THROW(Candlestick(100.0, 110.0, 95.0, 120.0, 100.0, 1, 2), std::invalid_argument);
}

TEST(CandlestickTest, InvalidTimeWindow) {
    EXPECT_THROW(Candlestick(100.0, 110.0, 95.0, 105.0, 100.0, 2000, 2000), std::invalid_argument);
}

TEST(CandlestickTest, NegativeVolumeThrows) {
    EXPECT_THROW(Candlestick(100.0, 110.0, 95.0, 105.0, -10.0, 2000, 3000), std::invalid_argument);
}

TEST(CandlestickTest, FuzzedExtremeCandle) {
    double base = 1e6;
    Candlestick c(base, base + 1.0, base - 1.0, base, 0.001, 1'725'000'000, 1'725'000'100);
    EXPECT_DOUBLE_EQ(c.high - c.low, 2.0);
}

TEST(CandlestickTest, TimeWarpAttackWindow) {
    EXPECT_THROW(Candlestick(100, 101, 99, 100, 10, 1'725'000'500, 1'000'000'000), std::invalid_argument);
}

TEST(CandlestickTest, MicropennyFluctuation) {
    Candlestick c(1.000001, 1.000009, 1.000000, 1.000004, 0.5, 1'725'000'000, 1'725'000'060);
    EXPECT_NEAR(c.high - c.low, 0.000009, 1e-9);
}

TEST(CandlestickTest, ZeroVolumeValid) {
    EXPECT_NO_THROW(Candlestick(100, 100, 100, 100, 0.0, 1'725'000'000, 1'725'000'060));
}

TEST(CandlestickTest, StableHashFingerprint) {
    std::vector<Order> input = {
        Order(100.0, 1.0, 1'725'000'001),
        Order(101.0, 1.5, 1'725'000'002)
    };
    std::string h1 = hash_orders(input);
    std::string h2 = hash_orders(input);
    EXPECT_EQ(h1, h2);
}
