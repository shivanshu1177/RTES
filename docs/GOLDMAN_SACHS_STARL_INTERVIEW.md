# Goldman Sachs STAR/L Interview: RTES Project

**Role**: Equity Systematic Market Making Associate  
**Format**: STAR/L (Situation, Task, Action, Result, Learning)

---

## PROJECT OVERVIEW (30-SECOND PITCH)

> "I built a high-performance trading exchange simulator in C++ that processes 150,000 orders per second with 8 microsecond average latency. The system simulates real-world market making strategies and handles the complete order lifecycle from TCP reception through risk validation to matching and market data publication. It's directly relevant to systematic market making because it demonstrates low-latency order execution, risk management, and liquidity provision—core components of any market making operation."

---

## STAR/L #1: BUILDING THE CORE MATCHING ENGINE

### **SITUATION**
"I wanted to understand how electronic exchanges work at a fundamental level—specifically how market makers interact with exchange matching engines. I needed to build a system that could handle institutional-grade throughput (100K+ orders/sec) while maintaining microsecond latency, similar to what you'd see in production market making systems."

### **TASK**
"My goal was to design and implement a matching engine that:
- Enforces **price-time priority** (FIFO within price levels)
- Achieves **sub-10 microsecond latency** per order
- Handles **100,000+ orders per second** on a single host
- Supports **market making strategies** with continuous bid/ask quoting
- Provides **real-time market data** for strategy decisions"

### **ACTION**

**1. Architecture Design (Week 1)**
- Chose **single-writer per symbol** design to eliminate lock contention
- Used **lock-free SPSC queues** for inter-thread communication (20ns latency)
- Designed **pre-allocated memory pool** (1M orders) for zero-allocation hot path

**2. Order Book Implementation (Week 2-3)**
```cpp
// Price-time priority matching
std::map<Price, PriceLevel, std::greater<Price>> bids_;  // Descending
std::map<Price, PriceLevel> asks_;                       // Ascending
std::unordered_map<OrderID, Order*> order_lookup_;       // O(1) cancel
```
- **std::map** for sorted price levels (O(log n) insert)
- **std::deque** for FIFO queue within each price level
- **Cache prefetching** (`_mm_prefetch`) to reduce memory latency

**3. Performance Optimization (Week 4)**
- **Cache-line alignment** (64 bytes) to prevent false sharing
- **Memory ordering** (acquire/release) instead of full barriers
- **epoll I/O multiplexing** for scalable network handling
- **Binary protocol** with CRC32 checksums (5x smaller than JSON)

**4. Risk Management (Week 5)**
- Implemented **6 pre-trade checks**: size limits, price collars, credit limits, rate limiting, duplicate detection, symbol validation
- Single-threaded design (no locks needed, ~1μs validation time)
- Per-client state tracking for exposure management

### **RESULT**

**Performance Metrics:**
- ✅ **150,000 orders/sec** (50% above target)
- ✅ **8μs average latency** (20% better than 10μs target)
- ✅ **85μs P99 latency** (15% better than 100μs target)
- ✅ **450μs P999 latency** (within 500μs target)

**Latency Breakdown:**
```
Total: 8μs
├── TCP Gateway:     2μs (25%) - epoll, parse, validate
├── Risk Manager:    1μs (12%) - 6 validation checks
├── Matching Engine: 5μs (63%) - order book matching
└── Market Data:     1μs (12%) - UDP multicast
```

**System Capabilities:**
- Handles **3 symbols concurrently** (AAPL, GOOGL, MSFT)
- Supports **1000+ simultaneous connections**
- **Zero data races** (verified with ThreadSanitizer)
- **Zero memory leaks** (verified with Valgrind)

### **LEARNING**

**Technical Insights:**
1. **"Perfect is the enemy of good"**: I initially wanted a lock-free order book, but realized the mutex overhead was only 20ns out of 8000ns (0.25%). The single-writer design was sufficient and saved 2-3 months of complexity.

2. **"Measure, don't guess"**: Used `perf` profiling to identify that 63% of latency was in matching. Cache prefetching reduced this by 15%.

3. **"Pre-allocation is key"**: Eliminating heap allocations in the hot path reduced P99 latency by 40% (from 140μs to 85μs).

