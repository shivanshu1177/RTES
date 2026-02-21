#include "rtes/risk_manager.hpp"
#include "rtes/matching_engine.hpp"
#include "rtes/logger.hpp"
#include <cmath>
#include <cstring>

namespace rtes {

RiskManager::RiskManager(const RiskConfig& risk,
                         const std::vector<SymbolConfig>& symbols)
    : risk_config_(risk),
      input_queue_(std::make_unique<SPSCQueue<Order*>>(65536))
{
    for (const auto& sc : symbols) {
        allowed_symbols_.insert(sc.symbol);
        price_collar_pct_[sc.symbol] = sc.price_collar_pct;
        // Default reference price: $150.00 expressed in the fixed-point unit
        // The protocol uses price * 10000 (i.e. 150.00 -> 1500000)
        reference_prices_[sc.symbol] = 1500000ULL;
    }
}

RiskManager::~RiskManager() { stop(); }

void RiskManager::start() {
    running_.store(true, std::memory_order_release);
    worker_thread_ = std::thread(&RiskManager::run, this);
}

void RiskManager::stop() {
    running_.store(false, std::memory_order_release);
    if (worker_thread_.joinable()) worker_thread_.join();
}

void RiskManager::add_matching_engine(const std::string& symbol, MatchingEngine* engine) {
    engines_[symbol] = engine;
}

void RiskManager::set_order_pool(OrderPool* pool) {
    pool_ = pool;
}

bool RiskManager::validate_order(const NewOrderMessage& msg, AuthContext& /*ctx*/) noexcept {
    // Quick pre-queue check (TCP gateway thread) — only reject obvious invalids
    char sym[9] = {};
    std::strncpy(sym, msg.symbol, 8);
    return allowed_symbols_.count(std::string(sym)) > 0 && msg.quantity > 0;
}

bool RiskManager::submit_order(Order* order) {
    for (int i = 0; i < 1000; ++i) {
        if (input_queue_->push(order)) return true;
    }
    LOG_WARN("RiskManager: input queue full, dropping order");
    if (pool_) pool_->release(order);
    return false;
}

void RiskManager::run() {
    while (running_.load(std::memory_order_acquire)) {
        Order* order = nullptr;
        if (input_queue_->pop(order) && order) {
            if (do_validate(order)) {
                auto it = engines_.find(order->symbol);
                if (it != engines_.end()) {
                    it->second->submit_order(order);
                    orders_accepted_.fetch_add(1, std::memory_order_relaxed);
                } else {
                    LOG_WARN("RiskManager: no engine for symbol " + order->symbol);
                    if (pool_) pool_->release(order);
                    orders_rejected_.fetch_add(1, std::memory_order_relaxed);
                }
            } else {
                if (pool_) pool_->release(order);
                orders_rejected_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
}

bool RiskManager::do_validate(Order* order) {
    // 1. Symbol
    if (!allowed_symbols_.count(order->symbol)) {
        LOG_WARN("Risk: unknown symbol " + order->symbol);
        return false;
    }

    // 2. Size limit
    if (order->quantity > risk_config_.max_order_size) {
        LOG_WARN("Risk: order size " + std::to_string(order->quantity) +
                 " exceeds limit " + std::to_string(risk_config_.max_order_size));
        return false;
    }

    // 3. Price collar (only for LIMIT orders)
    if (risk_config_.price_collar_enabled &&
        order->type == OrderType::LIMIT && order->price > 0) {
        auto ref = reference_prices_[order->symbol];
        auto pct = price_collar_pct_[order->symbol];
        // Use integer arithmetic: allowable range = ref ± ref*pct/100
        uint64_t delta = static_cast<uint64_t>(ref * pct / 100.0);
        uint64_t lo    = (ref > delta) ? (ref - delta) : 0;
        uint64_t hi    = ref + delta;
        if (order->price < lo || order->price > hi) {
            LOG_WARN("Risk: price " + std::to_string(order->price) +
                     " outside collar [" + std::to_string(lo) + "," +
                     std::to_string(hi) + "]");
            return false;
        }
    }

    // 4. Rate limit (per client)
    auto& state = client_states_[order->client_id];
    auto  now   = std::chrono::steady_clock::now();
    auto  elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - state.last_reset_time).count();
    if (elapsed_ms >= 1000) {
        state.order_count_last_second = 0;
        state.last_reset_time = now;
    }
    if (state.order_count_last_second >= risk_config_.max_orders_per_second) {
        LOG_WARN("Risk: rate limit exceeded for client " +
                 std::to_string(order->client_id));
        return false;
    }
    ++state.order_count_last_second;

    // 5. Duplicate order ID
    if (state.active_order_ids.count(order->order_id)) {
        LOG_WARN("Risk: duplicate order_id " + std::to_string(order->order_id));
        return false;
    }
    state.active_order_ids.insert(order->order_id);

    // 6. Notional exposure
    // price is in fixed-point *10000, so actual price = price / 10000.0
    double notional = (static_cast<double>(order->price) / 10000.0) * order->quantity;
    if (state.notional_exposure + notional > risk_config_.max_notional_per_client) {
        LOG_WARN("Risk: notional limit exceeded for client " +
                 std::to_string(order->client_id));
        return false;
    }
    state.notional_exposure += notional;

    return true;
}

} // namespace rtes
