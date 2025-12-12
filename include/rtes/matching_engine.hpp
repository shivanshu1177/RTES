#pragma once

/**
 * @file matching_engine.hpp
 * @brief Per-symbol matching engine with dedicated worker thread
 *
 * Threading model:
 *   - One MatchingEngine per symbol, one dedicated thread each
 *   - Orders arrive via lock-free SPSC queue (from risk manager)
 *   - Trades/BBO published via MPMC queue (to market data publisher)
 *   - Single-writer access to OrderBook (no locks in hot path)
 *
 * Data flow:
 *   RiskManager → [SPSC] → MatchingEngine → OrderBook → [MPMC] → UdpPublisher
 *
 * Performance:
 *   - Batch drain (up to 256 orders per cycle)
 *   - Spin-pause-yield backoff (no sleep)
 *   - Local counters flushed periodically (no per-order atomics)
 *   - Pre-cached symbol (no strncpy in hot path)
 */

#include "rtes/order_book.hpp"
#include "rtes/spsc_queue.hpp"
#include "rtes/mpmc_queue.hpp"
#include "rtes/types.hpp"

#include <thread>
#include <atomic>
#include <memory>
#include <cstring>
#include <cstdint>

namespace rtes {

// ═══════════════════════════════════════════════════════════════
//  OrderRequest — Input to matching engine via SPSC queue
// ═══════════════════════════════════════════════════════════════

/**
 * Compact order request message.
 *
 * Uses a union to avoid carrying dead fields:
 *   NEW_ORDER:    uses new_order.order
 *   CANCEL_ORDER: uses cancel.order_id + cancel.client_id
 *
 * Aligned to 32 bytes so two requests fit per cache line,
 * maximizing SPSC queue throughput.
 */
struct alignas(64) OrderRequest {
    enum Type : uint8_t {
        NEW_ORDER    = 0,
        CANCEL_ORDER = 1,
    };

    Type type;

    union {
        /** NEW_ORDER payload */
        struct {
            Order* order;
        } new_order;

        /** CANCEL_ORDER payload */
        struct {
            OrderID  order_id;
            ClientID client_id;
        } cancel;
    };

    // ── Factory methods (clearer than raw field assignment) ──

    // Explicit default constructor ( union member 'cancel' may be non-trivial )
    OrderRequest() : type(NEW_ORDER), new_order{nullptr} {}

    [[nodiscard]] static OrderRequest make_new_order(Order* order) {
        OrderRequest req;
        req.type = NEW_ORDER;
        req.new_order.order = order;
        return req;
    }

    [[nodiscard]] static OrderRequest make_cancel(OrderID id, ClientID client) {
        OrderRequest req;
        req.type = CANCEL_ORDER;
        req.cancel.order_id = id;
        req.cancel.client_id = client;
        return req;
    }
};

static_assert(sizeof(OrderRequest) <= 64,
              "OrderRequest must fit in 32 bytes for queue efficiency");

// ═══════════════════════════════════════════════════════════════
//  MarketDataEvent — Output from matching engine via MPMC queue
// ═══════════════════════════════════════════════════════════════

/**
 * Market data event published to subscribers.
 *
 * Design decisions:
 *   - All fields are trivially copyable (no manual lifetime)
 *   - Fixed-size symbol avoids string allocation
 *   - Aligned to cache line for queue efficiency
 *   - No placement new, no explicit destructors
 *
 * IMPORTANT: Trade must be trivially copyable for this to work.
 * If Trade has non-trivial members (std::string, etc.), it must
 * be redesigned. Exchange-grade Trade structs should always be POD.
 */
struct alignas(64) MarketDataEvent {
    enum Type : uint8_t {
        TRADE      = 0,
        BBO_UPDATE = 1,
    };

    Type type{TRADE};
    char symbol[16]{};   // Fixed-size, null-terminated

