#include <gtest/gtest.h>
#include "rtes/order_book.hpp"
#include "rtes/memory_pool.hpp"

namespace rtes {

class OrderBookTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool = std::make_unique<OrderPool>(1000);
        book = std::make_unique<OrderBook>("AAPL", *pool, 
            [this](const Trade& trade) { trades.push_back(trade); });
    }
    
    Order* create_order(OrderID id, ClientID client_id, Side side, Quantity qty, Price price) {
        auto* order = pool->allocate();
        new (order) Order(id, client_id, "AAPL", side, OrderType::LIMIT, qty, price);
        return order;
    }
    
    std::unique_ptr<OrderPool> pool;
    std::unique_ptr<OrderBook> book;
    std::vector<Trade> trades;
};

TEST_F(OrderBookTest, BasicOrderPlacement) {
    auto* buy_order = create_order(1, 100, Side::BUY, 1000, 15000);  // $150.00
    auto* sell_order = create_order(2, 101, Side::SELL, 500, 15100); // $151.00
    
    EXPECT_TRUE(book->add_order(buy_order));
    EXPECT_TRUE(book->add_order(sell_order));
    
    EXPECT_EQ(book->best_bid(), 15000);
    EXPECT_EQ(book->best_ask(), 15100);
    EXPECT_EQ(book->bid_quantity(), 1000);
    EXPECT_EQ(book->ask_quantity(), 500);
    EXPECT_EQ(trades.size(), 0);  // No crossing
}

TEST_F(OrderBookTest, ImmediateExecution) {
    auto* sell_order = create_order(1, 100, Side::SELL, 500, 15000);  // $150.00
    auto* buy_order = create_order(2, 101, Side::BUY, 1000, 15100);   // $151.00 (crosses)
    
    EXPECT_TRUE(book->add_order(sell_order));
    EXPECT_EQ(book->best_ask(), 15000);
    
    EXPECT_TRUE(book->add_order(buy_order));
    
    // Should generate one trade
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 500);
    EXPECT_EQ(trades[0].price, 15000);  // Trade at passive order price
    
    // Remaining buy order should be in book
    EXPECT_EQ(book->best_bid(), 15100);
    EXPECT_EQ(book->bid_quantity(), 500);  // 1000 - 500 executed
    EXPECT_EQ(book->best_ask(), 0);  // Sell order fully filled
}

TEST_F(OrderBookTest, PartialFill) {
    auto* sell_order = create_order(1, 100, Side::SELL, 1000, 15000);
    auto* buy_order = create_order(2, 101, Side::BUY, 300, 15000);
    
    EXPECT_TRUE(book->add_order(sell_order));
    EXPECT_TRUE(book->add_order(buy_order));
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 300);
    
    // Remaining sell order should be in book
    EXPECT_EQ(book->best_ask(), 15000);
    EXPECT_EQ(book->ask_quantity(), 700);  // 1000 - 300 executed
}

TEST_F(OrderBookTest, PriceTimePriority) {
    // Add orders at same price level
    auto* order1 = create_order(1, 100, Side::BUY, 100, 15000);
    auto* order2 = create_order(2, 101, Side::BUY, 200, 15000);
    auto* order3 = create_order(3, 102, Side::BUY, 300, 15000);
    
    EXPECT_TRUE(book->add_order(order1));
    EXPECT_TRUE(book->add_order(order2));
    EXPECT_TRUE(book->add_order(order3));
    
    // Add crossing sell order
    auto* sell_order = create_order(4, 103, Side::SELL, 150, 15000);
    EXPECT_TRUE(book->add_order(sell_order));
    
    // Should execute against first order (100) and part of second (50)
    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].buy_order_id, 1);  // First order filled first
    EXPECT_EQ(trades[0].quantity, 100);
    EXPECT_EQ(trades[1].buy_order_id, 2);  // Second order partially filled
    EXPECT_EQ(trades[1].quantity, 50);
    
    EXPECT_EQ(book->bid_quantity(), 450);  // 200-50 + 300 remaining
}

TEST_F(OrderBookTest, OrderCancellation) {
    auto* order = create_order(1, 100, Side::BUY, 1000, 15000);
    EXPECT_TRUE(book->add_order(order));
    EXPECT_EQ(book->order_count(), 1);
    
    EXPECT_TRUE(book->cancel_order(1));
    EXPECT_EQ(book->order_count(), 0);
    EXPECT_EQ(book->best_bid(), 0);
    
    // Cancel non-existent order
    EXPECT_FALSE(book->cancel_order(999));
}

TEST_F(OrderBookTest, MarketDepth) {
    // Add multiple price levels
    book->add_order(create_order(1, 100, Side::BUY, 100, 14900));
    book->add_order(create_order(2, 100, Side::BUY, 200, 14950));
    book->add_order(create_order(3, 100, Side::BUY, 300, 15000));
    
    book->add_order(create_order(4, 101, Side::SELL, 150, 15100));
    book->add_order(create_order(5, 101, Side::SELL, 250, 15150));
    book->add_order(create_order(6, 101, Side::SELL, 350, 15200));
    
    auto depth = book->get_depth(3);
    EXPECT_EQ(depth.size(), 6);  // 3 bid + 3 ask levels
    
    // Check bid levels (descending price)
    EXPECT_EQ(depth[0].price, 15000);
    EXPECT_EQ(depth[0].quantity, 300);
    EXPECT_EQ(depth[1].price, 14950);
    EXPECT_EQ(depth[1].quantity, 200);
    EXPECT_EQ(depth[2].price, 14900);
    EXPECT_EQ(depth[2].quantity, 100);
    
    // Check ask levels (ascending price)
    EXPECT_EQ(depth[3].price, 15100);
    EXPECT_EQ(depth[3].quantity, 150);
    EXPECT_EQ(depth[4].price, 15150);
    EXPECT_EQ(depth[4].quantity, 250);
    EXPECT_EQ(depth[5].price, 15200);
    EXPECT_EQ(depth[5].quantity, 350);
}

} // namespace rtes