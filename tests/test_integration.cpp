#include <gtest/gtest.h>
#include "rtes/exchange.hpp"
#include "rtes/config.hpp"
#include <thread>
#include <chrono>

namespace rtes {

class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test configuration
        auto config = std::make_unique<Config>();
        
        config->exchange.name = "TEST_EXCHANGE";
        config->exchange.tcp_port = 8888;
        config->exchange.udp_port = 9999;
        config->exchange.metrics_port = 8080;
        
        config->risk.max_order_size = 10000;
        config->risk.max_notional_per_client = 1000000.0;
        config->risk.max_orders_per_second = 1000;
        config->risk.price_collar_enabled = false;
        
        config->performance.order_pool_size = 10000;
        config->performance.queue_capacity = 1000;
        
        config->symbols = {
            {"AAPL", 0.01, 1, 10.0},
            {"MSFT", 0.01, 1, 10.0}
        };
        
        exchange = std::make_unique<Exchange>(std::move(config));
        exchange->start();
    }
    
    void TearDown() override {
        exchange->stop();
    }
    
    std::unique_ptr<Exchange> exchange;
};

TEST_F(IntegrationTest, EndToEndOrderFlow) {
    auto* risk_manager = exchange->get_risk_manager();
    auto* matching_engine = exchange->get_matching_engine("AAPL");
    
    ASSERT_NE(risk_manager, nullptr);
    ASSERT_NE(matching_engine, nullptr);
    
    // Create test orders
    OrderPool pool(100);
    
    auto* sell_order = pool.allocate();
    new (sell_order) Order(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 1000, 15000);
    
    auto* buy_order = pool.allocate();
    new (buy_order) Order(2, 101, "AAPL", Side::BUY, OrderType::LIMIT, 1000, 15000);
    
    // Submit orders through risk manager
    EXPECT_TRUE(risk_manager->submit_order(sell_order));
    EXPECT_TRUE(risk_manager->submit_order(buy_order));
    
    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Verify orders were processed
    EXPECT_EQ(risk_manager->orders_processed(), 2);
    EXPECT_EQ(risk_manager->orders_rejected(), 0);
    
    // Verify trade was executed
    EXPECT_EQ(matching_engine->trades_executed(), 1);
}

TEST_F(IntegrationTest, MultiSymbolTrading) {
    auto* risk_manager = exchange->get_risk_manager();
    auto* aapl_engine = exchange->get_matching_engine("AAPL");
    auto* msft_engine = exchange->get_matching_engine("MSFT");
    
    ASSERT_NE(aapl_engine, nullptr);
    ASSERT_NE(msft_engine, nullptr);
    
    OrderPool pool(100);
    
    // Submit orders for both symbols
    auto* aapl_order = pool.allocate();
    new (aapl_order) Order(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 500, 15000);
    
    auto* msft_order = pool.allocate();
    new (msft_order) Order(2, 100, "MSFT", Side::BUY, OrderType::LIMIT, 300, 30000);
    
    EXPECT_TRUE(risk_manager->submit_order(aapl_order));
    EXPECT_TRUE(risk_manager->submit_order(msft_order));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    
    EXPECT_EQ(risk_manager->orders_processed(), 2);
    EXPECT_EQ(aapl_engine->orders_processed(), 1);
    EXPECT_EQ(msft_engine->orders_processed(), 1);
}

TEST_F(IntegrationTest, RiskRejectionFlow) {
    auto* risk_manager = exchange->get_risk_manager();
    OrderPool pool(100);
    
    // Submit invalid order (exceeds size limit)
    auto* invalid_order = pool.allocate();
    new (invalid_order) Order(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 20000, 15000);
    
    EXPECT_TRUE(risk_manager->submit_order(invalid_order));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    EXPECT_EQ(risk_manager->orders_processed(), 1);
    EXPECT_EQ(risk_manager->orders_rejected(), 1);
    EXPECT_EQ(invalid_order->status, OrderStatus::REJECTED);
}

TEST_F(IntegrationTest, HighVolumeStressTest) {
    auto* risk_manager = exchange->get_risk_manager();
    OrderPool pool(5000);
    
    constexpr int num_orders = 1000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Submit many orders
    for (int i = 0; i < num_orders; ++i) {
        auto* order = pool.allocate();
        if (!order) break;
        
        ClientID client_id = 100 + (i % 10);  // 10 different clients
        Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        Price price = 15000 + (i % 20);  // Spread across price levels
        
        new (order) Order(i + 1, client_id, "AAPL", side, OrderType::LIMIT, 100, price);
        
        while (!risk_manager->submit_order(order)) {
            std::this_thread::yield();
        }
    }
    
    // Wait for processing
    while (risk_manager->orders_processed() < num_orders) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "Integration test processed " << num_orders << " orders in " 
              << duration.count() << " μs\n";
    std::cout << "End-to-end throughput: " << (num_orders * 1e6) / duration.count() 
              << " orders/sec\n";
    
    EXPECT_EQ(risk_manager->orders_processed(), num_orders);
    
    auto* matching_engine = exchange->get_matching_engine("AAPL");
    EXPECT_GT(matching_engine->trades_executed(), 0);
}

} // namespace rtes