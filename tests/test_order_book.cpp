#include <gtest/gtest.h>
#include "rtes/order_book.hpp"
#include "rtes/memory_pool.hpp"

namespace rtes {

// FIX: TradeCallback is now a C function pointer: void(*)(const Trade&, void*)
// Capturing lambdas cannot convert to function pointers, so use a free function.
static void test_trade_handler(const Trade& trade, void* ctx) {
    static_cast<std::vector<Trade>*>(ctx)->push_back(trade);
}

class OrderBookTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool = std::make_unique<OrderPool>(1000);
        // FIX: Pass function pointer + context instead of capturing lambda
        book = std::make_unique<OrderBook>("AAPL", *pool, 
            test_trade_handler, &trades);
    }
    
    Order* create_order(OrderID id, ClientID client_id, Side side, Quantity qty, Price price) {
        auto* order = pool->allocate();
        // FIX: Order constructor takes const char*, not ClientID
        new (order) Order(id, client_id.c_str(), "AAPL", side, OrderType::LIMIT, qty, price);
        return order;
    }
    
    std::unique_ptr<OrderPool> pool;
    std::unique_ptr<OrderBook> book;
    std::vector<Trade> trades;
};

TEST_F(OrderBookTest, BasicOrderPlacement) {
    auto* buy_order = create_order(1, ClientID("100"), Side::BUY, 1000, 15000);
    auto* sell_order = create_order(2, ClientID("101"), Side::SELL, 500, 15100);
    
    // FIX: add_order returns Result<void>, not bool — use .has_value()
    EXPECT_TRUE(book->add_order(buy_order).has_value());
    EXPECT_TRUE(book->add_order(sell_order).has_value());
    
    EXPECT_EQ(book->best_bid(), 15000);
    EXPECT_EQ(book->best_ask(), 15100);
    EXPECT_EQ(book->bid_quantity(), 1000);
    EXPECT_EQ(book->ask_quantity(), 500);
    EXPECT_EQ(trades.size(), 0);
}

TEST_F(OrderBookTest, ImmediateExecution) {
    auto* sell_order = create_order(1, ClientID("100"), Side::SELL, 500, 15000);
    auto* buy_order = create_order(2, ClientID("101"), Side::BUY, 1000, 15100);
    
    EXPECT_TRUE(book->add_order(sell_order).has_value());
    EXPECT_EQ(book->best_ask(), 15000);
    
    EXPECT_TRUE(book->add_order(buy_order).has_value());
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 500);
    EXPECT_EQ(trades[0].price, 15000);
    
    EXPECT_EQ(book->best_bid(), 15100);
    EXPECT_EQ(book->bid_quantity(), 500);
    EXPECT_EQ(book->best_ask(), 0);
}

TEST_F(OrderBookTest, PartialFill) {
    auto* sell_order = create_order(1, ClientID("100"), Side::SELL, 1000, 15000);
    auto* buy_order = create_order(2, ClientID("101"), Side::BUY, 300, 15000);
    
    EXPECT_TRUE(book->add_order(sell_order).has_value());
    EXPECT_TRUE(book->add_order(buy_order).has_value());
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 300);
    
    EXPECT_EQ(book->best_ask(), 15000);
    EXPECT_EQ(book->ask_quantity(), 700);
}

TEST_F(OrderBookTest, PriceTimePriority) {
    auto* order1 = create_order(1, ClientID("100"), Side::BUY, 100, 15000);
    auto* order2 = create_order(2, ClientID("101"), Side::BUY, 200, 15000);
    auto* order3 = create_order(3, ClientID("102"), Side::BUY, 300, 15000);
    
    EXPECT_TRUE(book->add_order(order1).has_value());
    EXPECT_TRUE(book->add_order(order2).has_value());
    EXPECT_TRUE(book->add_order(order3).has_value());
    
    auto* sell_order = create_order(4, ClientID("103"), Side::SELL, 150, 15000);
    EXPECT_TRUE(book->add_order(sell_order).has_value());
    
    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].buy_order_id, 1);
    EXPECT_EQ(trades[0].quantity, 100);
    EXPECT_EQ(trades[1].buy_order_id, 2);
    EXPECT_EQ(trades[1].quantity, 50);
    
    EXPECT_EQ(book->bid_quantity(), 450);
}

TEST_F(OrderBookTest, OrderCancellation) {
    auto* order = create_order(1, ClientID("100"), Side::BUY, 1000, 15000);
    EXPECT_TRUE(book->add_order(order).has_value());
    EXPECT_EQ(book->order_count(), 1);
    
    // FIX: cancel_order returns Result<void>, not bool
    EXPECT_TRUE(book->cancel_order(1).has_value());
    EXPECT_EQ(book->order_count(), 0);
    EXPECT_EQ(book->best_bid(), 0);
    
    // Cancelling non-existent order should fail
    EXPECT_FALSE(book->cancel_order(999).has_value());
}

TEST_F(OrderBookTest, MarketDepth) {
    // FIX: Capture [[nodiscard]] return values
    EXPECT_TRUE(book->add_order(create_order(1, ClientID("100"), Side::BUY, 100, 14900)).has_value());
    EXPECT_TRUE(book->add_order(create_order(2, ClientID("100"), Side::BUY, 200, 14950)).has_value());
    EXPECT_TRUE(book->add_order(create_order(3, ClientID("100"), Side::BUY, 300, 15000)).has_value());
    
    EXPECT_TRUE(book->add_order(create_order(4, ClientID("101"), Side::SELL, 150, 15100)).has_value());
    EXPECT_TRUE(book->add_order(create_order(5, ClientID("101"), Side::SELL, 250, 15150)).has_value());
    EXPECT_TRUE(book->add_order(create_order(6, ClientID("101"), Side::SELL, 350, 15200)).has_value());
    
    // FIX: get_depth() now takes an out-parameter, not a return value
    // Old API: auto depth = book->get_depth(3);
    // New API: void get_depth(DepthSnapshot& out, size_t max_levels) const;
    DepthSnapshot depth;
    book->get_depth(depth, 3);
    
    // FIX: DepthSnapshot has separate bids/asks arrays instead of a flat list
    EXPECT_EQ(depth.bid_levels + depth.ask_levels, 6);
    
    // Bids (descending by price — best bid first)
    EXPECT_EQ(depth.bids[0].price, 15000);
    EXPECT_EQ(depth.bids[0].quantity, 300);
    EXPECT_EQ(depth.bids[1].price, 14950);
    EXPECT_EQ(depth.bids[1].quantity, 200);
    EXPECT_EQ(depth.bids[2].price, 14900);
    EXPECT_EQ(depth.bids[2].quantity, 100);
    
    // Asks (ascending by price — best ask first)
    EXPECT_EQ(depth.asks[0].price, 15100);
    EXPECT_EQ(depth.asks[0].quantity, 150);
    EXPECT_EQ(depth.asks[1].price, 15150);
    EXPECT_EQ(depth.asks[1].quantity, 250);
    EXPECT_EQ(depth.asks[2].price, 15200);
    EXPECT_EQ(depth.asks[2].quantity, 350);
}

} // namespace rtes