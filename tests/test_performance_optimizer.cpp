#include <gtest/gtest.h>
#include "rtes/performance_optimizer.hpp"
#include <thread>
#include <vector>
#include <chrono>

using namespace rtes;

class PerformanceOptimizerTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(PerformanceOptimizerTest, FastStringParserSymbol) {
    char output[16];
    
    // Valid symbols
    EXPECT_TRUE(FastStringParser::parse_symbol("AAPL", output, sizeof(output)));
    EXPECT_STREQ(output, "AAPL");
    
    EXPECT_TRUE(FastStringParser::parse_symbol("MSFT123", output, sizeof(output)));
    EXPECT_STREQ(output, "MSFT123");
    
    // Invalid symbols
    EXPECT_FALSE(FastStringParser::parse_symbol("aapl", output, sizeof(output))); // lowercase
    EXPECT_FALSE(FastStringParser::parse_symbol("AAPL!", output, sizeof(output))); // special char
    EXPECT_FALSE(FastStringParser::parse_symbol("VERYLONGSYMBOLNAME", output, sizeof(output))); // too long
    EXPECT_FALSE(FastStringParser::parse_symbol("", output, sizeof(output))); // empty
}

TEST_F(PerformanceOptimizerTest, FastStringParserNumbers) {
    // Test uint64 parsing
    EXPECT_EQ(FastStringParser::parse_uint64("12345"), 12345ULL);
    EXPECT_EQ(FastStringParser::parse_uint64("0"), 0ULL);
    EXPECT_EQ(FastStringParser::parse_uint64("18446744073709551615"), 18446744073709551615ULL);
    EXPECT_EQ(FastStringParser::parse_uint64("abc"), 0ULL); // invalid
    
    // Test price parsing
    EXPECT_DOUBLE_EQ(FastStringParser::parse_price("123.45"), 123.45);
    EXPECT_DOUBLE_EQ(FastStringParser::parse_price("0.01"), 0.01);
    EXPECT_DOUBLE_EQ(FastStringParser::parse_price("1000"), 1000.0);
    EXPECT_DOUBLE_EQ(FastStringParser::parse_price("abc"), 0.0); // invalid
}

TEST_F(PerformanceOptimizerTest, FastStringParserValidation) {
    EXPECT_TRUE(FastStringParser::is_valid_symbol_fast("AAPL"));
    EXPECT_TRUE(FastStringParser::is_valid_symbol_fast("MSFT123"));
    EXPECT_TRUE(FastStringParser::is_valid_symbol_fast("A"));
    
    EXPECT_FALSE(FastStringParser::is_valid_symbol_fast(""));
    EXPECT_FALSE(FastStringParser::is_valid_symbol_fast("aapl"));
    EXPECT_FALSE(FastStringParser::is_valid_symbol_fast("AAPL!"));
    EXPECT_FALSE(FastStringParser::is_valid_symbol_fast("VERYLONGSYMBOL"));
}

TEST_F(PerformanceOptimizerTest, RingBufferBasicOperations) {
    RingBuffer<int, 8> buffer;
    
    EXPECT_TRUE(buffer.empty());
    EXPECT_FALSE(buffer.full());
    EXPECT_EQ(buffer.size(), 0);
    
    // Fill buffer
    for (int i = 0; i < 7; ++i) {
        EXPECT_TRUE(buffer.push(i));
        EXPECT_EQ(buffer.size(), i + 1);
    }
    
    EXPECT_TRUE(buffer.full());
    EXPECT_FALSE(buffer.push(999)); // Should fail when full
    
    // Empty buffer
    int value;
    for (int i = 0; i < 7; ++i) {
        EXPECT_TRUE(buffer.pop(value));
        EXPECT_EQ(value, i);
    }
    
    EXPECT_TRUE(buffer.empty());
    EXPECT_FALSE(buffer.pop(value)); // Should fail when empty
}

