# RTES Code Reference - Interview Discussion Guide

**Purpose**: Quick reference for discussing specific code implementations during Goldman Sachs interview  
**Usage**: Have this open during interview to reference exact line numbers and implementations

---

## 📁 FILE STRUCTURE OVERVIEW

```
RTES/
├── include/rtes/          # Header files
│   ├── types.hpp          # Core types, enums
│   ├── spsc_queue.hpp     # Lock-free SPSC queue
│   ├── mpmc_queue.hpp     # Lock-free MPMC queue
│   ├── memory_pool.hpp    # Pre-allocated memory pool
│   ├── order_book.hpp     # Order book (mutex-based)
│   ├── matching_engine.hpp # Matching engine
│   ├── risk_manager.hpp   # Risk validation
│   ├── tcp_gateway.hpp    # TCP order gateway
│   ├── udp_publisher.hpp  # UDP market data
│   └── protocol.hpp       # Binary protocol
└── src/                   # Implementation files
    ├── order_book.cpp     # Order book implementation
    ├── matching_engine.cpp # Matching logic
    ├── risk_manager.cpp   # Risk checks
    ├── tcp_gateway.cpp    # Network I/O
    └── strategies.cpp     # Market making strategy
```

---

## 🔥 CRITICAL CODE SNIPPETS (For Resume Defense)

### **1. LOCK-FREE SPSC QUEUE** ✅ (Actually Lock-Free)

**File**: `include/rtes/spsc_queue.hpp`  
**Lines**: 8-62  
**Discussion Topic**: "Lock-free inter-thread communication"

```cpp
// Lines 8-30: Cache-line alignment prevents false sharing
template<typename T>
class SPSCQueue {
public:
    explicit SPSCQueue(size_t capacity) 
        : capacity_(capacity + 1), buffer_(std::make_unique<T[]>(capacity_)) {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }
    
    // Lines 16-27: Producer side - single writer
    bool push(const T& item) {
        auto head = head_.load(std::memory_order_relaxed);
        auto next_head = (head + 1) % capacity_;
        
        if (next_head == tail_.load(std::memory_order_acquire)) {
            return false; // Queue full
        }
        
        buffer_[head] = item;
        head_.store(next_head, std::memory_order_release);
        return true;
    }
    
    // Lines 56-59: Cache-line alignment (64 bytes)
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};
```

**Key Points**:
- ✅ Truly lock-free (no mutex)
- ✅ Cache-line alignment (64 bytes)
- ✅ Memory ordering (acquire/release)
- ✅ ~20ns latency

---

### **2. LOCK-FREE MEMORY POOL** ✅ (CAS Loop)

**File**: `include/rtes/memory_pool.hpp`  
**Lines**: 10-63  
**Discussion Topic**: "Lock-free allocation with CAS"

```cpp
// Lines 20-33: Lock-free allocation with CAS loop
T* allocate() {
    auto count = free_count_.load(std::memory_order_acquire);
    while (count > 0) {
        if (free_count_.compare_exchange_weak(count, count - 1, 
                                            std::memory_order_acq_rel)) {
            auto index = free_list_[count - 1];
            return &pool_[index];
        }
    }
    return nullptr; // Pool exhausted
}

// Lines 35-47: Lock-free deallocation
void deallocate(T* ptr) {
    if (!ptr) return;
    
    auto index = ptr - pool_.data();
    if (index < 0 || index >= static_cast<ptrdiff_t>(capacity_)) return;
    
    auto count = free_count_.load(std::memory_order_acquire);
    while (count < capacity_) {
        free_list_[count] = static_cast<size_t>(index);
        if (free_count_.compare_exchange_weak(count, count + 1,
                                            std::memory_order_acq_rel)) {
            break;
        }
    }
}
```

**Key Points**:
- ✅ CAS loop for thread-safe allocation
- ✅ O(1) allocation/deallocation
- ✅ Pre-allocated 1M orders (~200MB)
- ✅ Zero heap allocations in hot path

---

### **3. ORDER BOOK** ⚠️ (Mutex-Based, NOT Lock-Free)

**File**: `include/rtes/order_book.hpp`  
**Lines**: 17-115  
**Discussion Topic**: "Single-writer optimization with mutex"

