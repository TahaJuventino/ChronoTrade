#include <gtest/gtest.h>
#include "core/OrderBook.hpp"
#include "utils/ArenaAllocator.hpp"

using engine::OrderBook;
using engine::Order;
using utils::ArenaAllocator;

TEST(OrderBookArenaTest, InsertsAndSnapshotsCorrectly) {
    ArenaAllocator arena(4096);  // 4KB = room for ~40–50 Orders
    OrderBook book(&arena, 50);

    for (int i = 0; i < 10; ++i) {
        Order o(100 + i, 1.0, 1'725'000'000 + i);
        book.insert(o);
    }

    EXPECT_EQ(book.size(), 10);

    auto snap = book.snapshot();
    ASSERT_EQ(snap.size(), 10);
    EXPECT_DOUBLE_EQ(snap.front().price, 100.0);
    EXPECT_DOUBLE_EQ(snap.back().price, 109.0);
}

TEST(OrderBookArenaTest, ArenaOverflowFailsGracefully) {
    // Calculate realistic arena size
    std::size_t order_size = sizeof(Order);
    std::size_t target_orders = 10;  // Reasonable target
    std::size_t arena_size = order_size * target_orders;
    
    ArenaAllocator arena(arena_size);
    OrderBook book(&arena, target_orders);

    int successful_inserts = 0;
    int total_attempts = target_orders * 2;  // Try to insert 2x capacity

    // Attempt to insert more orders than arena can handle
    for (int i = 0; i < total_attempts; ++i) {
        Order o(100.0 + i, 1.0, 1'725'000'000 + i);
        
        std::size_t size_before = book.size();
        book.insert(o);
        std::size_t size_after = book.size();
        
        if (size_after > size_before) {
            ++successful_inserts;
        }
    }

    // Verify graceful failure behavior
    EXPECT_EQ(book.size(), target_orders);  // Should not exceed capacity
    EXPECT_EQ(successful_inserts, target_orders);  // Only target_orders succeeded
    EXPECT_GT(book.failed_arena_inserts(), 0);  // Some insertions failed
    EXPECT_EQ(book.failed_arena_inserts(), total_attempts - target_orders);
    
    // Verify arena is full
    EXPECT_TRUE(book.is_arena_full());
}

TEST(OrderBookArenaTest, ArenaMemoryExhaustion) {
    // Test actual memory exhaustion (not just order count limit)
    ArenaAllocator arena(128);  // Very small buffer
    OrderBook book(&arena, 100);  // High order limit, low memory

    int successful_inserts = 0;
    
    // Insert until memory exhausted
    for (int i = 0; i < 50; ++i) {
        Order o(100.0 + i, 1.0, 1'725'000'000 + i);
        
        std::size_t size_before = book.size();
        book.insert(o);
        std::size_t size_after = book.size();
        
        if (size_after > size_before) {
            ++successful_inserts;
        }
    }

    // Detect if arena was used or fallback mode was triggered
    if (book.is_arena_full()) {
        // Arena mode engaged and filled — verify insert failures
        EXPECT_LT(book.size(), 50) << "Arena should not accept all inserts.";
        EXPECT_GT(book.failed_arena_inserts(), 0) << "At least one insert should fail in full arena.";
        EXPECT_EQ(successful_inserts, book.size()) << "Reported size should match successful inserts.";
    } else {
        // Arena allocation failed at constructor — fallback mode used
        EXPECT_EQ(book.size(), 50) << "Fallback mode should allow all inserts.";
        EXPECT_EQ(book.failed_arena_inserts(), 0) << "Fallback mode must not count arena failures.";
        SUCCEED() << "[Fallback Mode Activated] Arena allocation failed; std::vector used instead.";
    }
}

TEST(OrderBookArenaTest, ArenaFallbackMode) {
    // Test hybrid approach: arena + fallback
    ArenaAllocator arena(256);
    OrderBook book(&arena, 5);  // Small capacity

    // Fill arena to capacity
    for (int i = 0; i < 5; ++i) {
        Order o(100.0 + i, 1.0, 1'725'000'000 + i);
        book.insert(o);
    }

    EXPECT_EQ(book.size(), 5);
    EXPECT_EQ(book.failed_arena_inserts(), 0);

    // Attempt overflow
    Order overflow_order(200.0, 1.0, 1'725'000'010);
    book.insert(overflow_order);

    EXPECT_EQ(book.size(), 5);  // Size unchanged
    EXPECT_EQ(book.failed_arena_inserts(), 1);  // One failure recorded
}

TEST(OrderBookArenaTest, WorksWithoutArena) {
    OrderBook book(nullptr);  // fallback mode

    for (int i = 0; i < 5; ++i) {
        Order o(99 + i, 0.5, 1'725'000'100 + i);
        book.insert(o);
    }

    EXPECT_EQ(book.size(), 5);
    auto snap = book.snapshot();
    EXPECT_EQ(snap[0].price, 99.0);
    EXPECT_EQ(snap.back().price, 103.0);
}

TEST(OrderBookArenaTest, SortsDescendingByPrice) {
    ArenaAllocator arena(2048);
    OrderBook book(&arena, 10);

    book.insert(Order(101, 1, 1'725'000'001));
    book.insert(Order(105, 1, 1'725'000'002));
    book.insert(Order(99,  1, 1'725'000'003));

    book.sort_by_price_desc();
    auto sorted = book.snapshot();
    ASSERT_EQ(sorted[0].price, 105.0);
    ASSERT_EQ(sorted[1].price, 101.0);
    ASSERT_EQ(sorted[2].price, 99.0);
}

TEST(OrderBookArenaTest, ArenaFragmentationSimulation) {
    constexpr std::size_t capacity = 10;
    utils::ArenaAllocator arena(capacity * sizeof(Order));
    OrderBook book(&arena, capacity);

    for (std::size_t i = 0; i < capacity; ++i) {
        EXPECT_NO_THROW(book.insert(Order(100 + i, 1.0, 1'725'000'000 + i)));
    }

    EXPECT_TRUE(book.is_arena_full());
    EXPECT_EQ(book.failed_arena_inserts(), 0);

    // Insert past capacity to trigger graceful failure
    book.insert(Order(200.0, 1.0, 1'725'000'999));
    EXPECT_EQ(book.failed_arena_inserts(), 1);
}

TEST(OrderBookArenaTest, SnapshotRemainsStableUnderRace) {
    constexpr std::size_t capacity = 128;
    utils::ArenaAllocator arena(capacity * sizeof(Order));
    OrderBook book(&arena, capacity);

    std::atomic<bool> run = true;

    std::thread writer([&] {
        int64_t ts = 1'725'000'000;
        while (run) {
            try {
                book.insert(Order(100.0, 1.0, ts++));
            } catch (...) {}
        }
    });

    std::thread reader([&] {
        for (int i = 0; i < 50; ++i) {
            auto snap = book.snapshot();
            EXPECT_LE(snap.size(), capacity);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    run = false;

    writer.join();
    reader.join();
}

TEST(OrderBookArenaTest, ArenaResetAndRebindAllowsReuse) {
    constexpr std::size_t order_count = 8;
    std::size_t arena_bytes = sizeof(Order) * order_count;

    utils::ArenaAllocator arena(arena_bytes);
    OrderBook book(&arena, order_count);

    // Fill it up
    for (std::size_t i = 0; i < order_count; ++i)
        book.insert(Order(100.0 + i, 1.0, 1'725'100'000 + i));

    EXPECT_EQ(book.size(), order_count);
    EXPECT_TRUE(book.is_arena_full());

    // Reset allocator and rebind
    arena.reset();
    OrderBook reused(&arena, order_count);  // new book, rebind

    // Insert again
    for (std::size_t i = 0; i < order_count; ++i)
        EXPECT_NO_THROW(reused.insert(Order(200.0 + i, 1.0, 1'725'200'000 + i)));

    EXPECT_EQ(reused.size(), order_count);
    EXPECT_TRUE(reused.is_arena_full());
}