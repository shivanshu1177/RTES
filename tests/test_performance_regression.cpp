#include <gtest/gtest.h>
#include "rtes/order_book.hpp"
#include "rtes/memory_pool.hpp"
#include "rtes/protocol.hpp"
#include "rtes/queues.hpp"
#include <chrono>
#include <vector>
#include <numeric>
#include <algorithm>

using namespace rtes;
using namespace std::chrono;

class PerformanceRegressionTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool = std::make_unique<OrderPool>(100000);
    }
    
    template<typename Func>
    double measure_latency_us(Func&& func, int iterations = 1000) {
        std::vector<double> latencies;
        latencies.reserve(iterations);
        
        for (int i = 0; i < iterations; ++i) {
            auto start = high_resolution_clock::now();
            func();
            auto end = high_resolution_clock::now();
            latencies.push_back(duration_cast<nanoseconds>(end - start).count() / 1000.0);
        }
        
        return std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
    }
    
    template<typename Func>
    std::vector<double> measure_latencies_us(Func&& func, int iterations = 1000) {
        std::vector<double> latencies;
        latencies.reserve(iterations);
        
        for (int i = 0; i < iterations; ++i) {
            auto start = high_resolution_clock::now();
            func();
            auto end = high_resolution_clock::now();
            latencies.push_back(duration_cast<nanoseconds>(end - start).count() / 1000.0);
        }
        
        return latencies;
    }
    
    double calculate_percentile(std::vector<double>& data, double percentile) {
        std::sort(data.begin(), data.end());
        size_t index = static_cast<size_t>(data.size() * percentile / 100.0);
        return data[std::min(index, data.size() - 1)];
    }
    
    std::unique_ptr<OrderPool> pool;
};

// Memory Pool Performance Regression
TEST_F(PerformanceRegressionTest, MemoryPoolAllocationLatency) {
    constexpr int ITERATIONS = 10000;
    constexpr double TARGET_LATENCY_US = 1.0;
    
    auto latency = measure_latency_us([this]() {
        auto* order = pool->allocate();
        pool->deallocate(order);
    }, ITERATIONS);
    
    EXPECT_LT(latency, TARGET_LATENCY_US) 
        << "Memory pool allocation latency: " << latency << "μs (target: <" << TARGET_LATENCY_US << "μs)";
}

TEST_F(PerformanceRegressionTest, MemoryPoolThroughput) {
    constexpr int OPERATIONS = 100000;
    constexpr double TARGET_OPS_PER_SEC = 1000000;
    
    auto start = high_resolution_clock::now();
    for (int i = 0; i < OPERATIONS; ++i) {
        auto* order = pool->allocate();
        pool->deallocate(order);
    }
    auto end = high_resolution_clock::now();
    
    double duration_sec = duration_cast<microseconds>(end - start).count() / 1e6;
    double ops_per_sec = OPERATIONS / duration_sec;
    
    EXPECT_GT(ops_per_sec, TARGET_OPS_PER_SEC)
        << "Memory pool throughput: " << ops_per_sec << " ops/s (target: >" << TARGET_OPS_PER_SEC << " ops/s)";
}

// OrderBook Add Order Performance
TEST_F(PerformanceRegressionTest, OrderBookAddOrderLatency) {
    constexpr int ITERATIONS = 1000;
    constexpr double TARGET_AVG_LATENCY_US = 10.0;
    constexpr double TARGET_P99_LATENCY_US = 100.0;
    
    OrderBook book("AAPL", *pool);
    
    auto latencies = measure_latencies_us([&, order_id = 0]() mutable {
        auto* order = pool->allocate();
        order->order_id = ++order_id;
        order->side = (order_id % 2 == 0) ? Side::BUY : Side::SELL;
        order->price = 15000 + (order_id % 100);
        order->quantity = 100;
        order->remaining_quantity = 100;
        order->type = OrderType::LIMIT;
        book.add_order(order);
    }, ITERATIONS);
    
    double avg_latency = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
    double p99_latency = calculate_percentile(latencies, 99.0);
    
    EXPECT_LT(avg_latency, TARGET_AVG_LATENCY_US)
        << "OrderBook add_order avg latency: " << avg_latency << "μs (target: <" << TARGET_AVG_LATENCY_US << "μs)";
    EXPECT_LT(p99_latency, TARGET_P99_LATENCY_US)
        << "OrderBook add_order P99 latency: " << p99_latency << "μs (target: <" << TARGET_P99_LATENCY_US << "μs)";
}