```cpp
// Lines 17-40: Order book structure
class OrderBook {
public:
    using TradeCallback = std::function<void(const Trade&)>;
    
    explicit OrderBook(const std::string& symbol, OrderPool& pool, 
                      TradeCallback cb = nullptr);
    
    // O(1) order operations
    bool add_order(Order* order);
    bool cancel_order(OrderID order_id);
    
    // Market data accessors
    Price best_bid() const { return bids_.empty() ? 0 : bids_.rbegin()->first; }
    Price best_ask() const { return asks_.empty() ? 0 : asks_.begin()->first; }
    
private:
    // Lines 68-72: Price levels (sorted maps)
    std::map<Price, PriceLevel, std::greater<Price>> bids_;  // Descending
    std::map<Price, PriceLevel> asks_;                       // Ascending
    
    // Lines 74-75: O(1) order lookup for cancellation
    std::unordered_map<OrderID, Order*> order_lookup_ GUARDED_BY(order_mutex_);
    
    // Lines 77-78: Thread safety (MUTEX, not lock-free!)
    mutable std::mutex order_mutex_;
    atomic_wrapper<bool> shutdown_requested_;
};
```

**File**: `src/order_book.cpp`  
**Lines**: 150-220 (match_limit_order function)

```cpp
// Lines 150-220: Matching logic with cache prefetching
void OrderBook::match_limit_order(Order* order) {
    auto& opposite_side = (order->side == Side::BUY) ? asks_ : bids_;
    
    for (auto it = opposite_side.begin(); 
         it != opposite_side.end() && order->remaining_quantity > 0;) {
        
        // Price check
        if (!crosses(order->price, it->first, order->side)) break;
        
        // Lines 160-162: Cache prefetch for next level
        if (std::next(it) != opposite_side.end()) {
            _mm_prefetch(&(*std::next(it)), _MM_HINT_T0);
        }
        
        // Match FIFO within price level
        auto& level = it->second;
        while (!level.orders.empty() && order->remaining_quantity > 0) {
            execute_trade(order, level.orders.front(), ...);
        }
        
        if (level.orders.empty()) {
            it = opposite_side.erase(it);  // Remove empty level
        } else {
            ++it;
        }
    }
}
```

**Key Points**:
- ⚠️ Uses mutex (NOT lock-free)
- ✅ Single-writer per symbol (mutex uncontended)
- ✅ Cache prefetching (_mm_prefetch)
- ✅ Price-time priority (std::map + std::deque)

---

### **4. TCP GATEWAY** (epoll, NOT kernel bypass)

**File**: `include/rtes/tcp_gateway.hpp`  
**Lines**: 60-90  
**Discussion Topic**: "Syscall minimization with epoll"

```cpp
// Lines 60-90: TCP Gateway structure
class TcpGateway {
public:
    explicit TcpGateway(uint16_t port, RiskManager* risk_manager, 
                       OrderPool* order_pool);
    
    void start();
    void stop();
    
private:
    // Lines 72-74: Network with RAII (NOT DPDK!)
    FileDescriptor listen_fd_;
    FileDescriptor epoll_fd_;
    
    // Lines 76-78: Client connections
    std::unordered_map<int, std::shared_ptr<ClientConnection>> connections_;
    mutable std::mutex connections_mutex_;
    
    // Network setup
    bool setup_listen_socket();
    bool setup_epoll();
    
    // Thread functions
    void acceptor_loop();
    void worker_loop();
};
```

**File**: `src/tcp_gateway.cpp`  
**Lines**: 200-250 (worker_loop function)

```cpp
// Lines 200-250: epoll event loop
void TcpGateway::worker_loop() {
    epoll_event events[64];
    while (running_) {
        // epoll_wait (NOT io_uring or DPDK!)
        int n = epoll_wait(epoll_fd_.get(), events, 64, 100);
        
        for (int i = 0; i < n; ++i) {
            if (events[i].data.fd == listen_fd_.get()) {
                accept_connection();
            } else {
                handle_client_data(events[i].data.fd);
            }
        }
    }
}
```

**Key Points**:
- ⚠️ Uses epoll (NOT DPDK or io_uring)
- ✅ O(1) I/O multiplexing
- ✅ Non-blocking sockets with TCP_NODELAY
- ✅ 2μs gateway latency

---

### **5. MARKET MAKING STRATEGY** ✅

**File**: `src/strategies.cpp`  
**Lines**: 50-150  
**Discussion Topic**: "Systematic market making implementation"

