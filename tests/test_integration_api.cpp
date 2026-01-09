#include <gtest/gtest.h>
#include "rtes/order_book.hpp"
#include "rtes/matching_engine.hpp"
#include "rtes/risk_manager.hpp"
#include "rtes/memory_pool.hpp"
#include "rtes/protocol.hpp"
#include <thread>
#include <chrono>

using namespace rtes;
using namespace std::chrono_literals;

// Static callback for trade notifications (OrderBook uses function pointer, not lambda)
static std::vector<Trade>* g_trades_ptr = nullptr;

static void trade_callback(const Trade& t, void* ctx) {
    auto* trades = static_cast<std::vector<Trade>*>(ctx);
    if (trades) {
        trades->push_back(t);
    }
}

class IntegrationAPITest : public ::testing::Test {
protected:
    void SetUp() override {
        pool = std::make_unique<OrderPool>(1000);
    }
    
    std::unique_ptr<OrderPool> pool;

    Order* make_order(OrderID oid, Side side, Price price, Quantity qty, OrderType type = OrderType::LIMIT) {
        auto* o = pool->allocate();
        o->id = oid;
        o->side = side;
        o->price = price;
        o->quantity = qty;
        o->remaining_quantity = qty;
        o->type = type;
        o->status = OrderStatus::PENDING;
        return o;
    }
};

// OrderBook + MemoryPool Integration
TEST_F(IntegrationAPITest, OrderBookMemoryPoolIntegration) {
    std::vector<Trade> trades;
    
    OrderBook book("AAPL", *pool, trade_callback, &trades);
    
    auto* buy_order = make_order(1, Side::BUY, 15000, 100);
    auto result = book.add_order(buy_order);
    EXPECT_FALSE(result.has_error());
    EXPECT_EQ(book.best_bid(), 15000);
    EXPECT_EQ(book.order_count(), 1);
    
    auto* sell_order = make_order(2, Side::SELL, 15000, 50);
    result = book.add_order(sell_order);
    EXPECT_FALSE(result.has_error());
    EXPECT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 50);
    EXPECT_EQ(trades[0].price, 15000);
}

TEST_F(IntegrationAPITest, OrderBookDepthSnapshot) {
    OrderBook book("MSFT", *pool);
    
    for (int i = 0; i < 5; ++i) {
        auto* order = make_order(i + 1, Side::BUY, 30000 - (i * 10), 100 * (i + 1));
        (void)book.add_order(order);
    }
    
    DepthSnapshot depth;
    book.get_depth(depth, 3);
    EXPECT_EQ(depth.bid_levels, 3);
    EXPECT_EQ(depth.bids[0].price, 30000);
    EXPECT_EQ(depth.bids[0].quantity, 100);
}

TEST_F(IntegrationAPITest, OrderBookCancellation) {
    OrderBook book("GOOGL", *pool);
    
    auto* order = make_order(100, Side::BUY, 28000, 200);
    auto result = book.add_order(order);
    EXPECT_FALSE(result.has_error());
    EXPECT_EQ(book.order_count(), 1);
    
    result = book.cancel_order(100);
    EXPECT_FALSE(result.has_error());
    EXPECT_EQ(book.order_count(), 0);
    EXPECT_EQ(book.best_bid(), 0);
}

// Protocol Message Validation Integration
TEST_F(IntegrationAPITest, ProtocolMessageRoundTrip) {
    NewOrderMessage msg;
    msg.header.type = NEW_ORDER;
    msg.header.length = sizeof(NewOrderMessage);
    msg.header.sequence = 1;
    msg.header.timestamp = ProtocolUtils::get_timestamp_ns();
    msg.order_id = 12345;
    msg.side = 1;
    msg.quantity = 100;
    msg.price = 15000;
    msg.order_type = 2;
    
    ProtocolUtils::set_checksum(msg.header, &msg.order_id);
    EXPECT_TRUE(ProtocolUtils::validate_checksum(msg.header, &msg.order_id));
}

