/**
 * @file exchange.cpp
 * @brief Core exchange implementation - coordinates all trading components
 * 
 * The Exchange class is the central coordinator that:
 * - Manages order pool for memory efficiency
 * - Initializes risk manager for pre-trade validation
 * - Creates per-symbol matching engines
 * - Sets up market data distribution queues
 * - Wires components together for data flow
 */

#include "rtes/exchange.hpp"
#include "rtes/logger.hpp"

namespace rtes {

/**
 * @brief Exchange constructor - initializes all components
 * @param config Configuration object (ownership transferred)
 * 
 * Initialization order is critical:
 * 1. Order pool (memory allocation)
 * 2. Risk manager (validation)
 * 3. Matching engines (per symbol)
 * 4. Market data queue (distribution)
 * 5. Wire components (connect data flow)
 */
Exchange::Exchange(std::unique_ptr<Config> config) 
    : config_(std::move(config)) {
    
    initialize_order_pool();
    initialize_risk_manager();
    initialize_matching_engines();
    initialize_market_data();
    wire_components();
}

Exchange::~Exchange() {
    stop();
}

/**
 * @brief Start all exchange components
 * 
 * Starts components in order:
 * 1. Risk manager (validates orders before matching)
 * 2. Matching engines (one per symbol)
 * 
 * Each component starts its own worker thread
 */
void Exchange::start() {
    LOG_INFO("Starting exchange: " + config_->exchange.name);
    
    // Start risk manager first (validates orders before matching)
    risk_manager_->start();
    
    // Start all matching engines (one per symbol)
    for (auto& [symbol, engine] : matching_engines_) {
        engine->start();
    }
    
    LOG_INFO("Exchange started successfully");
}

/**
 * @brief Stop all exchange components gracefully
 * 
 * Stops components in reverse order:
 * 1. Matching engines (stop processing orders)
 * 2. Risk manager (stop validation)
 * 
 * Each component drains its queue before stopping
 */
void Exchange::stop() {
    LOG_INFO("Stopping exchange");
    
    // Stop matching engines first (stop processing new orders)
    for (auto& [symbol, engine] : matching_engines_) {
        engine->stop();
    }
    
    // Stop risk manager last (after all orders processed)
    if (risk_manager_) {
        risk_manager_->stop();
    }
    
    LOG_INFO("Exchange stopped");
}

MatchingEngine* Exchange::get_matching_engine(const std::string& symbol) {
    auto it = matching_engines_.find(symbol);
    return (it != matching_engines_.end()) ? it->second.get() : nullptr;
}

/**
 * @brief Initialize order pool for memory management
 * 
 * Pre-allocates memory for orders to avoid allocations in hot path.
 * Typical size: 1M orders for high-throughput trading.
 */
void Exchange::initialize_order_pool() {
    size_t pool_size = config_->performance.order_pool_size;
    order_pool_ = std::make_unique<OrderPool>(pool_size);
    
    LOG_INFO("Initialized order pool with " + std::to_string(pool_size) + " orders");
}

/**
 * @brief Initialize risk manager for pre-trade validation
 * 
 * Risk manager validates:
 * - Order size limits
 * - Price collars
 * - Credit limits per client
 * - Rate limits
 */
void Exchange::initialize_risk_manager() {
    risk_manager_ = std::make_unique<RiskManager>(config_->risk, config_->symbols);
    
    LOG_INFO("Initialized risk manager");
}

/**
 * @brief Initialize matching engines (one per symbol)
 * 
 * Each matching engine:
 * - Maintains its own order book
 * - Runs in dedicated thread
 * - Implements price-time priority matching
 */
void Exchange::initialize_matching_engines() {
    for (const auto& symbol_config : config_->symbols) {
        auto engine = std::make_unique<MatchingEngine>(symbol_config.symbol, *order_pool_);
        matching_engines_[symbol_config.symbol] = std::move(engine);
        
        LOG_INFO("Initialized matching engine for symbol: " + symbol_config.symbol);
    }
}

/**
 * @brief Initialize market data queue for distribution
 * 
 * MPMC queue allows multiple matching engines to publish
 * market data events (trades, BBO updates) to UDP publisher.
 */
void Exchange::initialize_market_data() {
    size_t queue_capacity = config_->performance.queue_capacity;
    market_data_queue_ = std::make_unique<MPMCQueue<MarketDataEvent>>(queue_capacity);
    
    LOG_INFO("Initialized market data queue with capacity: " + std::to_string(queue_capacity));
}

/**
 * @brief Wire components together for data flow
 * 
 * Data flow:
 * 1. Risk manager -> Matching engines (validated orders)
 * 2. Matching engines -> Market data queue (trades, BBO)
 * 3. Market data queue -> UDP publisher (multicast)
 */
void Exchange::wire_components() {
    // Connect risk manager to matching engines for order routing
    for (auto& [symbol, engine] : matching_engines_) {
        risk_manager_->add_matching_engine(symbol, engine.get());
        engine->set_market_data_queue(market_data_queue_.get());
    }
    
    LOG_INFO("Wired components together");
}

} // namespace rtes