/**
 * @file matching_engine.cpp
 * @brief Per-symbol matching engine with price-time priority
 */

#include "rtes/matching_engine.hpp"
#include "rtes/logger.hpp"

#include <sched.h>
#include <cstring>
#include <algorithm>

namespace rtes {

inline constexpr size_t BATCH_SIZE           = 256;
inline constexpr size_t SPIN_ITERATIONS      = 1024;
inline constexpr size_t PAUSE_PER_SPIN       = 4;
inline constexpr size_t STATS_FLUSH_INTERVAL = 4096;
inline constexpr size_t COMPACT_INTERVAL     = 8192;
inline constexpr size_t QUEUE_CAPACITY       = 65536;

// ── Static trade callback ─────────────────────────────────────
// OrderBook in order_book.cpp uses TradeCallback = void(*)(const Trade&)
// so we adapt via a file-level wrapper that calls on_trade_internal.
// The OrderBook constructor in order_book.cpp takes:
//   OrderBook(string, OrderPool&, TradeCallback)
// where TradeCallback = void(*)(const Trade&).
// We store `this` and use a static lambda-style free function.


static void trade_callback_trampoline(const Trade& trade, void* ctx) {
    auto* engine = static_cast<MatchingEngine*>(ctx);
    engine->on_trade_internal(trade);
}

// ═══════════════════════════════════════════════════════════════
// THIS CONSTRUCTOR DEFINITION WAS MISSING!
// ═══════════════════════════════════════════════════════════════
MatchingEngine::MatchingEngine(const std::string& symbol, OrderPool& pool)
    : symbol_(symbol)
    , pool_(pool)
    , input_queue_(std::make_unique<SPSCQueue<OrderRequest>>(QUEUE_CAPACITY))
    , book_(nullptr)
{
    book_ = std::make_unique<OrderBook>(symbol, pool, trade_callback_trampoline, this);

    std::memset(symbol_cache_, 0, sizeof(symbol_cache_));
    std::memcpy(symbol_cache_, symbol.c_str(),
                std::min(symbol.size(), sizeof(symbol_cache_) - 1));
}

MatchingEngine::~MatchingEngine() {
    stop();
}

// ═══════════════════════════════════════════════════════════════
//  Lifecycle
// ═══════════════════════════════════════════════════════════════

void MatchingEngine::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true,
            std::memory_order_acq_rel)) {
        return;
    }

    worker_thread_ = std::thread(&MatchingEngine::run, this);
    LOG_INFO("Matching engine started for {}", symbol_);
}

void MatchingEngine::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false,
            std::memory_order_acq_rel)) {
        return;
    }

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    LOG_INFO("Matching engine stopped for {} (processed={}, trades={})",
             symbol_,
             orders_processed_.load(std::memory_order_relaxed),
             trades_executed_.load(std::memory_order_relaxed));
}

// ═══════════════════════════════════════════════════════════════
//  Order Submission (called from gateway/risk thread)
// ═══════════════════════════════════════════════════════════════

bool MatchingEngine::submit_order(Order* order) {
    if (!order) [[unlikely]] return false;

    OrderRequest request = OrderRequest::make_new_order(order);  // ← use factory

    if (!input_queue_->push(request)) [[unlikely]] {
        ++local_stats_.queue_full_count;
        return false;
    }
    return true;
}

bool MatchingEngine::cancel_order(OrderID order_id, ClientID client_id) {
    OrderRequest request = OrderRequest::make_cancel(order_id, client_id);  // ← use factory

    if (!input_queue_->push(request)) [[unlikely]] {
        ++local_stats_.queue_full_count;
        return false;
    }
    return true;
}

void MatchingEngine::set_market_data_queue(MPMCQueue<MarketDataEvent>* queue) {
    market_data_queue_ = queue;
}

// ═══════════════════════════════════════════════════════════════
//  Hot Loop
// ═══════════════════════════════════════════════════════════════

void MatchingEngine::run() {
    LOG_INFO("Matching engine worker started for {}", symbol_);

    while (running_.load(std::memory_order_relaxed)) {
        size_t processed = drain_batch();

        if (processed > 0) {
            maybe_compact();
            maybe_flush_stats();
        } else {
            spin_wait();
        }
    }

    LOG_INFO("Draining remaining orders for {}", symbol_);
    size_t drained = 0;
    while (true) {
        size_t batch = drain_batch();
        if (batch == 0) break;
        drained += batch;
    }

    flush_stats();

    if (drained > 0) {
        LOG_INFO("Drained {} remaining orders for {}", drained, symbol_);
    }
    LOG_INFO("Matching engine worker exited for {}", symbol_);
}

