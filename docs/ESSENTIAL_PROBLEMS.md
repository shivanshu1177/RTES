# Essential Problems & Challenges in Building RTES

## **CRITICAL PROBLEMS YOU MUST UNDERSTAND**

These are the fundamental challenges that make low-latency trading systems difficult. You should be able to discuss each one in depth.

---

## **1. CONCURRENCY PROBLEMS**

### **Problem 1.1: Race Conditions**
**What:** Multiple threads accessing shared data simultaneously

**Example in RTES:**
```cpp
// BAD: Race condition
class OrderBook {
    std::map<Price, PriceLevel> bids_;  // Shared data
    
    void add_order(Order* order) {
        bids_[order->price].orders.push_back(order);  // ❌ RACE!
    }
};

// Thread 1: add_order(order1)
// Thread 2: add_order(order2)
// Result: Corrupted data structure
```

**Your Solution:**
- Single-writer per symbol (only one thread accesses each order book)
- Mutex for order book operations
- Lock-free queues for inter-thread communication

**Interview Question:** "How do you prevent race conditions?"
**Answer:** "Single-writer design per symbol eliminates races in the order book. Lock-free SPSC/MPMC queues handle inter-thread communication without locks."

---

### **Problem 1.2: Deadlocks**
**What:** Two threads waiting for each other's locks

**Example:**
```cpp
// BAD: Potential deadlock
Thread 1: lock(mutex_A) → lock(mutex_B)
Thread 2: lock(mutex_B) → lock(mutex_A)
// Result: Both threads stuck forever
```

**Your Solution:**
- Minimize lock usage (single-writer design)
- Lock-free queues (no locks = no deadlocks)
- When locks needed, consistent lock ordering

**Interview Question:** "How do you avoid deadlocks?"
**Answer:** "I minimize lock usage through single-writer design and lock-free queues. When locks are necessary, I maintain a consistent lock hierarchy."

---

### **Problem 1.3: False Sharing**
**What:** Two threads accessing different variables on same cache line

**Example:**
```cpp
// BAD: False sharing
struct Queue {
    atomic<size_t> head_;  // Byte 0-7
    atomic<size_t> tail_;  // Byte 8-15
    // Both on same 64-byte cache line!
};

// Thread 1 writes head_ → invalidates entire cache line
// Thread 2 writes tail_ → invalidates entire cache line
// Result: 50% performance loss
```

**Your Solution:**
```cpp
// GOOD: Cache-line aligned
struct Queue {
    alignas(64) atomic<size_t> head_;  // Bytes 0-63
    alignas(64) atomic<size_t> tail_;  // Bytes 64-127
    // Different cache lines!
};
```

**Interview Question:** "What is false sharing and how do you prevent it?"
**Answer:** "False sharing occurs when threads access different variables on the same cache line, causing unnecessary cache invalidations. I prevent it with 64-byte alignment using alignas(64)."

---

### **Problem 1.4: Memory Ordering**
**What:** CPU reordering instructions for performance

**Example:**
```cpp
// BAD: No memory ordering
atomic<bool> ready{false};
int data = 0;

// Thread 1 (Producer)
data = 42;           // May execute AFTER next line!
ready.store(true);   // CPU can reorder

// Thread 2 (Consumer)
if (ready.load()) {
    use(data);       // May see data = 0!
}
```

**Your Solution:**
```cpp
// GOOD: Proper memory ordering
atomic<bool> ready{false};
int data = 0;

// Thread 1
data = 42;
ready.store(true, memory_order_release);  // Barrier

// Thread 2
if (ready.load(memory_order_acquire)) {   // Barrier
    use(data);  // Guaranteed to see data = 42
}
```

**Interview Question:** "Explain memory ordering in lock-free programming"
**Answer:** "Memory ordering ensures operations happen in the correct sequence. I use memory_order_release for producers and memory_order_acquire for consumers to create happens-before relationships."

---

## **2. PERFORMANCE PROBLEMS**

