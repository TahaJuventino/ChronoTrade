#include <gtest/gtest.h>
#include "../engine/CandlestickGenerator.hpp"
#include "../utils/Hasher.hpp"
#include "../engine/SMAIndicator.hpp"
#include "../engine/IndicatorRegistry.hpp"
#include "../threads/ThreadPool.hpp"
#include <chrono>
#include <thread>

using namespace engine;

TEST(CandlestickGeneratorTest, CollectsOrdersWithinWindow) {
    CandlestickGenerator gen(60);
    gen.insert(Order(100.0, 1.0, 1'725'000'000));
    gen.insert(Order(101.0, 2.0, 1'725'000'020));
    gen.insert(Order(102.0, 1.5, 1'725'000'050));

    auto result = gen.flush_if_ready(1'725'000'055);
    EXPECT_FALSE(result.has_value());

    result = gen.flush_if_ready(1'725'000'061);
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
    gen.insert(Order(105.0, 1.0, 1'725'000'100)); // too late

    auto result = gen.flush_if_ready(1'725'000'061);
    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->high, 100.0);
}

TEST(CandlestickGeneratorTest, ResetsCountersAfterFlush) {
    CandlestickGenerator gen(60);
    gen.insert(Order(100.0, 1.0, 1'725'000'000));
    auto r1 = gen.flush_if_ready(1'725'000'100);
    ASSERT_TRUE(r1.has_value());

    gen.insert(Order(101.0, 1.0, 1'725'000'200));
    auto r2 = gen.flush_if_ready(1'725'000'300);
    ASSERT_TRUE(r2.has_value());
    EXPECT_DOUBLE_EQ(r2->volume, 1.0);
}

TEST(CandlestickGeneratorTest, HashTraceStability) {
    std::vector<Order> orders = {
        Order(100.0, 1.0, 1'725'000'000),
        Order(101.0, 2.0, 1'725'000'010)
    };

    std::string hash1 = hash_orders(orders);
    std::string hash2 = hash_orders(orders);

    EXPECT_EQ(hash1, hash2);
}

TEST(CandlestickGeneratorTest, DispatchesToRegistryAsync) {
    CandlestickGenerator gen(60);
    IndicatorRegistry registry;
    threads::ThreadPool pool(2);

    auto sma = std::make_shared<SMAIndicator>(3);
    registry.register_indicator("SMA", sma);

    gen.bind_registry(&registry);
    gen.bind_thread_pool(&pool);

    gen.insert(Order(100.0, 1.0, 1'725'000'000));
    gen.insert(Order(101.0, 1.0, 1'725'000'010));
    gen.insert(Order(102.0, 1.0, 1'725'000'050));

    auto result = gen.flush_if_ready(1'725'000'061);
    ASSERT_TRUE(result.has_value());

    // Allow async task to finish
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_NEAR(sma->value(), 102.0, 0.001);
}
