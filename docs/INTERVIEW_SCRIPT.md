# RTES Interview Script - Complete Explanation

## **Opening (30 seconds)**

"I built a high-performance trading exchange simulator in C++ that processes 150,000 orders per second with 8 microsecond average latency. It's designed like a real exchange with order matching, risk management, and market data publishing."

---

## **1. SYSTEM OVERVIEW** (2 minutes)

### **What It Does**
"The system accepts orders from traders via TCP, validates them for risk compliance, matches buy and sell orders to execute trades, and publishes market data via UDP multicast."

### **Architecture (Show architecture.puml)**
"There are 4 main components connected by lock-free queues:
1. **TCP Gateway** - Receives orders on port 8888
2. **Risk Manager** - Validates orders before trading
3. **Matching Engine** - Executes trades using price-time priority
4. **Market Data Publisher** - Broadcasts trades via UDP"

### **Key Metrics**
- Throughput: 150K orders/sec
- Latency: 8μs average, 85μs P99
- Memory: 1.5GB stable (no leaks)

---

## **2. COMPONENT DEEP DIVE** (10-15 minutes)

### **Component 1: TCP Gateway**

#### **What**
"Accepts client connections, parses binary protocol messages, validates checksums, and forwards orders to risk manager."

#### **Design Decisions**

**Decision 1: epoll vs select/poll**
- ✅ **Chose:** epoll (edge-triggered)
- **Why:** Scales to 10,000+ connections with O(1) event notification
- **Trade-off:** Linux-specific (not portable to Windows)
- **Alternative:** select() is portable but O(n) and limited to 1024 FDs

