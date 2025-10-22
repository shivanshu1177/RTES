#pragma once

#include "rtes/config.hpp"
#include "rtes/memory_pool.hpp"
#include "rtes/risk_manager.hpp"
#include "rtes/matching_engine.hpp"
#include "rtes/mpmc_queue.hpp"
#include <memory>
#include <unordered_map>

namespace rtes {

class Exchange {
public:
    explicit Exchange(std::unique_ptr<Config> config);
    ~Exchange();
    
    void start();
    void stop();
    
    // Component access for testing
    RiskManager* get_risk_manager() { return risk_manager_.get(); }
    OrderPool* get_order_pool() { return order_pool_.get(); }
    MPMCQueue<MarketDataEvent>* get_market_data_queue() { return market_data_queue_.get(); }
    MatchingEngine* get_matching_engine(const std::string& symbol);

private:
    std::unique_ptr<Config> config_;
    
    // Core components
    std::unique_ptr<OrderPool> order_pool_;
    std::unique_ptr<RiskManager> risk_manager_;
    std::unordered_map<std::string, std::unique_ptr<MatchingEngine>> matching_engines_;
    
    // Market data
    std::unique_ptr<MPMCQueue<MarketDataEvent>> market_data_queue_;
    
    // Initialize components
    void initialize_order_pool();
    void initialize_risk_manager();
    void initialize_matching_engines();
    void initialize_market_data();
    
    // Connect components
    void wire_components();
};

} // namespace rtes