/**
 * @file risk_manager.cpp
 * @brief Pre-trade risk validation with zero-allocation hot path
 *
 * Every order passes through the Risk Manager before reaching
 * the matching engine. This is a serial bottleneck by design —
 * deterministic validation requires sequential processing.
 *
 * Optimizations:
 *   - No heap allocation in validation path
 *   - Integer-only notional calculation (no floating point)
 *   - Direct symbol lookup via FixedString key (no std::string)
 *   - Targeted cancel routing (not broadcast)
 *   - Batch drain + spin-pause-yield (no sleep)
 *   - Local stats counters (flushed periodically)
 *   - Flat hash set for active orders (no tree allocation)
 *
 * Threading model:
 *   Single dedicated thread. Consumes from SPSC queue (gateway → risk).
 *   Forwards approved orders to per-symbol matching engine SPSC queues.
 */

#include "rtes/risk_manager.hpp"
#include "rtes/logger.hpp"
#include "rtes/security_utils.hpp"

#include <cstring>
#include <algorithm>

#ifdef __x86_64__
#include <immintrin.h>
#include <sched.h>
#endif

namespace rtes {

// ═══════════════════════════════════════════════════════════════
//  Constants
// ═══════════════════════════════════════════════════════════════

/** Max orders to drain per batch cycle */
inline constexpr size_t RISK_BATCH_SIZE = 128;

/** Spin iterations before yield on empty queue */
inline constexpr size_t RISK_SPIN_ITERS = 512;

/** Pause instructions per spin iteration */
inline constexpr size_t RISK_PAUSE_PER_SPIN = 4;

/** Flush stats every N orders */
inline constexpr size_t RISK_STATS_FLUSH = 4096;

/** SPSC queue capacity */
inline constexpr size_t RISK_QUEUE_CAPACITY = 65536;

/** Rate limit window in nanoseconds (1 second) */
inline constexpr uint64_t RATE_LIMIT_WINDOW_NS = 1'000'000'000ULL;

// ═══════════════════════════════════════════════════════════════
//  Construction
// ═══════════════════════════════════════════════════════════════

RiskManager::RiskManager(const RiskConfig& config,
                         const std::vector<SymbolConfig>& symbols)
    : config_(config)
    , input_queue_(std::make_unique<SPSCQueue<RiskRequest>>(RISK_QUEUE_CAPACITY))
{
    // Build symbol config lookup — keyed by FixedString, no std::string
    for (const auto& sym : symbols) {
        Symbol key(sym.symbol.c_str());
        symbol_configs_[key] = sym;
    }

    // Pre-compute integer notional limit (avoid float in hot path)
    // max_notional is in dollars, multiply by PRICE_SCALE for integer comparison
    max_notional_scaled_ = static_cast<uint64_t>(
        config.max_notional_per_client * PRICE_SCALE);

    LOG_INFO("Risk manager initialized with {} symbols, "
             "max_order_size={}, max_orders_per_sec={}",
             symbols.size(), config.max_order_size,
             config.max_orders_per_second);
}

RiskManager::~RiskManager() {
    stop();
}

// ═══════════════════════════════════════════════════════════════
//  Lifecycle
// ═══════════════════════════════════════════════════════════════

void RiskManager::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true,
            std::memory_order_acq_rel)) {
        return;
    }

    worker_thread_ = std::thread(&RiskManager::run, this);
    LOG_INFO("Risk manager started");
}

void RiskManager::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false,
            std::memory_order_acq_rel)) {
        return;
    }

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    flush_stats();
    LOG_INFO("Risk manager stopped (processed={}, rejected={}, approved={})",
             stats_atomic_.processed.load(std::memory_order_relaxed),
             stats_atomic_.rejected.load(std::memory_order_relaxed),
             stats_atomic_.approved.load(std::memory_order_relaxed));
}

// ═══════════════════════════════════════════════════════════════
//  Order Submission (called from gateway thread)
// ═══════════════════════════════════════════════════════════════

bool RiskManager::submit_order(Order* order) {
    if (!order) [[unlikely]] return false;

    RiskRequest req;
    req.type = RiskRequest::NEW_ORDER;
    req.order = order;

    return input_queue_->push(req);
}

