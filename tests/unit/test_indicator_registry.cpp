#include <gtest/gtest.h>
#include "engine/IndicatorRegistry.hpp"
#include "engine/SMAIndicator.hpp"
#include "engine/RSIIndicator.hpp"
#include "engine/MACDIndicator.hpp"
#include "engine/BollingerBandsIndicator.hpp"
#include "core/Candlestick.hpp"

using namespace engine;

TEST(IndicatorRegistryTest, UpdateEmptyRegistryNoThrow) {
    IndicatorRegistry registry;
    Candlestick c(100, 101, 99, 100, 10, 0, 1);
    EXPECT_NO_THROW(registry.update_all(c));
}

TEST(IndicatorRegistryTest, SingleIndicatorUpdatesCorrectly) {
    IndicatorRegistry registry;
    auto sma = std::make_shared<SMAIndicator>(3);
    registry.register_indicator("sma", sma);

    registry.update_all(Candlestick(100, 101, 99, 100, 10, 0, 1));
    registry.update_all(Candlestick(101, 102, 100, 101, 10, 1, 2));
    registry.update_all(Candlestick(102, 103, 101, 102, 10, 2, 3));

    EXPECT_EQ(sma->signal(), "buy");
    EXPECT_NEAR(sma->value(), 101.0, 1e-6);
}

TEST(IndicatorRegistryTest, MultiIndicatorIntegration) {
    IndicatorRegistry registry;
    auto sma = std::make_shared<SMAIndicator>(3);
    auto rsi = std::make_shared<RSIIndicator>(2);
    auto macd = std::make_shared<MACDIndicator>();
    auto bb = std::make_shared<BollingerBandsIndicator>(5);

    registry.register_indicator("sma", sma);
    registry.register_indicator("rsi", rsi);
    registry.register_indicator("macd", macd);
    registry.register_indicator("bb", bb);

    for (int i = 0; i < 50; ++i) {
        double open = 100 + (i % 3);          // 100, 101, 102
        double close = open;                  // flat close
        double high = open + 1;               // always above open
        double low = open - 1;                // always below open
        registry.update_all(Candlestick(open, high, low, close, 1, i, i + 1));
    }

    EXPECT_NO_THROW(sma->signal());
    EXPECT_NO_THROW(rsi->signal());
    EXPECT_NO_THROW(macd->signal());
    EXPECT_NO_THROW(bb->signal());
}

TEST(IndicatorRegistryTest, ResetClearsState) {
    IndicatorRegistry registry;
    auto sma = std::make_shared<SMAIndicator>(3);
    registry.register_indicator("sma", sma);

    registry.update_all(Candlestick(100, 101, 99, 100, 10, 0, 1));
    EXPECT_NE(sma->value(), 0.0);

    registry.reset();
    EXPECT_EQ(registry.count(), 0);
}