TEST_F(PerformanceOptimizerTest, RingBufferConcurrency) {
    RingBuffer<int, 1024> buffer;
    std::atomic<int> producer_count{0};
    std::atomic<int> consumer_count{0};
    
    const int num_items = 10000;
    
    // Producer thread
    std::thread producer([&buffer, &producer_count, num_items]() {
        for (int i = 0; i < num_items; ++i) {
            while (!buffer.push(i)) {
                std::this_thread::yield();
            }
            producer_count.fetch_add(1);
        }
    });
    
    // Consumer thread
    std::thread consumer([&buffer, &consumer_count, num_items]() {
        int value;
        for (int i = 0; i < num_items; ++i) {
            while (!buffer.pop(value)) {
                std::this_thread::yield();
            }
            EXPECT_EQ(value, i);
            consumer_count.fetch_add(1);
        }
    });
    
    producer.join();
    consumer.join();
    
    EXPECT_EQ(producer_count.load(), num_items);
    EXPECT_EQ(consumer_count.load(), num_items);
    EXPECT_TRUE(buffer.empty());
}

TEST_F(PerformanceOptimizerTest, CompactAllocator) {
    CompactAllocator<int> allocator;
    
    std::vector<int*> ptrs;
    
    // Allocate many objects
    for (int i = 0; i < 1000; ++i) {
        int* ptr = allocator.allocate();
        EXPECT_NE(ptr, nullptr);
        *ptr = i;
        ptrs.push_back(ptr);
    }
    
    // Verify values
    for (int i = 0; i < 1000; ++i) {
        EXPECT_EQ(*ptrs[i], i);
    }
    
    // Reset allocator
    allocator.reset();
    
    // Should be able to allocate again
    int* new_ptr = allocator.allocate();
    EXPECT_NE(new_ptr, nullptr);
}

TEST_F(PerformanceOptimizerTest, LatencyTracker) {
    LatencyTracker tracker;
    
    // Record some latencies
    tracker.record_latency(1000); // 1μs
    tracker.record_latency(2000); // 2μs
    tracker.record_latency(500);  // 0.5μs
    tracker.record_latency(1500); // 1.5μs
    
    auto stats = tracker.get_stats();
    EXPECT_EQ(stats.count, 4);
    EXPECT_EQ(stats.avg_ns, 1250); // (1000+2000+500+1500)/4
    EXPECT_EQ(stats.min_ns, 500);
    EXPECT_EQ(stats.max_ns, 2000);
    
    // Reset and verify
    tracker.reset();
    stats = tracker.get_stats();
    EXPECT_EQ(stats.count, 0);
    EXPECT_EQ(stats.avg_ns, 0);
}

TEST_F(PerformanceOptimizerTest, LatencyTrackerConcurrency) {
    LatencyTracker tracker;
    const int num_threads = 4;
    const int measurements_per_thread = 1000;
    
    std::vector<std::thread> threads;
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&tracker, measurements_per_thread, t]() {
            for (int i = 0; i < measurements_per_thread; ++i) {
                tracker.record_latency(1000 + t * 100 + i);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto stats = tracker.get_stats();
    EXPECT_EQ(stats.count, num_threads * measurements_per_thread);
    EXPECT_GT(stats.avg_ns, 0);
    EXPECT_GT(stats.max_ns, stats.min_ns);
}

TEST_F(PerformanceOptimizerTest, MemoryMonitor) {
    MemoryMonitor monitor;
    
    // Record allocations
    monitor.record_allocation(1024);
    monitor.record_allocation(2048);
    monitor.record_allocation(512);
    
    auto stats = monitor.get_stats();
    EXPECT_EQ(stats.total_allocated, 3584);
    EXPECT_EQ(stats.current_allocated, 3584);
    EXPECT_EQ(stats.peak_allocated, 3584);
    EXPECT_EQ(stats.allocation_count, 3);
    
    // Record deallocation
    monitor.record_deallocation(1024);
    
    stats = monitor.get_stats();
    EXPECT_EQ(stats.total_allocated, 3584); // Total doesn't change
    EXPECT_EQ(stats.current_allocated, 2560); // Current decreases
    EXPECT_EQ(stats.peak_allocated, 3584); // Peak remains
}

TEST_F(PerformanceOptimizerTest, ThroughputTracker) {
    ThroughputTracker tracker;
    
    // Record events rapidly
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i) {
        tracker.record_event();
    }
    
    // Wait a bit and check throughput
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    double throughput = tracker.get_throughput_per_second();
    EXPECT_GT(throughput, 0.0);
    
    // Should be roughly 10,000 events/sec (1000 events in 0.1 sec)
    // Allow for timing variations
    EXPECT_GT(throughput, 5000.0);
    EXPECT_LT(throughput, 20000.0);
}