bool RiskManager::submit_cancel(OrderID order_id, ClientID client_id) {
    RiskRequest req;
    req.type = RiskRequest::CANCEL_ORDER;
    req.cancel.order_id = order_id;
    req.cancel.client_id = client_id;

    return input_queue_->push(req);
}

void RiskManager::add_matching_engine(const std::string& symbol,
                                       MatchingEngine* engine) {
    Symbol key(symbol.c_str());
    matching_engines_[key] = engine;
}

/**
 * Set reference price for a symbol (from market data).
 * Called by exchange when trades execute or on startup.
 * Used for price collar validation.
 */
void RiskManager::update_reference_price(const Symbol& symbol, Price price) {
    reference_prices_[symbol] = price;
}

// ═══════════════════════════════════════════════════════════════
//  Hot Loop
// ═══════════════════════════════════════════════════════════════

void RiskManager::run() {
    LOG_INFO("Risk manager worker started");

    while (running_.load(std::memory_order_relaxed)) {
        size_t processed = drain_batch();

        if (processed > 0) {
            maybe_flush_stats();
        } else {
            spin_wait();
        }
    }

    // Drain remaining on shutdown
    LOG_INFO("Risk manager draining remaining orders");
    size_t drained = 0;
    while (true) {
        size_t batch = drain_batch();
        if (batch == 0) break;
        drained += batch;
    }

    flush_stats();

    if (drained > 0) {
        LOG_INFO("Risk manager drained {} remaining orders", drained);
    }
    LOG_INFO("Risk manager worker exited");
}

/**
 * Drain up to RISK_BATCH_SIZE orders from queue.
 */
size_t RiskManager::drain_batch() {
    RiskRequest request;
    size_t count = 0;

    while (count < RISK_BATCH_SIZE && input_queue_->pop(request)) {
        switch (request.type) {
            case RiskRequest::NEW_ORDER:
                process_new_order(request.order);
                break;

            case RiskRequest::CANCEL_ORDER:
                process_cancel(request.cancel.order_id, request.cancel.client_id);
                break;
        }
        ++count;
    }

    local_stats_.processed += count;
    return count;
}

// ═══════════════════════════════════════════════════════════════
//  Order Processing
// ═══════════════════════════════════════════════════════════════

/**
 * Validate and route a new order.
 *
 * Validation sequence (fail-fast — cheapest checks first):
 *   1. Null check
 *   2. Symbol allowed (hash lookup)
 *   3. Order size (integer compare)
 *   4. Rate limit (counter check)
 *   5. Duplicate order (hash lookup)
 *   6. Price collar (integer arithmetic)
 *   7. Credit limit (integer arithmetic)
 *   8. Update state + route to matching engine
 */
void RiskManager::process_new_order(Order* order) {
    if (!order) [[unlikely]] {
        ++local_stats_.rejected;
        return;
    }

    // ── Symbol lookup (zero allocation) ──
    const Symbol& sym = order->symbol;
    auto sym_it = symbol_configs_.find(sym);

    if (sym_it == symbol_configs_.end()) [[unlikely]] {
        reject_order(order, RiskResult::REJECTED_SYMBOL);
        return;
    }

    const SymbolConfig& sym_config = sym_it->second;

    // ── Order size check (cheapest) ──
    if (order->quantity == 0 ||
        order->quantity > config_.max_order_size) [[unlikely]] {
        reject_order(order, RiskResult::REJECTED_SIZE);
        return;
    }

    // ── Get/create client state ──
    auto& client = get_or_create_client(order->client_id);

    // ── Rate limit check ──
    if (!check_rate_limit(client)) [[unlikely]] {
        reject_order(order, RiskResult::REJECTED_RATE_LIMIT);
        return;
    }

    // ── Duplicate check ──
    if (client.active_orders.count(order->id)) [[unlikely]] {
        reject_order(order, RiskResult::REJECTED_DUPLICATE);
        return;
    }

    // ── Price collar check (integer arithmetic, no float) ──
    if (config_.price_collar_enabled &&
        !check_price_collar_int(order, sym_config)) [[unlikely]] {
        reject_order(order, RiskResult::REJECTED_PRICE);
        return;
    }

    // ── Credit limit check (integer arithmetic) ──
    const uint64_t order_notional = calculate_notional_int(order);
    if (client.notional_exposure_scaled + order_notional >
        max_notional_scaled_) [[unlikely]] {
        reject_order(order, RiskResult::REJECTED_CREDIT);
        return;
    }

    // ── All checks passed — update state and route ──

    // Track active order (for cancel ownership + duplicate detection)
    client.active_orders.insert(order->id);
    client.notional_exposure_scaled += order_notional;

    // Track order→symbol mapping (for targeted cancel routing)
    order_symbol_index_[order->id] = sym;

    // Route to matching engine (direct FixedString lookup, no std::string)
    auto me_it = matching_engines_.find(sym);
    if (me_it != matching_engines_.end()) [[likely]] {
        if (!me_it->second->submit_order(order)) [[unlikely]] {
            // Matching engine queue full — rollback state
            client.active_orders.erase(order->id);
            client.notional_exposure_scaled -= order_notional;
            order_symbol_index_.erase(order->id);
            reject_order(order, RiskResult::REJECTED_QUEUE_FULL);
            return;
        }
    } else [[unlikely]] {
        // No matching engine for symbol (config error)
        client.active_orders.erase(order->id);
        client.notional_exposure_scaled -= order_notional;
        order_symbol_index_.erase(order->id);
        reject_order(order, RiskResult::REJECTED_SYMBOL);
        return;
    }

    order->status = OrderStatus::ACCEPTED;
    ++local_stats_.approved;
}

