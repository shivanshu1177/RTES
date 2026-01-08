#include <gtest/gtest.h>
#include "rtes/strategies.hpp"
#include <thread>
#include <chrono>

namespace rtes {

class StrategiesTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Note: These tests would require a running exchange for full integration
        // Here we test the basic construction and configuration
    }
};

TEST_F(StrategiesTest, MarketMakerConstruction) {
    MarketMakerStrategy mm("localhost", 8888, 100, "AAPL", 10);
    
    EXPECT_EQ(mm.orders_sent(), 0);
    EXPECT_EQ(mm.orders_acked(), 0);
    EXPECT_EQ(mm.orders_rejected(), 0);
    EXPECT_EQ(mm.trades_received(), 0);
}

TEST_F(StrategiesTest, MomentumStrategyConstruction) {
    MomentumStrategy momentum("localhost", 8888, 101, "MSFT");
    
    EXPECT_EQ(momentum.orders_sent(), 0);
    EXPECT_EQ(momentum.orders_acked(), 0);
    EXPECT_EQ(momentum.orders_rejected(), 0);
    EXPECT_EQ(momentum.trades_received(), 0);
}

TEST_F(StrategiesTest, ArbitrageStrategyConstruction) {
    ArbitrageStrategy arbitrage("localhost", 8888, 102, "AAPL", "MSFT");
    
    EXPECT_EQ(arbitrage.orders_sent(), 0);
    EXPECT_EQ(arbitrage.orders_acked(), 0);
    EXPECT_EQ(arbitrage.orders_rejected(), 0);
    EXPECT_EQ(arbitrage.trades_received(), 0);
}

TEST_F(StrategiesTest, LiquidityTakerConstruction) {
    LiquidityTakerStrategy taker("localhost", 8888, 103, "GOOGL");
    
    EXPECT_EQ(taker.orders_sent(), 0);
    EXPECT_EQ(taker.orders_acked(), 0);
    EXPECT_EQ(taker.orders_rejected(), 0);
    EXPECT_EQ(taker.trades_received(), 0);
}

TEST_F(StrategiesTest, ClientBaseUtilities) {
    class TestClient : public ClientBase {
    public:
        TestClient() : ClientBase("localhost", 8888, 999) {}
        
        void on_tick() override {}
        
        // Expose protected methods for testing
        using ClientBase::random_double;
        using ClientBase::random_int;
        using ClientBase::generate_order_id;
    };
    
    TestClient client;
    
    // Test random number generation
    for (int i = 0; i < 100; ++i) {
        double d = client.random_double(1.0, 2.0);
        EXPECT_GE(d, 1.0);
        EXPECT_LE(d, 2.0);
        
        int n = client.random_int(10, 20);
        EXPECT_GE(n, 10);
        EXPECT_LE(n, 20);
    }
    
    // Test order ID generation
    uint64_t id1 = client.generate_order_id();
    uint64_t id2 = client.generate_order_id();
    EXPECT_GT(id2, id1);
}

// Integration test that would require a running exchange
TEST_F(StrategiesTest, DISABLED_IntegrationTest) {
    // This test is disabled by default as it requires a running exchange
    // To enable: start exchange and rename to remove DISABLED_ prefix
    
    MarketMakerStrategy mm("localhost", 8888, 200, "AAPL");
    
    if (mm.connect()) {
        std::cout << "Running market maker for 5 seconds...\n";
        mm.run(std::chrono::seconds(5));
        
        std::cout << "Orders sent: " << mm.orders_sent() << "\n";
        std::cout << "Orders acked: " << mm.orders_acked() << "\n";
        std::cout << "Orders rejected: " << mm.orders_rejected() << "\n";
        std::cout << "Trades received: " << mm.trades_received() << "\n";
        
        EXPECT_GT(mm.orders_sent(), 0);
        
        mm.disconnect();
    } else {
        std::cout << "Could not connect to exchange (expected if not running)\n";
    }
}

} // namespace rtes