size_t MatchingEngine::drain_batch() {
    OrderRequest request;
    size_t count = 0;

    while (count < BATCH_SIZE && input_queue_->pop(request)) {
        input_queue_->prefetch_next();

        switch (request.type) {
            case OrderRequest::NEW_ORDER:
                process_new_order(request.new_order.order);       // ← FIXED
                break;

            case OrderRequest::CANCEL_ORDER:
                process_cancel(request.cancel.order_id,           // ← FIXED
                               request.cancel.client_id);         // ← FIXED
                break;
        }
        ++count;
    }

    local_stats_.total_processed += count;
    return count;
}

// ═══════════════════════════════════════════════════════════════
//  Order Processing
// ═══════════════════════════════════════════════════════════════

void MatchingEngine::process_new_order(Order* order) {
    if (!order) [[unlikely]] return;

    const Price old_bid = book_->best_bid();
    const Price old_ask = book_->best_ask();

    auto result = book_->add_order(order);

    if (result.has_value()) {
        ++local_stats_.orders_accepted;

        const Price new_bid = book_->best_bid();
        const Price new_ask = book_->best_ask();
        if (new_bid != old_bid || new_ask != old_ask) {
            publish_bbo_update();
        }
    } else {
        ++local_stats_.orders_rejected;
        order->status = OrderStatus::REJECTED;
        pool_.deallocate(order);

        LOG_DEBUG("Order {} rejected: {}", order->id,
                  result.error().value());                        // ← FIXED
    }
}

void MatchingEngine::process_cancel(OrderID order_id, ClientID client_id) {
    const Price old_bid = book_->best_bid();
    const Price old_ask = book_->best_ask();

    auto result = book_->cancel_order(order_id);

    if (result.has_value()) {
        ++local_stats_.cancels_accepted;

        const Price new_bid = book_->best_bid();
        const Price new_ask = book_->best_ask();
        if (new_bid != old_bid || new_ask != old_ask) {
            publish_bbo_update();
        }
    } else {
        ++local_stats_.cancels_rejected;
    }
}

// ═══════════════════════════════════════════════════════════════
//  Market Data Publishing
// ═══════════════════════════════════════════════════════════════

void MatchingEngine::on_trade_internal(const Trade& trade) {
    ++local_stats_.trades_executed;
    publish_trade(trade);
}

void MatchingEngine::publish_trade(const Trade& trade) {
    if (!market_data_queue_) [[unlikely]] return;

    MarketDataEvent event;
    event.type = MarketDataEvent::TRADE;
    std::memcpy(event.symbol, symbol_cache_, sizeof(event.symbol));
    event.trade = trade;

    if (!market_data_queue_->push(event)) [[unlikely]] {
        ++local_stats_.md_drops;
    }
}

void MatchingEngine::publish_bbo_update() {
    if (!market_data_queue_) [[unlikely]] return;

    MarketDataEvent event;
    event.type = MarketDataEvent::BBO_UPDATE;
    std::memcpy(event.symbol, symbol_cache_, sizeof(event.symbol));

    event.bbo.bid_price    = book_->best_bid();
    event.bbo.bid_quantity = book_->bid_quantity();
    event.bbo.ask_price    = book_->best_ask();
    event.bbo.ask_quantity = book_->ask_quantity();

    if (!market_data_queue_->push(event)) [[unlikely]] {
        ++local_stats_.md_drops;
    }
}

// ═══════════════════════════════════════════════════════════════
//  Backoff Strategy
// ═══════════════════════════════════════════════════════════════

void MatchingEngine::spin_wait() {
    for (size_t i = 0; i < SPIN_ITERATIONS; ++i) {
        if (!input_queue_->empty()) return;

        for (size_t p = 0; p < PAUSE_PER_SPIN; ++p) {
#if defined(__aarch64__)
            asm volatile("yield" ::: "memory");
#elif defined(__x86_64__)
            _mm_pause();
#endif
        }
    }

    sched_yield();
}

// ═══════════════════════════════════════════════════════════════
//  Maintenance
// ═══════════════════════════════════════════════════════════════

void MatchingEngine::maybe_compact() {
    if (local_stats_.total_processed % COMPACT_INTERVAL == 0) {
        book_->compact();
        book_->prune();
    }
}

void MatchingEngine::maybe_flush_stats() {
    if (local_stats_.total_processed % STATS_FLUSH_INTERVAL == 0) {
        flush_stats();
    }
}

void MatchingEngine::flush_stats() {
    orders_processed_.store(local_stats_.orders_accepted, std::memory_order_relaxed);
    trades_executed_.store(local_stats_.trades_executed, std::memory_order_relaxed);
    orders_rejected_.store(local_stats_.orders_rejected, std::memory_order_relaxed);
    md_drops_.store(local_stats_.md_drops, std::memory_order_relaxed);
}

} // namespace rtes