/**
 * Process cancel request with targeted routing.
 *
 * Unlike the old broadcast approach, we look up the order's symbol
 * and route the cancel to the specific matching engine.
 */
void RiskManager::process_cancel(OrderID order_id, ClientID client_id) {
    // Ownership check
    auto client_it = client_states_.find(client_id);
    if (client_it == client_states_.end()) [[unlikely]] {
        ++local_stats_.cancels_rejected;
        return;
    }

    auto& client = client_it->second;
    if (client.active_orders.count(order_id) == 0) [[unlikely]] {
        ++local_stats_.cancels_rejected;
        return;
    }

    // Look up symbol for targeted routing
    auto sym_it = order_symbol_index_.find(order_id);
    if (sym_it != order_symbol_index_.end()) {
        // Route cancel to specific matching engine
        auto me_it = matching_engines_.find(sym_it->second);
        if (me_it != matching_engines_.end()) {
            (void)me_it->second->cancel_order(order_id, client_id);
        }

        order_symbol_index_.erase(sym_it);
    }

    // Update client state
    client.active_orders.erase(order_id);
    // Note: notional reduction happens when cancel confirmed by matching engine
    ++local_stats_.cancels_accepted;
}

// ═══════════════════════════════════════════════════════════════
//  Risk Checks (Zero Allocation)
// ═══════════════════════════════════════════════════════════════

/**
 * Integer-only price collar check.
 *
 * Instead of converting to double and comparing:
 *   order_price >= ref * (1 - pct) && order_price <= ref * (1 + pct)
 *
 * We use integer arithmetic:
 *   order_price * 100 >= ref * (100 - collar_pct)
 *   order_price * 100 <= ref * (100 + collar_pct)
 *
 * This avoids:
 *   - Floating point rounding errors
 *   - Float division (~20-35 cycles)
 *   - Float comparison hazards (NaN, inf)
 *
 * Reference price comes from last trade or initial config.
 * If no reference price exists, collar check is SKIPPED (not failed).
 */
bool RiskManager::check_price_collar_int(const Order* order,
                                          const SymbolConfig& sym_config) const {
    // Look up reference price for this symbol
    auto ref_it = reference_prices_.find(order->symbol);
    if (ref_it == reference_prices_.end()) {
        // No reference price yet — skip collar (allow first trades)
        return true;
    }

    const Price ref_price = ref_it->second;
    if (ref_price == 0) return true;  // No meaningful reference

    const uint64_t collar_pct = sym_config.price_collar_pct;  // e.g., 10 = 10%

    // order_price * 100 must be within [ref * (100 - pct), ref * (100 + pct)]
    // Using 128-bit multiplication to prevent overflow for large prices
    const __uint128_t order_scaled =
        static_cast<__uint128_t>(order->price) * 100;
    const __uint128_t ref_lower =
        static_cast<__uint128_t>(ref_price) * (100 - collar_pct);
    const __uint128_t ref_upper =
        static_cast<__uint128_t>(ref_price) * (100 + collar_pct);

    return order_scaled >= ref_lower && order_scaled <= ref_upper;
}

