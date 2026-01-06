#pragma once

/**
 * @file exchange.hpp
 * @brief Exchange orchestrator — owns and wires all components
 *
 * The Exchange class is the top-level container that:
 *   1. Owns all component lifetimes (pool, risk, engines, queues)
 *   2. Wires components together (queues, callbacks, references)
 *   3. Manages lifecycle (start/stop in correct order)
 *   4. Exposes health, stats, and config for monitoring
 *
 * Ownership hierarchy:
 *   Exchange owns:
 *     ├── Config (moved in at construction)
 *     ├── OrderPool (pre-allocated memory for orders)
 *     ├── RiskManager (single instance, single thread)
 *     ├── MatchingEngine[] (one per symbol, one thread each)
 *     └── MPMCQueue<MarketDataEvent> (engines → UDP publisher)
 *
 * Thread model:
 *   Exchange itself is NOT thread-safe.
 *   start()/stop() must be called from the main thread.
 *   Component accessors return pointers used by their respective threads:
 *     - Gateway thread uses: risk_manager, order_pool
 *     - Publisher thread uses: market_data_queue
 *     - Monitoring thread uses: get_stats(), get_health()
 *
 * Lifecycle state machine:
 *   CREATED → STARTING → RUNNING → STOPPING → STOPPED
 *   Only forward transitions allowed. No restart.
 */

#include "rtes/config.hpp"
#include "rtes/memory_pool.hpp"
#include "rtes/risk_manager.hpp"
#include "rtes/matching_engine.hpp"
#include "rtes/mpmc_queue.hpp"
#include "rtes/types.hpp"

#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>

namespace rtes {

// ═══════════════════════════════════════════════════════════════
//  Lifecycle State
// ═══════════════════════════════════════════════════════════════

enum class ExchangeState : uint8_t {
    CREATED  = 0,   // Constructed, not yet started
    STARTING = 1,   // Components being initialized
    RUNNING  = 2,   // All components active
    STOPPING = 3,   // Shutdown in progress
    STOPPED  = 4,   // All components stopped
};

// ═══════════════════════════════════════════════════════════════
//  Health & Statistics
// ═══════════════════════════════════════════════════════════════

/**
 * Per-component health status.
 */
struct ComponentHealth {
    std::string name;
    bool        healthy{false};
    std::string detail;
};

/**
 * Exchange-wide health snapshot.
 */
struct ExchangeHealth {
    bool                          overall_healthy{false};
    ExchangeState                 state{ExchangeState::CREATED};
    std::vector<ComponentHealth>  components;
};

/**
 * Exchange-wide statistics (aggregated from all components).
 */
struct ExchangeStats {
    // Order flow
    uint64_t total_orders_processed{0};
    uint64_t total_orders_approved{0};
    uint64_t total_orders_rejected{0};
    uint64_t total_trades_executed{0};
    uint64_t total_cancels{0};

    // Market data
    uint64_t market_data_events{0};
    uint64_t market_data_drops{0};
    size_t   market_data_queue_depth{0};

    // Memory
    size_t   order_pool_capacity{0};
    size_t   order_pool_allocated{0};
    size_t   order_pool_high_water{0};
    double   order_pool_utilization{0.0};

    // Per-symbol engine stats
    struct EngineStats {
        Symbol   symbol;
        uint64_t orders_processed{0};
        uint64_t trades_executed{0};
    };
    std::vector<EngineStats> engines;
};

// ═══════════════════════════════════════════════════════════════
//  Exchange
// ═══════════════════════════════════════════════════════════════

class Exchange {
public:
    /**
     * Construct exchange from configuration.
     * Takes ownership of config. All components are created
     * but NOT started (threads not spawned until start()).
     *
     * @param config Unique pointer to validated configuration
     * @throws std::runtime_error if component initialization fails
     */
    explicit Exchange(std::unique_ptr<Config> config);

    /**
     * Destructor — stops all components if still running.
     */
    ~Exchange();

    // Non-copyable, non-movable (owns threads and shared state)
    Exchange(const Exchange&) = delete;
    Exchange& operator=(const Exchange&) = delete;
    Exchange(Exchange&&) = delete;
    Exchange& operator=(Exchange&&) = delete;

    // ═══════════════════════════════════════════════════════
    //  Lifecycle
    // ═══════════════════════════════════════════════════════

    /**
     * Start all components in dependency order:
     *   1. OrderPool (already ready — pre-allocated)
     *   2. MatchingEngines (spawn per-symbol threads)
     *   3. RiskManager (spawn validation thread)
     *
     * @throws std::runtime_error if any component fails to start
     * @throws std::logic_error if not in CREATED state
     */
    void start();

    /**
     * Stop all components in reverse order:
     *   1. RiskManager (stop accepting new orders)
     *   2. MatchingEngines (drain and stop)
     *   3. OrderPool (no action needed)
     *
     * Safe to call multiple times (idempotent after first call).
     */
    void stop();

    /**
     * Current lifecycle state.
     */
    [[nodiscard]] ExchangeState state() const { return state_; }

    /**
     * Whether the exchange is in RUNNING state.
     */
    [[nodiscard]] bool is_running() const {
        return state_ == ExchangeState::RUNNING;
    }

    // ═══════════════════════════════════════════════════════
    //  Configuration Access
    // ═══════════════════════════════════════════════════════