```cpp
// Lines 50-80: Market maker strategy
class MarketMakerStrategy : public TradingStrategy {
    Price base_price_;        // Mid-market reference
    uint64_t spread_ticks_;   // Half-spread (e.g., 10 ticks)
    Quantity quote_size_;     // Size per side
    
    // Lines 60-75: Update quotes (continuous quoting)
    void update_quotes() {
        cancel_existing_orders();  // Cancel stale quotes
        
        Price bid = base_price_ - spread_ticks_;  // $149.90
        Price ask = base_price_ + spread_ticks_;  // $150.10
        
        send_new_order(symbol_, Side::BUY, quote_size_, bid);
        send_new_order(symbol_, Side::SELL, quote_size_, ask);
    }
    
    // Lines 80-90: On trade (price discovery)
    void on_trade(const TradeMessage& trade) {
        base_price_ = trade.price;  // Adjust to market
        cancel_existing_orders();
        update_quotes();  // Requote immediately
    }
    
    // Lines 100-110: On fill (inventory management)
    void on_order_fill(const Order& order) {
        if (order.side == Side::BUY) {
            position_ += order.quantity;
        } else {
            position_ -= order.quantity;
        }
        
        // Requote with inventory skew
        update_quotes();
    }
};
```

**Key Points**:
- ✅ Continuous bid/ask quoting
- ✅ Spread capture ($0.20 per round-trip)
- ✅ Inventory management (requote on fills)
- ✅ Price discovery (adjust to trades)

---

### **6. RISK MANAGER** ✅

**File**: `include/rtes/risk_manager.hpp`  
**Lines**: 10-80  
**Discussion Topic**: "Pre-trade risk validation"

```cpp
// Lines 40-80: Risk validation
class RiskManager {
private:
    // Lines 60-75: 6 pre-trade checks
    RiskResult validate_new_order(Order* order) {
        auto& state = client_states_[order->client_id];
        
        // 6 validation checks (~1μs total)
        if (!check_symbol_allowed(order)) return REJECTED_SYMBOL;
        if (!check_order_size(order)) return REJECTED_SIZE;
        if (!check_price_collar(order)) return REJECTED_PRICE;
        if (!check_rate_limit(state)) return REJECTED_RATE_LIMIT;
        if (!check_duplicate_order(order, state)) return REJECTED_DUPLICATE;
        if (!check_credit_limit(order, state)) return REJECTED_CREDIT;
        
        update_client_state(order, state);
        return APPROVED;
    }
};
```

**Key Points**:
- ✅ 6 pre-trade checks
- ✅ Single-threaded (no locks needed)
- ✅ ~1μs validation time
- ✅ Per-client state tracking

---

## 📊 PERFORMANCE METRICS CODE

### **Latency Measurement**

**File**: `src/matching_engine.cpp`  
**Lines**: 100-120

```cpp
// High-resolution timing
auto start = std::chrono::steady_clock::now();

// Process order
process_new_order(order);

auto end = std::chrono::steady_clock::now();
auto latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
    end - start).count();

// Update metrics
latency_histogram_.observe(latency_ns / 1000.0);  // Convert to μs
```

---

## 🎯 QUICK REFERENCE TABLE

| Topic | File | Lines | Key Concept |
|-------|------|-------|-------------|
| **Lock-Free SPSC Queue** | `spsc_queue.hpp` | 8-62 | Cache-line alignment, acquire/release |
| **Lock-Free Memory Pool** | `memory_pool.hpp` | 20-47 | CAS loop, O(1) allocation |
| **Order Book (Mutex)** | `order_book.hpp` | 17-115 | Single-writer, mutex-based |
| **Cache Prefetching** | `order_book.cpp` | 160-162 | `_mm_prefetch` for next level |
| **TCP Gateway (epoll)** | `tcp_gateway.cpp` | 200-250 | epoll event loop |
| **Market Making** | `strategies.cpp` | 50-150 | Continuous quoting, spread capture |
| **Risk Validation** | `risk_manager.cpp` | 60-120 | 6 pre-trade checks |
| **Binary Protocol** | `protocol.hpp` | 10-80 | CRC32, sequence numbers |

---

## 🗣️ INTERVIEW DISCUSSION SCRIPTS

### **Script 1: "Show me the lock-free queue"**

> "Sure, let me walk you through the SPSC queue in `spsc_queue.hpp`, lines 8-62.
>
> The key optimization is **cache-line alignment** on lines 56-59:
> ```cpp
> alignas(64) std::atomic<size_t> head_{0};
> alignas(64) std::atomic<size_t> tail_{0};
> ```
>
> This prevents false sharing—producer and consumer each have their own cache line.
>
> The push operation on lines 16-27 uses **memory_order_release** to publish to the consumer, and the consumer uses **memory_order_acquire** to read. This creates a happens-before relationship without full sequential consistency.
>
> Result: ~20ns latency per operation."

---

### **Script 2: "Explain the memory pool CAS loop"**

