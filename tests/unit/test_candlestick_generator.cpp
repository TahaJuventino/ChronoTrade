#include <gtest/gtest.h>
#include "engine/CandlestickGenerator.hpp"
#include "infrastructure/Hasher.hpp"
#include "engine/SMAIndicator.hpp"
#include "engine/IndicatorRegistry.hpp"
#include "infrastructure/ThreadPool.hpp"
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

TEST(CandlestickGeneratorTest, FlushConsistentUnderRace) {
    CandlestickGenerator gen(60);

    std::atomic<bool> ready = false;
    std::vector<std::thread> threads;

    // Inserters
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&gen, &ready, i]() {
            while (!ready) std::this_thread::yield();
            for (int j = 0; j < 25; ++j) {
                gen.insert(Order(100.0 + i + j * 0.1, 1.0, 1'725'000'000 + j));
            }
        });
    }

    // Flusher thread
    std::optional<Candlestick> flushed;
    threads.emplace_back([&gen, &ready, &flushed]() {
        while (!ready) std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        flushed = gen.flush_if_ready(1'725'000'060);
    });

    // Go
    ready = true;
    for (auto& t : threads) t.join();

    ASSERT_TRUE(flushed.has_value());
    EXPECT_GE(flushed->volume, 50.0);
    EXPECT_LE(flushed->volume, 100.0);
    EXPECT_GE(flushed->high, flushed->low);  // sanity
}

TEST(CandlestickGeneratorTest, DispatchesToCallbackAfterFlush) {
    engine::CandlestickGenerator gen(60);

    std::optional<engine::Candlestick> received;
    std::mutex mtx;
    std::condition_variable cv;
    bool signaled = false;

    // Inject callback
    gen.set_dispatch_callback([&](const engine::Candlestick& c) {
        std::lock_guard<std::mutex> lock(mtx);
        received = c;
        signaled = true;
        cv.notify_one();
    });

    // Insert orders
    gen.insert(engine::Order(100.0, 1.0, 1'725'000'000));
    gen.insert(engine::Order(101.0, 2.0, 1'725'000'030));

    // Trigger flush
    auto flush = gen.flush_if_ready(1'725'000'061);
    ASSERT_TRUE(flush.has_value());

    // Wait for callback signal
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait_for(lock, std::chrono::milliseconds(100), [&]() { return signaled; });

    ASSERT_TRUE(received.has_value());
    EXPECT_DOUBLE_EQ(received->open, 100.0);
    EXPECT_DOUBLE_EQ(received->close, 101.0);
    EXPECT_DOUBLE_EQ(received->volume, 3.0);
    EXPECT_EQ(received->start_time, 1'725'000'000);
}

TEST(CandlestickGeneratorTest, FlushUnderConcurrentInsertsIsStable) {
    engine::CandlestickGenerator gen(60);
    std::atomic<bool> start_flag = false;
    std::optional<engine::Candlestick> result;

    std::vector<std::thread> inserters;
    for (int i = 0; i < 4; ++i) {
        inserters.emplace_back([&gen, &start_flag, i]() {
            while (!start_flag) std::this_thread::yield();
            for (int j = 0; j < 25; ++j) {
                gen.insert(engine::Order(100.0 + i + j * 0.1, 1.0, 1'725'000'000 + j));
            }
        });
    }

    std::thread flusher([&gen, &start_flag, &result]() {
        while (!start_flag) std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        result = gen.flush_if_ready(1'725'000'060);
    });

    start_flag = true;

    for (auto& t : inserters) t.join();
    flusher.join();

    ASSERT_TRUE(result.has_value());
    EXPECT_GE(result->volume, 50.0);
    EXPECT_LE(result->volume, 100.0);
    EXPECT_GE(result->high, result->low);
}