TEST_F(IntegrationAPITest, ProtocolCancelOrderMessage) {
    CancelOrderMessage msg;
    msg.header.type = CANCEL_ORDER;
    msg.header.length = sizeof(CancelOrderMessage);
    msg.header.sequence = 2;
    msg.header.timestamp = ProtocolUtils::get_timestamp_ns();
    msg.order_id = 12345;
    
    ProtocolUtils::set_checksum(msg.header, &msg.order_id);
    EXPECT_TRUE(ProtocolUtils::validate_checksum(msg.header, &msg.order_id));
}

// Multi-Symbol OrderBook Integration
TEST_F(IntegrationAPITest, MultiSymbolOrderBooks) {
    std::vector<Trade> aapl_trades, msft_trades;
    
    OrderBook aapl_book("AAPL", *pool, trade_callback, &aapl_trades);
    OrderBook msft_book("MSFT", *pool, trade_callback, &msft_trades);
    
    (void)aapl_book.add_order(make_order(1, Side::BUY, 15000, 100));
    (void)msft_book.add_order(make_order(2, Side::BUY, 30000, 50));
    
    EXPECT_EQ(aapl_book.best_bid(), 15000);
    EXPECT_EQ(msft_book.best_bid(), 30000);
    EXPECT_EQ(aapl_book.order_count(), 1);
    EXPECT_EQ(msft_book.order_count(), 1);
}

// Error Handling Integration
TEST_F(IntegrationAPITest, OrderBookErrorHandling) {
    OrderBook book("TEST", *pool);
    
    // Test with nullptr - should return error
    auto result = book.add_order(nullptr);
    EXPECT_TRUE(result.has_error());
    
    // Test cancelling non-existent order - should return error
    result = book.cancel_order(99999);
    EXPECT_TRUE(result.has_error());
}

// Market Order Matching Integration
TEST_F(IntegrationAPITest, MarketOrderMatching) {
    std::vector<Trade> trades;
    OrderBook book("AAPL", *pool, trade_callback, &trades);
    
    for (int i = 0; i < 3; ++i) {
        (void)book.add_order(make_order(i + 1, Side::SELL, 15000 + (i * 10), 50));
    }
    
    (void)book.add_order(make_order(100, Side::BUY, 0, 120, OrderType::MARKET));
    
    EXPECT_EQ(trades.size(), 3);
    EXPECT_EQ(trades[0].quantity, 50);
    EXPECT_EQ(trades[1].quantity, 50);
    EXPECT_EQ(trades[2].quantity, 20);
}

// Price-Time Priority Integration
TEST_F(IntegrationAPITest, PriceTimePriority) {
    std::vector<Trade> trades;
    OrderBook book("AAPL", *pool, trade_callback, &trades);
    
    (void)book.add_order(make_order(1, Side::BUY, 15000, 100));
    (void)book.add_order(make_order(2, Side::BUY, 15000, 50));
    (void)book.add_order(make_order(3, Side::SELL, 15000, 75));
    
    EXPECT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].buy_order_id, 1);
    EXPECT_EQ(trades[0].quantity, 75);
}

// Partial Fill Integration
TEST_F(IntegrationAPITest, PartialFillScenario) {
    std::vector<Trade> trades;
    OrderBook book("AAPL", *pool, trade_callback, &trades);
    
    (void)book.add_order(make_order(1, Side::BUY, 15000, 100));
    (void)book.add_order(make_order(2, Side::SELL, 15000, 30));
    
    EXPECT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 30);
    EXPECT_EQ(book.order_count(), 1);
    
    (void)book.add_order(make_order(3, Side::SELL, 15000, 70));
    
    EXPECT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[1].quantity, 70);
    EXPECT_EQ(book.order_count(), 0);
}

// Bid-Ask Spread Integration
TEST_F(IntegrationAPITest, BidAskSpread) {
    OrderBook book("AAPL", *pool);
    
    (void)book.add_order(make_order(1, Side::BUY, 14990, 100));
    (void)book.add_order(make_order(2, Side::SELL, 15010, 100));
    
    EXPECT_EQ(book.best_bid(), 14990);
    EXPECT_EQ(book.best_ask(), 15010);
    EXPECT_EQ(book.best_ask() - book.best_bid(), 20);
}

