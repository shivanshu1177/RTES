#pragma once

#include "rtes/types.hpp"
#include "rtes/config.hpp"
#include "rtes/protocol.hpp"
#include "rtes/security_utils.hpp"
#include "rtes/memory_pool.hpp"
#include "rtes/spsc_queue.hpp"
#include <thread>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

namespace rtes {

// Forward declaration
class MatchingEngine;
struct OrderRequest;

class RiskManager {
public:
    RiskManager(const RiskConfig& risk, const std::vector<SymbolConfig>& symbols);
    ~RiskManager();

    void start();
    void stop();

    // Wire in a matching engine for a specific symbol (called before start())
    void add_matching_engine(const std::string& symbol, MatchingEngine* engine);

    // Wire in the order pool so rejected orders can be returned (called before start())
    void set_order_pool(OrderPool* pool);

    // Quick pre-queue validation (called from TCP gateway thread before submit)
    // Returns false if the order is obviously invalid (e.g. unknown symbol)
    bool validate_order(const NewOrderMessage& msg, AuthContext& ctx) noexcept;

    // Thread-safe: enqueue order for full risk validation + routing
    // Ownership of order* passes to RiskManager; it will release to pool on rejection
    bool submit_order(Order* order);

    uint64_t orders_accepted() const { return orders_accepted_.load(std::memory_order_relaxed); }
    uint64_t orders_rejected() const { return orders_rejected_.load(std::memory_order_relaxed); }

private:
    struct ClientRiskState {
        uint32_t order_count_last_second{0};
        std::chrono::steady_clock::time_point last_reset_time{std::chrono::steady_clock::now()};
        double notional_exposure{0.0};
        std::unordered_set<uint64_t> active_order_ids;
    };

    RiskConfig risk_config_;
    std::unordered_set<std::string> allowed_symbols_;
    std::unordered_map<std::string, double> price_collar_pct_; // per symbol
    std::unordered_map<std::string, uint64_t> reference_prices_; // fixed-point

    std::unordered_map<std::string, MatchingEngine*> engines_;
    OrderPool* pool_{nullptr};

    std::unique_ptr<SPSCQueue<Order*>> input_queue_;
    std::thread worker_thread_;
    std::atomic<bool> running_{false};

    std::unordered_map<uint64_t, ClientRiskState> client_states_; // only accessed by worker

    std::atomic<uint64_t> orders_accepted_{0};
    std::atomic<uint64_t> orders_rejected_{0};

    void run();
    bool do_validate(Order* order);
};

} // namespace rtes
