#include <gtest/gtest.h>
#include "rtes/observability.hpp"
#include <thread>
#include <chrono>

using namespace rtes;

class ObservabilityTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize observability stack
        ObservabilityStack::instance().initialize();
    }
    
    void TearDown() override {
        // Clean up
    }
};

TEST_F(ObservabilityTest, StructuredLogging) {
    auto& logger = StructuredLogger::instance();
    
    // Test basic logging
    logger.log(LogLevel::INFO, "TestComponent", "Test message");
    
    // Test logging with fields
    logger.log(LogLevel::ERROR, "TestComponent", "Error occurred", {
        {"error_code", "E001"},
        {"user_id", "test_user"}
    });
    
    // Test trace ID
    logger.set_trace_id("trace123");
    EXPECT_EQ(logger.get_trace_id(), "trace123");
    
    // Test global fields
    logger.add_global_field("service", "rtes_test");
    logger.log(LogLevel::DEBUG, "TestComponent", "Debug message");
}

TEST_F(ObservabilityTest, MetricsCollection) {
    auto& metrics = MetricsCollector::instance();
    
    // Test counter
    metrics.increment_counter("test_counter");
    metrics.increment_counter("test_counter", {{"label", "value"}});
    
    // Test gauge
    metrics.set_gauge("test_gauge", 42.5);
    metrics.set_gauge("test_gauge", 100.0, {{"instance", "test"}});
    
    // Test histogram
    metrics.record_histogram("test_histogram", 1.5);
    metrics.record_histogram("test_histogram", 2.5);
    metrics.record_histogram("test_histogram", 3.5);
    
    // Export Prometheus format
    std::string prometheus_output = metrics.export_prometheus();
    EXPECT_FALSE(prometheus_output.empty());
    EXPECT_NE(prometheus_output.find("test_counter"), std::string::npos);
    EXPECT_NE(prometheus_output.find("test_gauge"), std::string::npos);
    EXPECT_NE(prometheus_output.find("test_histogram"), std::string::npos);
    
    // Get all metrics
    auto all_metrics = metrics.get_all_metrics();
    EXPECT_FALSE(all_metrics.empty());
}

TEST_F(ObservabilityTest, DistributedTracing) {
    auto& tracer = DistributedTracer::instance();
    
    // Start root span
    auto root_span = tracer.start_span("root_operation");
    EXPECT_FALSE(root_span->trace_id.empty());
    EXPECT_FALSE(root_span->span_id.empty());
    EXPECT_TRUE(root_span->parent_span_id.empty());
    
    // Add tags
    tracer.add_tag(root_span, "component", "test");
    tracer.add_tag(root_span, "version", "1.0");
    
    // Start child span
    auto child_span = tracer.start_span("child_operation", root_span->span_id);
    EXPECT_EQ(child_span->trace_id, root_span->trace_id);
    EXPECT_EQ(child_span->parent_span_id, root_span->span_id);
    
    // Finish spans
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    tracer.finish_span(child_span);
    tracer.finish_span(root_span);
    
    EXPECT_TRUE(child_span->finished);
    EXPECT_TRUE(root_span->finished);
    
    // Get trace
    auto trace = tracer.get_trace(root_span->trace_id);
    EXPECT_EQ(trace.size(), 2);
}

TEST_F(ObservabilityTest, AnomalyDetection) {
    auto& detector = AnomalyDetector::instance();
    
    // Configure metric
    AnomalyThreshold threshold{100.0, 10.0, 2.0, 50};
    detector.configure_metric("test_metric", threshold);
    
    // Update baseline with normal values
    for (int i = 0; i < 20; ++i) {
        detector.update_baseline("test_metric", 100.0 + (i % 5));
    }
    
    // Test normal value
    EXPECT_FALSE(detector.is_anomaly("test_metric", 105.0));
    
    // Test anomalous value
    EXPECT_TRUE(detector.is_anomaly("test_metric", 150.0));
}

TEST_F(ObservabilityTest, AlertManager) {
    auto& alert_manager = AlertManager::instance();
    auto& metrics = MetricsCollector::instance();
    
    // Register threshold alert
    alert_manager.register_threshold_alert("test_metric", 100.0, 
                                          AlertSeverity::WARNING, 
                                          "Test metric exceeded threshold");
    
    // Set metric below threshold
    metrics.set_gauge("test_metric", 50.0);
    alert_manager.check_alerts();
    
    auto alerts = alert_manager.get_active_alerts();
    EXPECT_TRUE(alerts.empty());
    
    // Set metric above threshold
    metrics.set_gauge("test_metric", 150.0);
    alert_manager.check_alerts();
    
    alerts = alert_manager.get_active_alerts();
    EXPECT_FALSE(alerts.empty());
    
    // Resolve alert
    if (!alerts.empty()) {
        alert_manager.resolve_alert(alerts[0].name);
        
        alerts = alert_manager.get_active_alerts();
        EXPECT_TRUE(alerts.empty() || alerts[0].resolved);
    }
}

TEST_F(ObservabilityTest, PerformanceCounters) {
    auto& perf = PerformanceCounters::instance();
    
    // Record latencies
    perf.record_latency("test_operation", std::chrono::nanoseconds(1000));
    perf.record_latency("test_operation", std::chrono::nanoseconds(2000));
    perf.record_latency("test_operation", std::chrono::nanoseconds(3000));
    
    // Record throughput
    perf.record_throughput("test_operation", 10);
    perf.record_throughput("test_operation", 5);
    
    // Record errors
    perf.record_error("test_operation", "timeout");
    perf.record_error("test_operation", "validation");
    
    // Get metrics
    double avg_latency = perf.get_avg_latency("test_operation");
    EXPECT_GT(avg_latency, 0.0);
    EXPECT_LT(avg_latency, 10.0); // Should be around 2.0 microseconds
    
    uint64_t throughput = perf.get_throughput("test_operation");
    EXPECT_EQ(throughput, 15);
    
    double error_rate = perf.get_error_rate("test_operation");
    EXPECT_GT(error_rate, 0.0);
    EXPECT_LT(error_rate, 1.0);
}