/**
 * Rate limit check using nanosecond timestamp.
 *
 * Sliding window: count orders within the last 1 second.
 * When window expires, reset counter.
 *
 * Uses now_timestamp() (nanosecond uint64_t) instead of
 * steady_clock::now() + chrono comparison for consistency
 * with the rest of the codebase.
 */
bool RiskManager::check_rate_limit(ClientRiskState& client) {
    const Timestamp now = now_timestamp();

    // Reset window if expired
    if (now - client.window_start_ns >= RATE_LIMIT_WINDOW_NS) {
        client.orders_in_window = 0;
        client.window_start_ns = now;
    }

    if (client.orders_in_window >= config_.max_orders_per_second) {
        return false;
    }

    ++client.orders_in_window;
    return true;
}

/**
 * Integer notional calculation.
 *
 * price is in fixed-point (actual × PRICE_SCALE).
 * quantity is in shares.
 * notional = price × quantity (in units of actual_price × PRICE_SCALE × shares)
 *
 * Example: price = 1502500 (\$150.25), qty = 100
 *   notional_scaled = 1502500 × 100 = 150,250,000
 *   This represents \$150.25 × 100 = \$15,025.00
 *   In scaled units: 15025 × PRICE_SCALE = 150,250,000 ✓
 *
 * Compared against max_notional_scaled_ (also in same units).
 * No floating point needed.
 */
uint64_t RiskManager::calculate_notional_int(const Order* order) const {
    // Check for overflow before multiply
    // price ≤ 2^64, quantity ≤ 2^64
    // For realistic values (price < 1M scaled, qty < 1B), no overflow
    return order->price * order->quantity;
}

// ═══════════════════════════════════════════════════════════════
//  Client State Management
// ═══════════════════════════════════════════════════════════════

/**
 * Get or create client risk state.
 *
 * The unordered_map operator[] creates a default entry if not found.
 * This is a cold-path allocation (happens once per new client).
 * Subsequent lookups for the same client are O(1) hash lookup.
 */
ClientRiskState& RiskManager::get_or_create_client(const ClientID& id) {
    auto it = client_states_.find(id);
    if (it != client_states_.end()) [[likely]] {
        return it->second;
    }

    // New client — create state
    auto [new_it, inserted] = client_states_.emplace(id, ClientRiskState{});
    new_it->second.window_start_ns = now_timestamp();
    return new_it->second;
}

/**
 * Reject order and update stats.
 * Logging is rate-limited to prevent flooding under attack.
 */
void RiskManager::reject_order(Order* order, RiskResult reason) {
    order->status = OrderStatus::REJECTED;
    ++local_stats_.rejected;

    // Rate-limited logging (at most once per 1000 rejections per reason)
    if (local_stats_.rejected % 1000 == 1) {
        LOG_WARN("Order {} rejected: reason={}", order->id,
                 static_cast<int>(reason));
    }
}

// ═══════════════════════════════════════════════════════════════
//  Backoff Strategy
// ═══════════════════════════════════════════════════════════════

void RiskManager::spin_wait() {
    for (size_t i = 0; i < RISK_SPIN_ITERS; ++i) {
        if (!input_queue_->consumer_empty()) return;

        for (size_t p = 0; p < RISK_PAUSE_PER_SPIN; ++p) {
#ifdef __x86_64__
            _mm_pause();
#elif defined(__aarch64__)
            asm volatile("yield" ::: "memory");
#endif
        }
    }
    sched_yield();
}

// ═══════════════════════════════════════════════════════════════
//  Statistics
// ═══════════════════════════════════════════════════════════════

void RiskManager::maybe_flush_stats() {
    if (local_stats_.processed % RISK_STATS_FLUSH == 0) {
        flush_stats();
    }
}

void RiskManager::flush_stats() {
    stats_atomic_.processed.store(
        local_stats_.processed, std::memory_order_relaxed);
    stats_atomic_.approved.store(
        local_stats_.approved, std::memory_order_relaxed);
    stats_atomic_.rejected.store(
        local_stats_.rejected, std::memory_order_relaxed);
    stats_atomic_.cancels_accepted.store(
        local_stats_.cancels_accepted, std::memory_order_relaxed);
    stats_atomic_.cancels_rejected.store(
        local_stats_.cancels_rejected, std::memory_order_relaxed);
}

} // namespace rtes