// OrderBook Cancel Order Performance
TEST_F(PerformanceRegressionTest, OrderBookCancelOrderLatency) {
    constexpr int ITERATIONS = 1000;
    constexpr double TARGET_LATENCY_US = 5.0;
    
    OrderBook book("AAPL", *pool);
    
    for (int i = 0; i < ITERATIONS; ++i) {
        auto* order = pool->allocate();
        order->order_id = i + 1;
        order->side = Side::BUY;
        order->price = 15000;
        order->quantity = 100;
        order->remaining_quantity = 100;
        order->type = OrderType::LIMIT;
        book.add_order(order);
    }
    
    auto latency = measure_latency_us([&, order_id = 1]() mutable {
        book.cancel_order(order_id++);
    }, ITERATIONS);
    
    EXPECT_LT(latency, TARGET_LATENCY_US)
        << "OrderBook cancel_order latency: " << latency << "μs (target: <" << TARGET_LATENCY_US << "μs)";
}

// OrderBook Matching Performance
TEST_F(PerformanceRegressionTest, OrderBookMatchingLatency) {
    constexpr int ITERATIONS = 500;
    constexpr double TARGET_LATENCY_US = 15.0;
    
    std::vector<Trade> trades;
    OrderBook book("AAPL", *pool, [&](const Trade& t) { trades.push_back(t); });
    
    for (int i = 0; i < ITERATIONS; ++i) {
        auto* order = pool->allocate();
        order->order_id = i + 1;
        order->side = Side::BUY;
        order->price = 15000;
        order->quantity = 100;
        order->remaining_quantity = 100;
        order->type = OrderType::LIMIT;
        book.add_order(order);
    }
    
    auto latency = measure_latency_us([&, order_id = ITERATIONS + 1]() mutable {
        auto* order = pool->allocate();
        order->order_id = order_id++;
        order->side = Side::SELL;
        order->price = 15000;
        order->quantity = 100;
        order->remaining_quantity = 100;
        order->type = OrderType::LIMIT;
        book.add_order(order);
    }, ITERATIONS);
    
    EXPECT_LT(latency, TARGET_LATENCY_US)
        << "OrderBook matching latency: " << latency << "μs (target: <" << TARGET_LATENCY_US << "μs)";
}

// OrderBook Depth Snapshot Performance
TEST_F(PerformanceRegressionTest, OrderBookDepthSnapshotLatency) {
    constexpr int BOOK_SIZE = 100;
    constexpr int ITERATIONS = 1000;
    constexpr double TARGET_LATENCY_US = 20.0;
    
    OrderBook book("AAPL", *pool);
    
    for (int i = 0; i < BOOK_SIZE; ++i) {
        auto* buy_order = pool->allocate();
        buy_order->order_id = i + 1;
        buy_order->side = Side::BUY;
        buy_order->price = 15000 - i;
        buy_order->quantity = 100;
        buy_order->remaining_quantity = 100;
        buy_order->type = OrderType::LIMIT;
        book.add_order(buy_order);
        
        auto* sell_order = pool->allocate();
        sell_order->order_id = BOOK_SIZE + i + 1;
        sell_order->side = Side::SELL;
        sell_order->price = 15100 + i;
        sell_order->quantity = 100;
        sell_order->remaining_quantity = 100;
        sell_order->type = OrderType::LIMIT;
        book.add_order(sell_order);
    }
    
    auto latency = measure_latency_us([&]() {
        auto depth = book.get_depth(10);
    }, ITERATIONS);
    
    EXPECT_LT(latency, TARGET_LATENCY_US)
        << "OrderBook depth snapshot latency: " << latency << "μs (target: <" << TARGET_LATENCY_US << "μs)";
}

// Protocol Checksum Performance
TEST_F(PerformanceRegressionTest, ProtocolChecksumLatency) {
    constexpr int ITERATIONS = 10000;
    constexpr double TARGET_LATENCY_US = 2.0;
    
    NewOrderMessage msg;
    msg.header.type = NEW_ORDER;
    msg.header.length = sizeof(NewOrderMessage);
    msg.order_id = 12345;
    
    auto latency = measure_latency_us([&]() {
        ProtocolUtils::set_checksum(msg.header, &msg);
    }, ITERATIONS);
    
    EXPECT_LT(latency, TARGET_LATENCY_US)
        << "Protocol checksum latency: " << latency << "μs (target: <" << TARGET_LATENCY_US << "μs)";
}

TEST_F(PerformanceRegressionTest, ProtocolValidateChecksumLatency) {
    constexpr int ITERATIONS = 10000;
    constexpr double TARGET_LATENCY_US = 2.0;
    
    NewOrderMessage msg;
    msg.header.type = NEW_ORDER;
    msg.header.length = sizeof(NewOrderMessage);
    msg.order_id = 12345;
    ProtocolUtils::set_checksum(msg.header, &msg);
    
    auto latency = measure_latency_us([&]() {
        ProtocolUtils::validate_checksum(msg.header, &msg);
    }, ITERATIONS);
    
    EXPECT_LT(latency, TARGET_LATENCY_US)
        << "Protocol validate checksum latency: " << latency << "μs (target: <" << TARGET_LATENCY_US << "μs)";
}

