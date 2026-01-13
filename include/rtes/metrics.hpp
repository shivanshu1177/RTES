#pragma once

#include <atomic>
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace rtes {

class Counter {
public:
    void increment(uint64_t value = 1) { value_.fetch_add(value, std::memory_order_relaxed); }
    uint64_t get() const { return value_.load(std::memory_order_relaxed); }
    void reset() { value_.store(0, std::memory_order_relaxed); }

private:
    std::atomic<uint64_t> value_{0};
};

class Histogram {
public:
    explicit Histogram(const std::vector<double>& buckets);
    
    void observe(double value);
    std::string get_prometheus_output(const std::string& name) const;
    
    struct Stats {
        uint64_t count;
        double sum;
        std::vector<uint64_t> bucket_counts;
    };
    
    Stats get_stats() const;

private:
    std::vector<double> buckets_;
    mutable std::mutex mutex_;
    uint64_t count_{0};
    double sum_{0.0};
    std::vector<uint64_t> bucket_counts_;
};

class MetricsRegistry {
public:
    static MetricsRegistry& instance();
    
    Counter* get_counter(const std::string& name);
    Histogram* get_histogram(const std::string& name, const std::vector<double>& buckets = {});
    
    std::string get_prometheus_output() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<Counter>> counters_;
    std::unordered_map<std::string, std::unique_ptr<Histogram>> histograms_;
    
    static std::vector<double> default_latency_buckets();
};

// Convenience macros
#define METRICS_COUNTER(name) rtes::MetricsRegistry::instance().get_counter(name)
#define METRICS_HISTOGRAM(name, buckets) rtes::MetricsRegistry::instance().get_histogram(name, buckets)
#define METRICS_LATENCY_HISTOGRAM(name) rtes::MetricsRegistry::instance().get_histogram(name)

} // namespace rtes