### **Problem 2.1: Memory Allocation Overhead**
**What:** malloc/free are slow (~100ns) and non-deterministic

**Example:**
```cpp
// BAD: Allocation in hot path
void process_order() {
    Order* order = new Order();  // ❌ 100ns overhead!
    // ... process ...
    delete order;
}
```

**Your Solution:**
```cpp
// GOOD: Memory pool
class MemoryPool {
    vector<Order> pool_;  // Pre-allocated at startup
    
    Order* allocate() {
        return &pool_[free_list_[--free_count_]];  // O(1), ~5ns
    }
};
```

**Interview Question:** "Why avoid malloc in the hot path?"
**Answer:** "malloc is slow (~100ns), non-deterministic, and causes fragmentation. I use pre-allocated memory pools for O(1) allocation with ~5ns overhead."

---

### **Problem 2.2: Cache Misses**
**What:** Accessing data not in CPU cache (80ns penalty)

**Example:**
```cpp
// BAD: Cache miss on every access
for (Order* order : orders) {
    process(order);  // ❌ Cache miss if orders scattered in memory
}
```

**Your Solution:**
```cpp
// GOOD: Cache prefetching
for (size_t i = 0; i < orders.size(); ++i) {
    if (i + 1 < orders.size()) {
        _mm_prefetch(orders[i+1], _MM_HINT_T0);  // Prefetch next
    }
    process(orders[i]);  // Cache hit!
}
```

**Interview Question:** "How do you optimize for CPU cache?"
**Answer:** "I use cache prefetching with _mm_prefetch to load data into L1 cache before accessing it. I also align data structures to cache lines and keep hot data contiguous."

---

### **Problem 2.3: System Call Overhead**
**What:** Syscalls (read/write) are expensive (~1μs)

**Example:**
```cpp
// BAD: Syscall per order
for (Order order : orders) {
    write(socket, &order, sizeof(order));  // ❌ 1μs per order!
}
```

**Your Solution:**
```cpp
// GOOD: Batch syscalls
vector<Order> batch;
for (Order order : orders) {
    batch.push_back(order);
    if (batch.size() >= 100) {
        write(socket, batch.data(), batch.size() * sizeof(Order));
        batch.clear();
    }
}
```

**Interview Question:** "How do you minimize syscall overhead?"
**Answer:** "I use non-blocking I/O with epoll to batch operations, reducing syscalls from thousands to dozens per second. I also use large socket buffers to minimize write() calls."

---

### **Problem 2.4: Context Switches**
**What:** OS switching between threads (~1-10μs)

**Example:**
```cpp
// BAD: Frequent context switches
while (true) {
    if (!queue.pop(item)) {
        sleep(1ms);  // ❌ Context switch!
    }
}
```

**Your Solution:**
```cpp
// GOOD: Spin briefly, then yield
while (true) {
    if (!queue.pop(item)) {
        for (int i = 0; i < 100; ++i) {
            if (queue.pop(item)) break;
            _mm_pause();  // Spin without context switch
        }
        std::this_thread::yield();  // Then yield
    }
}
```

**Interview Question:** "How do you minimize context switches?"
**Answer:** "I use lock-free queues with brief spinning before yielding. I also pin threads to specific CPU cores to avoid migration overhead."

---

## **3. NETWORKING PROBLEMS**

### **Problem 3.1: Nagle's Algorithm**
**What:** TCP batches small packets (adds 40ms delay)

**Example:**
```cpp
// BAD: Nagle's algorithm enabled
send(socket, small_message, size, 0);  // ❌ Waits 40ms to batch
```

**Your Solution:**
```cpp
// GOOD: Disable Nagle's algorithm
int flag = 1;
setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
send(socket, small_message, size, 0);  // Sends immediately
```

**Interview Question:** "What is Nagle's algorithm and why disable it?"
**Answer:** "Nagle's algorithm batches small TCP packets to reduce overhead, but adds 40ms delay. I disable it with TCP_NODELAY for low-latency trading where every microsecond matters."