// Queue Performance
TEST_F(PerformanceRegressionTest, SPSCQueueLatency) {
    constexpr int ITERATIONS = 10000;
    constexpr double TARGET_LATENCY_US = 1.0;
    
    SPSCQueue<Order*> queue(1024);
    
    auto* order = pool->allocate();
    
    auto latency = measure_latency_us([&]() {
        queue.push(order);
        Order* popped;
        queue.pop(popped);
    }, ITERATIONS);
    
    pool->deallocate(order);
    
    EXPECT_LT(latency, TARGET_LATENCY_US)
        << "SPSC queue latency: " << latency << "μs (target: <" << TARGET_LATENCY_US << "μs)";
}

// End-to-End Order Processing Performance
TEST_F(PerformanceRegressionTest, EndToEndOrderProcessingLatency) {
    constexpr int ITERATIONS = 500;
    constexpr double TARGET_AVG_LATENCY_US = 20.0;
    constexpr double TARGET_P99_LATENCY_US = 100.0;
    constexpr double TARGET_P999_LATENCY_US = 500.0;
    
    std::vector<Trade> trades;
    OrderBook book("AAPL", *pool, [&](const Trade& t) { trades.push_back(t); });
    
    auto latencies = measure_latencies_us([&, order_id = 0]() mutable {
        NewOrderMessage msg;
        msg.header.type = NEW_ORDER;
        msg.header.length = sizeof(NewOrderMessage);
        msg.header.sequence = order_id;
        msg.header.timestamp = ProtocolUtils::get_timestamp_ns();
        msg.order_id = ++order_id;
        msg.side = (order_id % 2 == 0) ? 1 : 2;
        msg.quantity = 100;
        msg.price = 15000;
        msg.order_type = 2;
        
        ProtocolUtils::set_checksum(msg.header, &msg);
        
        if (ProtocolUtils::validate_checksum(msg.header, &msg)) {
            auto* order = pool->allocate();
            order->order_id = msg.order_id;
            order->side = (msg.side == 1) ? Side::BUY : Side::SELL;
            order->price = msg.price;
            order->quantity = msg.quantity;
            order->remaining_quantity = msg.quantity;
            order->type = OrderType::LIMIT;
            book.add_order(order);
        }
    }, ITERATIONS);
    
    double avg_latency = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
    double p99_latency = calculate_percentile(latencies, 99.0);
    double p999_latency = calculate_percentile(latencies, 99.9);
    
    EXPECT_LT(avg_latency, TARGET_AVG_LATENCY_US)
        << "End-to-end avg latency: " << avg_latency << "μs (target: <" << TARGET_AVG_LATENCY_US << "μs)";
    EXPECT_LT(p99_latency, TARGET_P99_LATENCY_US)
        << "End-to-end P99 latency: " << p99_latency << "μs (target: <" << TARGET_P99_LATENCY_US << "μs)";
    EXPECT_LT(p999_latency, TARGET_P999_LATENCY_US)
        << "End-to-end P999 latency: " << p999_latency << "μs (target: <" << TARGET_P999_LATENCY_US << "μs)";
}

// Throughput Regression Tests
TEST_F(PerformanceRegressionTest, OrderBookThroughput) {
    constexpr int OPERATIONS = 100000;
    constexpr double TARGET_OPS_PER_SEC = 100000;
    
    OrderBook book("AAPL", *pool);
    
    auto start = high_resolution_clock::now();
    for (int i = 0; i < OPERATIONS; ++i) {
        auto* order = pool->allocate();
        order->order_id = i + 1;
        order->side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        order->price = 15000 + (i % 100);
        order->quantity = 100;
        order->remaining_quantity = 100;
        order->type = OrderType::LIMIT;
        book.add_order(order);
    }
    auto end = high_resolution_clock::now();
    
    double duration_sec = duration_cast<microseconds>(end - start).count() / 1e6;
    double ops_per_sec = OPERATIONS / duration_sec;
    
    EXPECT_GT(ops_per_sec, TARGET_OPS_PER_SEC)
        << "OrderBook throughput: " << ops_per_sec << " ops/s (target: >" << TARGET_OPS_PER_SEC << " ops/s)";
}

// Memory Usage Regression
TEST_F(PerformanceRegressionTest, MemoryPoolFragmentation) {
    constexpr int ALLOCATIONS = 1000;
    constexpr int CYCLES = 100;
    
    std::vector<Order*> orders;
    orders.reserve(ALLOCATIONS);
    
    for (int cycle = 0; cycle < CYCLES; ++cycle) {
        for (int i = 0; i < ALLOCATIONS; ++i) {
            orders.push_back(pool->allocate());
        }
        
        for (int i = 0; i < ALLOCATIONS / 2; ++i) {
            pool->deallocate(orders[i * 2]);
        }
        
        for (int i = 0; i < ALLOCATIONS / 2; ++i) {
            orders[i * 2] = pool->allocate();
        }
        
        for (auto* order : orders) {
            pool->deallocate(order);
        }
        orders.clear();
    }
    
    SUCCEED() << "Memory pool survived " << CYCLES << " fragmentation cycles";
}
