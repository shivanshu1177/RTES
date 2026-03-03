#pragma once

#include "rtes/types.hpp"
#include <string_view>
#include <chrono>
#include <atomic>
#include <array>
#include <memory>
#include <immintrin.h>

namespace rtes {

// High-performance string parsing with string_view
class FastStringParser {
public:
    static constexpr bool parse_symbol(std::string_view input, char* output, size_t max_len) noexcept;
    static constexpr uint64_t parse_uint64(std::string_view input) noexcept;
    static constexpr double parse_price(std::string_view input) noexcept;
    static constexpr bool is_valid_symbol_fast(std::string_view symbol) noexcept;
    
private:
    static constexpr bool is_alpha_upper(char c) noexcept { return c >= 'A' && c <= 'Z'; }
    static constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }
};

// Lock-free ring buffer for message queues
template<typename T, size_t SIZE>
class RingBuffer {
    static_assert((SIZE & (SIZE - 1)) == 0, "SIZE must be power of 2");
    
public:
    RingBuffer() : head_(0), tail_(0) {}
    
    bool push(const T& item) noexcept {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) & (SIZE - 1);
        
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false; // Full
        }
        
        buffer_[current_tail] = item;
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }
    
    bool pop(T& item) noexcept {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        
        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false; // Empty
        }
        
        item = buffer_[current_head];
        head_.store((current_head + 1) & (SIZE - 1), std::memory_order_release);
        return true;
    }
    
    size_t size() const noexcept {
        return (tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_acquire)) & (SIZE - 1);
    }
    
    bool empty() const noexcept { return head_.load() == tail_.load(); }
    bool full() const noexcept { return ((tail_.load() + 1) & (SIZE - 1)) == head_.load(); }
    
private:
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
    std::array<T, SIZE> buffer_;
};

// Compact allocator for small objects with cache-friendly allocation
template<typename T, size_t BLOCK_SIZE = 4096>
class CompactAllocator {
public:
    CompactAllocator() : current_block_(nullptr), current_offset_(0) {
        allocate_new_block();
    }
    
    ~CompactAllocator() {
        for (auto* block : blocks_) {
            std::aligned_alloc_free(block);
        }
    }
    
    T* allocate() noexcept {
        if (current_offset_ + sizeof(T) > BLOCK_SIZE) {
            allocate_new_block();
        }
        
        T* ptr = reinterpret_cast<T*>(static_cast<char*>(current_block_) + current_offset_);
        current_offset_ += sizeof(T);
        return ptr;
    }
    
    void reset() noexcept {
        current_offset_ = 0;
        if (!blocks_.empty()) {
            current_block_ = blocks_[0];
        }
    }
    
private:
    std::vector<void*> blocks_;
    void* current_block_;
    size_t current_offset_;
    
    void allocate_new_block() {
        void* block = std::aligned_alloc(64, BLOCK_SIZE);
        if (block) {
            blocks_.push_back(block);
            current_block_ = block;
            current_offset_ = 0;
        }
    }
};

// Cache-friendly struct-of-arrays for order book levels
struct OrderBookLevelsSOA {
    static constexpr size_t MAX_LEVELS = 1024;
    
    alignas(64) std::array<Price, MAX_LEVELS> prices;
    alignas(64) std::array<Quantity, MAX_LEVELS> quantities;
    alignas(64) std::array<uint32_t, MAX_LEVELS> order_counts;
    alignas(64) std::array<Order*, MAX_LEVELS> first_orders;
    
    size_t size = 0;
    
    void insert_level(Price price, Quantity qty, uint32_t count, Order* first_order) noexcept {
        if (size < MAX_LEVELS) {
            prices[size] = price;
            quantities[size] = qty;
            order_counts[size] = count;
            first_orders[size] = first_order;
            ++size;
        }
    }
    
    void prefetch_level(size_t index) const noexcept {
        if (index < size) {
            _mm_prefetch(&prices[index], _MM_HINT_T0);
            _mm_prefetch(&quantities[index], _MM_HINT_T0);
        }
    }
};

// Real-time latency tracker with minimal overhead
class LatencyTracker {
public:
    LatencyTracker() : count_(0), total_ns_(0), min_ns_(UINT64_MAX), max_ns_(0) {}
    
    void record_latency(uint64_t latency_ns) noexcept {
        count_.fetch_add(1, std::memory_order_relaxed);
        total_ns_.fetch_add(latency_ns, std::memory_order_relaxed);
        
        // Update min/max with compare-and-swap
        uint64_t current_min = min_ns_.load(std::memory_order_relaxed);
        while (latency_ns < current_min && 
               !min_ns_.compare_exchange_weak(current_min, latency_ns, std::memory_order_relaxed)) {}
        
        uint64_t current_max = max_ns_.load(std::memory_order_relaxed);
        while (latency_ns > current_max && 
               !max_ns_.compare_exchange_weak(current_max, latency_ns, std::memory_order_relaxed)) {}
    }
    
    struct Stats {
        uint64_t count;
        uint64_t avg_ns;
        uint64_t min_ns;
        uint64_t max_ns;
    };
    