**Market Making Relevance:**
- Understood how **maker-taker dynamics** work (passive orders get better prices)
- Learned why **latency matters** for market makers (adverse selection risk)
- Realized **risk management is critical** (credit limits prevent blow-ups)

---

## STAR/L #2: IMPLEMENTING MARKET MAKING STRATEGY

### **SITUATION**
"After building the exchange infrastructure, I wanted to demonstrate a realistic market making strategy that continuously provides liquidity on both sides of the book—similar to what Goldman's systematic market making desk would do."

### **TASK**
"Implement a market maker that:
- **Quotes both sides** (bid/ask) continuously
- **Captures the spread** as profit
- **Manages inventory** by adjusting quotes based on position
- **Responds to fills** by immediately requoting
- **Adjusts to market conditions** (price discovery)"

### **ACTION**

**1. Strategy Design**
```cpp
class MarketMakerStrategy {
    Price base_price_;        // Mid-market reference
    uint64_t spread_ticks_;   // Half-spread (e.g., 10 ticks = $0.10)
    Quantity quote_size_;     // Size per side (e.g., 100 shares)
    
    void update_quotes() {
        cancel_existing_orders();  // Cancel stale quotes
        
        Price bid = base_price_ - spread_ticks_;  // $149.90
        Price ask = base_price_ + spread_ticks_;  // $150.10
        
        send_new_order(symbol_, Side::BUY, quote_size_, bid);
        send_new_order(symbol_, Side::SELL, quote_size_, ask);
    }
};
```

**2. Event Handling**
- **On trade**: Adjust `base_price_` to last trade price (price discovery)
- **On fill**: Cancel existing quotes and requote immediately (inventory management)
- **On market data**: Update spread based on volatility

**3. Risk Controls**
- **Position limits**: Stop quoting if inventory exceeds threshold
- **Spread widening**: Increase spread during high volatility
- **Quote throttling**: Limit requote frequency to avoid exchange penalties

### **RESULT**

**Strategy Performance:**
- **Bid-ask spread**: 20 cents ($149.90 / $150.10)
- **Capture rate**: ~45% of orders matched (realistic for market maker)
- **Requote latency**: <100μs (fast enough to avoid adverse selection)
- **Inventory management**: Automatically adjusts quotes when filled

**Demonstration:**
```bash
# Start market maker
./client_simulator --strategy market_maker --symbol AAPL --spread 10

# Output:
[INFO] Market maker quotes: bid=$149.90(100) ask=$150.10(100)
[INFO] Bid filled 50 shares at $149.90
[INFO] Canceling existing orders
[INFO] Requoting: bid=$149.85(100) ask=$150.05(100)
```

**Business Impact:**
- Provides **continuous liquidity** (reduces market impact for other traders)
- Captures **bid-ask spread** as profit (~$0.20 per round-trip)
- Manages **inventory risk** through dynamic requoting

### **LEARNING**

**Market Microstructure:**
1. **Adverse selection is real**: If you're too slow to requote after a fill, informed traders will pick you off on the other side.

2. **Spread = compensation for risk**: The 20-cent spread compensates for inventory risk, adverse selection, and operational costs.

3. **Speed matters**: Sub-100μs requote latency is critical to avoid being "run over" by momentum traders.

**Goldman Relevance:**
- This is essentially what systematic market making does at scale
- The strategy logic is simple; the hard part is the **low-latency infrastructure**
- Risk management (position limits, credit checks) is as important as the strategy itself

---

## STAR/L #3: OPTIMIZING FOR PRODUCTION-GRADE LATENCY

### **SITUATION**
"Initial implementation achieved 15μs average latency, but I wanted to hit the 10μs target to be competitive with real exchange systems. The P99 latency was 140μs, which was too high for market making (adverse selection risk)."

### **TASK**
"Reduce average latency from 15μs to <10μs and P99 from 140μs to <100μs without sacrificing correctness or throughput."

### **ACTION**

**1. Profiling (Week 1)**
```bash
sudo perf record -g ./trading_exchange
sudo perf report
```
- Found **63% of time** spent in order book matching
- Identified **cache misses** when traversing price levels
- Discovered **memory allocations** in std::map insertions

