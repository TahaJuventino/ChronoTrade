#include <gtest/gtest.h>
#include "core/OrderBook.hpp"
#include "utils/ArenaAllocator.hpp"

#include <thread>

using engine::OrderBook;
using engine::Order;

TEST(OrderBookTest, InsertAndSize) {
    OrderBook book;
    EXPECT_EQ(book.size(), 0);

    book.insert(Order(100.0, 1.0, 1725000000));
    book.insert(Order(101.5, 2.0, 1725000001));

    EXPECT_EQ(book.size(), 2);
}

TEST(OrderBookTest, SnapshotContainsOrders) {
    OrderBook book;
    book.insert(Order(100.0, 1.0, 1725000000));
    book.insert(Order(105.0, 1.5, 1725000001));

    auto snapshot = book.snapshot();
    ASSERT_EQ(snapshot.size(), 2);
    EXPECT_DOUBLE_EQ(snapshot[0].price, 100.0);
    EXPECT_DOUBLE_EQ(snapshot[1].price, 105.0);
}

TEST(OrderBookTest, SortByPriceDescending) {
    OrderBook book;
    book.insert(Order(101.0, 1.0, 1725000001));
    book.insert(Order(99.0, 1.0, 1725000002));
    book.insert(Order(105.0, 1.0, 1725000003));

    book.sort_by_price_desc();
    auto snapshot = book.snapshot();

    ASSERT_EQ(snapshot.size(), 3);
    EXPECT_DOUBLE_EQ(snapshot[0].price, 105.0);
    EXPECT_DOUBLE_EQ(snapshot[1].price, 101.0);
    EXPECT_DOUBLE_EQ(snapshot[2].price, 99.0);
}

TEST(OrderBookTest, DuplicateTimestampRejected) {
    OrderBook book;
    book.insert(Order(100.0, 1.0, 1725000000));
    book.insert(Order(105.0, 1.0, 1725000000)); // same timestamp

    auto snapshot = book.snapshot();
    EXPECT_EQ(snapshot.size(), 1); // duplicate should not be inserted
}

TEST(OrderBookTest, ReplayAttackWindowSimulated) {
    OrderBook book;
    for (int i = 0; i < 100; ++i) {
        book.insert(Order(100.0, 1.0, 1725000000 + i));
    }
    // Re-insert 10 duplicates
    for (int i = 0; i < 10; ++i) {
        book.insert(Order(999.0, 2.0, 1725000000 + i)); // same timestamps, different data
    }

    EXPECT_EQ(book.size(), 100); // Duplicates must be ignored
}

TEST(OrderBookTest, ParallelInsertionStress) {
    OrderBook book;
    std::vector<std::thread> threads;

    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&book, i]() {
            for (int j = 0; j < 100; ++j) {
                book.insert(Order(100.0 + j, 1.0, 1725000000 + i * 100 + j));
            }
        });
    }

    for (auto& t : threads) t.join();

    EXPECT_EQ(book.size(), 1000);
}

TEST(OrderBookTest, WorstCaseSortTime) {
    OrderBook book;
    for (int i = 10000; i >= 1; --i)
        book.insert(Order(static_cast<double>(i), 1.0, 1725000000 + i));

    EXPECT_NO_THROW(book.sort_by_price_desc());
    auto snapshot = book.snapshot();
    ASSERT_EQ(snapshot.front().price, 10000.0);
}