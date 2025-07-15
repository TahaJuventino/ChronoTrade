#include <gtest/gtest.h>
#include "../engine/SMAIndicator.hpp"
#include "../engine/IIndicator.hpp"
#include "../core/Candlestick.hpp"

using engine::SMAIndicator;
using engine::IIndicator;
using engine::Candlestick;

TEST(SMAIndicatorTest, SignalReturnsHoldInitially) {
    SMAIndicator sma(3);
    EXPECT_EQ(sma.signal(), "hold");
}

TEST(SMAIndicatorTest, ValueReturnsSMA) {
    SMAIndicator sma(3);
    sma.update(Candlestick(100, 110, 90, 105, 50, 1'725'000'000, 1'725'000'060));
    EXPECT_DOUBLE_EQ(sma.value(), 105.0);
}

TEST(SMAIndicatorTest, UpdateDoesNotThrow) {
    Candlestick dummy(100, 110, 90, 105, 50, 1725000000, 1725000060);
    SMAIndicator sma(3);
    EXPECT_NO_THROW(sma.update(dummy));
}

TEST(SMAIndicatorTest, IntegrationLikeUsageSimulated) {
    IIndicator* indicator = new SMAIndicator(3);
    Candlestick candle(101, 111, 99, 105, 60, 1725000000, 1725000060);
    EXPECT_NO_THROW(indicator->update(candle));
    delete indicator;
}