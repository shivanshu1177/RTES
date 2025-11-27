#pragma once

#include "rtes/types.hpp"
#include "rtes/memory_pool.hpp"
#include "rtes/error_handling.hpp"
#include "rtes/thread_safety.hpp"
#include "rtes/performance_optimizer.hpp"
#include <map>
#include <deque>
#include <unordered_map>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>

namespace rtes {

struct PriceLevel {
    Price price;
    std::deque<Order*> orders;  // FIFO queue for price-time priority
    Quantity total_quantity{0};
    
    explicit PriceLevel(Price p) : price(p) {}
};

class OrderBook {
public:
    using TradeCallback = std::function<void(const Trade&)>;
    
    explicit OrderBook(const std::string& symbol, OrderPool& pool, TradeCallback cb = nullptr);
    
    // O(1) order operations
    bool add_order(Order* order);
    bool cancel_order(OrderID order_id);
    
    // Safe operations with error handling
    Result<void> add_order_safe(Order* order);
    Result<void> cancel_order_safe(OrderID order_id);
    
    // Market data accessors
    Price best_bid() const { return bids_.empty() ? 0 : bids_.rbegin()->first; }
    Price best_ask() const { return asks_.empty() ? 0 : asks_.begin()->first; }
    Quantity bid_quantity() const;
    Quantity ask_quantity() const;
    
    // Depth snapshot (top N levels)
    struct DepthLevel {
        Price price;
        Quantity quantity;
        uint32_t order_count;
    };
    
    std::vector<DepthLevel> get_depth(size_t levels = 10) const;
    
    size_t order_count() const { return order_lookup_.size(); }

private:
    std::string symbol_;
    OrderPool& pool_;
    TradeCallback trade_callback_;
    TradeID next_trade_id_{1};
    
    // Performance optimization components
    std::unique_ptr<class PerformanceOptimizer> performance_optimizer_;
    class LatencyTracker* add_order_latency_;
    class LatencyTracker* match_order_latency_;
    class LatencyTracker* execute_trade_latency_;
    class ThroughputTracker* add_order_throughput_;
    class ThroughputTracker* match_throughput_;
    
    // Price levels: bids descending, asks ascending
    std::map<Price, PriceLevel, std::greater<Price>> bids_;  // Descending
    std::map<Price, PriceLevel> asks_;                       // Ascending
    
    // O(1) order lookup for cancellation
    std::unordered_map<OrderID, Order*> order_lookup_ GUARDED_BY(order_mutex_);
    
    // Thread safety
    mutable std::mutex order_mutex_;
    atomic_wrapper<bool> shutdown_requested_;
    
    // Matching logic
    void match_order(Order* order);
    void match_market_order(Order* order);
    void match_limit_order(Order* order);
    
    // Safe matching logic
    Result<void> match_order_safe(Order* order);
    Result<void> match_market_order_safe(Order* order);
    Result<void> match_limit_order_safe(Order* order);
    
    // Optimized matching logic
    Result<void> match_market_order_safe_optimized(Order* order);
    Result<void> match_limit_order_safe_optimized(Order* order);
    
    // Helper methods
    void execute_trade(Order* aggressive_order, Order* passive_order, Quantity quantity, Price price);
    void execute_trade_optimized(Order* aggressive_order, Order* passive_order, Quantity quantity, Price price);
    void remove_order_from_book(Order* order) REQUIRES(order_mutex_);
    bool add_to_book(Order* order);
    Result<void> add_to_book_safe(Order* order) REQUIRES(order_mutex_);
    
    // Shutdown coordination
    void shutdown();
};

} // namespace rtes