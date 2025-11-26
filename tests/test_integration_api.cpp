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

class IntegrationAPITest : public ::testing::Test {
protected:
    void SetUp() override {
        pool = std::make_unique<OrderPool>(1000);
    }
    
    std::unique_ptr<OrderPool> pool;
};

// OrderBook + MemoryPool Integration
TEST_F(IntegrationAPITest, OrderBookMemoryPoolIntegration) {
    std::vector<Trade> trades;
    auto callback = [&trades](const Trade& t) { trades.push_back(t); };
    
    OrderBook book("AAPL", *pool, callback);
    
    auto* buy_order = pool->allocate();
    buy_order->order_id = 1;
    buy_order->side = Side::BUY;
    buy_order->price = 15000;
    buy_order->quantity = 100;
    buy_order->remaining_quantity = 100;
    buy_order->type = OrderType::LIMIT;
    
    EXPECT_TRUE(book.add_order(buy_order));
    EXPECT_EQ(book.best_bid(), 15000);
    EXPECT_EQ(book.order_count(), 1);
    
    auto* sell_order = pool->allocate();
    sell_order->order_id = 2;
    sell_order->side = Side::SELL;
    sell_order->price = 15000;
    sell_order->quantity = 50;
    sell_order->remaining_quantity = 50;
    sell_order->type = OrderType::LIMIT;
    
    EXPECT_TRUE(book.add_order(sell_order));
    EXPECT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 50);
    EXPECT_EQ(trades[0].price, 15000);
}

TEST_F(IntegrationAPITest, OrderBookDepthSnapshot) {
    OrderBook book("MSFT", *pool);
    
    for (int i = 0; i < 5; ++i) {
        auto* order = pool->allocate();
        order->order_id = i + 1;
        order->side = Side::BUY;
        order->price = 30000 - (i * 10);
        order->quantity = 100 * (i + 1);
        order->remaining_quantity = order->quantity;
        order->type = OrderType::LIMIT;
        book.add_order(order);
    }
    
    auto depth = book.get_depth(3);
    EXPECT_EQ(depth.size(), 3);
    EXPECT_EQ(depth[0].price, 30000);
    EXPECT_EQ(depth[0].quantity, 100);
}

TEST_F(IntegrationAPITest, OrderBookCancellation) {
    OrderBook book("GOOGL", *pool);
    
    auto* order = pool->allocate();
    order->order_id = 100;
    order->side = Side::BUY;
    order->price = 28000;
    order->quantity = 200;
    order->remaining_quantity = 200;
    order->type = OrderType::LIMIT;
    
    EXPECT_TRUE(book.add_order(order));
    EXPECT_EQ(book.order_count(), 1);
    
    EXPECT_TRUE(book.cancel_order(100));
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
    
    ProtocolUtils::set_checksum(msg.header, &msg);
    EXPECT_TRUE(ProtocolUtils::validate_checksum(msg.header, &msg));
    
    auto header_result = MessageValidator::validate_message_header(msg.header);
    EXPECT_TRUE(header_result.is_ok());
}

TEST_F(IntegrationAPITest, ProtocolCancelOrderMessage) {
    CancelOrderMessage msg;
    msg.header.type = CANCEL_ORDER;
    msg.header.length = sizeof(CancelOrderMessage);
    msg.header.sequence = 2;
    msg.header.timestamp = ProtocolUtils::get_timestamp_ns();
    msg.order_id = 12345;
    
    ProtocolUtils::set_checksum(msg.header, &msg);
    EXPECT_TRUE(ProtocolUtils::validate_checksum(msg.header, &msg));
}

// Multi-Symbol OrderBook Integration
TEST_F(IntegrationAPITest, MultiSymbolOrderBooks) {
    std::vector<Trade> aapl_trades, msft_trades;
    
    OrderBook aapl_book("AAPL", *pool, [&](const Trade& t) { aapl_trades.push_back(t); });
    OrderBook msft_book("MSFT", *pool, [&](const Trade& t) { msft_trades.push_back(t); });
    
    auto* aapl_order = pool->allocate();
    aapl_order->order_id = 1;
    aapl_order->side = Side::BUY;
    aapl_order->price = 15000;
    aapl_order->quantity = 100;
    aapl_order->remaining_quantity = 100;
    aapl_order->type = OrderType::LIMIT;
    aapl_book.add_order(aapl_order);
    
    auto* msft_order = pool->allocate();
    msft_order->order_id = 2;
    msft_order->side = Side::BUY;
    msft_order->price = 30000;
    msft_order->quantity = 50;
    msft_order->remaining_quantity = 50;
    msft_order->type = OrderType::LIMIT;
    msft_book.add_order(msft_order);
    
    EXPECT_EQ(aapl_book.best_bid(), 15000);
    EXPECT_EQ(msft_book.best_bid(), 30000);
    EXPECT_EQ(aapl_book.order_count(), 1);
    EXPECT_EQ(msft_book.order_count(), 1);
}

