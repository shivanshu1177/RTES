#include <gtest/gtest.h>
#include "rtes/matching_engine.hpp"
#include "rtes/memory_pool.hpp"
#include <thread>
#include <chrono>

namespace rtes {

class MatchingEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool = std::make_unique<OrderPool>(1000);
        market_data_queue = std::make_unique<MPMCQueue<MarketDataEvent>>(1000);
        engine = std::make_unique<MatchingEngine>("AAPL", *pool);
        engine->set_market_data_queue(market_data_queue.get());
        engine->start();
    }
    
    void TearDown() override {
        engine->stop();
    }
    
    Order* create_order(OrderID id, ClientID client_id, Side side, Quantity qty, Price price) {
        auto* order = pool->allocate();
        new (order) Order(id, client_id, "AAPL", side, OrderType::LIMIT, qty, price);
        return order;
    }
    
    std::unique_ptr<OrderPool> pool;
    std::unique_ptr<MPMCQueue<MarketDataEvent>> market_data_queue;
    std::unique_ptr<MatchingEngine> engine;
};

TEST_F(MatchingEngineTest, BasicOrderSubmission) {
    auto* order = create_order(1, 100, Side::BUY, 1000, 15000);
    
    EXPECT_TRUE(engine->submit_order(order));
    
    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    EXPECT_EQ(engine->orders_processed(), 1);
    EXPECT_EQ(engine->trades_executed(), 0);
}

TEST_F(MatchingEngineTest, OrderMatching) {
    auto* sell_order = create_order(1, 100, Side::SELL, 500, 15000);
    auto* buy_order = create_order(2, 101, Side::BUY, 500, 15000);
    
    EXPECT_TRUE(engine->submit_order(sell_order));
    EXPECT_TRUE(engine->submit_order(buy_order));
    
    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    EXPECT_EQ(engine->orders_processed(), 2);
    EXPECT_EQ(engine->trades_executed(), 1);
    
    // Check market data events
    MarketDataEvent event;
    bool found_trade = false;
    
    while (market_data_queue->pop(event)) {
        if (event.type == MarketDataEvent::TRADE) {
            found_trade = true;
            EXPECT_EQ(event.trade.quantity, 500);
            EXPECT_EQ(event.trade.price, 15000);
        }
    }
    
    EXPECT_TRUE(found_trade);
}

TEST_F(MatchingEngineTest, OrderCancellation) {
    auto* order = create_order(1, 100, Side::BUY, 1000, 15000);
    
    EXPECT_TRUE(engine->submit_order(order));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    
    EXPECT_TRUE(engine->cancel_order(1, 100));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    
    EXPECT_EQ(engine->orders_processed(), 1);
}

TEST_F(MatchingEngineTest, HighThroughput) {
    constexpr int num_orders = 1000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Submit alternating buy/sell orders
    for (int i = 0; i < num_orders; ++i) {
        Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        Price price = 15000 + (i % 10);  // Spread orders across price levels
        
        auto* order = create_order(i + 1, 100, side, 100, price);
        EXPECT_TRUE(engine->submit_order(order));
    }
    
    // Wait for all orders to be processed
    while (engine->orders_processed() < num_orders) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "Processed " << num_orders << " orders in " 
              << duration.count() << " μs\n";
    std::cout << "Throughput: " << (num_orders * 1e6) / duration.count() 
              << " orders/sec\n";
    
    EXPECT_GT(engine->trades_executed(), 0);
}

TEST_F(MatchingEngineTest, BBOUpdates) {
    auto* buy_order = create_order(1, 100, Side::BUY, 1000, 14900);
    auto* sell_order = create_order(2, 101, Side::SELL, 1000, 15100);
    
    EXPECT_TRUE(engine->submit_order(buy_order));
    EXPECT_TRUE(engine->submit_order(sell_order));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Should have BBO updates
    MarketDataEvent event;
    bool found_bbo = false;
    
    while (market_data_queue->pop(event)) {
        if (event.type == MarketDataEvent::BBO_UPDATE) {
            found_bbo = true;
            EXPECT_EQ(event.bbo.bid_price, 14900);
            EXPECT_EQ(event.bbo.ask_price, 15100);
            EXPECT_EQ(event.bbo.bid_quantity, 1000);
            EXPECT_EQ(event.bbo.ask_quantity, 1000);
        }
    }
    
    EXPECT_TRUE(found_bbo);
}

} // namespace rtes