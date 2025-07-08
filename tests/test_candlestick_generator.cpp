#include <gtest/gtest.h>
#include "../engine/CandlestickGenerator.hpp"

TEST(CandlestickGeneratorTest, CollectsOrdersWithinWindow) {
    CandlestickGenerator gen(60); // 60-second window

    gen.insert(Order(100.0, 1.0, 1'725'000'000));
    gen.insert(Order(101.0, 2.0, 1'725'000'020));
    gen.insert(Order(102.0, 1.5, 1'725'000'050));

    auto result = gen.flush_if_ready(1'725'000'055); // Too early
    EXPECT_FALSE(result.has_value());

    result = gen.flush_if_ready(1'725'000'061); // Window [0,60] ends
    ASSERT_TRUE(result.has_value());

    const auto& candle = result.value();
    EXPECT_DOUBLE_EQ(candle.open, 100.0);
    EXPECT_DOUBLE_EQ(candle.close, 102.0);
    EXPECT_DOUBLE_EQ(candle.high, 102.0);
    EXPECT_DOUBLE_EQ(candle.low, 100.0);
    EXPECT_DOUBLE_EQ(candle.volume, 4.5);
    EXPECT_EQ(candle.start_time, 1'725'000'000);
    EXPECT_EQ(candle.end_time, 1'725'000'060);
}


TEST(CandlestickGeneratorTest, EmptyFlushReturnsNothing) {
    CandlestickGenerator gen(60);
    auto result = gen.flush_if_ready(2000);
    EXPECT_FALSE(result.has_value());
}