    /**
     * Access to the owned configuration.
     * Safe to call from any thread (Config is immutable after construction).
     *
     * Used by main.cpp to read port numbers etc. after moving
     * ownership into Exchange.
     */
    [[nodiscard]] const Config& get_config() const {
        return *config_;
    }

    // ═══════════════════════════════════════════════════════
    //  Component Access (for wiring external components)
    // ═══════════════════════════════════════════════════════

    /**
     * Risk manager pointer. Used by TcpGateway to submit orders.
     * @pre state >= CREATED
     */
    [[nodiscard]] RiskManager* get_risk_manager() {
        return risk_manager_.get();
    }

    [[nodiscard]] const RiskManager* get_risk_manager() const {
        return risk_manager_.get();
    }

    /**
     * Order pool pointer. Used by TcpGateway to allocate orders.
     * @pre state >= CREATED
     */
    [[nodiscard]] OrderPool* get_order_pool() {
        return order_pool_.get();
    }

    [[nodiscard]] const OrderPool* get_order_pool() const {
        return order_pool_.get();
    }

    /**
     * Market data queue. Used by UdpPublisher to consume events.
     * @pre state >= CREATED
     */
    [[nodiscard]] MPMCQueue<MarketDataEvent>* get_market_data_queue() {
        return market_data_queue_.get();
    }

    [[nodiscard]] const MPMCQueue<MarketDataEvent>* get_market_data_queue() const {
        return market_data_queue_.get();
    }

    /**
     * Matching engine for a specific symbol.
     * Zero allocation: uses Symbol (FixedString) key directly.
     *
     * @param symbol Symbol to look up
     * @return Engine pointer, or nullptr if symbol not found
     */
    [[nodiscard]] MatchingEngine* get_matching_engine(const Symbol& symbol) {
        auto it = matching_engines_.find(symbol);
        return (it != matching_engines_.end()) ? it->second.get() : nullptr;
    }

    [[nodiscard]] const MatchingEngine* get_matching_engine(const Symbol& symbol) const {
        auto it = matching_engines_.find(symbol);
        return (it != matching_engines_.end()) ? it->second.get() : nullptr;
    }

    /**
     * Overload accepting C-string (convenience for tests/CLI).
     * Constructs a Symbol from the string — small stack allocation only.
     */
    [[nodiscard]] MatchingEngine* get_matching_engine(const char* symbol) {
        return get_matching_engine(Symbol(symbol));
    }

    /**
     * Get list of all configured symbols.
     */
    [[nodiscard]] std::vector<Symbol> get_symbols() const {
        std::vector<Symbol> result;
        result.reserve(matching_engines_.size());
        for (const auto& [sym, engine] : matching_engines_) {
            result.push_back(sym);
        }
        return result;
    }

    /**
     * Number of configured symbols.
     */
    [[nodiscard]] size_t symbol_count() const {
        return matching_engines_.size();
    }

    // ═══════════════════════════════════════════════════════
    //  Monitoring & Observability
    // ═══════════════════════════════════════════════════════

    /**
     * Get exchange-wide health status.
     * Checks all components and returns aggregate health.
     * Safe to call from monitoring thread.
     */
    [[nodiscard]] ExchangeHealth get_health() const;

    /**
     * Get exchange-wide statistics.
     * Aggregates stats from all matching engines, risk manager, and pool.
     * Safe to call from monitoring thread.
     *
     * All values are eventually consistent (components flush periodically).
     */
    [[nodiscard]] ExchangeStats get_stats() const;

private:
    // ═══════════════════════════════════════════════════════
    //  Owned State
    // ═══════════════════════════════════════════════════════

    /** Configuration (immutable after construction) */
    std::unique_ptr<Config> config_;

    /** Lifecycle state (main thread only — not atomic) */
    ExchangeState state_{ExchangeState::CREATED};

    // ═══════════════════════════════════════════════════════
    //  Core Components (owned, created in constructor)
    // ═══════════════════════════════════════════════════════

    /** Pre-allocated order memory pool */
    std::unique_ptr<OrderPool> order_pool_;

    /** Pre-trade risk validation */
    std::unique_ptr<RiskManager> risk_manager_;

    /** Per-symbol matching engines */
    std::unordered_map<Symbol,
                       std::unique_ptr<MatchingEngine>,
                       Symbol::Hash> matching_engines_;

    /** Market data queue (matching engines → UDP publisher) */
    std::unique_ptr<MPMCQueue<MarketDataEvent>> market_data_queue_;

    // ═══════════════════════════════════════════════════════
    //  Initialization (called from constructor)
    // ═══════════════════════════════════════════════════════

    /**
     * Initialize order pool with configured capacity.
     * Pre-allocates all memory upfront.
     */
    void initialize_order_pool();

    /**
     * Initialize market data queue.
     * Must be created before matching engines (they publish to it).
     */
    void initialize_market_data_queue();

    /**
     * Initialize per-symbol matching engines.
     * One engine per configured symbol, each with its own thread (on start).
     * Engines are wired to the market data queue.
     */
    void initialize_matching_engines();

    /**
     * Initialize risk manager.
     * Must be created after matching engines (routes orders to them).
     * Wired to all matching engines for order routing.
     */
    void initialize_risk_manager();

    /**
     * Wire components together:
     *   - Matching engines → market data queue
     *   - Risk manager → matching engines
     *   - Reference price feedback loop
     */
    void wire_components();
};

} // namespace rtes