---

### **Problem 3.2: Scalability with select/poll**
**What:** select/poll are O(n) in number of connections

**Example:**
```cpp
// BAD: O(n) scalability
fd_set readfds;
for (int fd : connections) {
    FD_SET(fd, &readfds);  // O(n)
}
select(max_fd, &readfds, ...);  // O(n) scan
```

**Your Solution:**
```cpp
// GOOD: O(1) with epoll
int epoll_fd = epoll_create1(0);
epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);  // O(1)
epoll_wait(epoll_fd, events, max_events, timeout);   // O(k) where k = ready FDs
```

**Interview Question:** "Why use epoll instead of select?"
**Answer:** "select is O(n) in connections and limited to 1024 FDs. epoll is O(1) for adding FDs and O(k) for waiting, where k is the number of ready FDs. This scales to 10,000+ connections."

---

### **Problem 3.3: UDP Packet Loss**
**What:** UDP doesn't guarantee delivery

**Example:**
```cpp
// BAD: No loss detection
sendto(socket, &trade, sizeof(trade), ...);  // May be lost!
```

**Your Solution:**
```cpp
// GOOD: Sequence numbers for gap detection
struct MarketDataMessage {
    uint64_t sequence;  // Monotonic counter
    // ... data ...
};

// Receiver detects gaps
if (msg.sequence != expected_sequence) {
    LOG_WARN("Gap detected: expected {} got {}", expected_sequence, msg.sequence);
}
```

**Interview Question:** "How do you handle UDP packet loss?"
**Answer:** "I include sequence numbers in every message so receivers can detect gaps. For critical data, I'd add a retransmission mechanism or use reliable multicast protocols like PGM."

---

## **4. DATA STRUCTURE PROBLEMS**

### **Problem 4.1: Order Book Efficiency**
**What:** Need O(1) cancel, O(log n) best price, O(1) FIFO

**Example:**
```cpp
// BAD: Linear search for cancel
vector<Order*> orders;

void cancel(OrderID id) {
    for (auto it = orders.begin(); it != orders.end(); ++it) {
        if ((*it)->id == id) {
            orders.erase(it);  // ❌ O(n)
            break;
        }
    }
}
```

**Your Solution:**
```cpp
// GOOD: Multiple data structures
class OrderBook {
    map<Price, deque<Order*>> bids_;           // O(log n) best price
    unordered_map<OrderID, Order*> lookup_;    // O(1) cancel
    
    void cancel(OrderID id) {
        auto it = lookup_.find(id);  // O(1)
        if (it != lookup_.end()) {
            remove_from_book(it->second);
            lookup_.erase(it);
        }
    }
};
```

**Interview Question:** "Why use multiple data structures for the order book?"
**Answer:** "I use std::map for O(log n) sorted price levels, std::deque for O(1) FIFO within each level, and std::unordered_map for O(1) order cancellation. Each structure optimizes a different operation."

---

### **Problem 4.2: Priority Queue for Matching**
**What:** Need best price quickly

**Example:**
```cpp
// BAD: Unsorted orders
vector<Order*> orders;

Order* get_best() {
    return *min_element(orders.begin(), orders.end(),
        [](Order* a, Order* b) { return a->price < b->price; });  // ❌ O(n)
}
```

**Your Solution:**
```cpp
// GOOD: Sorted map
map<Price, PriceLevel, greater<Price>> bids_;  // Descending

Order* get_best_bid() {
    return bids_.begin()->second.orders.front();  // O(1)
}
```

**Interview Question:** "How do you efficiently find the best bid/ask?"
**Answer:** "I use std::map which maintains sorted order. Best bid is bids_.begin() and best ask is asks_.begin(), both O(1) operations."

---

## **5. CORRECTNESS PROBLEMS**

### **Problem 5.1: Price-Time Priority**
**What:** Orders at same price must execute in time order (FIFO)

