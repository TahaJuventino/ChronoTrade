#include <gtest/gtest.h>
#include "../core/OrderBook.hpp"

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
