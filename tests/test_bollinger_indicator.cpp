#include <gtest/gtest.h>
#include "../engine/BollingerBandsIndicator.hpp"
#include "../core/Candlestick.hpp"

using engine::BollingerBandsIndicator;
using engine::Candlestick;

TEST(BollingerBandsIndicatorTest, InitiallyHoldSignal) {
    BollingerBandsIndicator bb;
    EXPECT_EQ(bb.signal(), "hold");
    EXPECT_DOUBLE_EQ(bb.get_sma(), 0.0);
}

TEST(BollingerBandsIndicatorTest, StableUpdateNoThrow) {
    BollingerBandsIndicator bb;
    for (int i = 0; i < 20; ++i)
        EXPECT_NO_THROW(bb.update(Candlestick(100, 101, 99, 100, 10, i, i + 1)));
}

TEST(BollingerBandsIndicatorTest, SignalBuyWhenBelowLower) {
    BollingerBandsIndicator bb(5, 1.0);
    double values[] = {100, 102, 98, 101, 99};
    for (int i = 0; i < 5; ++i)
        bb.update(Candlestick(values[i], values[i]+1, values[i]-1, values[i], 1, i, i+1));

    bb.update(Candlestick(95, 96, 94, 95, 1, 6, 7));
    EXPECT_EQ(bb.signal(), "buy");
    EXPECT_LT(95.0, bb.get_lower_band());
}

TEST(BollingerBandsIndicatorTest, SignalSellWhenAboveUpper) {
    BollingerBandsIndicator bb(5, 1.0);
    double values[] = {100, 102, 98, 101, 99};
    for (int i = 0; i < 5; ++i)
        bb.update(Candlestick(values[i], values[i]+1, values[i]-1, values[i], 1, i, i+1));

    bb.update(Candlestick(110, 111, 109, 110, 1, 6, 7));
    EXPECT_EQ(bb.signal(), "sell");
    EXPECT_GT(110.0, bb.get_upper_band());
}

TEST(BollingerBandsIndicatorTest, SignalHoldInNormalRange) {
    BollingerBandsIndicator bb(5, 2.0);
    double values[] = {100, 102, 98, 101, 99};
    for (int i = 0; i < 5; ++i)
        bb.update(Candlestick(values[i], values[i]+1, values[i]-1, values[i], 1, i, i+1));

    bb.update(Candlestick(100, 101, 99, 100, 1, 6, 7));
    EXPECT_EQ(bb.signal(), "hold");
}

TEST(BollingerBandsIndicatorTest, ConvergesOnFlatPrice) {
    BollingerBandsIndicator bb(20, 2.0);
    for (int i = 0; i < 30; ++i)
        bb.update(Candlestick(100, 100, 100, 100, 1, i, i + 1));

    EXPECT_NEAR(bb.get_sma(), 100.0, 1e-6);
    EXPECT_NEAR(bb.get_upper_band(), 100.0, 1e-6);
    EXPECT_NEAR(bb.get_lower_band(), 100.0, 1e-6);
    EXPECT_EQ(bb.signal(), "hold");
}

TEST(BollingerBandsIndicatorTest, DetectsFlappingAroundBandEdges) {
    BollingerBandsIndicator bb(10, 1.0);
    for (int i = 0; i < 10; ++i)
        bb.update(Candlestick(100, 101, 99, 100, 1, i, i + 1));

    // Flip rapidly around band edges
    bb.update(Candlestick(120, 121, 119, 120, 1, 11, 12));
    EXPECT_EQ(bb.signal(), "sell");

    bb.update(Candlestick(80, 81, 79, 80, 1, 12, 13));
    EXPECT_EQ(bb.signal(), "buy");

    bb.update(Candlestick(100, 101, 99, 100, 1, 13, 14));
    EXPECT_EQ(bb.signal(), "hold");
}

TEST(BollingerBandsIndicatorTest, HandlesOutlierShockWithoutNan) {
    BollingerBandsIndicator bb(10, 2.0);
    for (int i = 0; i < 10; ++i)
        bb.update(Candlestick(100, 101, 99, 100, 1, i, i + 1));

    EXPECT_NO_THROW(bb.update(Candlestick(10000, 10001, 9999, 10000, 1, 11, 12)));
    EXPECT_TRUE(std::isfinite(bb.get_sma()));
    EXPECT_TRUE(std::isfinite(bb.get_upper_band()));
    EXPECT_TRUE(std::isfinite(bb.get_lower_band()));
}

TEST(BollingerBandsIndicatorTest, TraceOutputIncludesSignalAndBands) {
    BollingerBandsIndicator bb(5, 1.0);
    for (int i = 0; i < 5; ++i)
        bb.update(Candlestick(100 + i, 101 + i, 99 + i, 100 + i, 1, i, i + 1));

    std::string trace = bb.trace();
    EXPECT_NE(trace.find("SMA"), std::string::npos);
    EXPECT_NE(trace.find("Signal"), std::string::npos);
    EXPECT_NE(trace.find("Distance"), std::string::npos);
}