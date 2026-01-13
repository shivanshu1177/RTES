#include <gtest/gtest.h>
#include "rtes/metrics.hpp"
#include <thread>
#include <vector>

namespace rtes {

class MetricsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset registry for clean tests
        auto& registry = MetricsRegistry::instance();
        // Note: In production, we'd need a way to reset the registry
    }
};

TEST_F(MetricsTest, CounterBasicOperations) {
    auto* counter = METRICS_COUNTER("test_counter");
    
    EXPECT_EQ(counter->get(), 0);
    
    counter->increment();
    EXPECT_EQ(counter->get(), 1);
    
    counter->increment(5);
    EXPECT_EQ(counter->get(), 6);
    
    counter->reset();
    EXPECT_EQ(counter->get(), 0);
}

TEST_F(MetricsTest, CounterConcurrency) {
    auto* counter = METRICS_COUNTER("concurrent_counter");
    constexpr int num_threads = 4;
    constexpr int increments_per_thread = 1000;
    
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([counter, increments_per_thread]() {
            for (int j = 0; j < increments_per_thread; ++j) {
                counter->increment();
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(counter->get(), num_threads * increments_per_thread);
}

TEST_F(MetricsTest, HistogramBasicOperations) {
    std::vector<double> buckets = {1.0, 5.0, 10.0, 50.0, 100.0};
    auto* histogram = METRICS_HISTOGRAM("test_histogram", buckets);
    
    histogram->observe(0.5);   // Below first bucket
    histogram->observe(3.0);   // In first bucket
    histogram->observe(7.0);   // In second bucket
    histogram->observe(25.0);  // In third bucket
    histogram->observe(150.0); // Above all buckets
    
    auto stats = histogram->get_stats();
    EXPECT_EQ(stats.count, 5);
    EXPECT_DOUBLE_EQ(stats.sum, 185.5);
    
    // Check bucket distribution
    EXPECT_EQ(stats.bucket_counts[0], 2);  // 0.5, 3.0
    EXPECT_EQ(stats.bucket_counts[1], 1);  // 7.0
    EXPECT_EQ(stats.bucket_counts[2], 1);  // 25.0
    EXPECT_EQ(stats.bucket_counts[3], 0);  // none
    EXPECT_EQ(stats.bucket_counts[4], 0);  // none
    EXPECT_EQ(stats.bucket_counts[5], 1);  // 150.0 (+Inf bucket)
}

TEST_F(MetricsTest, HistogramPrometheusOutput) {
    std::vector<double> buckets = {1.0, 5.0, 10.0};
    auto* histogram = METRICS_HISTOGRAM("latency_seconds", buckets);
    
    histogram->observe(0.5);
    histogram->observe(3.0);
    histogram->observe(7.0);
    
    std::string output = histogram->get_prometheus_output("latency_seconds");
    
    // Check that output contains expected Prometheus format
    EXPECT_NE(output.find("latency_seconds_bucket{le=\"1\"} 1"), std::string::npos);
    EXPECT_NE(output.find("latency_seconds_bucket{le=\"5\"} 2"), std::string::npos);
    EXPECT_NE(output.find("latency_seconds_bucket{le=\"10\"} 3"), std::string::npos);
    EXPECT_NE(output.find("latency_seconds_bucket{le=\"+Inf\"} 3"), std::string::npos);
    EXPECT_NE(output.find("latency_seconds_count 3"), std::string::npos);
    EXPECT_NE(output.find("latency_seconds_sum 10.500000"), std::string::npos);
}

TEST_F(MetricsTest, MetricsRegistryOutput) {
    auto* counter1 = METRICS_COUNTER("orders_total");
    auto* counter2 = METRICS_COUNTER("trades_total");
    auto* histogram = METRICS_LATENCY_HISTOGRAM("order_latency_seconds");
    
    counter1->increment(100);
    counter2->increment(50);
    histogram->observe(0.000005);  // 5μs
    histogram->observe(0.000010);  // 10μs
    
    std::string output = MetricsRegistry::instance().get_prometheus_output();
    
    // Check that output contains all metrics
    EXPECT_NE(output.find("orders_total 100"), std::string::npos);
    EXPECT_NE(output.find("trades_total 50"), std::string::npos);
    EXPECT_NE(output.find("order_latency_seconds_count 2"), std::string::npos);
}

TEST_F(MetricsTest, DefaultLatencyBuckets) {
    auto* histogram = METRICS_LATENCY_HISTOGRAM("default_latency");
    
    // Test various latency values
    histogram->observe(0.000001);  // 1μs
    histogram->observe(0.000010);  // 10μs
    histogram->observe(0.000100);  // 100μs
    histogram->observe(0.001000);  // 1ms
    
    auto stats = histogram->get_stats();
    EXPECT_EQ(stats.count, 4);
    
    // Verify buckets capture the range we care about
    EXPECT_GT(stats.bucket_counts.size(), 10);  // Should have many buckets for latency
}

} // namespace rtes