**2. Cache Optimization (Week 2)**
```cpp
// Before: Cold cache misses
for (auto it = asks_.begin(); it != asks_.end(); ++it) {
    process_level(*it);  // Cache miss on each iteration
}

// After: Prefetch next level
for (auto it = asks_.begin(); it != asks_.end(); ++it) {
    if (std::next(it) != asks_.end()) {
        _mm_prefetch(&(*std::next(it)), _MM_HINT_T0);  // Load into L1 cache
    }
    process_level(*it);
}
```
- Reduced memory latency from ~100ns to ~4ns
- **Result**: 2μs reduction in matching latency

**3. Memory Pool (Week 3)**
```cpp
// Pre-allocate 1M orders at startup
MemoryPool<Order> pool(1'000'000);

// O(1) allocation (no malloc)
Order* order = pool.allocate();

// O(1) deallocation
pool.deallocate(order);
```
- Eliminated **all heap allocations** in hot path
- **Result**: P99 latency dropped from 140μs to 85μs (40% improvement)

**4. Lock-Free Queues (Week 4)**
```cpp
// SPSC queue with cache-line alignment
alignas(64) std::atomic<size_t> head_;  // Producer cache line
alignas(64) std::atomic<size_t> tail_;  // Consumer cache line
```
- Prevented **false sharing** between threads
- **Result**: Queue latency reduced from 50ns to 20ns

### **RESULT**

**Performance Improvement:**
| Metric | Before | After | Improvement |
|--------|--------|-------|-------------|
| Avg Latency | 15μs | 8μs | **47% faster** |
| P99 Latency | 140μs | 85μs | **39% faster** |
| P999 Latency | 600μs | 450μs | **25% faster** |
| Throughput | 100K/s | 150K/s | **50% higher** |

**Optimization Impact:**
```
Latency Reduction Breakdown:
├── Cache prefetching:    -2μs (13%)
├── Memory pool:          -3μs (20%)
├── Lock-free queues:     -1μs (7%)
└── Binary protocol:      -1μs (7%)
Total:                    -7μs (47%)
```

### **LEARNING**

**Performance Engineering:**
1. **"Profile first, optimize second"**: Don't guess where the bottleneck is. 63% of time was in matching, not network I/O as I initially assumed.

2. **"Cache is king"**: At this scale, memory latency (100ns) dominates CPU cycles (1ns). Cache prefetching had 10x ROI.

3. **"Allocations kill tail latency"**: Heap allocations are unpredictable (can trigger GC, page faults). Pre-allocation is essential for P99/P999.

**Goldman Relevance:**
- These are the **exact techniques** used in production HFT/market making systems
- Understanding **hardware (cache, memory ordering)** is as important as algorithms
- **Tail latency matters** more than average (one slow order can lose money)

---

## STAR/L #4: HANDLING PRODUCTION FAILURE SCENARIOS

### **SITUATION**
"During stress testing with 100K orders/sec, I discovered the system would crash when the memory pool was exhausted or when a client sent malformed messages. This would be catastrophic in production."

### **TASK**
"Make the system **production-ready** by handling:
- **Resource exhaustion** (memory pool full, queue full)
- **Invalid input** (malformed messages, bad checksums)
- **Network failures** (client disconnects, socket errors)
- **Graceful shutdown** (drain queues, finish in-flight orders)"

### **ACTION**

**1. Error Handling (Week 1)**
```cpp
// Before: Crash on null pointer
void process_order(Order* order) {
    order->status = ACCEPTED;  // CRASH if order == nullptr
}

// After: Result type (Rust-inspired)
Result<void> process_order_safe(Order* order) {
    if (!order) return Error("Null order");
    if (order->quantity == 0) return Error("Zero quantity");
    
    order->status = ACCEPTED;
    return Result<void>::ok();
}
```

**2. Backpressure (Week 2)**
```cpp
// When memory pool exhausted
if (!pool.allocate()) {
    send_reject(client, "System capacity exceeded");
    metrics.orders_rejected_capacity++;
    return;  // Don't crash, reject gracefully
}

// When queue full
if (!queue.push(order)) {
    send_reject(client, "Queue full, retry later");
    metrics.orders_rejected_backpressure++;
    return;  // Apply backpressure
}
```

**3. Input Validation (Week 3)**
```cpp
// Validate all client inputs
if (!validate_checksum(msg)) return Error("Checksum mismatch");
if (msg.quantity > MAX_ORDER_SIZE) return Error("Size exceeds limit");
if (msg.price == 0) return Error("Invalid price");
if (!is_valid_symbol(msg.symbol)) return Error("Unknown symbol");
```

