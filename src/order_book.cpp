#include "rtes/order_book.hpp"
#include "rtes/logger.hpp"

namespace rtes {

OrderBook::OrderBook(const std::string& symbol, OrderPool& pool, TradeCallback cb)
    : symbol_(symbol), pool_(pool), trade_callback_(cb) {
}

bool OrderBook::add_order(Order* order) {
    if (!order) return false;
    
    std::lock_guard<std::mutex> lock(mutex_);
    order_lookup_[order->order_id] = order;
    
    match_order(order);
    
    if (!order->is_filled()) {
        add_to_book(order);
    }
    
    return true;
}

bool OrderBook::cancel_order(OrderID order_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = order_lookup_.find(order_id);
    if (it == order_lookup_.end()) {
        return false;
    }
    
    remove_order_from_book(it->second);
    pool_.release(it->second);
    order_lookup_.erase(it);
    
    return true;
}

Price OrderBook::best_bid() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bids_.empty() ? 0 : bids_.begin()->first;
}

Price OrderBook::best_ask() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return asks_.empty() ? 0 : asks_.begin()->first;
}

size_t OrderBook::order_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return order_lookup_.size();
}

void OrderBook::match_order(Order* order) {
    auto& opposite_side = (order->side == Side::BUY) ? asks_ : bids_;
    
    while (!order->is_filled() && !opposite_side.empty()) {
        auto& best_level = opposite_side.begin()->second;
        
        if (order->type == OrderType::MARKET || 
            (order->side == Side::BUY && order->price >= best_level.price) ||
            (order->side == Side::SELL && order->price <= best_level.price)) {
            
            auto* passive_order = best_level.orders.front();
            Quantity trade_qty = std::min(order->remaining(), passive_order->remaining());
            
            execute_trade(order, passive_order, trade_qty, best_level.price);
            
            if (passive_order->is_filled()) {
                best_level.orders.pop_front();
                best_level.total_quantity -= passive_order->quantity;
                order_lookup_.erase(passive_order->order_id);
                pool_.release(passive_order);
                
                if (best_level.orders.empty()) {
                    opposite_side.erase(opposite_side.begin());
                }
            }
        } else {
            break;
        }
    }
}

void OrderBook::execute_trade(Order* aggressive_order, Order* passive_order, Quantity quantity, Price price) {
    aggressive_order->filled += quantity;
    passive_order->filled += quantity;
    
    if (trade_callback_) {
        Trade trade{
            .trade_id = next_trade_id_++,
            .buy_order_id = (aggressive_order->side == Side::BUY) ? aggressive_order->order_id : passive_order->order_id,
            .sell_order_id = (aggressive_order->side == Side::SELL) ? aggressive_order->order_id : passive_order->order_id,
            .symbol = symbol_,
            .price = price,
            .quantity = quantity
        };
        trade_callback_(trade);
    }
}

void OrderBook::remove_order_from_book(Order* order) {
    auto& side = (order->side == Side::BUY) ? bids_ : asks_;
    
    auto level_it = side.find(order->price);
    if (level_it != side.end()) {
        auto& orders = level_it->second.orders;
        orders.erase(std::remove(orders.begin(), orders.end(), order), orders.end());
        level_it->second.total_quantity -= order->remaining();
        
        if (orders.empty()) {
            side.erase(level_it);
        }
    }
}

bool OrderBook::add_to_book(Order* order) {
    if (order->type == OrderType::MARKET) {
        return false; // Market orders don't rest in book
    }
    
    auto& side = (order->side == Side::BUY) ? bids_ : asks_;
    
    auto [level_it, inserted] = side.try_emplace(order->price, order->price);
    level_it->second.orders.push_back(order);
    level_it->second.total_quantity += order->remaining();
    
    return true;
}

} // namespace rtes