**Example:**
```cpp
// BAD: No time ordering
set<Order*> orders;  // ❌ No FIFO guarantee
```

**Your Solution:**
```cpp
// GOOD: FIFO queue per price level
struct PriceLevel {
    Price price;
    deque<Order*> orders;  // FIFO queue
};

map<Price, PriceLevel> bids_;
```

**Interview Question:** "How do you ensure price-time priority?"
**Answer:** "I use std::map for price priority (sorted) and std::deque for time priority (FIFO) within each price level. Orders at the same price execute in the order they arrived."

---

### **Problem 5.2: Partial Fills**
**What:** Order may match multiple times

**Example:**
```cpp
// BAD: Doesn't handle partial fills
if (order->quantity <= passive->quantity) {
    execute_trade(order, passive);
    // ❌ What if order->quantity < passive->quantity?
}
```

**Your Solution:**
```cpp
// GOOD: Track remaining quantity
Quantity trade_qty = min(order->remaining_quantity, 
                         passive->remaining_quantity);
execute_trade(order, passive, trade_qty);

order->remaining_quantity -= trade_qty;
passive->remaining_quantity -= trade_qty;

if (order->remaining_quantity > 0) {
    continue_matching(order);  // Keep matching
}
```

**Interview Question:** "How do you handle partial fills?"
**Answer:** "I track remaining_quantity separately from original quantity. After each trade, I update remaining_quantity and continue matching until the order is fully filled or no more matches exist."

---

### **Problem 5.3: Order Lifecycle Management**
**What:** Orders transition through multiple states

**Example:**
```cpp
// BAD: No state tracking
void cancel_order(OrderID id) {
    delete orders[id];  // ❌ What if already filled?
}
```

**Your Solution:**
```cpp
// GOOD: State machine
enum class OrderStatus {
    PENDING, ACCEPTED, PARTIALLY_FILLED, FILLED, CANCELLED, REJECTED
};

void cancel_order(OrderID id) {
    Order* order = lookup_[id];
    if (order->status == FILLED) {
        return;  // Can't cancel filled order
    }
    order->status = CANCELLED;
    remove_from_book(order);
}
```

**Interview Question:** "How do you manage order lifecycle?"
**Answer:** "I use a state machine with 6 states: PENDING → ACCEPTED → PARTIALLY_FILLED → FILLED. Orders can also transition to CANCELLED or REJECTED. State transitions are validated to prevent invalid operations like canceling a filled order."

---

## **6. RELIABILITY PROBLEMS**

### **Problem 6.1: Memory Leaks**
**What:** Allocated memory never freed

**Example:**
```cpp
// BAD: Memory leak
void process_order() {
    Order* order = new Order();
    if (validate(order)) {
        add_to_book(order);
    }
    // ❌ Leak if validation fails!
}
```

**Your Solution:**
```cpp
// GOOD: RAII with smart pointers
void process_order() {
    auto order = pool_.allocate();  // From pool
    if (validate(order)) {
        add_to_book(order);
    } else {
        pool_.deallocate(order);  // Always freed
    }
}
```

**Interview Question:** "How do you prevent memory leaks?"
**Answer:** "I use RAII principles with memory pools. Every allocation has a corresponding deallocation. I also use Valgrind and AddressSanitizer to detect leaks during testing."

---

### **Problem 6.2: Buffer Overflows**
**What:** Writing beyond buffer bounds

**Example:**
```cpp
// BAD: Buffer overflow
char buffer[100];
strcpy(buffer, user_input);  // ❌ What if user_input > 100?
```

**Your Solution:**
```cpp
// GOOD: Bounds checking
template<size_t N>
class BoundedString {
    char data_[N];
    size_t size_;
    
    void assign(const char* str) {
        size_t len = strlen(str);
        if (len >= N) {
            throw BufferOverflowError();
        }
        memcpy(data_, str, len);
        size_ = len;
    }
};
```