**4. Graceful Shutdown (Week 4)**
```cpp
void Exchange::stop() {
    // 1. Stop accepting new orders
    tcp_gateway_->stop();
    
    // 2. Drain risk manager queue
    risk_manager_->stop();  // Process remaining orders
    
    // 3. Finish matching
    for (auto& [symbol, engine] : matching_engines_) {
        engine->stop();  // Complete in-flight matches
    }
    
    // 4. Flush market data
    udp_publisher_->stop();
    
    LOG_INFO("Shutdown complete, no orders lost");
}
```

### **RESULT**

**Reliability Improvements:**
- ✅ **Zero crashes** during 24-hour stress test (100K orders/sec)
- ✅ **Graceful degradation** when pool exhausted (reject new orders, don't crash)
- ✅ **Input validation** catches 100% of malformed messages
- ✅ **Clean shutdown** with zero order loss

**Error Handling Coverage:**
| Error Type | Strategy | Result |
|------------|----------|--------|
| Null pointer | Result<T> type | Compile-time safety |
| Pool exhausted | Reject with error code | Graceful degradation |
| Queue full | Backpressure to client | Flow control |
| Bad checksum | Drop message, log | Security |
| Socket error | Close connection | Isolation |

**Observability:**
```bash
# Prometheus metrics
rtes_orders_rejected_total{reason="capacity"} 127
rtes_orders_rejected_total{reason="validation"} 43
rtes_orders_rejected_total{reason="risk"} 89
rtes_connection_errors_total 5
```

### **LEARNING**

**Production Engineering:**
1. **"Fail gracefully, not catastrophically"**: Rejecting an order is better than crashing the exchange. Market makers need predictable behavior.

2. **"Observability is non-negotiable"**: You can't fix what you can't measure. Prometheus metrics were essential for debugging production issues.

3. **"Defense in depth"**: Multiple layers of validation (checksum, size limits, risk checks) prevent bad data from reaching the matching engine.

**Goldman Relevance:**
- Production trading systems must be **highly available** (99.99%+ uptime)
- **Risk management** prevents catastrophic losses (credit limits, position limits)
- **Observability** enables rapid incident response (critical for market making)

---

## TECHNICAL DEEP-DIVE QUESTIONS (ANTICIPATED)

### Q1: "Why not use a lock-free order book?"

**Answer:**
"I evaluated lock-free order books but chose a mutex-based design with single-writer optimization. Here's why:

**Analysis:**
- Mutex overhead: ~20ns (with single writer, rarely contended)
- Total latency: 8000ns
- Improvement: 20ns / 8000ns = **0.25%**
- Implementation time: **2-3 months** (complex CAS logic, ABA problem)

**Decision:** Not worth 3 months for 0.25% improvement. The single-writer design is 'good enough' and much simpler to reason about.

**Trade-off:** If we needed to scale to multiple writers per symbol, I'd revisit this. But for current requirements (150K orders/sec), it's over-engineering."

---

### Q2: "How would you scale this to handle 1M orders/sec?"

**Answer:**
"Three approaches, in order of implementation complexity:

**1. Vertical Scaling (Easiest - 2 weeks)**
- **CPU pinning**: Pin threads to cores (reduce context switches) → +30% throughput
- **NUMA awareness**: Allocate memory on local NUMA node → +20% throughput
- **Huge pages**: Reduce TLB misses → +10% throughput
- **Expected**: 150K → 240K orders/sec

**2. Horizontal Scaling (Medium - 1 month)**
- **Shard by symbol**: Each machine handles subset of symbols
- **Partition risk manager**: Shard by client ID
- **Replicate market data**: Multiple UDP publishers
- **Expected**: 240K → 1M+ orders/sec (linear scaling)

**3. Hardware Acceleration (Hard - 3 months)**
- **io_uring**: Async I/O (Linux 5.1+) → -2μs latency
- **Kernel bypass (DPDK)**: Userspace networking → -1μs latency
- **FPGA matching**: Hardware order book → -5μs latency
- **Expected**: 1M+ orders/sec, <3μs latency

**Goldman Context:** Your systematic market making likely uses approach #2 (horizontal scaling) with approach #3 (kernel bypass) for ultra-low latency."

---

### Q3: "How do you handle market data sequencing and gap detection?"

**Answer:**
"Market data uses **sequence numbers** for gap detection:

```cpp
struct TradeMessage {
    uint64_t sequence;  // Monotonically increasing
    // ... trade data
};
```

**Client-side gap detection:**
```cpp
uint64_t expected_seq = last_seq + 1;
if (msg.sequence != expected_seq) {
    LOG_WARN("Gap detected", "expected", expected_seq, "got", msg.sequence);
    request_retransmission(last_seq + 1, msg.sequence - 1);
}
```

**Why this matters for market making:**
- Missing a trade update can cause **stale quotes** (adverse selection)
- Gap detection enables **fast recovery** (request retransmit)
- Sequence numbers provide **ordering guarantees** (critical for price discovery)

**Goldman Context:** Your market data feeds (e.g., OPRA, CTA) use similar sequencing. Handling gaps correctly is essential for systematic strategies."

---

### Q4: "What's your approach to risk management in market making?"

**Answer:**
"I implemented **6 pre-trade risk checks** (similar to production systems):

**1. Symbol Validation** (prevent fat-finger errors)
```cpp
if (!is_valid_symbol(order->symbol)) return REJECTED_SYMBOL;
```

**2. Size Limits** (prevent oversized orders)
```cpp
if (order->quantity > config.max_order_size) return REJECTED_SIZE;
```

**3. Price Collars** (prevent away-from-market orders)
```cpp
if (order->price < ref_price * 0.95 || order->price > ref_price * 1.05)
    return REJECTED_PRICE;
```

**4. Credit Limits** (prevent overexposure)
```cpp
double notional = order->quantity * order->price;
if (client_state.exposure + notional > config.credit_limit)
    return REJECTED_CREDIT;
```

**5. Rate Limiting** (prevent quote spam)
```cpp
if (client_state.orders_last_second > config.rate_limit)
    return REJECTED_RATE_LIMIT;
```

**6. Duplicate Detection** (prevent accidental double-sends)
```cpp
if (client_state.active_orders.contains(order->id))
    return REJECTED_DUPLICATE;
```

**Performance:** All 6 checks complete in ~1μs (12% of total latency).

**Goldman Context:** Your risk systems likely have additional checks (position limits, Greeks limits, concentration limits), but the principle is the same: **fail fast, fail safe**."

---

## KEY TALKING POINTS FOR GOLDMAN

### **1. Market Making Relevance**
- "This project taught me how **market microstructure** works—maker-taker dynamics, adverse selection, inventory risk"
- "I understand why **latency matters** for market makers: every microsecond increases adverse selection risk"
- "The risk management component mirrors what systematic market making desks need: credit limits, position limits, rate controls"

### **2. Technical Depth**
- "I can discuss **low-level optimizations**: cache-line alignment, memory ordering, SIMD, prefetching"
- "I understand **concurrency primitives**: lock-free queues, single-writer optimization, memory barriers"
- "I've done **production engineering**: error handling, observability, graceful degradation"

### **3. Business Acumen**
- "I understand the **economics of market making**: spread capture, inventory risk, adverse selection"
- "I know why **reliability matters**: one crash can lose millions in a production market making system"
- "I appreciate the **trade-offs**: sometimes 'good enough' (mutex) is better than 'perfect' (lock-free)"

### **4. Learning Agility**
- "I taught myself **C++20/23** (memory ordering, concepts, ranges)"
- "I learned **performance engineering** (profiling, cache optimization, lock-free algorithms)"
- "I understand **market microstructure** (price-time priority, maker-taker, liquidity provision)"

---

## CLOSING STATEMENT

> "This project gave me hands-on experience with the **core infrastructure** that powers systematic market making: low-latency order execution, risk management, and market data distribution. I understand the technical challenges (cache optimization, lock-free algorithms, tail latency) and the business context (spread capture, adverse selection, inventory risk). I'm excited about the opportunity to apply these skills to Goldman's equity systematic market making desk, where I can contribute to building and optimizing production trading systems at scale."

---

**Document Version**: 1.0 (Goldman Sachs Interview Prep)  
**Last Updated**: 2024  
**Target Role**: Equity Systematic Market Making Associate
