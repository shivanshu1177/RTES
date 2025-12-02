/**
 * @file order_book.cpp
 * @brief Order book implementation matching order_book.hpp interface
 */

#include "rtes/order_book.hpp"
#include "rtes/logger.hpp"
#include "rtes/error_handling.hpp"
#include "rtes/performance_optimizer.hpp"
#include "rtes/thread_safety.hpp"
#include <algorithm>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define PREFETCH(addr, hint) _mm_prefetch((const char*)(addr), hint)
#define PREFETCH_HINT_T0 _MM_HINT_T0
#else
#define PREFETCH(addr, hint) ((void)0)
#define PREFETCH_HINT_T0 0
#endif

namespace rtes {

OrderBook::OrderBook(const std::string& symbol, OrderPool& pool,
                     TradeCallback callback, void* cb_ctx)
    : pool_(pool)
    , trade_callback_(callback)
    , callback_ctx_(cb_ctx)
    , symbol_(symbol)
    , shutdown_requested_(false)
{
    ShutdownManager::instance().register_component(
        "OrderBook_" + symbol_,
        [this]() { shutdown(); }
    );

    perf_optimizer_ = std::make_unique<PerformanceOptimizer>();

    metrics_.add_order_latency = &perf_optimizer_->get_latency_tracker("add_order");
    metrics_.match_latency     = &perf_optimizer_->get_latency_tracker("match_order");
    metrics_.trade_latency     = &perf_optimizer_->get_latency_tracker("execute_trade");
    metrics_.order_throughput  = &perf_optimizer_->get_throughput_tracker("add_order");
    metrics_.match_throughput  = &perf_optimizer_->get_throughput_tracker("matching");
}

OrderBook::~OrderBook() = default;

Result<void> OrderBook::add_order(Order* order) {
    if (shutdown_requested_) return ErrorCode::SYSTEM_SHUTDOWN;
    MEASURE_LATENCY(*metrics_.add_order_latency);
    metrics_.order_throughput->record_event();

    if (!order || order->remaining_quantity == 0) return ErrorCode::ORDER_INVALID;
    if (order_lookup_.find(order->id) != order_lookup_.end()) return ErrorCode::ORDER_DUPLICATE;

    try {
        order_lookup_[order->id] = order;

        auto match_result = match_order(order);
        if (match_result.has_error()) {
            order_lookup_.erase(order->id);
            return match_result.error();
        }

        if (order->remaining_quantity > 0) {
            auto book_result = add_to_book(order);
            if (book_result.has_error()) {
                order_lookup_.erase(order->id);
                return book_result.error();
            }
        } else {
            order_lookup_.erase(order->id);
        }

        update_bbo_snapshot();
        return Result<void>();
    } catch (const std::exception& e) {
        LOG_ERROR_SAFE("Exception in add_order: {}", e.what());
        return ErrorCode::SYSTEM_CORRUPTED_STATE;
    }
}

Result<void> OrderBook::cancel_order(OrderID order_id) {
    if (shutdown_requested_) return ErrorCode::SYSTEM_SHUTDOWN;
    auto it = order_lookup_.find(order_id);
    if (it == order_lookup_.end()) return ErrorCode::ORDER_NOT_FOUND;

    try {
        Order* order = it->second;
        remove_from_book(order);
        order_lookup_.erase(it);
        order->status = OrderStatus::CANCELLED;
        pool_.deallocate(order);
        update_bbo_snapshot();
        return Result<void>();
    } catch (const std::exception& e) {
        LOG_ERROR_SAFE("Exception in cancel_order: {}", e.what());
        return ErrorCode::SYSTEM_CORRUPTED_STATE;
    }
}

Result<void> OrderBook::match_order(Order* order) {
    if (shutdown_requested_) return ErrorCode::SYSTEM_SHUTDOWN;
    MEASURE_LATENCY(*metrics_.match_latency);
    metrics_.match_throughput->record_event();
    if (!order) return ErrorCode::ORDER_INVALID;

    try {
        if (order->type == OrderType::MARKET) return match_market_order(order);
        else return match_limit_order(order);
    } catch (const std::exception& e) {
        LOG_ERROR_SAFE("Exception in match_order: {}", e.what());
        return ErrorCode::SYSTEM_CORRUPTED_STATE;
    }
}

Result<void> OrderBook::match_market_order(Order* order) {
    try {
        auto sweep = [&](auto& opposite) -> Result<void> {
            while (order->remaining_quantity > 0 && !opposite.empty()) {
                FlatLevel& level = opposite[0];
                if (level.empty()) { opposite.remove(level.price); continue; }
                Order* passive = level.front();

                if (level.size() > 1) PREFETCH(level.orders[level.head_ + 1], PREFETCH_HINT_T0);

                Quantity qty = std::min(order->remaining_quantity, passive->remaining_quantity);
                execute_trade(order, passive, qty, level.price);

                if (passive->remaining_quantity == 0) {
                    level.pop_front(qty);
                    order_lookup_.erase(passive->id);
                    passive->status = OrderStatus::FILLED;
                    pool_.deallocate(passive);
                } else {
                    level.reduce_quantity(qty);
                }

                if (level.empty()) opposite.remove(level.price);
            }
            return Result<void>();
        };
        if (order->side == Side::BUY) return sweep(asks_);
        else return sweep(bids_);
    } catch (const std::exception& e) {
        LOG_ERROR_SAFE("Exception in match_market_order: {}", e.what());
        return ErrorCode::SYSTEM_CORRUPTED_STATE;
    }
}

Result<void> OrderBook::match_limit_order(Order* order) {
    try {
        auto sweep = [&](auto& opposite) -> Result<void> {
            while (order->remaining_quantity > 0 && !opposite.empty()) {
                FlatLevel& level = opposite[0];
                const bool crosses = (order->side == Side::BUY) ? (order->price >= level.price) : (order->price <= level.price);
                if (!crosses) break;

                if (level.empty()) { opposite.remove(level.price); continue; }
                Order* passive = level.front();

                if (level.size() > 1) PREFETCH(level.orders[level.head_ + 1], PREFETCH_HINT_T0);

                Quantity qty = std::min(order->remaining_quantity, passive->remaining_quantity);
                execute_trade(order, passive, qty, level.price);

                if (passive->remaining_quantity == 0) {
                    level.pop_front(qty);
                    order_lookup_.erase(passive->id);
                    passive->status = OrderStatus::FILLED;
                    pool_.deallocate(passive);
                } else {
                    level.reduce_quantity(qty);
                }

                if (level.empty()) opposite.remove(level.price);
            }
            return Result<void>();
        };
        if (order->side == Side::BUY) return sweep(asks_);
        else return sweep(bids_);
    } catch (const std::exception& e) {
        LOG_ERROR_SAFE("Exception in match_limit_order: {}", e.what());
        return ErrorCode::SYSTEM_CORRUPTED_STATE;
    }
}

void OrderBook::execute_trade(Order* aggressive, Order* passive, Quantity quantity, Price price) {
    MEASURE_LATENCY(*metrics_.trade_latency);
    PREFETCH(aggressive, PREFETCH_HINT_T0);
    PREFETCH(passive, PREFETCH_HINT_T0);

    aggressive->remaining_quantity -= quantity;
    passive->remaining_quantity -= quantity;

    aggressive->status = (aggressive->remaining_quantity == 0) ? OrderStatus::FILLED : OrderStatus::PARTIALLY_FILLED;
    passive->status = (passive->remaining_quantity == 0) ? OrderStatus::FILLED : OrderStatus::PARTIALLY_FILLED;

    Trade trade(next_trade_id_++,
                (aggressive->side == Side::BUY) ? aggressive->id : passive->id,
                (aggressive->side == Side::SELL) ? aggressive->id : passive->id,
                symbol_.c_str(), quantity, price);

    if (trade_callback_) trade_callback_(trade, callback_ctx_);
}

Result<void> OrderBook::add_to_book(Order* order) {
    if (!order) return ErrorCode::ORDER_INVALID;
    try {
        auto insert_into = [&](auto& book) -> Result<void> {
            FlatLevel& level = book.find_or_insert(order->price);
            level.push_back(order);
            order->status = OrderStatus::ACCEPTED;
            return Result<void>();
        };
        if (order->side == Side::BUY) return insert_into(bids_);
        else return insert_into(asks_);
    } catch (const std::exception& e) {
        LOG_ERROR_SAFE("Exception in add_to_book: {}", e.what());
        return ErrorCode::SYSTEM_CORRUPTED_STATE;
    }
}

void OrderBook::remove_from_book(Order* order) {
    auto remove_from = [&](auto& book) {
        FlatLevel* level = book.find(order->price);
        if (!level) return;
        auto& v = level->orders;
        for (size_t i = level->head_; i < v.size(); ++i) {
            if (v[i] == order) {
                v.erase(v.begin() + i);
                if (i < level->head_) --level->head_;
                level->total_quantity -= order->remaining_quantity;
                if (level->empty()) book.remove(order->price);
                return;
            }
        }
    };
    if (order->side == Side::BUY) remove_from(bids_);
    else remove_from(asks_);
}

void OrderBook::get_depth(DepthSnapshot& out, size_t max_levels) const {
    out.bid_levels = 0;
    out.ask_levels = 0;

    for (size_t i = 0; i < bids_.level_count() && out.bid_levels < max_levels && out.bid_levels < MAX_DEPTH_LEVELS; ++i) {
        const FlatLevel& l = bids_[i];
        if (!l.empty()) out.bids[out.bid_levels++] = {l.price, l.total_quantity, static_cast<uint32_t>(l.size())};
    }
    for (size_t i = 0; i < asks_.level_count() && out.ask_levels < max_levels && out.ask_levels < MAX_DEPTH_LEVELS; ++i) {
        const FlatLevel& l = asks_[i];
        if (!l.empty()) out.asks[out.ask_levels++] = {l.price, l.total_quantity, static_cast<uint32_t>(l.size())};
    }
}

bool OrderBook::read_bbo(BBOSnapshot& out) const {
    uint64_t seq1 = bbo_snapshot_.sequence;
    if (seq1 & 1) return false;
    out = bbo_snapshot_;
    uint64_t seq2 = bbo_snapshot_.sequence;
    return seq1 == seq2;
}

void OrderBook::update_bbo_snapshot() {
    ++bbo_snapshot_.sequence;
    bbo_snapshot_.bid_price    = bids_.best_price();
    bbo_snapshot_.bid_quantity = bids_.best_quantity();
    bbo_snapshot_.ask_price    = asks_.best_price();
    bbo_snapshot_.ask_quantity = asks_.best_quantity();
    ++bbo_snapshot_.sequence;
}

void OrderBook::shutdown() {
    shutdown_requested_ = true;
    for (auto& [order_id, order] : order_lookup_) {
        order->status = OrderStatus::CANCELLED;
        pool_.deallocate(order);
    }
    order_lookup_.clear();
    bids_.clear();
    asks_.clear();
    LOG_INFO_SAFE("OrderBook {} shutdown complete", symbol_);
}

} // namespace rtes