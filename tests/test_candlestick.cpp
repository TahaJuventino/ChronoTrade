#include <gtest/gtest.h>
#include "../core/Candlestick.hpp"

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