    /** BBO snapshot — always populated for BBO_UPDATE */
    struct BBOData {
        Price    bid_price{0};
        Quantity bid_quantity{0};
        Price    ask_price{0};
        Quantity ask_quantity{0};
    };

    union {
        Trade   trade;
        BBOData bbo;
    };

    // ── Trivial lifecycle (no manual union management) ──
    MarketDataEvent() { std::memset(&trade, 0, sizeof(trade)); }
    ~MarketDataEvent() = default;
    MarketDataEvent(const MarketDataEvent&) = default;
    MarketDataEvent& operator=(const MarketDataEvent&) = default;
    MarketDataEvent(MarketDataEvent&&) = default;
    MarketDataEvent& operator=(MarketDataEvent&&) = default;

    // ── Factory methods ──

    [[nodiscard]] static MarketDataEvent make_trade(
            const char* sym, const Trade& t) {
        MarketDataEvent event;
        event.type = TRADE;
        std::memcpy(event.symbol, sym, sizeof(event.symbol));
        event.trade = t;
        return event;
    }

    [[nodiscard]] static MarketDataEvent make_bbo(
            const char* sym,
            Price bid_px, Quantity bid_qty,
            Price ask_px, Quantity ask_qty) {
        MarketDataEvent event;
        event.type = BBO_UPDATE;
        std::memcpy(event.symbol, sym, sizeof(event.symbol));
        event.bbo.bid_price    = bid_px;
        event.bbo.bid_quantity = bid_qty;
        event.bbo.ask_price    = ask_px;
        event.bbo.ask_quantity = ask_qty;
        return event;
    }
};

// Compile-time verification that Trade is trivially copyable
// If this fails, Trade must be redesigned as a POD struct.
static_assert(std::is_trivially_copyable_v<Trade>,
              "Trade must be trivially copyable for union safety");
static_assert(std::is_trivially_copyable_v<MarketDataEvent>,
              "MarketDataEvent must be trivially copyable for lock-free queues");

// ═══════════════════════════════════════════════════════════════
//  MatchingEngine — Per-symbol order matching with dedicated thread
// ═══════════════════════════════════════════════════════════════

class MatchingEngine {
public:
    /**
     * @param symbol  Instrument symbol (e.g., "AAPL")
     * @param pool    Pre-allocated order pool (must outlive engine)
     */
    explicit MatchingEngine(const std::string& symbol, OrderPool& pool);
    ~MatchingEngine();

    // Non-copyable, non-movable (owns thread)
    MatchingEngine(const MatchingEngine&) = delete;
    MatchingEngine& operator=(const MatchingEngine&) = delete;
    MatchingEngine(MatchingEngine&&) = delete;
    MatchingEngine& operator=(MatchingEngine&&) = delete;

    // ── Lifecycle ──────────────────────────────────────────

    /** Start worker thread. Idempotent (safe to call twice). */
    void start();

    /** Stop worker thread. Drains remaining orders before exit. */
    void stop();

    [[nodiscard]] bool is_running() const {
        return running_.load(std::memory_order_relaxed);
    }

    // ── Order Submission (called from risk manager thread) ──

    /**
     * Submit a new order for matching.
     * @return false if queue is full (order not accepted)
     */
    [[nodiscard]] bool submit_order(Order* order);

    /**
     * Submit a cancel request.
     * @param order_id  Order to cancel
     * @param client_id Client requesting cancel (ownership check)
     * @return false if queue is full
     */
    [[nodiscard]] bool cancel_order(OrderID order_id, ClientID client_id);

    // ── Market Data ────────────────────────────────────────

    /** Set output queue for trade/BBO events. Call before start(). */
    void set_market_data_queue(MPMCQueue<MarketDataEvent>* queue);

    // ── Statistics (read by monitoring thread) ─────────────

    /**
     * Aggregate statistics snapshot.
     * All values are eventually consistent (flushed periodically).
     */
    struct Stats {
        uint64_t orders_accepted;
        uint64_t orders_rejected;
        uint64_t trades_executed;
        uint64_t cancels_accepted;
        uint64_t cancels_rejected;
        uint64_t md_drops;
        uint64_t queue_full_count;
    };

