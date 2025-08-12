#include <gtest/gtest.h>
#include "engine/RSIIndicator.hpp"
#include "engine/MACDIndicator.hpp"
#include "engine/SMAIndicator.hpp"
#include "engine/BollingerBandsIndicator.hpp"
#include "core/Candlestick.hpp"
#include <vector>
#include <memory>
#include <unordered_set>

using namespace engine;

// Helper to create consistent Candlestick data
Candlestick make_candle(double price, double volume = 1.0, std::int64_t ts = 1'725'000'000) {
    return Candlestick(price, price + 1, price - 1, price, volume, ts, ts + 60);
}

// === RSI ===
TEST(IndicatorTest, RSISignalAccuracy) {
    RSIIndicator rsi(3);
    rsi.update(make_candle(90));
    rsi.update(make_candle(100));
    rsi.update(make_candle(110));  // 3 gains
    EXPECT_EQ(rsi.signal(), "sell");

    rsi.update(make_candle(100));
    rsi.update(make_candle(90));
    rsi.update(make_candle(80));   // 3 losses
    EXPECT_EQ(rsi.signal(), "buy");
}

TEST(IndicatorTest, RSIHandlesSpoofDesync) {
    RSIIndicator rsi(5);
    for (int i = 0; i < 5; ++i)
        rsi.update(make_candle(100 + i));

    // Inject wild spikes
    rsi.update(make_candle(10000));
    rsi.update(make_candle(1));

    std::string s = rsi.signal();  
    static const std::unordered_set<std::string> valid = {"buy", "sell", "hold"};
    EXPECT_TRUE(valid.count(s)) << "Unexpected RSI signal: " << s;
}

// === MACD ===
TEST(IndicatorTest, MACDCrossoverCorrect) {
    MACDIndicator macd(3, 6, 3);

    std::vector<double> up = {100, 102, 104, 106, 108, 110, 112, 114};
    for (double p : up) macd.update(make_candle(p));
    EXPECT_EQ(macd.signal(), "hold");

    std::vector<double> down = {114, 112, 110, 108, 106, 104, 102};
    for (double p : down) macd.update(make_candle(p));
    EXPECT_EQ(macd.signal(), "hold");
}

// === SMA ===
TEST(IndicatorTest, SMAAccuracy) {
    SMAIndicator sma(3);
    sma.update(make_candle(100));
    sma.update(make_candle(200));
    sma.update(make_candle(300));
    EXPECT_DOUBLE_EQ(sma.value(), 200.0);
}

TEST(IndicatorTest, SMAHighFrequencySliding) {
    SMAIndicator sma(5);
    for (int i = 0; i < 100; ++i)
        sma.update(make_candle(100 + (i % 3)));
    EXPECT_NEAR(sma.value(), 101.0, 1.0);
}

// === Bollinger Bands ===
TEST(IndicatorTest, BollingerBandsStability) {
    BollingerBandsIndicator bb(3, 2.0);
    bb.update(make_candle(100));
    bb.update(make_candle(100));
    bb.update(make_candle(100));

    auto [lower, upper] = bb.band_values();
    EXPECT_DOUBLE_EQ(lower, 100.0);
    EXPECT_DOUBLE_EQ(upper, 100.0);
}