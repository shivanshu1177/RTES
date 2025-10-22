#include "rtes/metrics.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace rtes {

Histogram::Histogram(const std::vector<double>& buckets) : buckets_(buckets) {
    std::sort(buckets_.begin(), buckets_.end());
    bucket_counts_.resize(buckets_.size() + 1, 0);  // +1 for +Inf bucket
}

void Histogram::observe(double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    count_++;
    sum_ += value;
    
    // Find appropriate bucket
    for (size_t i = 0; i < buckets_.size(); ++i) {
        if (value <= buckets_[i]) {
            bucket_counts_[i]++;
            return;
        }
    }
    
    // Value exceeds all buckets, goes to +Inf bucket
    bucket_counts_.back()++;
}

std::string Histogram::get_prometheus_output(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream ss;
    
    // Bucket counts
    uint64_t cumulative = 0;
    for (size_t i = 0; i < buckets_.size(); ++i) {
        cumulative += bucket_counts_[i];
        ss << name << "_bucket{le=\"" << buckets_[i] << "\"} " << cumulative << "\n";
    }
    
    // +Inf bucket
    cumulative += bucket_counts_.back();
    ss << name << "_bucket{le=\"+Inf\"} " << cumulative << "\n";
    
    // Count and sum
    ss << name << "_count " << count_ << "\n";
    ss << name << "_sum " << std::fixed << std::setprecision(6) << sum_ << "\n";
    
    return ss.str();
}

Histogram::Stats Histogram::get_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {count_, sum_, bucket_counts_};
}

MetricsRegistry& MetricsRegistry::instance() {
    static MetricsRegistry registry;
    return registry;
}

Counter* MetricsRegistry::get_counter(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = counters_.find(name);
    if (it != counters_.end()) {
        return it->second.get();
    }
    
    auto counter = std::make_unique<Counter>();
    Counter* ptr = counter.get();
    counters_[name] = std::move(counter);
    
    return ptr;
}

Histogram* MetricsRegistry::get_histogram(const std::string& name, const std::vector<double>& buckets) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = histograms_.find(name);
    if (it != histograms_.end()) {
        return it->second.get();
    }
    
    auto bucket_config = buckets.empty() ? default_latency_buckets() : buckets;
    auto histogram = std::make_unique<Histogram>(bucket_config);
    Histogram* ptr = histogram.get();
    histograms_[name] = std::move(histogram);
    
    return ptr;
}

std::string MetricsRegistry::get_prometheus_output() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream ss;
    
    // Add header
    ss << "# HELP rtes_metrics RTES Exchange Metrics\n";
    ss << "# TYPE rtes_orders_total counter\n";
    ss << "# TYPE rtes_trades_total counter\n";
    ss << "# TYPE rtes_latency_seconds histogram\n\n";
    
    // Output counters
    for (const auto& [name, counter] : counters_) {
        ss << name << " " << counter->get() << "\n";
    }
    
    // Output histograms
    for (const auto& [name, histogram] : histograms_) {
        ss << histogram->get_prometheus_output(name);
    }
    
    return ss.str();
}

std::vector<double> MetricsRegistry::default_latency_buckets() {
    return {
        0.000001,   // 1μs
        0.000005,   // 5μs
        0.00001,    // 10μs
        0.00005,    // 50μs
        0.0001,     // 100μs
        0.0005,     // 500μs
        0.001,      // 1ms
        0.005,      // 5ms
        0.01,       // 10ms
        0.05,       // 50ms
        0.1         // 100ms
    };
}

} // namespace rtes