> "The memory pool in `memory_pool.hpp`, lines 20-33, uses a CAS loop for lock-free allocation:
>
> ```cpp
> auto count = free_count_.load(std::memory_order_acquire);
> while (count > 0) {
>     if (free_count_.compare_exchange_weak(count, count - 1, 
>                                         std::memory_order_acq_rel)) {
>         return &pool_[free_list_[count - 1]];
>     }
> }
> ```
>
> The CAS ensures atomic decrement of `free_count`. If it fails, `count` is updated with the current value, and we retry. This allows multiple threads to allocate concurrently without locks.
>
> I use **compare_exchange_weak** because it's faster—it allows spurious failures, but the while loop handles retries."

---

### **Script 3: "Walk through order matching"**

> "The matching logic is in `order_book.cpp`, lines 150-220. The key optimization is **cache prefetching** on lines 160-162:
>
> ```cpp
> if (std::next(it) != opposite_side.end()) {
>     _mm_prefetch(&(*std::next(it)), _MM_HINT_T0);
> }
> ```
>
> This loads the next price level into L1 cache while we're processing the current level. Memory latency drops from ~100ns to ~4ns, saving 2μs per order.
>
> The order book uses `std::map` for sorted prices and `std::deque` for FIFO within each level—this gives us correct price-time priority with O(log n) insert and O(1) cancel."

---

### **Script 4: "Explain your TCP gateway"**

> "The TCP gateway in `tcp_gateway.cpp`, lines 200-250, uses **epoll** for I/O multiplexing:
>
> ```cpp
> int n = epoll_wait(epoll_fd_.get(), events, 64, 100);
> ```
>
> epoll is O(1) scalable—it only returns ready file descriptors, unlike select/poll which are O(n).
>
> I also use:
> - Non-blocking sockets with TCP_NODELAY
> - Edge-triggered epoll to reduce syscall frequency
> - Batched reads/writes
>
> This achieves 2μs gateway latency, which is 25% of total latency. To go faster, I'd use io_uring (saves ~2μs) or DPDK (saves ~3μs), but epoll was sufficient for my targets."

---

### **Script 5: "Show me the market making strategy"**

> "The market maker is in `strategies.cpp`, lines 50-150. The core logic is in `update_quotes()`:
>
> ```cpp
> void update_quotes() {
>     cancel_existing_orders();
>     
>     Price bid = base_price_ - spread_ticks_;  // $149.90
>     Price ask = base_price_ + spread_ticks_;  // $150.10
>     
>     send_new_order(symbol_, Side::BUY, quote_size_, bid);
>     send_new_order(symbol_, Side::SELL, quote_size_, ask);
> }
> ```
>
> This continuously quotes both sides with a 20-cent spread. When filled, `on_order_fill()` immediately cancels and requotes—this is inventory management.
>
> The `on_trade()` function adjusts `base_price_` to the last trade—this is price discovery.
>
> This demonstrates the infrastructure needed for systematic market making: continuous quoting, spread capture, and inventory management."

---

## 📝 CODE WALKTHROUGH CHECKLIST

### **Before Interview**:
- [ ] Review SPSC queue implementation (lines 8-62)
- [ ] Review memory pool CAS loop (lines 20-47)
- [ ] Review order book matching (lines 150-220)
- [ ] Review cache prefetching (lines 160-162)
- [ ] Review market making strategy (lines 50-150)
- [ ] Have files open in editor (for screen share)

### **During Interview** (If Asked to Share Screen):
1. Open `spsc_queue.hpp` → Show cache-line alignment
2. Open `memory_pool.hpp` → Show CAS loop
3. Open `order_book.cpp` → Show cache prefetching
4. Open `strategies.cpp` → Show market making logic

---

## 🚨 CRITICAL REMINDERS

### **What to Say**:
- ✅ "The SPSC/MPMC queues are lock-free"
- ✅ "The memory pool uses CAS for lock-free allocation"
- ✅ "The order book uses mutex with single-writer optimization"
- ✅ "I use epoll for syscall minimization"

### **What NOT to Say**:
- ❌ "The order book is lock-free" (it's not!)
- ❌ "I use DPDK" (you don't!)
- ❌ "Everything is lock-free" (only queues and pool)
- ❌ "I use kernel bypass" (you use epoll)

---

## 📚 ADDITIONAL REFERENCES

### **Architecture Diagram**
- File: `docs/architecture.puml`
- Shows: Component interaction, queue types, threading model

### **Performance Benchmarks**
- File: `PERFORMANCE_BENCHMARKS.md`
- Shows: Latency breakdown, throughput tests, optimization impact

### **System Design**
- File: `docs/CPP_SOFTWARE_DESIGN.md`
- Shows: Design patterns, trade-offs, scalability

---

**Document Version**: 1.0  
**Last Updated**: 2024  
**Purpose**: Code reference for Goldman Sachs interview  
**Status**: Ready for discussion 📖
