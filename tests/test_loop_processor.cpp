#include <gtest/gtest.h>
#include "../engine/LoopProcessor.hpp"
#include "../engine/IndicatorRegistry.hpp"
#include "../engine/SMAIndicator.hpp"
#include "../core/Candlestick.hpp"

using namespace engine;

TEST(LoopProcessorTest, ProcessesSingleCandleCorrectly) {
    IndicatorRegistry registry;
    auto sma = std::make_shared<SMAIndicator>(3);
    registry.register_indicator("sma", sma);

    LoopProcessor loop(registry);
    loop.run(Candlestick(100, 101, 99, 100, 10, 0, 1));
    loop.run(Candlestick(101, 102, 100, 101, 10, 1, 2));
    loop.run(Candlestick(102, 103, 101, 102, 10, 2, 3));

    EXPECT_EQ(sma->signal(), "buy");
    EXPECT_NEAR(sma->value(), 101.0, 1e-6);
}

TEST(LoopProcessorTest, HandlesEmptyRegistryGracefully) {
    IndicatorRegistry emptyRegistry;
    LoopProcessor loop(emptyRegistry);
    EXPECT_NO_THROW(loop.run(Candlestick(100, 101, 99, 100, 10, 0, 1)));
}

TEST(LoopProcessorTest, UpdatesMultipleIndicators) {
    IndicatorRegistry registry;
    auto sma = std::make_shared<SMAIndicator>(3);
    auto sma2 = std::make_shared<SMAIndicator>(5);
    registry.register_indicator("sma1", sma);
    registry.register_indicator("sma2", sma2);

    LoopProcessor loop(registry);
    for (int i = 0; i < 5; ++i) {
        loop.run(Candlestick(100 + i, 101 + i, 99 + i, 100 + i, 1, i, i + 1));
    }

    EXPECT_NO_THROW(sma->signal());
    EXPECT_NO_THROW(sma2->signal());
}

TEST(LoopProcessorTest, HandlesMalformedCandlestick) {
    IndicatorRegistry registry;
    auto sma = std::make_shared<SMAIndicator>(3);
    registry.register_indicator("sma", sma);

    LoopProcessor loop(registry);
    EXPECT_THROW(
        loop.run(Candlestick(105, 100, 110, 106, 10, 0, 1)),
        std::invalid_argument
    );
}
