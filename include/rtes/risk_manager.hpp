#pragma once

#include "rtes/types.hpp"
#include "rtes/config.hpp"
#include "rtes/spsc_queue.hpp"
#include "rtes/matching_engine.hpp"
#include <thread>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <chrono>

namespace rtes {

enum class RiskResult {
    APPROVED,
    REJECTED_SIZE,
    REJECTED_PRICE,
    REJECTED_CREDIT,
    REJECTED_RATE_LIMIT,
    REJECTED_DUPLICATE,
    REJECTED_SYMBOL,
    REJECTED_OWNERSHIP
};

struct ClientRiskState {
    double notional_exposure{0.0};
    uint32_t order_count_last_second{0};
    std::chrono::steady_clock::time_point last_reset_time;
    std::unordered_set<OrderID> active_orders;
};

struct RiskRequest {
    enum Type { NEW_ORDER, CANCEL_ORDER } type;
    Order* order;  // For NEW_ORDER
    OrderID order_id;  // For CANCEL_ORDER
    ClientID client_id;  // For ownership verification
};

class RiskManager {
public:
    explicit RiskManager(const RiskConfig& config, const std::vector<SymbolConfig>& symbols);
    ~RiskManager();
    
    void start();
    void stop();
    
    // Thread-safe order submission from gateway
    bool submit_order(Order* order);
    bool submit_cancel(OrderID order_id, ClientID client_id);
    
    // Connect to matching engines
    void add_matching_engine(const std::string& symbol, MatchingEngine* engine);
    
    // Statistics
    uint64_t orders_processed() const { return orders_processed_.load(); }
    uint64_t orders_rejected() const { return orders_rejected_.load(); }

private:
    RiskConfig config_;
    std::unordered_map<std::string, SymbolConfig> symbol_configs_;
    
    // Threading
    std::thread worker_thread_;
    std::atomic<bool> running_{false};
    
    // Input queue (SPSC from gateway)
    std::unique_ptr<SPSCQueue<RiskRequest>> input_queue_;
    
    // Output to matching engines
    std::unordered_map<std::string, MatchingEngine*> matching_engines_;
    
    // Risk state (confined to risk thread)
    std::unordered_map<ClientID, ClientRiskState> client_states_;
    
    // Statistics
    std::atomic<uint64_t> orders_processed_{0};
    std::atomic<uint64_t> orders_rejected_{0};
    
    // Worker thread main loop
    void run();
    
    // Risk checks
    RiskResult validate_new_order(Order* order);
    RiskResult validate_cancel_order(OrderID order_id, ClientID client_id);
    
    // Individual risk checks
    bool check_order_size(const Order* order) const;
    bool check_price_collar(const Order* order) const;
    bool check_credit_limit(const Order* order, ClientRiskState& state) const;
    bool check_rate_limit(ClientRiskState& state);
    bool check_duplicate_order(const Order* order, const ClientRiskState& state) const;
    bool check_symbol_allowed(const Order* order) const;
    bool check_cancel_ownership(OrderID order_id, ClientID client_id) const;
    
    // State management
    void update_client_state(Order* order, ClientRiskState& state);
    void remove_from_client_state(OrderID order_id, ClientID client_id);
    
    // Helper methods
    double calculate_notional(const Order* order) const;
    const SymbolConfig* get_symbol_config(const std::string& symbol) const;
};

} // namespace rtes