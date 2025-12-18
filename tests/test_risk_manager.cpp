#include <gtest/gtest.h>
#include "rtes/risk_manager.hpp"
#include "rtes/memory_pool.hpp"
#include "rtes/matching_engine.hpp"
#include <thread>
#include <chrono>

namespace rtes {

class RiskManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        RiskConfig risk_config;
        risk_config.max_order_size = 10000;
        risk_config.max_notional_per_client = 1000000.0;
        risk_config.max_orders_per_second = 100;
        risk_config.price_collar_enabled = true;
        
        std::vector<SymbolConfig> symbols = {
            {"AAPL", 0.01, 1, 10.0},
            {"MSFT", 0.01, 1, 10.0}
        };
        
        pool = std::make_unique<OrderPool>(1000);
        risk_manager = std::make_unique<RiskManager>(risk_config, symbols);
        
        matching_engine = std::make_unique<MatchingEngine>("AAPL", *pool);
        risk_manager->add_matching_engine("AAPL", matching_engine.get());
        
        risk_manager->start();
        matching_engine->start();
    }
    
    void TearDown() override {
        risk_manager->stop();
        matching_engine->stop();
    }
    
    Order* create_order(OrderID id, ClientID client_id, const std::string& symbol, 
                       Side side, Quantity qty, Price price) {
        auto* order = pool->allocate();
        // FIX: Order constructor takes const char*, not ClientID
        new (order) Order(id, client_id.c_str(), symbol.c_str(), side, OrderType::LIMIT, qty, price);
        return order;
    }
    
    std::unique_ptr<OrderPool> pool;
    std::unique_ptr<RiskManager> risk_manager;
    std::unique_ptr<MatchingEngine> matching_engine;
};

TEST_F(RiskManagerTest, ValidOrderApproval) {
    auto* order = create_order(1, ClientID("100"), "AAPL", Side::BUY, 1000, 15000);
    
    EXPECT_TRUE(risk_manager->submit_order(order));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // FIX: orders_processed()/orders_rejected() don't exist.
    // Use get_stats() which returns Stats struct.
    EXPECT_EQ(risk_manager->get_stats().processed, 1);
    EXPECT_EQ(risk_manager->get_stats().rejected, 0);
}

TEST_F(RiskManagerTest, OrderSizeRejection) {
    auto* order = create_order(1, ClientID("100"), "AAPL", Side::BUY, 20000, 15000);
    
    EXPECT_TRUE(risk_manager->submit_order(order));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    EXPECT_EQ(risk_manager->get_stats().processed, 1);
    EXPECT_EQ(risk_manager->get_stats().rejected, 1);
    EXPECT_EQ(order->status, OrderStatus::REJECTED);
}

TEST_F(RiskManagerTest, InvalidSymbolRejection) {
    auto* order = create_order(1, ClientID("100"), "INVALID", Side::BUY, 1000, 15000);
    
    EXPECT_TRUE(risk_manager->submit_order(order));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    EXPECT_EQ(risk_manager->get_stats().processed, 1);
    EXPECT_EQ(risk_manager->get_stats().rejected, 1);
}

TEST_F(RiskManagerTest, DuplicateOrderRejection) {
    auto* order1 = create_order(1, ClientID("100"), "AAPL", Side::BUY, 1000, 15000);
    auto* order2 = create_order(1, ClientID("100"), "AAPL", Side::BUY, 500, 15100);
    
    EXPECT_TRUE(risk_manager->submit_order(order1));
    EXPECT_TRUE(risk_manager->submit_order(order2));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    EXPECT_EQ(risk_manager->get_stats().processed, 2);
    EXPECT_EQ(risk_manager->get_stats().rejected, 1);
}

TEST_F(RiskManagerTest, RateLimitRejection) {
    for (int i = 0; i < 150; ++i) {
        auto* order = create_order(i + 1, ClientID("100"), "AAPL", Side::BUY, 100, 15000);
        // FIX: Suppress [[nodiscard]] warning
        (void)risk_manager->submit_order(order);
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_EQ(risk_manager->get_stats().processed, 150);
    EXPECT_GT(risk_manager->get_stats().rejected, 40u);
}

TEST_F(RiskManagerTest, CreditLimitRejection) {
    for (int i = 0; i < 10; ++i) {
        auto* order = create_order(i + 1, ClientID("100"), "AAPL", Side::BUY, 10000, 15000);
        // FIX: Suppress [[nodiscard]] warning
        (void)risk_manager->submit_order(order);
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_EQ(risk_manager->get_stats().processed, 10);
    EXPECT_GT(risk_manager->get_stats().rejected, 3u);
}

TEST_F(RiskManagerTest, OrderCancellation) {
    auto* order = create_order(1, ClientID("100"), "AAPL", Side::BUY, 1000, 15000);
    
    EXPECT_TRUE(risk_manager->submit_order(order));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    
    EXPECT_TRUE(risk_manager->submit_cancel(1, ClientID("100")));
    EXPECT_TRUE(risk_manager->submit_cancel(1, ClientID("101")));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    EXPECT_EQ(risk_manager->get_stats().processed, 1);
}

TEST_F(RiskManagerTest, MultipleClients) {
    auto* order1 = create_order(1, ClientID("100"), "AAPL", Side::BUY, 5000, 15000);
    auto* order2 = create_order(2, ClientID("101"), "AAPL", Side::BUY, 5000, 15000);
    
    EXPECT_TRUE(risk_manager->submit_order(order1));
    EXPECT_TRUE(risk_manager->submit_order(order2));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    EXPECT_EQ(risk_manager->get_stats().processed, 2);
    EXPECT_EQ(risk_manager->get_stats().rejected, 0);
}

TEST_F(RiskManagerTest, HighThroughput) {
    constexpr int num_orders = 1000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_orders; ++i) {
        ClientID client_id(std::to_string(100 + (i % 10)).c_str());
        auto* order = create_order(i + 1, client_id, "AAPL", Side::BUY, 100, 15000);
        EXPECT_TRUE(risk_manager->submit_order(order));
    }
    
    // FIX: Use get_stats().processed instead of orders_processed()
    while (risk_manager->get_stats().processed < static_cast<uint64_t>(num_orders)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "Risk manager processed " << num_orders << " orders in " 
              << duration.count() << " μs\n";
    std::cout << "Throughput: " << (num_orders * 1e6) / duration.count() 
              << " orders/sec\n";
    
    EXPECT_EQ(risk_manager->get_stats().processed, static_cast<uint64_t>(num_orders));
}

} // namespace rtes