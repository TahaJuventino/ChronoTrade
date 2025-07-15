#include <gtest/gtest.h>
#include "../engine/RSIIndicator.hpp"
#include "../core/Candlestick.hpp"

using engine::RSIIndicator;
using engine::Candlestick;

TEST(RSIIndicatorTest, SignalInitiallyHold) {
    RSIIndicator rsi(5);
    EXPECT_EQ(rsi.signal(), "hold");
}

TEST(RSIIndicatorTest, UpdateDoesNotThrow) {
    RSIIndicator rsi(5);
    Candlestick c(100, 110, 90, 105, 50, 1'725'000'000, 1'725'000'060);
    EXPECT_NO_THROW(rsi.update(c));
}

TEST(RSIIndicatorTest, ReturnsCorrectSignalRanges) {
    RSIIndicator rsi(2);

    rsi.update(Candlestick(100, 110, 90, 100, 0, 1'725'000'000, 1'725'000'060));
    rsi.update(Candlestick(100, 120, 95, 110, 0, 1'725'000'061, 1'725'000'120));
    rsi.update(Candlestick(110, 125, 100, 120, 0, 1'725'000'121, 1'725'000'180));
    EXPECT_EQ(rsi.signal(), "sell");

    rsi.update(Candlestick(120, 130, 100, 100, 0, 1'725'000'181, 1'725'000'240));
    rsi.update(Candlestick(100, 110, 70, 80, 0, 1'725'000'241, 1'725'000'300));
    EXPECT_EQ(rsi.signal(), "buy");
}

TEST(RSIIndicatorTest, ValueStaysInRange) {
    RSIIndicator rsi(14);
    for (int i = 0; i < 20; ++i) {
        int price = 100 + (i % 3);
        rsi.update(Candlestick(price - 2, price + 2, price - 4, price, 0, 1'725'001'000 + i * 60, 1'725'001'060 + i * 60));
    }
    EXPECT_GE(rsi.value(), 0.0);
    EXPECT_LE(rsi.value(), 100.0);
}

TEST(RSIIndicatorTest, HandlesZigzagPriceMovement) {
    RSIIndicator rsi(5);
    double base = 100;
    for (int i = 0; i < 20; ++i) {
        double close = base + (i % 2 == 0 ? +5 : -5);
        rsi.update(Candlestick(close-1, close+1, close-2, close, 0, 1'726'000'000 + i*60, 1'726'000'060 + i*60));
    }
    EXPECT_GE(rsi.value(), 0.0);
    EXPECT_LE(rsi.value(), 100.0);
}

TEST(RSIIndicatorTest, AllGainsReaches100) {
    RSIIndicator rsi(2);

    // Valid candlestick: low ≤ close ≤ high
    rsi.update(Candlestick(100, 100, 100, 100, 0, 0, 10));  // init
    rsi.update(Candlestick(101, 101, 101, 101, 0, 10, 20)); // gain 1
    rsi.update(Candlestick(110, 110, 110, 110, 0, 20, 30)); // gain 2

    EXPECT_EQ(rsi.signal(), "sell");          // Should now trigger
    EXPECT_NEAR(rsi.value(), 100.0, 0.01);     // RSI ≈ 100
}

TEST(RSIIndicatorTest, AllLossesReaches0) {
    RSIIndicator rsi(5);
    double close = 100;
    for (int i = 0; i < 6; ++i) {
        rsi.update(Candlestick(close+1, close+2, close-1, close, 0, 1'726'200'000 + i*60, 1'726'200'060 + i*60));
        close -= 5;
    }
    EXPECT_EQ(rsi.signal(), "buy");
    EXPECT_NEAR(rsi.value(), 0.0, 0.0001);
}

TEST(RSIIndicatorTest, AllGainsReach100) {
    RSIIndicator rsi(5);
    double base = 100.0;

    // Provide 6 increasing candles to fully populate the window
    for (int i = 1; i <= 6; ++i) {
        rsi.update(Candlestick(base + i - 1, base + i, base + i - 2, base + i, 10, 0, 1));
    }

    EXPECT_NEAR(rsi.value(), 100.0, 0.01);
    EXPECT_EQ(rsi.signal(), "sell");
}