**Decision 2: Binary Protocol vs JSON**
- ✅ **Chose:** Binary protocol with packed structs
- **Why:** 10x faster parsing, 50% less bandwidth
- **Trade-off:** Harder to debug (can't read with curl)
- **Alternative:** JSON is human-readable but slow to parse

**Decision 3: TCP_NODELAY enabled**
- ✅ **Chose:** Disable Nagle's algorithm
- **Why:** Send packets immediately (low latency)
- **Trade-off:** More packets = higher bandwidth
- **Alternative:** Nagle's algorithm batches packets but adds 40ms delay

**Decision 4: Non-blocking I/O**
- ✅ **Chose:** Non-blocking sockets with epoll
- **Why:** One thread handles 1000s of connections
- **Trade-off:** More complex code (state machines)
- **Alternative:** Thread-per-connection is simpler but doesn't scale

#### **Latency Budget: 2μs**

---

### **Component 2: Risk Manager**

#### **What**
"Validates every order against 6 risk checks before allowing it to trade."

#### **Design Decisions**

**Decision 1: Single-threaded vs Multi-threaded**
- ✅ **Chose:** Single-threaded
- **Why:** All client state in one thread (no locks, no races)
- **Trade-off:** Can't scale beyond one CPU core
- **Alternative:** Multi-threaded with locks is slower due to contention

**Decision 2: Synchronous vs Asynchronous Validation**
- ✅ **Chose:** Synchronous (blocking)
- **Why:** Deterministic validation order, simpler logic
- **Trade-off:** Can't parallelize checks
- **Alternative:** Async validation is complex and non-deterministic

**Decision 3: In-Memory State vs Database**
- ✅ **Chose:** In-memory hash maps
- **Why:** O(1) lookup, no I/O latency
- **Trade-off:** State lost on crash (no persistence)
- **Alternative:** Database is durable but adds 1-10ms latency

**Decision 4: Fail-Fast Validation**
- ✅ **Chose:** Stop at first failed check
- **Why:** Reject bad orders immediately
- **Trade-off:** Don't know all validation failures
- **Alternative:** Check all rules but slower

#### **Risk Checks (6 total)**
1. Symbol allowed? (whitelist)
2. Size within limits? (max 10K shares)
3. Price within collar? (±10% from reference)
4. Rate limit OK? (max 1000 orders/sec)
5. Duplicate order? (check active orders)
6. Credit available? (max $1M notional)

#### **Latency Budget: 1μs**

---

### **Component 3: Matching Engine & Order Book**

#### **What**
"Maintains order book per symbol, matches orders using price-time priority, executes trades."

#### **Design Decisions**

**Decision 1: Single-Writer per Symbol**
- ✅ **Chose:** One thread per symbol (AAPL, MSFT, etc.)
- **Why:** No lock contention, deterministic matching
- **Trade-off:** Can't scale one symbol beyond one core
- **Alternative:** Multi-threaded with locks is 10x slower

**Decision 2: Price-Time Priority Algorithm**
- ✅ **Chose:** Best price first, then FIFO within price
- **Why:** Industry standard, fair to all participants
- **Trade-off:** Pro-rata would be fairer for large orders
- **Alternative:** Pro-rata splits fills proportionally

**Decision 3: Data Structure for Order Book**
- ✅ **Chose:** `map<Price, deque<Order*>>`
- **Why:** 
  - map: O(log n) best price lookup, sorted automatically
  - deque: O(1) push_back/pop_front for FIFO
- **Trade-off:** map has O(log n) insert (vs O(1) for hash map)
- **Alternative:** Skip list is O(log n) but more complex

**Decision 4: Cancellation Lookup**
- ✅ **Chose:** `unordered_map<OrderID, Order*>`
- **Why:** O(1) lookup for cancel requests
- **Trade-off:** Extra memory (8 bytes per order)
- **Alternative:** Linear search is O(n) - too slow

**Decision 5: Mutex per Symbol vs Lock-Free**
- ✅ **Chose:** Mutex (coarse-grained lock)
- **Why:** Single-writer makes lock uncontended (fast path)
- **Trade-off:** Lock overhead (~20ns)
- **Alternative:** Lock-free order book is complex and error-prone

**Decision 6: Cache Prefetching**
- ✅ **Chose:** `_mm_prefetch` for next orders
- **Why:** Reduces L1 cache miss from 80ns to 4ns
- **Trade-off:** x86-specific (not portable)
- **Alternative:** Let CPU prefetcher handle it (slower)

#### **Matching Example**
```
Order Book:
  Bids: $150.50 (100), $150.40 (50)
  Asks: $150.60 (200), $150.70 (150)

New Order: BUY 100 @ $150.00 (limit)
  → Check crossing: $150.00 >= $150.60? NO
  → Add to book at $150.00
  → Status: ACCEPTED

New Order: SELL 50 @ $150.50 (limit)
  → Check crossing: $150.50 <= $150.50? YES
  → Match with bid at $150.50
  → Execute 50 shares @ $150.50
  → Status: FILLED
```

#### **Latency Budget: 5μs**

---

### **Component 4: Market Data Publisher**

#### **What**
"Publishes trades and BBO (Best Bid/Offer) updates via UDP multicast."

#### **Design Decisions**

**Decision 1: UDP vs TCP**
- ✅ **Chose:** UDP multicast
- **Why:** Low latency, one-to-many broadcast
- **Trade-off:** Packet loss possible (no reliability)
- **Alternative:** TCP requires connection per subscriber (doesn't scale)

**Decision 2: Multicast vs Unicast**
- ✅ **Chose:** Multicast (239.0.0.1:9999)
- **Why:** One packet reaches all subscribers
- **Trade-off:** Requires multicast-enabled network
- **Alternative:** Unicast sends N packets for N subscribers

**Decision 3: HMAC Authentication**
- ✅ **Chose:** HMAC-SHA256 on each message
- **Why:** Prevents spoofed market data
- **Trade-off:** 500ns overhead per message
- **Alternative:** No auth is faster but insecure

**Decision 4: Message Format**
- ✅ **Chose:** Binary with sequence numbers
- **Why:** Detect gaps, compact format
- **Trade-off:** Not human-readable
- **Alternative:** JSON is readable but 5x larger

#### **Latency Budget: 1μs**

---

## **3. CROSS-CUTTING CONCERNS** (5 minutes)

### **Memory Management**

**Decision: Memory Pool vs malloc/free**
- ✅ **Chose:** Pre-allocated pool of 1M orders
- **Why:** 
  - Zero allocations in hot path
  - O(1) alloc/dealloc (vs ~100ns for malloc)
  - No fragmentation
- **Trade-off:** Fixed capacity (pool can exhaust)
- **Alternative:** malloc/free is flexible but slow and non-deterministic

**Pool Design:**
```cpp
class MemoryPool<Order> {
    vector<Order> pool_;        // Pre-allocated
    vector<size_t> free_list_;  // Available indices
    atomic<size_t> free_count_; // Lock-free counter
};
```

---

### **Concurrency Model**

**Decision: Lock-Free Queues vs Mutexes**
- ✅ **Chose:** Lock-free SPSC/MPMC queues
- **Why:**
  - SPSC: 10x faster than mutex for 1-to-1 threads
  - MPMC: Scales to multiple producers/consumers
  - No deadlocks, no priority inversion
- **Trade-off:** Complex implementation, ABA problem
- **Alternative:** Mutex-protected queues are simpler but slower

**SPSC Queue Design:**
```cpp
class SPSCQueue<T> {
    alignas(64) atomic<size_t> head_;  // Producer
    alignas(64) atomic<size_t> tail_;  // Consumer
    T[] buffer_;                        // Ring buffer
    
    // Producer: head_.fetch_add(1, memory_order_release)
    // Consumer: tail_.fetch_add(1, memory_order_acquire)
};
```

**Why cache-line alignment (64 bytes)?**
- Prevents false sharing (CPU cache line = 64 bytes)
- Without alignment: 50% performance loss

---

### **Threading Model**

**Decision: Thread-per-Symbol vs Thread Pool**
- ✅ **Chose:** Dedicated thread per symbol
- **Why:** 
  - Single-writer (no locks in order book)
  - CPU cache stays hot for one symbol
  - Deterministic latency
- **Trade-off:** 1000 symbols = 1000 threads (doesn't scale)
- **Alternative:** Thread pool shares threads but adds contention

**Thread Architecture (8 threads):**
1. TCP Acceptor (accept connections)
2. TCP Worker (epoll I/O)
3. Risk Manager (validation)
4. Matching - AAPL (order book)
5. Matching - MSFT (order book)
6. Matching - GOOGL (order book)
7. UDP Publisher (market data)
8. Metrics Server (Prometheus)

---

### **Protocol Design**

**Decision: Fixed-Size Headers vs Variable**
- ✅ **Chose:** Fixed-size MessageHeader (24 bytes)
- **Why:** Cache-friendly, predictable memory layout
- **Trade-off:** Wastes space if fields unused
- **Alternative:** Variable-length is compact but slower to parse

**Message Structure:**
```cpp
struct MessageHeader {
    uint32_t type;       // NEW_ORDER, CANCEL, etc.
    uint32_t length;     // Total message size
    uint64_t sequence;   // Monotonic counter
    uint64_t timestamp;  // Nanoseconds
    uint32_t checksum;   // CRC32 for integrity
} __attribute__((packed));  // No padding
```

**Why CRC32 vs SHA256?**
- CRC32: 10ns per byte, detects corruption
- SHA256: 100ns per byte, cryptographic security
- **Chose CRC32:** Speed matters more than crypto

---

### **Error Handling**

**Decision: Result<T> Monad vs Exceptions**
- ✅ **Chose:** Result<T> with error codes
- **Why:**
  - Zero overhead in hot path (no stack unwinding)
  - Explicit error handling (can't ignore)
  - Deterministic performance
- **Trade-off:** More verbose code
- **Alternative:** Exceptions are cleaner but add 1-10μs overhead

**Result Pattern:**
```cpp
Result<void> add_order(Order* order) {
    if (!order) return ErrorCode::ORDER_INVALID;
    if (duplicate) return ErrorCode::ORDER_DUPLICATE;
    return Result<void>();  // Success
}
```

---

## **4. PERFORMANCE OPTIMIZATIONS** (3 minutes)

### **Optimization 1: Cache Prefetching**
```cpp
_mm_prefetch(next_order, _MM_HINT_T0);  // Load into L1 cache
```
- **Impact:** 35% reduction in cache misses
- **Cost:** x86-specific code

### **Optimization 2: Branch Prediction**
```cpp
if (__builtin_expect(order->quantity > 0, 1)) {  // Likely
    // Hot path
}
```
- **Impact:** 10% faster on hot path
- **Cost:** GCC-specific

### **Optimization 3: Conditional Moves**
```cpp
order->status = (qty == 0) ? FILLED : PARTIALLY_FILLED;
// Compiles to: cmov (no branch misprediction)
```
- **Impact:** Eliminates branch misprediction penalty
- **Cost:** None

### **Optimization 4: Cache-Line Alignment**
```cpp
alignas(64) atomic<size_t> head_;
alignas(64) atomic<size_t> tail_;
```
- **Impact:** 50% faster queue operations
- **Cost:** 64 bytes wasted per atomic

### **Optimization 5: Fixed-Point Arithmetic**
```cpp
Price = uint64_t;  // price * 10000
// $100.50 = 1005000
```
- **Impact:** Deterministic, no rounding errors
- **Cost:** Manual scaling required

---

## **5. TRADE-OFFS SUMMARY** (2 minutes)

### **Performance vs Complexity**
- ✅ Chose: Performance (lock-free, cache optimization)
- Cost: Complex code, harder to debug

### **Latency vs Throughput**
- ✅ Chose: Latency (single-writer, no batching)
- Cost: Lower max throughput per symbol

### **Memory vs Speed**
- ✅ Chose: Speed (memory pools, extra indexes)
- Cost: 1.5GB memory footprint

### **Portability vs Performance**
- ✅ Chose: Performance (epoll, _mm_prefetch)
- Cost: Linux/x86 only

### **Simplicity vs Scalability**
- ✅ Chose: Simplicity (in-memory, single-writer)
- Cost: Limited to single-host deployment

---

## **6. WHAT I'D DO DIFFERENTLY** (1 minute)

### **For Production**
1. **Add Persistence:** Write-ahead log for crash recovery
2. **Add Replication:** Multi-region deployment
3. **Add Monitoring:** Distributed tracing (OpenTelemetry)
4. **Add Testing:** Chaos engineering, fault injection

### **For Scale**
1. **Symbol Sharding:** Distribute symbols across hosts
2. **Lock-Free Order Book:** Eliminate mutex entirely
3. **RDMA Networking:** Bypass kernel for <1μs latency
4. **FPGA Matching:** Hardware acceleration for <100ns

---

## **7. CLOSING** (30 seconds)

"This project demonstrates low-latency system design with lock-free concurrency, cache optimization, and zero-allocation hot paths. Every design decision was made with the 10μs latency target in mind, and we achieved 8μs average with 150K orders/sec throughput."

---

## **QUICK REFERENCE CARD**

### **Key Numbers to Remember**
- **Latency:** 8μs avg, 85μs P99, 450μs P999
- **Throughput:** 150K orders/sec
- **Memory:** 1.5GB stable
- **Threads:** 8 total (1 per symbol + infrastructure)
- **Queue Capacity:** 65K orders per queue
- **Pool Size:** 1M pre-allocated orders

### **Key Design Decisions**
1. **epoll** for scalable I/O
2. **Binary protocol** for speed
3. **Single-writer** per symbol
4. **Lock-free queues** for thread communication
5. **Memory pools** for zero allocations
6. **Cache prefetching** for performance
7. **Fixed-point** arithmetic for determinism
8. **Result<T>** for error handling

### **Key Trade-Offs**
- Performance > Portability
- Latency > Throughput
- Speed > Memory
- Simplicity > Scalability (for now)

---

## **INTERVIEW TIPS**

### **When Asked "Why X?"**
Always structure answer as:
1. What we chose
2. Why we chose it
3. What we gave up
4. What the alternative was

### **Example:**
"We chose lock-free SPSC queues because they're 10x faster than mutexes for 1-to-1 thread communication. The trade-off is complexity - they're harder to implement correctly. The alternative was mutex-protected queues, which are simpler but add 100ns overhead per operation."

### **Red Flags to Avoid**
- ❌ "It's the best way" (no absolutes)
- ❌ "I didn't consider alternatives" (always have alternatives)
- ❌ "No trade-offs" (everything has trade-offs)
- ❌ "I'd do it the same way" (show growth mindset)

### **Green Flags to Hit**
- ✅ Mention specific numbers (8μs, 150K/sec)
- ✅ Explain trade-offs explicitly
- ✅ Reference alternatives considered
- ✅ Discuss what you'd improve

Good luck! 🚀
