#pragma once

/**
 * @file types.hpp
 * @brief Core domain types for RTES trading exchange
 *
 * Design principles:
 *   1. ALL types used in lock-free queues MUST be trivially copyable
 *   2. Hot-path fields (price, quantity, side) grouped for cache locality
 *   3. Cold fields (client_id, symbol) separated or accessed by pointer
 *   4. No syscalls in default constructors (no steady_clock::now())
 *   5. Fixed-size types only — no std::string, no heap allocation
 *
 * Price representation:
 *   Fixed-point integer: actual_price × PRICE_SCALE (10000)
 *   Example: \$150.25 → 1502500
 *   Range: 0 to 1,844,674,407,370,955 (uint64_t max / 10000)
 *
 * Timestamp representation:
 *   Nanoseconds since epoch (steady_clock or raw TSC).
 *   Using uint64_t instead of chrono::time_point for:
 *     - Guaranteed trivial copyability
 *     - No chrono header dependency in hot path
 *     - Compatible with lock-free queues
 *     - Convertible to/from chrono when needed
 */

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace rtes {

// ═══════════════════════════════════════════════════════════════
//  Scalar Type Aliases
// ═══════════════════════════════════════════════════════════════

using OrderID        = uint64_t;
using TradeID        = uint64_t;
using Price          = uint64_t;   // Fixed-point: price × PRICE_SCALE
using Quantity       = uint64_t;
using SequenceNumber = uint64_t;
using Timestamp      = uint64_t;   // Nanoseconds since steady_clock epoch
using ClientIDRaw    = uint32_t;   // Numeric client identifier (fast)

/** Price scaling factor: 4 decimal places of precision */
inline constexpr uint64_t PRICE_SCALE = 10000;

/** Convert double price to fixed-point */
[[nodiscard]] constexpr Price price_from_double(double p) {
    return static_cast<Price>(p * PRICE_SCALE + 0.5);
}

/** Convert fixed-point price to double */
[[nodiscard]] constexpr double price_to_double(Price p) {
    return static_cast<double>(p) / PRICE_SCALE;
}

// ═══════════════════════════════════════════════════════════════
//  FixedString — Trivially copyable fixed-size string
// ═══════════════════════════════════════════════════════════════

/**
 * Stack-allocated, null-terminated, trivially copyable string.
 *
 * Replaces BoundedString<N> for all types that must be trivially
 * copyable (required by lock-free queues and union storage).
 *
 * No heap allocation. No virtual methods. No non-trivial members.
 * Can be safely memcpy'd, stored in unions, passed through queues.
 *
 * @tparam N Maximum string length INCLUDING null terminator.
 *           FixedString<8> holds up to 7 characters + null.
 */
template<size_t N>
struct FixedString {
    static_assert(N > 0, "FixedString must have capacity > 0");

    char data[N]{};  // Zero-initialized, null-terminated

    // ── Defaulted lifecycle (preserves trivial copyability) ──
    FixedString() = default;
    ~FixedString() = default;
    FixedString(const FixedString&) = default;
    FixedString& operator=(const FixedString&) = default;
    FixedString(FixedString&&) = default;
    FixedString& operator=(FixedString&&) = default;

    // ── Construction from string ──

    /** Construct from C-string (truncates if too long) */
    explicit FixedString(const char* str) {
        assign(str);
    }

    // ── Assignment ──

    void assign(const char* str) {
        if (str) {
            std::strncpy(data, str, N - 1);
            data[N - 1] = '\0';
        } else {
            data[0] = '\0';
        }
    }

    void clear() {
        data[0] = '\0';
    }

    // ── Accessors ──

    [[nodiscard]] const char* c_str() const { return data; }
    [[nodiscard]] size_t length() const { return std::strlen(data); }
    [[nodiscard]] bool empty() const { return data[0] == '\0'; }

    // ── Comparison ──

    [[nodiscard]] bool operator==(const FixedString& other) const {
        return std::strncmp(data, other.data, N) == 0;
    }
    [[nodiscard]] bool operator!=(const FixedString& other) const {
        return !(*this == other);
    }
    [[nodiscard]] bool operator==(const char* str) const {
        return std::strncmp(data, str, N) == 0;
    }

    // ── For use as hash map key ──

    struct Hash {
        size_t operator()(const FixedString& s) const {
            // FNV-1a hash — fast, good distribution for short strings
            size_t hash = 14695981039346656037ULL;
            for (size_t i = 0; i < N && s.data[i]; ++i) {
                hash ^= static_cast<size_t>(s.data[i]);
                hash *= 1099511628211ULL;
            }
            return hash;
        }
    };
};

// Compile-time verification
static_assert(std::is_trivially_copyable_v<FixedString<8>>,
    "FixedString must be trivially copyable");
static_assert(std::is_trivially_copyable_v<FixedString<32>>,
    "FixedString must be trivially copyable");

// ═══════════════════════════════════════════════════════════════
//  Type Aliases Using FixedString
// ═══════════════════════════════════════════════════════════════

using Symbol   = FixedString<8>;    // "AAPL", "MSFT", etc.
using ClientID = FixedString<32>;   // Client identifier string

// ═══════════════════════════════════════════════════════════════
//  Enums
// ═══════════════════════════════════════════════════════════════

enum class Side : uint8_t {
    BUY  = 1,
    SELL = 2,
};

enum class OrderType : uint8_t {
    MARKET = 1,
    LIMIT  = 2,
};

enum class OrderStatus : uint8_t {
    PENDING          = 0,
    ACCEPTED         = 1,
    REJECTED         = 2,
    FILLED           = 3,
    PARTIALLY_FILLED = 4,
    CANCELLED        = 5,
};

// ═══════════════════════════════════════════════════════════════
//  Timestamp Utilities
// ═══════════════════════════════════════════════════════════════

/**
 * Get current timestamp as nanoseconds since steady_clock epoch.
 * 
 * IMPORTANT: Do NOT call this in default constructors.
 * Call it explicitly when an event occurs (order accepted, trade executed).
 * steady_clock::now() is a syscall (~20-50ns). Every nanosecond counts.
 */
[[nodiscard]] inline Timestamp now_timestamp() {
    auto now = std::chrono::steady_clock::now();
    return static_cast<Timestamp>(
        now.time_since_epoch().count());
}

// ═══════════════════════════════════════════════════════════════
//  Order — Hot/Cold split layout
// ═══════════════════════════════════════════════════════════════

/**
 * Core order structure with cache-optimized field layout.
 *
 * Layout strategy: HOT fields first, COLD fields last.
 *
 * During matching, the engine accesses:
 *   price, remaining_quantity, side, type, id, status
 * These are packed into the first 32 bytes (half a cache line).
 *
 * Cold fields (client_id, symbol, original quantity, timestamp)
 * are accessed only for:
 *   - Trade reporting
 *   - Cancel ownership verification
 *   - Logging
 *
 * Total size: 96 bytes (1.5 cache lines).
 * Hot data fits in first cache line load.
 */
struct Order {
    // ── HOT DATA (accessed during matching) ── first 32 bytes ──

    Price         price{0};               //  8B  [0]
    Quantity      remaining_quantity{0};   //  8B  [8]
    OrderID       id{0};                  //  8B  [16]
    OrderStatus   status{OrderStatus::PENDING}; // 1B [24]
    Side          side{Side::BUY};        //  1B  [25]
    OrderType     type{OrderType::LIMIT}; //  1B  [26]
    uint8_t       pad_[5]{};              //  5B  [27-31] explicit padding

    // ── WARM DATA (accessed for trade reporting) ── bytes 32-63 ──

    Quantity      quantity{0};            //  8B  [32] original quantity
    Timestamp     timestamp{0};           //  8B  [40] creation time
    Symbol        symbol;                 //  8B  [48] instrument

    // ── COLD DATA (accessed rarely) ── bytes 64+ ──

    ClientID      client_id;              // 32B  [56] owner

    // ── Constructors ──

    Order() = default;

    Order(OrderID order_id,
          const char* client,
          const char* sym,
          Side s,
          OrderType t,
          Quantity qty,
          Price p)
        : price(p)
        , remaining_quantity(qty)
        , id(order_id)
        , status(OrderStatus::PENDING)
        , side(s)
        , type(t)
        , quantity(qty)
        , timestamp(now_timestamp())  // Only on explicit construction
        , symbol(sym)
        , client_id(client)
    {}
};

// Verify trivial copyability (required if Order is ever stored by value)
static_assert(std::is_trivially_copyable_v<Order>,
    "Order must be trivially copyable");

// Verify hot data fits in first 32 bytes
static_assert(offsetof(Order, quantity) >= 32,
    "Warm data must start at or after byte 32");

// ═══════════════════════════════════════════════════════════════
//  Trade — Minimal, trivially copyable trade record
// ═══════════════════════════════════════════════════════════════

/**
 * Executed trade record. Stored by value in MarketDataEvent union.
 * MUST be trivially copyable.
 *
 * No chrono::time_point — uses uint64_t nanosecond timestamp.
 * No BoundedString — uses FixedString.
 * No syscalls in default constructor.
 */
struct Trade {
    TradeID   id{0};
    OrderID   buy_order_id{0};
    OrderID   sell_order_id{0};
    Price     price{0};
    Quantity  quantity{0};
    Timestamp timestamp{0};     // Set at execution time, NOT construction
    Symbol    symbol;           // FixedString<8>

    // ── Constructors ──

    Trade() = default;  // Zero-initialized, NO syscalls

    Trade(TradeID trade_id,
          OrderID buy_id,
          OrderID sell_id,
          const char* sym,
          Quantity qty,
          Price p)
        : id(trade_id)
        , buy_order_id(buy_id)
        , sell_order_id(sell_id)
        , price(p)
        , quantity(qty)
        , timestamp(now_timestamp())  // Only on explicit construction
        , symbol(sym)
    {}
};

// Critical compile-time checks
static_assert(std::is_trivially_copyable_v<Trade>,
    "Trade MUST be trivially copyable for MarketDataEvent union "
    "and lock-free queue safety");

static_assert(sizeof(Trade) <= 64,
    "Trade should fit in one cache line for queue efficiency");

} // namespace rtes