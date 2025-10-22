#include "rtes/exchange.hpp"
#include "rtes/logger.hpp"

namespace rtes {

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

void Exchange::start() {
    LOG_INFO("Starting exchange: " + config_->exchange.name);
    
    // Start risk manager
    risk_manager_->start();
    
    // Start matching engines
    for (auto& [symbol, engine] : matching_engines_) {
        engine->start();
    }
    
    LOG_INFO("Exchange started successfully");
}

void Exchange::stop() {
    LOG_INFO("Stopping exchange");
    
    // Stop matching engines
    for (auto& [symbol, engine] : matching_engines_) {
        engine->stop();
    }
    
    // Stop risk manager
    if (risk_manager_) {
        risk_manager_->stop();
    }
    
    LOG_INFO("Exchange stopped");
}

MatchingEngine* Exchange::get_matching_engine(const std::string& symbol) {
    auto it = matching_engines_.find(symbol);
    return (it != matching_engines_.end()) ? it->second.get() : nullptr;
}

void Exchange::initialize_order_pool() {
    size_t pool_size = config_->performance.order_pool_size;
    order_pool_ = std::make_unique<OrderPool>(pool_size);
    
    LOG_INFO("Initialized order pool with " + std::to_string(pool_size) + " orders");
}

void Exchange::initialize_risk_manager() {
    risk_manager_ = std::make_unique<RiskManager>(config_->risk, config_->symbols);
    
    LOG_INFO("Initialized risk manager");
}

void Exchange::initialize_matching_engines() {
    for (const auto& symbol_config : config_->symbols) {
        auto engine = std::make_unique<MatchingEngine>(symbol_config.symbol, *order_pool_);
        matching_engines_[symbol_config.symbol] = std::move(engine);
        
        LOG_INFO("Initialized matching engine for symbol: " + symbol_config.symbol);
    }
}

void Exchange::initialize_market_data() {
    size_t queue_capacity = config_->performance.queue_capacity;
    market_data_queue_ = std::make_unique<MPMCQueue<MarketDataEvent>>(queue_capacity);
    
    LOG_INFO("Initialized market data queue with capacity: " + std::to_string(queue_capacity));
}

void Exchange::wire_components() {
    // Connect risk manager to matching engines
    for (auto& [symbol, engine] : matching_engines_) {
        risk_manager_->add_matching_engine(symbol, engine.get());
        engine->set_market_data_queue(market_data_queue_.get());
    }
    
    LOG_INFO("Wired components together");
}

} // namespace rtes