    Stats get_stats() const noexcept {
        uint64_t count = count_.load(std::memory_order_relaxed);
        uint64_t total = total_ns_.load(std::memory_order_relaxed);
        return {
            count,
            count > 0 ? total / count : 0,
            min_ns_.load(std::memory_order_relaxed),
            max_ns_.load(std::memory_order_relaxed)
        };
    }
    
    void reset() noexcept {
        count_.store(0, std::memory_order_relaxed);
        total_ns_.store(0, std::memory_order_relaxed);
        min_ns_.store(UINT64_MAX, std::memory_order_relaxed);
        max_ns_.store(0, std::memory_order_relaxed);
    }
    
private:
    alignas(64) std::atomic<uint64_t> count_;
    alignas(64) std::atomic<uint64_t> total_ns_;
    alignas(64) std::atomic<uint64_t> min_ns_;
    alignas(64) std::atomic<uint64_t> max_ns_;
};

// Memory usage monitor
class MemoryMonitor {
public:
    struct MemoryStats {
        size_t total_allocated;
        size_t peak_allocated;
        size_t current_allocated;
        size_t allocation_count;
    };
    
    void record_allocation(size_t size) noexcept {
        total_allocated_.fetch_add(size, std::memory_order_relaxed);
        current_allocated_.fetch_add(size, std::memory_order_relaxed);
        allocation_count_.fetch_add(1, std::memory_order_relaxed);
        
        // Update peak
        size_t current = current_allocated_.load(std::memory_order_relaxed);
        size_t peak = peak_allocated_.load(std::memory_order_relaxed);
        while (current > peak && 
               !peak_allocated_.compare_exchange_weak(peak, current, std::memory_order_relaxed)) {
            current = current_allocated_.load(std::memory_order_relaxed);
        }
    }
    
    void record_deallocation(size_t size) noexcept {
        current_allocated_.fetch_sub(size, std::memory_order_relaxed);
    }
    
    MemoryStats get_stats() const noexcept {
        return {
            total_allocated_.load(std::memory_order_relaxed),
            peak_allocated_.load(std::memory_order_relaxed),
            current_allocated_.load(std::memory_order_relaxed),
            allocation_count_.load(std::memory_order_relaxed)
        };
    }
    
private:
    alignas(64) std::atomic<size_t> total_allocated_{0};
    alignas(64) std::atomic<size_t> peak_allocated_{0};
    alignas(64) std::atomic<size_t> current_allocated_{0};
    alignas(64) std::atomic<size_t> allocation_count_{0};
};

// Throughput metrics tracker
class ThroughputTracker {
public:
    ThroughputTracker() : last_reset_(std::chrono::steady_clock::now()) {}
    
    void record_event() noexcept {
        event_count_.fetch_add(1, std::memory_order_relaxed);
    }
    
    double get_throughput_per_second() noexcept {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - last_reset_).count();
        
        if (elapsed > 1000000) { // 1 second
            uint64_t events = event_count_.exchange(0, std::memory_order_relaxed);
            last_reset_ = now;
            return static_cast<double>(events) * 1000000.0 / elapsed;
        }
        
        return 0.0;
    }
    
private:
    alignas(64) std::atomic<uint64_t> event_count_{0};
    std::chrono::steady_clock::time_point last_reset_;
};

// High-performance timer for latency measurement
class HighResTimer {
public:
    static uint64_t now_ns() noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    
    static uint64_t rdtsc() noexcept {
#ifdef __x86_64__
        return __rdtsc();
#else
        return now_ns();
#endif
    }
};

// RAII latency measurement
class ScopedLatencyMeasurement {
public:
    explicit ScopedLatencyMeasurement(LatencyTracker& tracker) 
        : tracker_(tracker), start_time_(HighResTimer::now_ns()) {}
    
    ~ScopedLatencyMeasurement() {
        uint64_t end_time = HighResTimer::now_ns();
        tracker_.record_latency(end_time - start_time_);
    }
    
private:
    LatencyTracker& tracker_;
    uint64_t start_time_;
};

#define MEASURE_LATENCY(tracker) ScopedLatencyMeasurement _measure(tracker)

// Performance optimizer orchestrator
class PerformanceOptimizer {
public:
    PerformanceOptimizer();
    
    // String parsing optimizations
    bool parse_symbol_fast(std::string_view input, char* output) const noexcept;
    uint64_t parse_order_id_fast(std::string_view input) const noexcept;
    Price parse_price_fast(std::string_view input) const noexcept;
    
    // Memory pool management
    Order* allocate_order() noexcept;
    void deallocate_order(Order* order) noexcept;
    
    // Performance monitoring
    LatencyTracker& get_latency_tracker(const std::string& operation);
    MemoryMonitor& get_memory_monitor() { return memory_monitor_; }
    ThroughputTracker& get_throughput_tracker(const std::string& operation);
    
    // Performance statistics
    void print_performance_stats() const;
    void reset_all_stats();
    
private:
    std::unique_ptr<CompactAllocator<Order>> order_allocator_;
    MemoryMonitor memory_monitor_;
    std::unordered_map<std::string, std::unique_ptr<LatencyTracker>> latency_trackers_;
    std::unordered_map<std::string, std::unique_ptr<ThroughputTracker>> throughput_trackers_;
};

} // namespace rtes