TEST_F(ObservabilityTest, BusinessMetrics) {
    auto& business = BusinessMetrics::instance();
    
    // Record business activities
    business.record_order_placed("AAPL", 10000.0);
    business.record_order_placed("GOOGL", 5000.0);
    
    business.record_trade_executed("AAPL", 100, 150.0);
    business.record_trade_executed("AAPL", 50, 151.0);
    
    business.record_client_activity("client1", "login");
    business.record_client_activity("client1", "place_order");
    
    // Get metrics
    double aapl_volume = business.get_total_volume("AAPL");
    EXPECT_GT(aapl_volume, 0.0);
    
    uint64_t aapl_trades = business.get_trade_count("AAPL");
    EXPECT_EQ(aapl_trades, 2);
}

TEST_F(ObservabilityTest, SecurityAuditTrail) {
    auto& audit = SecurityAuditTrail::instance();
    
    // Log security events
    audit.log_security_event("LOGIN", "user1", "system", "authenticate", true, "Successful login", "192.168.1.100");
    audit.log_security_event("LOGIN", "user2", "system", "authenticate", false, "Invalid password", "192.168.1.101");
    audit.log_security_event("ORDER", "user1", "orders", "place", true, "Order placed", "192.168.1.100");
    
    // Get events
    auto now = std::chrono::system_clock::now();
    auto one_hour_ago = now - std::chrono::hours(1);
    
    auto events = audit.get_events(one_hour_ago, now);
    EXPECT_EQ(events.size(), 3);
    
    // Check event details
    bool found_login_success = false;
    bool found_login_failure = false;
    
    for (const auto& event : events) {
        if (event.event_type == "LOGIN" && event.success) {
            found_login_success = true;
            EXPECT_EQ(event.user_id, "user1");
            EXPECT_EQ(event.source_ip, "192.168.1.100");
        } else if (event.event_type == "LOGIN" && !event.success) {
            found_login_failure = true;
            EXPECT_EQ(event.user_id, "user2");
        }
    }
    
    EXPECT_TRUE(found_login_success);
    EXPECT_TRUE(found_login_failure);
}

TEST_F(ObservabilityTest, ObservabilityStackIntegration) {
    auto& stack = ObservabilityStack::instance();
    
    // Start monitoring
    stack.start_monitoring();
    
    // Set trace context
    stack.set_trace_context("integration_test_trace");
    
    // Start operation
    auto span = stack.start_operation("test_integration");
    EXPECT_FALSE(span->trace_id.empty());
    EXPECT_EQ(span->operation_name, "test_integration");
    
    // Simulate some work
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Finish operation
    DistributedTracer::instance().finish_span(span);
    
    // Stop monitoring
    stack.stop_monitoring();
}

TEST_F(ObservabilityTest, MacroConvenience) {
    // Test logging macro
    LOG_STRUCTURED(LogLevel::INFO, "TestMacro", "Test macro logging",
                  {"key1", "value1"}, {"key2", "value2"});
    
    // Test tracing macro
    {
        TRACE_OPERATION("macro_test_operation");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    
    // Test metrics macro
    RECORD_METRIC("test_macro_metric", 42.0);
    
    // Test security audit macro
    SECURITY_AUDIT("TEST_EVENT", "test_user", "test_resource", "test_action", true);
}

TEST_F(ObservabilityTest, ConcurrentAccess) {
    const int num_threads = 10;
    const int operations_per_thread = 100;
    
    std::vector<std::thread> threads;
    
    // Test concurrent logging
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([i, operations_per_thread]() {
            auto& logger = StructuredLogger::instance();
            auto& metrics = MetricsCollector::instance();
            
            for (int j = 0; j < operations_per_thread; ++j) {
                logger.log(LogLevel::INFO, "ConcurrentTest", "Thread " + std::to_string(i),
                          {"thread_id", std::to_string(i)}, {"operation", std::to_string(j)});
                
                metrics.increment_counter("concurrent_test");
                metrics.set_gauge("thread_gauge", i * 10 + j);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Verify metrics were recorded
    auto& metrics = MetricsCollector::instance();
    auto all_metrics = metrics.get_all_metrics();
    
    auto counter_it = all_metrics.find("concurrent_test_counter");
    EXPECT_NE(counter_it, all_metrics.end());
    
    // Should have num_threads * operations_per_thread counter increments
    double total_count = 0;
    for (const auto& value : counter_it->second) {
        total_count += value.value;
    }
    EXPECT_EQ(total_count, num_threads * operations_per_thread);
}

// Performance test
TEST_F(ObservabilityTest, LoggingPerformance) {
    auto& logger = StructuredLogger::instance();
    
    const int iterations = 10000;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        logger.log(LogLevel::INFO, "PerfTest", "Performance test message",
                  {"iteration", std::to_string(i)});
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Should be fast (less than 100μs per log on average)
    double avg_time_us = static_cast<double>(duration.count()) / iterations;
    EXPECT_LT(avg_time_us, 100.0);
    
    std::cout << "Average logging time: " << avg_time_us << " μs per log" << std::endl;
}