// Error Handling Integration
TEST_F(IntegrationAPITest, OrderBookErrorHandling) {
    OrderBook book("TEST", *pool);
    
    auto result = book.add_order_safe(nullptr);
    EXPECT_TRUE(result.is_err());
    
    result = book.cancel_order_safe(99999);
    EXPECT_TRUE(result.is_err());
}

// Market Order Matching Integration
TEST_F(IntegrationAPITest, MarketOrderMatching) {
    std::vector<Trade> trades;
    OrderBook book("AAPL", *pool, [&](const Trade& t) { trades.push_back(t); });
    
    for (int i = 0; i < 3; ++i) {
        auto* order = pool->allocate();
        order->order_id = i + 1;
        order->side = Side::SELL;
        order->price = 15000 + (i * 10);
        order->quantity = 50;
        order->remaining_quantity = 50;
        order->type = OrderType::LIMIT;
        book.add_order(order);
    }
    
    auto* market_order = pool->allocate();
    market_order->order_id = 100;
    market_order->side = Side::BUY;
    market_order->price = 0;
    market_order->quantity = 120;
    market_order->remaining_quantity = 120;
    market_order->type = OrderType::MARKET;
    
    book.add_order(market_order);
    
    EXPECT_EQ(trades.size(), 3);
    EXPECT_EQ(trades[0].quantity, 50);
    EXPECT_EQ(trades[1].quantity, 50);
    EXPECT_EQ(trades[2].quantity, 20);
}

// Price-Time Priority Integration
TEST_F(IntegrationAPITest, PriceTimePriority) {
    std::vector<Trade> trades;
    OrderBook book("AAPL", *pool, [&](const Trade& t) { trades.push_back(t); });
    
    auto* order1 = pool->allocate();
    order1->order_id = 1;
    order1->side = Side::BUY;
    order1->price = 15000;
    order1->quantity = 100;
    order1->remaining_quantity = 100;
    order1->type = OrderType::LIMIT;
    book.add_order(order1);
    
    auto* order2 = pool->allocate();
    order2->order_id = 2;
    order2->side = Side::BUY;
    order2->price = 15000;
    order2->quantity = 50;
    order2->remaining_quantity = 50;
    order2->type = OrderType::LIMIT;
    book.add_order(order2);
    
    auto* sell_order = pool->allocate();
    sell_order->order_id = 3;
    sell_order->side = Side::SELL;
    sell_order->price = 15000;
    sell_order->quantity = 75;
    sell_order->remaining_quantity = 75;
    sell_order->type = OrderType::LIMIT;
    book.add_order(sell_order);
    
    EXPECT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].buy_order_id, 1);
    EXPECT_EQ(trades[0].quantity, 75);
}

// Partial Fill Integration
TEST_F(IntegrationAPITest, PartialFillScenario) {
    std::vector<Trade> trades;
    OrderBook book("AAPL", *pool, [&](const Trade& t) { trades.push_back(t); });
    
    auto* buy_order = pool->allocate();
    buy_order->order_id = 1;
    buy_order->side = Side::BUY;
    buy_order->price = 15000;
    buy_order->quantity = 100;
    buy_order->remaining_quantity = 100;
    buy_order->type = OrderType::LIMIT;
    book.add_order(buy_order);
    
    auto* sell_order1 = pool->allocate();
    sell_order1->order_id = 2;
    sell_order1->side = Side::SELL;
    sell_order1->price = 15000;
    sell_order1->quantity = 30;
    sell_order1->remaining_quantity = 30;
    sell_order1->type = OrderType::LIMIT;
    book.add_order(sell_order1);
    
    EXPECT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 30);
    EXPECT_EQ(book.order_count(), 1);
    
    auto* sell_order2 = pool->allocate();
    sell_order2->order_id = 3;
    sell_order2->side = Side::SELL;
    sell_order2->price = 15000;
    sell_order2->quantity = 70;
    sell_order2->remaining_quantity = 70;
    sell_order2->type = OrderType::LIMIT;
    book.add_order(sell_order2);
    
    EXPECT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[1].quantity, 70);
    EXPECT_EQ(book.order_count(), 0);
}

// Bid-Ask Spread Integration
TEST_F(IntegrationAPITest, BidAskSpread) {
    OrderBook book("AAPL", *pool);
    
    auto* buy_order = pool->allocate();
    buy_order->order_id = 1;
    buy_order->side = Side::BUY;
    buy_order->price = 14990;
    buy_order->quantity = 100;
    buy_order->remaining_quantity = 100;
    buy_order->type = OrderType::LIMIT;
    book.add_order(buy_order);
    
    auto* sell_order = pool->allocate();
    sell_order->order_id = 2;
    sell_order->side = Side::SELL;
    sell_order->price = 15010;
    sell_order->quantity = 100;
    sell_order->remaining_quantity = 100;
    sell_order->type = OrderType::LIMIT;
    book.add_order(sell_order);
    
    EXPECT_EQ(book.best_bid(), 14990);
    EXPECT_EQ(book.best_ask(), 15010);
    EXPECT_EQ(book.best_ask() - book.best_bid(), 20);
}
