#include "rtes/exchange.hpp"
#include "rtes/logger.hpp"

namespace rtes {

// ═══════════════════════════════════════════════════════════════
//  Construction
// ═══════════════════════════════════════════════════════════════

Exchange::Exchange(std::unique_ptr<Config> config)
    : config_(std::move(config))
{
    if (!config_) {
        throw std::runtime_error("Exchange: null config");
    }

    LOG_INFO("Initializing exchange '{}'", config_->exchange.name);

    // Order matters: dependencies must be created first
    initialize_order_pool();
    initialize_market_data_queue();
    initialize_matching_engines();
    initialize_risk_manager();
    wire_components();

    LOG_INFO("Exchange initialized: {} symbols, pool capacity {}",
             matching_engines_.size(),
             order_pool_->capacity());
}

Exchange::~Exchange() {
    if (state_ == ExchangeState::RUNNING ||
        state_ == ExchangeState::STARTING) {
        stop();
    }
}

// ═══════════════════════════════════════════════════════════════
//  Lifecycle
// ═══════════════════════════════════════════════════════════════

void Exchange::start() {
    if (state_ != ExchangeState::CREATED) {
        throw std::logic_error(
            "Exchange::start() called in invalid state");
    }

    state_ = ExchangeState::STARTING;
    LOG_INFO("Starting exchange components");

    // Start in dependency order:
    // 1. Matching engines (must be ready before risk routes to them)
    for (auto& [symbol, engine] : matching_engines_) {
        engine->start();
        LOG_INFO("  Started matching engine for {}", symbol.c_str());
    }

    // 2. Risk manager (routes to running matching engines)
    risk_manager_->start();
    LOG_INFO("  Started risk manager");

    state_ = ExchangeState::RUNNING;
    LOG_INFO("Exchange is RUNNING");
}

void Exchange::stop() {
    if (state_ != ExchangeState::RUNNING &&
        state_ != ExchangeState::STARTING) {
        return;  // Already stopped or never started
    }

    state_ = ExchangeState::STOPPING;
    LOG_INFO("Stopping exchange components");

    // Stop in reverse dependency order:
    // 1. Risk manager (stop accepting new orders)
    risk_manager_->stop();
    LOG_INFO("  Stopped risk manager");

    // 2. Matching engines (drain remaining orders)
    for (auto& [symbol, engine] : matching_engines_) {
        engine->stop();
        LOG_INFO("  Stopped matching engine for {}", symbol.c_str());
    }

    state_ = ExchangeState::STOPPED;
    LOG_INFO("Exchange is STOPPED");
}

// ═══════════════════════════════════════════════════════════════
//  Initialization
// ═══════════════════════════════════════════════════════════════

void Exchange::initialize_order_pool() {
    const size_t capacity = config_->performance.order_pool_size;
    order_pool_ = std::make_unique<OrderPool>(capacity);
    LOG_INFO("Order pool initialized: {} slots", capacity);
}

void Exchange::initialize_market_data_queue() {
    const size_t capacity = config_->performance.market_data_queue_size;
    market_data_queue_ = std::make_unique<MPMCQueue<MarketDataEvent>>(capacity);
    LOG_INFO("Market data queue initialized: {} slots", capacity);
}

void Exchange::initialize_matching_engines() {
    for (const auto& sym_config : config_->symbols) {
        Symbol symbol(sym_config.symbol.c_str());

        auto engine = std::make_unique<MatchingEngine>(
            sym_config.symbol, *order_pool_);

        matching_engines_[symbol] = std::move(engine);

        LOG_INFO("Matching engine created for {}", sym_config.symbol);
    }
}

void Exchange::initialize_risk_manager() {
    risk_manager_ = std::make_unique<RiskManager>(
        config_->risk, config_->symbols);
}

void Exchange::wire_components() {
    // Wire matching engines to market data queue
    for (auto& [symbol, engine] : matching_engines_) {
        engine->set_market_data_queue(market_data_queue_.get());
    }

    // Wire risk manager to matching engines
    for (auto& [symbol, engine] : matching_engines_) {
        risk_manager_->add_matching_engine(
            std::string(symbol.c_str()), engine.get());
    }

    LOG_INFO("Components wired: {} engines → market data queue, "
             "risk manager → {} engines",
             matching_engines_.size(), matching_engines_.size());
}

// ═══════════════════════════════════════════════════════════════
//  Monitoring
// ═══════════════════════════════════════════════════════════════

ExchangeHealth Exchange::get_health() const {
    ExchangeHealth health;
    health.state = state_;
    health.overall_healthy = (state_ == ExchangeState::RUNNING);

    // Risk manager health
    if (risk_manager_) {
        auto stats = risk_manager_->get_stats();
        health.components.push_back({
            .name = "risk_manager",
            .healthy = (state_ == ExchangeState::RUNNING),
            .detail = "processed=" + std::to_string(stats.processed),
        });
    }

    // Per-engine health
    for (const auto& [symbol, engine] : matching_engines_) {
        bool engine_healthy = engine->is_running();
        auto stats = engine->get_stats();

        health.components.push_back({
            .name = "matching_engine_" + std::string(symbol.c_str()),
            .healthy = engine_healthy,
            .detail = "orders=" + std::to_string(stats.orders_accepted) +
                      " trades=" + std::to_string(stats.trades_executed),
        });

        if (!engine_healthy) {
            health.overall_healthy = false;
        }
    }

    // Order pool health
    if (order_pool_) {
        auto pool_stats = order_pool_->get_stats();
        bool pool_healthy = pool_stats.utilization < 0.95;

        health.components.push_back({
            .name = "order_pool",
            .healthy = pool_healthy,
            .detail = "utilization=" +
                      std::to_string(pool_stats.utilization * 100) + "%",
        });

        if (!pool_healthy) {
            health.overall_healthy = false;
        }
    }

    return health;
}

ExchangeStats Exchange::get_stats() const {
    ExchangeStats stats;

    // Aggregate risk manager stats
    if (risk_manager_) {
        auto risk_stats = risk_manager_->get_stats();
        stats.total_orders_processed = risk_stats.processed;
        stats.total_orders_approved  = risk_stats.approved;
        stats.total_orders_rejected  = risk_stats.rejected;
        stats.total_cancels =
            risk_stats.cancels_accepted + risk_stats.cancels_rejected;
    }

    // Aggregate matching engine stats
    for (const auto& [symbol, engine] : matching_engines_) {
        auto eng_stats = engine->get_stats();

        stats.total_trades_executed += eng_stats.trades_executed;
        stats.market_data_drops    += eng_stats.md_drops;

        stats.engines.push_back({
            .symbol           = symbol,
            .orders_processed = eng_stats.orders_accepted,
            .trades_executed  = eng_stats.trades_executed,
        });
    }

    // Market data queue stats
    if (market_data_queue_) {
        stats.market_data_queue_depth = market_data_queue_->size_approx();
    }

    // Order pool stats
    if (order_pool_) {
        auto pool_stats = order_pool_->get_stats();
        stats.order_pool_capacity    = pool_stats.capacity;
        stats.order_pool_allocated   = pool_stats.allocated;
        stats.order_pool_high_water  = pool_stats.high_water_mark;
        stats.order_pool_utilization = pool_stats.utilization;
    }

    return stats;
}

} // namespace rtes