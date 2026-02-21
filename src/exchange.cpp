#include "rtes/exchange.hpp"
#include "rtes/logger.hpp"

namespace rtes {

Exchange::Exchange(std::unique_ptr<Config> config)
    : config_(std::move(config))
{
    initialize_order_pool();
    initialize_risk_manager();
    initialize_matching_engines();
    initialize_market_data();
    wire_components();
}

Exchange::~Exchange() { stop(); }

void Exchange::initialize_order_pool() {
    order_pool_ = std::make_unique<OrderPool>(config_->performance.order_pool_size);
    LOG_INFO("Exchange: order pool initialised (" +
             std::to_string(config_->performance.order_pool_size) + " slots)");
}

void Exchange::initialize_risk_manager() {
    risk_manager_ = std::make_unique<RiskManager>(config_->risk, config_->symbols);
    LOG_INFO("Exchange: risk manager initialised");
}

void Exchange::initialize_matching_engines() {
    for (const auto& sym : config_->symbols) {
        matching_engines_[sym.symbol] =
            std::make_unique<MatchingEngine>(sym.symbol, *order_pool_);
        LOG_INFO("Exchange: matching engine initialised for " + sym.symbol);
    }
}

void Exchange::initialize_market_data() {
    market_data_queue_ =
        std::make_unique<MPMCQueue<MarketDataEvent>>(config_->performance.queue_capacity);
    LOG_INFO("Exchange: market data queue initialised (capacity=" +
             std::to_string(config_->performance.queue_capacity) + ")");
}

void Exchange::wire_components() {
    risk_manager_->set_order_pool(order_pool_.get());
    for (auto& [sym, engine] : matching_engines_) {
        risk_manager_->add_matching_engine(sym, engine.get());
        engine->set_market_data_queue(market_data_queue_.get());
    }
    LOG_INFO("Exchange: components wired");
}

void Exchange::start() {
    LOG_INFO("Exchange: starting threads");
    risk_manager_->start();
    for (auto& [sym, engine] : matching_engines_) {
        engine->start();
        LOG_INFO("Exchange: matching engine started for " + sym);
    }
    LOG_INFO("Exchange: ready");
}

void Exchange::stop() {
    LOG_INFO("Exchange: stopping");
    for (auto& [sym, engine] : matching_engines_) engine->stop();
    risk_manager_->stop();
    LOG_INFO("Exchange: stopped");
}

} // namespace rtes