TEST_F(PerformanceOptimizerTest, HighResTimer) {
    uint64_t time1 = HighResTimer::now_ns();
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    uint64_t time2 = HighResTimer::now_ns();
    
    EXPECT_GT(time2, time1);
    uint64_t elapsed = time2 - time1;
    
    // Should be roughly 100μs = 100,000ns
    // Allow for timing variations
    EXPECT_GT(elapsed, 50000);   // At least 50μs
    EXPECT_LT(elapsed, 200000);  // At most 200μs
    
    // Test RDTSC (if available)
    uint64_t tsc1 = HighResTimer::rdtsc();
    uint64_t tsc2 = HighResTimer::rdtsc();
    EXPECT_GT(tsc2, tsc1);
}

TEST_F(PerformanceOptimizerTest, ScopedLatencyMeasurement) {
    LatencyTracker tracker;
    
    {
        ScopedLatencyMeasurement measure(tracker);
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    } // Measurement recorded on destruction
    
    auto stats = tracker.get_stats();
    EXPECT_EQ(stats.count, 1);
    EXPECT_GT(stats.avg_ns, 50000);  // At least 50μs
    EXPECT_LT(stats.avg_ns, 200000); // At most 200μs
}

TEST_F(PerformanceOptimizerTest, PerformanceOptimizerIntegration) {
    PerformanceOptimizer optimizer;
    
    // Test string parsing
    char symbol[16];
    EXPECT_TRUE(optimizer.parse_symbol_fast("AAPL", symbol));
    EXPECT_STREQ(symbol, "AAPL");
    
    EXPECT_EQ(optimizer.parse_order_id_fast("12345"), 12345ULL);
    EXPECT_EQ(optimizer.parse_price_fast("123.45"), 1234500ULL); // Fixed point
    
    // Test performance tracking
    auto& latency_tracker = optimizer.get_latency_tracker("test_operation");
    latency_tracker.record_latency(1000);
    
    auto& throughput_tracker = optimizer.get_throughput_tracker("test_operation");
    throughput_tracker.record_event();
    
    // Test memory monitoring
    auto& memory_monitor = optimizer.get_memory_monitor();
    auto stats = memory_monitor.get_stats();
    EXPECT_EQ(stats.allocation_count, 0); // No allocations yet
    
    // Test stats printing (should not crash)
    EXPECT_NO_THROW(optimizer.print_performance_stats());
    
    // Test reset
    EXPECT_NO_THROW(optimizer.reset_all_stats());
}

TEST_F(PerformanceOptimizerTest, OrderBookLevelsSOA) {
    OrderBookLevelsSOA levels;
    
    EXPECT_EQ(levels.size, 0);
    
    // Insert some levels
    Order dummy_order;
    levels.insert_level(15000, 100, 1, &dummy_order);
    levels.insert_level(15010, 200, 2, &dummy_order);
    levels.insert_level(15020, 150, 1, &dummy_order);
    
    EXPECT_EQ(levels.size, 3);
    EXPECT_EQ(levels.prices[0], 15000);
    EXPECT_EQ(levels.quantities[0], 100);
    EXPECT_EQ(levels.order_counts[0], 1);
    
    EXPECT_EQ(levels.prices[1], 15010);
    EXPECT_EQ(levels.quantities[1], 200);
    EXPECT_EQ(levels.order_counts[1], 2);
    
    // Test prefetching (should not crash)
    EXPECT_NO_THROW(levels.prefetch_level(0));
    EXPECT_NO_THROW(levels.prefetch_level(1));
    EXPECT_NO_THROW(levels.prefetch_level(10)); // Out of bounds, should be safe
}

TEST_F(PerformanceOptimizerTest, PerformanceBenchmark) {
    // Benchmark string parsing performance
    const int iterations = 100000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        char symbol[16];
        FastStringParser::parse_symbol("AAPL", symbol, sizeof(symbol));
        uint64_t order_id = FastStringParser::parse_uint64("12345");
        double price = FastStringParser::parse_price("123.45");
        
        // Prevent optimization
        volatile char* vol_symbol = symbol;
        volatile uint64_t vol_order_id = order_id;
        volatile double vol_price = price;
        (void)vol_symbol;
        (void)vol_order_id;
        (void)vol_price;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    
    double ns_per_operation = static_cast<double>(duration.count()) / iterations;
    
    // Should be very fast (less than 100ns per operation)
    EXPECT_LT(ns_per_operation, 100.0);
    
    std::cout << "String parsing performance: " << ns_per_operation << " ns/operation\n";
}