#include <gtest/gtest.h>
#include "rtes/memory_pool.hpp"
#include "rtes/types.hpp"
#include <thread>
#include <vector>

namespace rtes {

class MemoryPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool = std::make_unique<OrderPool>(1000);
    }
    
    std::unique_ptr<OrderPool> pool;
};

TEST_F(MemoryPoolTest, BasicAllocation) {
    EXPECT_EQ(pool->available(), 1000);
    
    auto* order = pool->allocate();
    ASSERT_NE(order, nullptr);
    EXPECT_EQ(pool->available(), 999);
    
    pool->deallocate(order);
    EXPECT_EQ(pool->available(), 1000);
}

TEST_F(MemoryPoolTest, ExhaustPool) {
    std::vector<Order*> orders;
    
    // Allocate all orders
    for (size_t i = 0; i < 1000; ++i) {
        auto* order = pool->allocate();
        ASSERT_NE(order, nullptr);
        orders.push_back(order);
    }
    
    EXPECT_EQ(pool->available(), 0);
    
    // Next allocation should fail
    auto* order = pool->allocate();
    EXPECT_EQ(order, nullptr);
    
    // Deallocate all
    for (auto* o : orders) {
        pool->deallocate(o);
    }
    
    EXPECT_EQ(pool->available(), 1000);
}

TEST_F(MemoryPoolTest, ConcurrentAccess) {
    constexpr int num_threads = 4;
    constexpr int ops_per_thread = 100;
    
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                auto* order = pool->allocate();
                if (order) {
                    // Simulate some work
                    order->id = i;
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                    pool->deallocate(order);
                    success_count.fetch_add(1);
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_GT(success_count.load(), 0);
    EXPECT_EQ(pool->available(), 1000);
}

TEST_F(MemoryPoolTest, OrderConstruction) {
    auto* order = pool->allocate();
    ASSERT_NE(order, nullptr);
    
    // Construct order in-place
    new (order) Order(12345, 100, "AAPL", Side::BUY, OrderType::LIMIT, 1000, 15000);
    
    EXPECT_EQ(order->id, 12345);
    EXPECT_EQ(order->client_id, 100);
    EXPECT_STREQ(order->symbol, "AAPL");
    EXPECT_EQ(order->side, Side::BUY);
    EXPECT_EQ(order->quantity, 1000);
    EXPECT_EQ(order->price, 15000);
    
    pool->deallocate(order);
}

} // namespace rtes