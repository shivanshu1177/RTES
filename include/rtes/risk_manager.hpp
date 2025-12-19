#pragma once

#include "rtes/types.hpp"
#include "rtes/config.hpp"
#include "rtes/spsc_queue.hpp"
#include "rtes/matching_engine.hpp"

#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <thread>
#include <memory>
#include <vector>

namespace rtes {

// ═══════════════════════════════════════════════════════════════
//  Risk Configuration
// ═══════════════════════════════════════════════════════════════



// ═══════════════════════════════════════════════════════════════
//  Risk Results
// ═══════════════════════════════════════════════════════════════

enum class RiskResult : uint8_t {
    APPROVED             = 0,
    REJECTED_SIZE        = 1,
    REJECTED_PRICE       = 2,
    REJECTED_CREDIT      = 3,
    REJECTED_RATE_LIMIT  = 4,
    REJECTED_DUPLICATE   = 5,
    REJECTED_SYMBOL      = 6,
    REJECTED_OWNERSHIP   = 7,
    REJECTED_QUEUE_FULL  = 8,
};

// ═══════════════════════════════════════════════════════════════
//  Risk Request (input via SPSC queue)
// ═══════════════════════════════════════════════════════════════

struct RiskRequest {
    enum Type : uint8_t {
        NEW_ORDER    = 0,
        CANCEL_ORDER = 1,
    };

    Type type;

    union {
        Order* order;  // NEW_ORDER
        struct {
            OrderID  order_id;
            ClientID client_id;
        } cancel;

    };

    RiskRequest() : type(NEW_ORDER), order(nullptr) {}
};

// ═══════════════════════════════════════════════════════════════
//  Per-Client Risk State
// ═══════════════════════════════════════════════════════════════

struct ClientRiskState {
    std::unordered_set<OrderID> active_orders;  // For dup check + ownership
    uint64_t  notional_exposure_scaled{0};       // Integer notional
    uint32_t  orders_in_window{0};               // Current window count
    Timestamp window_start_ns{0};                // Rate limit window start
};

// ═══════════════════════════════════════════════════════════════
//  Risk Manager
// ═══════════════════════════════════════════════════════════════

class RiskManager {
public:
    RiskManager(const RiskConfig& config,
                const std::vector<SymbolConfig>& symbols);
    ~RiskManager();

    RiskManager(const RiskManager&) = delete;
    RiskManager& operator=(const RiskManager&) = delete;

    // ── Lifecycle ──
    void start();
    void stop();

    // ── Order submission (from gateway thread) ──
    [[nodiscard]] bool submit_order(Order* order);
    [[nodiscard]] bool submit_cancel(OrderID order_id, ClientID client_id);

    // ── Configuration ──
    void add_matching_engine(const std::string& symbol, MatchingEngine* engine);
    void update_reference_price(const Symbol& symbol, Price price);

    // ── Statistics ──
    struct Stats {
        uint64_t processed;
        uint64_t approved;
        uint64_t rejected;
        uint64_t cancels_accepted;
        uint64_t cancels_rejected;
    };

    [[nodiscard]] Stats get_stats() const {
        return Stats{
            .processed        = stats_atomic_.processed.load(std::memory_order_relaxed),
            .approved         = stats_atomic_.approved.load(std::memory_order_relaxed),
            .rejected         = stats_atomic_.rejected.load(std::memory_order_relaxed),
            .cancels_accepted = stats_atomic_.cancels_accepted.load(std::memory_order_relaxed),
            .cancels_rejected = stats_atomic_.cancels_rejected.load(std::memory_order_relaxed),
        };
    }

private:
    // ── Configuration (read-only after construction) ──
    RiskConfig config_;
    uint64_t   max_notional_scaled_{0};  // Pre-computed integer limit

    // ── Symbol data ──
    std::unordered_map<Symbol, SymbolConfig, Symbol::Hash>   symbol_configs_;
    std::unordered_map<Symbol, MatchingEngine*, Symbol::Hash> matching_engines_;
    std::unordered_map<Symbol, Price, Symbol::Hash>          reference_prices_;

    // ── Client data ──
    std::unordered_map<ClientID, ClientRiskState, ClientID::Hash> client_states_;

    // ── Order→Symbol index (for targeted cancel routing) ──
    std::unordered_map<OrderID, Symbol> order_symbol_index_;

    // ── Input queue ──
    std::unique_ptr<SPSCQueue<RiskRequest>> input_queue_;

    // ── Threading ──
    std::thread worker_thread_;
    std::atomic<bool> running_{false};

    // ── Local statistics (no atomics in hot path) ──
    struct LocalStats {
        size_t processed{0};
        size_t approved{0};
        size_t rejected{0};
        size_t cancels_accepted{0};
        size_t cancels_rejected{0};
    };
    LocalStats local_stats_;

    // ── Atomic statistics (read by monitoring) ──
    struct AtomicStats {
        std::atomic<uint64_t> processed{0};
        std::atomic<uint64_t> approved{0};
        std::atomic<uint64_t> rejected{0};
        std::atomic<uint64_t> cancels_accepted{0};
        std::atomic<uint64_t> cancels_rejected{0};
    };
    AtomicStats stats_atomic_;

    // ── Worker internals ──
    void run();
    size_t drain_batch();
    void process_new_order(Order* order);
    void process_cancel(OrderID order_id, ClientID client_id);

    // ── Risk checks ──
    bool check_price_collar_int(const Order* order,
                                 const SymbolConfig& sym_config) const;
    bool check_rate_limit(ClientRiskState& client);
    uint64_t calculate_notional_int(const Order* order) const;
    ClientRiskState& get_or_create_client(const ClientID& id);

    // ── Helpers ──
    void reject_order(Order* order, RiskResult reason);
    void spin_wait();
    void maybe_flush_stats();
    void flush_stats();
};

} // namespace rtes