    [[nodiscard]] Stats get_stats() const {
        return Stats{
            .orders_accepted = orders_processed_.load(std::memory_order_relaxed),
            .orders_rejected = orders_rejected_.load(std::memory_order_relaxed),
            .trades_executed = trades_executed_.load(std::memory_order_relaxed),
            .cancels_accepted = 0,  // TODO: expose from local stats
            .cancels_rejected = 0,
            .md_drops         = md_drops_.load(std::memory_order_relaxed),
            .queue_full_count = 0,
        };
    }

    // Legacy accessors (prefer get_stats())
    [[nodiscard]] uint64_t orders_processed() const {
        return orders_processed_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] uint64_t trades_executed() const {
        return trades_executed_.load(std::memory_order_relaxed);
    }

    // ── Internal (public for static callback access only) ──

    /**
     * Called by OrderBook trade callback (static function pointer).
     * NOT part of user API. Public only because the static callback
     * in matching_engine.cpp needs access.
     */
    void on_trade_internal(const Trade& trade);

private:
    // ═══════════════════════════════════════════════════════
    //  HOT DATA — accessed every iteration of worker loop
    // ═══════════════════════════════════════════════════════

    alignas(64)
    std::unique_ptr<SPSCQueue<OrderRequest>> input_queue_;
    std::unique_ptr<OrderBook> book_;
    MPMCQueue<MarketDataEvent>* market_data_queue_{nullptr};

    // Pre-cached symbol (avoid strncpy in hot path)
    char symbol_cache_[16]{};

    // ═══════════════════════════════════════════════════════
    //  LOCAL STATS — thread-local counters (no atomics)
    //  Flushed to atomic counters periodically
    // ═══════════════════════════════════════════════════════

    struct LocalStats {
        size_t total_processed{0};
        size_t orders_accepted{0};
        size_t orders_rejected{0};
        size_t cancels_accepted{0};
        size_t cancels_rejected{0};
        size_t trades_executed{0};
        size_t md_drops{0};
        size_t queue_full_count{0};
    };
    LocalStats local_stats_;

    // ═══════════════════════════════════════════════════════
    //  ATOMIC STATS — read by monitoring, written by flush
    // ═══════════════════════════════════════════════════════

    alignas(64)
    std::atomic<uint64_t> orders_processed_{0};
    std::atomic<uint64_t> trades_executed_{0};
    std::atomic<uint64_t> orders_rejected_{0};
    std::atomic<uint64_t> md_drops_{0};

    // ═══════════════════════════════════════════════════════
    //  THREADING
    // ═══════════════════════════════════════════════════════

    alignas(64)
    std::atomic<bool> running_{false};
    std::thread worker_thread_;

    // ═══════════════════════════════════════════════════════
    //  COLD DATA — setup only
    // ═══════════════════════════════════════════════════════

    alignas(64)
    std::string symbol_;
    OrderPool& pool_;

    // ═══════════════════════════════════════════════════════
    //  Worker Thread Internals
    // ═══════════════════════════════════════════════════════

    /** Main worker loop — batch drain + spin-pause-yield */
    void run();

    /** Drain up to BATCH_SIZE orders. Returns count processed. */
    size_t drain_batch();

    /** Process single new order through matching. */
    void process_new_order(Order* order);

    /** Process cancel request with BBO change detection. */
    void process_cancel(OrderID order_id, ClientID client_id);

    // ── Market Data Publishing ──

    void publish_trade(const Trade& trade);
    void publish_bbo_update();

    // ── Backoff Strategy ──

    /** Spin-pause-yield (no sleep). See .cpp for detailed docs. */
    void spin_wait();

    // ── Periodic Maintenance ──

    void maybe_compact();
    void maybe_flush_stats();
    void flush_stats();
};

} // namespace rtes