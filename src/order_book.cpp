#include "rtes/order_book.hpp"
#include "rtes/logger.hpp"
#include "rtes/error_handling.hpp"
#include "rtes/transaction.hpp"
#include "rtes/performance_optimizer.hpp"
#include "rtes/thread_safety.hpp"
#include <algorithm>

namespace rtes {

OrderBook::OrderBook(const std::string& symbol, OrderPool& pool, TradeCallback cb)
    : symbol_(symbol), pool_(pool), trade_callback_(cb), shutdown_requested_(false) {
    
    // Register with shutdown manager
    ShutdownManager::instance().register_component(
        "OrderBook_" + symbol_, 
        [this]() { shutdown(); }
    );
    performance_optimizer_ = std::make_unique<PerformanceOptimizer>();
    
    // Initialize performance trackers
    add_order_latency_ = &performance_optimizer_->get_latency_tracker("add_order");
    match_order_latency_ = &performance_optimizer_->get_latency_tracker("match_order");
    execute_trade_latency_ = &performance_optimizer_->get_latency_tracker("execute_trade");
    
    add_order_throughput_ = &performance_optimizer_->get_throughput_tracker("add_order");
    match_throughput_ = &performance_optimizer_->get_throughput_tracker("matching");
}

Result<void> OrderBook::add_order_safe(Order* order) {
    if (shutdown_requested_.load()) {
        return ErrorCode::SYSTEM_SHUTDOWN;
    }
    
    MEASURE_LATENCY(*add_order_latency_);
    add_order_throughput_->record_event();
    
    if (!order || order->remaining_quantity == 0) {
        return ErrorCode::ORDER_INVALID;
    }
    
    scoped_lock lock(order_mutex_);
    
    if (order_lookup_.find(order->id) != order_lookup_.end()) {
        return ErrorCode::ORDER_DUPLICATE;
    }
    
    TransactionScope tx("AddOrder_" + std::to_string(order->id));
    
    try {
        order_lookup_[order->id] = order;
        auto match_result = match_order_safe(order);
        if (match_result.has_error()) return match_result.error();
        
        if (order->remaining_quantity > 0) {
            auto book_result = add_to_book_safe(order);
            if (book_result.has_error()) return book_result.error();
        }
        
        return tx.commit();
    } catch (const std::exception& e) {
        LOG_ERROR_SAFE("Exception in add_order: {}", e.what());
        return ErrorCode::SYSTEM_CORRUPTED_STATE;
    }
}

bool OrderBook::add_order(Order* order) {
    auto result = add_order_safe(order);
    if (result.has_error()) {
        LOG_ERROR_SAFE("Failed to add order {}: {}", order ? order->id : 0, result.error().message());
        return false;
    }
    return true;
}

Result<void> OrderBook::cancel_order_safe(OrderID order_id) {
    if (shutdown_requested_.load()) {
        return ErrorCode::SYSTEM_SHUTDOWN;
    }
    
    scoped_lock lock(order_mutex_);
    
    auto it = order_lookup_.find(order_id);
    if (it == order_lookup_.end()) {
        return ErrorCode::ORDER_NOT_FOUND;
    }
    
    TransactionScope tx("CancelOrder_" + std::to_string(order_id));
    
    try {
        Order* order = it->second;
        remove_order_from_book(order);
        order_lookup_.erase(it);
        order->status = OrderStatus::CANCELLED;
        pool_.deallocate(order);
        return tx.commit();
    } catch (const std::exception& e) {
        LOG_ERROR_SAFE("Exception in cancel_order: {}", e.what());
        return ErrorCode::SYSTEM_CORRUPTED_STATE;
    }
}

bool OrderBook::cancel_order(OrderID order_id) {
    auto result = cancel_order_safe(order_id);
    if (result.has_error()) {
        LOG_ERROR_SAFE("Failed to cancel order {}: {}", order_id, result.error().message());
        return false;
    }
    return true;
}

Result<void> OrderBook::match_order_safe(Order* order) {
    if (shutdown_requested_.load()) {
        return ErrorCode::SYSTEM_SHUTDOWN;
    }
    
    MEASURE_LATENCY(*match_order_latency_);
    match_throughput_->record_event();
    
    if (!order) return ErrorCode::ORDER_INVALID;
    
    // Register memory access for race detection
    RaceDetector::instance().register_memory_access(order, true);
    
    try {
        if (order->type == OrderType::MARKET) {
            return match_market_order_safe_optimized(order);
        } else {
            return match_limit_order_safe_optimized(order);
        }
    } catch (const std::exception& e) {
        LOG_ERROR_SAFE("Exception in match_order: {}", e.what());
        return ErrorCode::SYSTEM_CORRUPTED_STATE;
    }
}

void OrderBook::match_order(Order* order) {
    auto result = match_order_safe(order);
    if (result.has_error()) {
        LOG_ERROR_SAFE("Failed to match order {}: {}", order ? order->id : 0, result.error().message());
    }
}

Result<void> OrderBook::match_market_order_safe_optimized(Order* order) {
    try {
        auto& opposite_side = (order->side == Side::BUY) ? asks_ : bids_;
        
        // Prefetch first few levels for better cache performance
        auto it = opposite_side.begin();
        if (it != opposite_side.end()) {
            _mm_prefetch(&it->second, _MM_HINT_T0);
            auto next_it = std::next(it);
            if (next_it != opposite_side.end()) {
                _mm_prefetch(&next_it->second, _MM_HINT_T0);
            }
        }
        
        while (order->remaining_quantity > 0 && !opposite_side.empty()) {
            auto& [price, level] = *opposite_side.begin();
            
            if (level.orders.empty()) {
                opposite_side.erase(opposite_side.begin());
                continue;
            }
            
            Order* passive_order = level.orders.front();
            
            // Prefetch next order for cache efficiency
            if (level.orders.size() > 1) {
                _mm_prefetch(level.orders[1], _MM_HINT_T0);
            }
            
            Quantity trade_qty = std::min(order->remaining_quantity, passive_order->remaining_quantity);
            
            execute_trade_optimized(order, passive_order, trade_qty, price);
            
            if (passive_order->remaining_quantity == 0) {
                level.orders.pop_front();
                level.total_quantity -= passive_order->quantity;
                order_lookup_.erase(passive_order->id);
                passive_order->status = OrderStatus::FILLED;
                pool_.deallocate(passive_order);
            }
            
            if (level.orders.empty()) {
                opposite_side.erase(opposite_side.begin());
            }
        }
        return Result<void>();
    } catch (const std::exception& e) {
        LOG_ERROR_SAFE("Exception in match_market_order: {}", e.what());
        return ErrorCode::SYSTEM_CORRUPTED_STATE;
    }
}

void OrderBook::match_market_order(Order* order) {
    auto result = match_market_order_safe(order);
    if (result.has_error()) {
        LOG_ERROR_SAFE("Market order matching failed: {}", result.error().message());
    }
}

Result<void> OrderBook::match_limit_order_safe_optimized(Order* order) {
    try {
        auto& opposite_side = (order->side == Side::BUY) ? asks_ : bids_;
        
        // Prefetch for cache efficiency
        auto it = opposite_side.begin();
        if (it != opposite_side.end()) {
            _mm_prefetch(&it->second, _MM_HINT_T0);
        }
        
        while (order->remaining_quantity > 0 && !opposite_side.empty()) {
            auto& [price, level] = *opposite_side.begin();
            
            // Optimized price crossing check
            bool crosses = (order->side == Side::BUY) ? (order->price >= price) : (order->price <= price);
            if (!crosses) break;
            
            if (level.orders.empty()) {
                opposite_side.erase(opposite_side.begin());
                continue;
            }
            
            Order* passive_order = level.orders.front();
            
            // Prefetch next order
            if (level.orders.size() > 1) {
                _mm_prefetch(level.orders[1], _MM_HINT_T0);
            }
            
            Quantity trade_qty = std::min(order->remaining_quantity, passive_order->remaining_quantity);
            
            execute_trade_optimized(order, passive_order, trade_qty, price);
            
            if (passive_order->remaining_quantity == 0) {
                level.orders.pop_front();
                level.total_quantity -= passive_order->quantity;
                order_lookup_.erase(passive_order->id);
                passive_order->status = OrderStatus::FILLED;
                pool_.deallocate(passive_order);
            }
            
            if (level.orders.empty()) {
                opposite_side.erase(opposite_side.begin());
            }
        }
        return Result<void>();
    } catch (const std::exception& e) {
        LOG_ERROR_SAFE("Exception in match_limit_order: {}", e.what());
        return ErrorCode::SYSTEM_CORRUPTED_STATE;
    }
}

void OrderBook::match_limit_order(Order* order) {
    auto result = match_limit_order_safe(order);
    if (result.has_error()) {
        LOG_ERROR_SAFE("Limit order matching failed: {}", result.error().message());
    }
}

void OrderBook::execute_trade_optimized(Order* aggressive_order, Order* passive_order, Quantity quantity, Price price) {
    MEASURE_LATENCY(*execute_trade_latency_);
    
    // Prefetch order data for cache efficiency
    _mm_prefetch(aggressive_order, _MM_HINT_T0);
    _mm_prefetch(passive_order, _MM_HINT_T0);
    
    // Update order quantities
    aggressive_order->remaining_quantity -= quantity;
    passive_order->remaining_quantity -= quantity;
    
    // Optimized status updates
    aggressive_order->status = (aggressive_order->remaining_quantity == 0) ? 
        OrderStatus::FILLED : OrderStatus::PARTIALLY_FILLED;
    passive_order->status = (passive_order->remaining_quantity == 0) ? 
        OrderStatus::FILLED : OrderStatus::PARTIALLY_FILLED;
    
    // Create trade with optimized construction
    Trade trade(next_trade_id_++, 
                (aggressive_order->side == Side::BUY) ? aggressive_order->id : passive_order->id,
                (aggressive_order->side == Side::SELL) ? aggressive_order->id : passive_order->id,
                symbol_.c_str(), quantity, price);
    
    // Notify callback
    if (trade_callback_) {
        trade_callback_(trade);
    }
}

// Keep original method for backward compatibility
void OrderBook::execute_trade(Order* aggressive_order, Order* passive_order, Quantity quantity, Price price) {
    execute_trade_optimized(aggressive_order, passive_order, quantity, price);
}

Result<void> OrderBook::add_to_book_safe(Order* order) {
    if (!order) return ErrorCode::ORDER_INVALID;
    
    try {
        auto& side = (order->side == Side::BUY) ? bids_ : asks_;
        
        auto it = side.find(order->price);
        if (it == side.end()) {
            auto [inserted_it, success] = side.emplace(order->price, PriceLevel(order->price));
            if (!success) return ErrorCode::SYSTEM_CORRUPTED_STATE;
            it = inserted_it;
        }
        
        it->second.orders.push_back(order);
        it->second.total_quantity += order->remaining_quantity;
        order->status = OrderStatus::ACCEPTED;
        return Result<void>();
    } catch (const std::exception& e) {
        LOG_ERROR_SAFE("Exception in add_to_book: {}", e.what());
        return ErrorCode::SYSTEM_CORRUPTED_STATE;
    }
}

bool OrderBook::add_to_book(Order* order) {
    auto result = add_to_book_safe(order);
    return result.has_value();
}

void OrderBook::remove_order_from_book(Order* order) {
    auto& side = (order->side == Side::BUY) ? bids_ : asks_;
    
    auto level_it = side.find(order->price);
    if (level_it == side.end()) return;
    
    auto& level = level_it->second;
    auto order_it = std::find(level.orders.begin(), level.orders.end(), order);
    
    if (order_it != level.orders.end()) {
        level.orders.erase(order_it);
        level.total_quantity -= order->remaining_quantity;
        
        // Remove empty price level
        if (level.orders.empty()) {
            side.erase(level_it);
        }
    }
}

Quantity OrderBook::bid_quantity() const {
    return bids_.empty() ? 0 : bids_.rbegin()->second.total_quantity;
}

Quantity OrderBook::ask_quantity() const {
    return asks_.empty() ? 0 : asks_.begin()->second.total_quantity;
}

std::vector<OrderBook::DepthLevel> OrderBook::get_depth(size_t levels) const {
    std::vector<DepthLevel> depth;
    depth.reserve(levels * 2);
    
    // Add bid levels (descending price)
    size_t count = 0;
    for (const auto& [price, level] : bids_) {
        if (count >= levels) break;
        if (!level.orders.empty()) {
            depth.push_back({price, level.total_quantity, static_cast<uint32_t>(level.orders.size())});
            ++count;
        }
    }
    
    // Add ask levels (ascending price)
    count = 0;
    for (const auto& [price, level] : asks_) {
        if (count >= levels) break;
        if (!level.orders.empty()) {
            depth.push_back({price, level.total_quantity, static_cast<uint32_t>(level.orders.size())});
            ++count;
        }
    }
    
    return depth;
}

void OrderBook::shutdown() {
    shutdown_requested_.store(true);
    
    scoped_lock lock(order_mutex_);
    
    // Cancel all remaining orders
    for (auto& [order_id, order] : order_lookup_) {
        order->status = OrderStatus::CANCELLED;
        pool_.deallocate(order);
    }
    
    order_lookup_.clear();
    bids_.clear();
    asks_.clear();
    
    LOG_INFO("OrderBook {} shutdown complete", symbol_);
}

} // namespace rtes