**Interview Question:** "How do you prevent buffer overflows?"
**Answer:** "I use fixed-size buffers with compile-time bounds checking. All string operations validate length before copying. I also use AddressSanitizer to detect overflows during testing."

---

### **Problem 6.3: Integer Overflow**
**What:** Arithmetic exceeds type limits

**Example:**
```cpp
// BAD: Integer overflow
uint32_t price = 1000000;
uint32_t quantity = 10000;
uint32_t notional = price * quantity;  // ❌ Overflow!
```

**Your Solution:**
```cpp
// GOOD: Use larger type
uint64_t price = 1000000;
uint64_t quantity = 10000;
uint64_t notional = price * quantity;  // ✓ No overflow

// Or check before operation
if (price > UINT32_MAX / quantity) {
    return ErrorCode::OVERFLOW;
}
```

**Interview Question:** "How do you handle integer overflow?"
**Answer:** "I use uint64_t for calculations that might overflow uint32_t. For critical operations, I check for overflow before performing arithmetic. I also enable UndefinedBehaviorSanitizer to detect overflows."

---

## **7. TESTING PROBLEMS**

### **Problem 7.1: Race Condition Testing**
**What:** Races are non-deterministic and hard to reproduce

**Your Solution:**
- ThreadSanitizer (TSAN) to detect races
- Stress tests with many threads
- Chaos engineering (inject delays, failures)

**Interview Question:** "How do you test for race conditions?"
**Answer:** "I use ThreadSanitizer to detect data races during testing. I also run stress tests with 100+ threads to expose timing-dependent bugs. For production, I use single-writer design to eliminate most races."

---

### **Problem 7.2: Performance Regression**
**What:** Changes accidentally slow down the system

**Your Solution:**
- Benchmark suite with latency/throughput tests
- CI/CD runs benchmarks on every commit
- Alert if latency increases >10%

**Interview Question:** "How do you prevent performance regressions?"
**Answer:** "I have a benchmark suite that measures P50/P99/P999 latency and throughput. CI/CD runs these on every commit and fails if latency increases >10%. I also profile with perf to identify hotspots."

---

## **INTERVIEW PREPARATION CHECKLIST**

### **Must Be Able to Explain:**
- ✅ Race conditions and how you prevent them
- ✅ False sharing and cache-line alignment
- ✅ Memory ordering (acquire/release)
- ✅ Why memory pools over malloc
- ✅ Cache prefetching benefits
- ✅ epoll vs select scalability
- ✅ TCP_NODELAY and Nagle's algorithm
- ✅ Price-time priority algorithm
- ✅ Partial fill handling
- ✅ Order lifecycle state machine

### **Must Be Able to Discuss Trade-offs:**
- ✅ Single-writer vs multi-threaded
- ✅ Lock-free vs mutex
- ✅ Memory pool vs malloc
- ✅ Binary protocol vs JSON
- ✅ UDP vs TCP
- ✅ In-memory vs persistent

### **Must Know Performance Numbers:**
- ✅ Cache miss: ~80ns
- ✅ Mutex lock: ~20ns (uncontended)
- ✅ malloc: ~100ns
- ✅ Syscall: ~1μs
- ✅ Context switch: ~1-10μs
- ✅ Network RTT: ~100μs (local), ~10ms (cross-country)

---

## **FINAL TAKEAWAY**

**These problems are WHY low-latency systems are hard:**
1. Concurrency (races, deadlocks, false sharing)
2. Performance (cache misses, allocations, syscalls)
3. Networking (Nagle, scalability, packet loss)
4. Data structures (efficiency, correctness)
5. Reliability (leaks, overflows, races)
6. Testing (non-determinism, regressions)

**Your system solves these through:**
- Single-writer design (eliminates races)
- Lock-free queues (no contention)
- Memory pools (no allocations)
- Cache optimization (prefetching, alignment)
- epoll (scalable I/O)
- Comprehensive testing (TSAN, ASAN, benchmarks)

**Be ready to discuss each problem and your solution in depth!** 🎯
