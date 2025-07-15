#include <gtest/gtest.h>
#include "../engine/MACDIndicator.hpp"
#include "../core/Candlestick.hpp"

using engine::MACDIndicator;
using engine::Candlestick;

TEST(MACDIndicatorTest, InitiallyHoldSignal) {
    MACDIndicator macd;
    EXPECT_EQ(macd.signal(), "hold");
    EXPECT_EQ(macd.get_last_crossover(), std::nullopt);
}

TEST(MACDIndicatorTest, StableUpdatesDoNotThrow) {
    MACDIndicator macd;
    for (int i = 0; i < 50; ++i)
        EXPECT_NO_THROW(macd.update(Candlestick(100, 101, 99, 100, 10, i, i + 1)));
}

TEST(MACDIndicatorTest, BuySignalAfterCrossover) {
    MACDIndicator macd;

    // Create initial downward trend to establish negative histogram
    for (int i = 0; i < 50; ++i) {
        double price = 150.0 - i * 0.5;  // Gradual decline: 150→125
        macd.update(Candlestick(price, price + 1, price - 1, price, 1, i, i + 1));
    }

    // Now reverse with strong upward momentum
    for (int i = 50; i < 100; ++i) {
        double price = 125.0 + (i - 50) * 1.5;  // Strong rise: 125→200
        macd.update(Candlestick(price, price + 2, price - 1, price, 1, i, i + 1));
    }

    // Verify buy signal after bullish crossover
    EXPECT_EQ(macd.signal(), "buy");
    EXPECT_GT(macd.value(), 0.0);  // Histogram should be positive
    EXPECT_GT(macd.get_fast_ema(), macd.get_slow_ema());  // Fast EMA above slow
}

TEST(MACDIndicatorTest, SellSignalAfterCrossUnder) {
    MACDIndicator macd;

    // Create initial upward trend to establish positive histogram
    for (int i = 0; i < 50; ++i) {
        double price = 100.0 + i * 0.8;  // Gradual rise: 100→140
        macd.update(Candlestick(price, price + 1, price - 1, price, 1, i, i + 1));
    }

    // Now reverse with strong downward momentum
    for (int i = 50; i < 100; ++i) {
        double price = 140.0 - (i - 50) * 1.2;  // Strong fall: 140→80
        macd.update(Candlestick(price, price + 1, price - 2, price, 1, i, i + 1));
    }

    // Verify sell signal after bearish crossover
    EXPECT_EQ(macd.signal(), "sell");
    EXPECT_LT(macd.value(), 0.0);  // Histogram should be negative
    EXPECT_LT(macd.get_fast_ema(), macd.get_slow_ema());  // Fast EMA below slow
}

TEST(MACDIndicatorTest, ValueRemainsInBounds) {
    MACDIndicator macd;
    for (int i = 0; i < 100; ++i) {
        double base = 100.0 + std::sin(i);
        macd.update(Candlestick(base, base + 1, base, base + 0.5, 0, i, i + 1));
    }

    double v = macd.value();
    EXPECT_GE(v, -100.0);
    EXPECT_LE(v, 100.0);
}

TEST(MACDIndicatorTest, HandlesMalformedCandlesGracefully) {
    MACDIndicator macd;
    EXPECT_NO_THROW(macd.update(Candlestick(105, 110, 100, 106, 50, 0, 1)));  // valid
    EXPECT_NO_THROW(macd.update(Candlestick(0, 0, 0, 0, 0, 1, 2)));           // flat but valid
}

TEST(MACDIndicatorTest, ConvergesOnFlatPrice) {
    MACDIndicator macd;
    for (int i = 0; i < 100; ++i)
        macd.update(Candlestick(100, 101, 99, 100, 0, i, i + 1));

    EXPECT_NEAR(macd.value(), 0.0, 1e-3);
    EXPECT_EQ(macd.signal(), "hold");
    EXPECT_EQ(macd.get_last_crossover(), std::nullopt);
}
