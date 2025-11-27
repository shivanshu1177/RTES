#include "rtes/performance_optimizer.hpp"
#include "rtes/logger.hpp"
#include <cstring>
#include <iostream>

namespace rtes {

// FastStringParser implementation
constexpr bool FastStringParser::parse_symbol(std::string_view input, char* output, size_t max_len) noexcept {
    if (input.empty() || input.size() > max_len - 1) return false;
    
    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];
        if (!is_alpha_upper(c) && !is_digit(c)) return false;
        output[i] = c;
    }
    output[input.size()] = '\0';
    return true;
}

constexpr uint64_t FastStringParser::parse_uint64(std::string_view input) noexcept {
    uint64_t result = 0;
    for (char c : input) {
        if (!is_digit(c)) return 0;
        result = result * 10 + (c - '0');
    }
    return result;
}

constexpr double FastStringParser::parse_price(std::string_view input) noexcept {
    double result = 0.0;
    double fraction = 0.0;
    bool after_decimal = false;
    double decimal_place = 0.1;
    
    for (char c : input) {
        if (c == '.') {
            after_decimal = true;
        } else if (is_digit(c)) {
            if (after_decimal) {
                fraction += (c - '0') * decimal_place;
                decimal_place *= 0.1;
            } else {
                result = result * 10 + (c - '0');
            }
        } else {
            return 0.0;
        }
    }
    
    return result + fraction;
}

constexpr bool FastStringParser::is_valid_symbol_fast(std::string_view symbol) noexcept {
    if (symbol.empty() || symbol.size() > 8) return false;
    
    for (char c : symbol) {
        if (!is_alpha_upper(c) && !is_digit(c)) return false;
    }
    return true;
}

// PerformanceOptimizer implementation
PerformanceOptimizer::PerformanceOptimizer() {
    order_allocator_ = std::make_unique<CompactAllocator<Order>>();
}

bool PerformanceOptimizer::parse_symbol_fast(std::string_view input, char* output) const noexcept {
    return FastStringParser::parse_symbol(input, output, 9);
}

uint64_t PerformanceOptimizer::parse_order_id_fast(std::string_view input) const noexcept {
    return FastStringParser::parse_uint64(input);
}

Price PerformanceOptimizer::parse_price_fast(std::string_view input) const noexcept {
    double price = FastStringParser::parse_price(input);
    return static_cast<Price>(price * 10000); // Convert to fixed point
}

Order* PerformanceOptimizer::allocate_order() noexcept {
    Order* order = order_allocator_->allocate();
    if (order) {
        memory_monitor_.record_allocation(sizeof(Order));
    }
    return order;
}

void PerformanceOptimizer::deallocate_order(Order* order) noexcept {
    if (order) {
        memory_monitor_.record_deallocation(sizeof(Order));
        // Note: CompactAllocator doesn't support individual deallocation
        // Orders are freed when allocator is reset
    }
}

LatencyTracker& PerformanceOptimizer::get_latency_tracker(const std::string& operation) {
    auto it = latency_trackers_.find(operation);
    if (it == latency_trackers_.end()) {
        auto tracker = std::make_unique<LatencyTracker>();
        auto& ref = *tracker;
        latency_trackers_[operation] = std::move(tracker);
        return ref;
    }
    return *it->second;
}

ThroughputTracker& PerformanceOptimizer::get_throughput_tracker(const std::string& operation) {
    auto it = throughput_trackers_.find(operation);
    if (it == throughput_trackers_.end()) {
        auto tracker = std::make_unique<ThroughputTracker>();
        auto& ref = *tracker;
        throughput_trackers_[operation] = std::move(tracker);
        return ref;
    }
    return *it->second;
}

void PerformanceOptimizer::print_performance_stats() const {
    std::cout << "\n=== Performance Statistics ===\n";
    
    // Memory statistics
    auto mem_stats = memory_monitor_.get_stats();
    std::cout << "Memory:\n";
    std::cout << "  Total Allocated: " << mem_stats.total_allocated << " bytes\n";
    std::cout << "  Peak Allocated: " << mem_stats.peak_allocated << " bytes\n";
    std::cout << "  Current Allocated: " << mem_stats.current_allocated << " bytes\n";
    std::cout << "  Allocation Count: " << mem_stats.allocation_count << "\n\n";
    
    // Latency statistics
    std::cout << "Latency (nanoseconds):\n";
    for (const auto& [operation, tracker] : latency_trackers_) {
        auto stats = tracker->get_stats();
        if (stats.count > 0) {
            std::cout << "  " << operation << ":\n";
            std::cout << "    Count: " << stats.count << "\n";
            std::cout << "    Average: " << stats.avg_ns << " ns\n";
            std::cout << "    Min: " << stats.min_ns << " ns\n";
            std::cout << "    Max: " << stats.max_ns << " ns\n";
        }
    }
    
    // Throughput statistics
    std::cout << "\nThroughput (events/second):\n";
    for (const auto& [operation, tracker] : throughput_trackers_) {
        double throughput = const_cast<ThroughputTracker*>(tracker.get())->get_throughput_per_second();
        if (throughput > 0) {
            std::cout << "  " << operation << ": " << throughput << " events/sec\n";
        }
    }
    
    std::cout << "==============================\n\n";
}

void PerformanceOptimizer::reset_all_stats() {
    // Reset memory monitor by creating new instance
    memory_monitor_.~MemoryMonitor();
    new (&memory_monitor_) MemoryMonitor();
    
    for (auto& [operation, tracker] : latency_trackers_) {
        tracker->reset();
    }
    
    // Throughput trackers reset automatically
    order_allocator_->reset();
}

} // namespace rtes