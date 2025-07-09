#include <gtest/gtest.h>
#include "../engine/CandlestickGenerator.hpp"
#include "../utils/Hasher.hpp"

TEST(CandlestickGeneratorTest, CollectsOrdersWithinWindow) {
    CandlestickGenerator gen(60); // 60-second window

    gen.insert(Order(100.0, 1.0, 1'725'000'000));
    gen.insert(Order(101.0, 2.0, 1'725'000'020));
    gen.insert(Order(102.0, 1.5, 1'725'000'050));

    auto result = gen.flush_if_ready(1'725'000'055);
    EXPECT_FALSE(result.has_value());

    result = gen.flush_if_ready(1'725'000'061); // triggers flush
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
    auto result = gen.flush_if_ready(1'725'000'000);
    EXPECT_FALSE(result.has_value());
}

TEST(CandlestickGeneratorTest, RejectsLateOrder) {
    CandlestickGenerator gen(60);
    gen.insert(Order(100.0, 1.0, 1'725'000'000));
    gen.insert(Order(105.0, 1.0, 1'725'000'100));  // late

    auto result = gen.flush_if_ready(1'725'000'061);
    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->high, 100.0);  // 105.0 was dropped
}

TEST(CandlestickGeneratorTest, ResetsCountersAfterFlush) {
    CandlestickGenerator gen(60);
    gen.insert(Order(100.0, 1.0, 1'725'000'000));
    auto r1 = gen.flush_if_ready(1'725'000'100);  // Flush once

    gen.insert(Order(101.0, 1.0, 1'725'000'200)); // New window
    auto r2 = gen.flush_if_ready(1'725'000'300);
    EXPECT_TRUE(r2.has_value());
    EXPECT_DOUBLE_EQ(r2->volume, 1.0);
}

TEST(CandlestickGeneratorTest, HashTraceStability) {
    std::vector<Order> orders = {
        Order(100.0, 1.0, 1'725'000'000),
        Order(101.0, 2.0, 1'725'000'010)
    };
    std::string h1 = hash_orders(orders);
    std::string h2 = hash_orders(orders);
    EXPECT_EQ(h1, h2);
}