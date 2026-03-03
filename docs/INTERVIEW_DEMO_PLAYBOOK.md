# RTES Interview Demonstration Playbook

## **PREPARATION CHECKLIST (Before Interview)**

### **1. Environment Setup (5 minutes)**
```bash
# Clone and build
cd ~/projects/RTES
git pull origin main
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Verify binaries exist
ls -lh trading_exchange client_simulator tcp_client udp_receiver

# Test run
./trading_exchange ../configs/config.json &
sleep 2
pkill trading_exchange
```

### **2. Have These Files Open**
- `docs/architecture.puml` (rendered diagram)
- `docs/order_flow.puml` (rendered diagram)
- `README.md` (quick reference)
- `src/order_book.cpp` (code walkthrough)
- `include/rtes/spsc_queue.hpp` (lock-free example)

### **3. Terminal Setup**
- **Terminal 1:** Exchange server
- **Terminal 2:** Client simulator
- **Terminal 3:** Metrics monitoring
- **Terminal 4:** Market data receiver

---

## **DEMONSTRATION SCRIPT**

### **PHASE 1: OVERVIEW (2 minutes)**

**What to Say:**
> "Let me show you the system running. I'll start the exchange, send some orders, and show you the metrics in real-time."

**Actions:**
```bash
# Terminal 1: Start exchange
cd ~/projects/RTES/build
./trading_exchange ../configs/config.json

# You should see:
# [INFO] Order pool initialized: 1000000 orders
# [INFO] Risk manager started
# [INFO] Matching engine started for symbol: AAPL
# [INFO] TCP gateway started on port 8888
# [INFO] UDP publisher started on 239.0.0.1:9999
# [INFO] Metrics server started on port 8080
```

**What to Point Out:**
- "Notice the initialization order: pool → risk → matching → gateway"
- "Each component starts its own thread"
- "Total startup time: ~50ms"

---

### **PHASE 2: ARCHITECTURE WALKTHROUGH (3 minutes)**

**Show Diagram:**
Open `docs/architecture.puml` (rendered)

**What to Say:**
> "The architecture has 4 main components connected by lock-free queues:
> 
> 1. **TCP Gateway** receives orders via epoll
> 2. **Risk Manager** validates with 6 checks
> 3. **Matching Engine** executes trades (one thread per symbol)
> 4. **Market Data** publishes via UDP multicast
>
> The key insight is single-writer per symbol - no lock contention in the order book."

**Point to Specific Parts:**
- SPSC queues (Gateway → Risk → Matching)
- MPMC queue (Matching → Market Data)
- Memory pool (shared, lock-free allocation)

---

### **PHASE 3: LIVE ORDER SUBMISSION (5 minutes)**

#### **Demo 3.1: Single Order**

**What to Say:**
> "Let me submit a single order and trace it through the system."

**Actions:**
```bash
# Terminal 2: Send one order
./tcp_client --host localhost --port 8888 \
  --order "BUY,AAPL,100,150.00,LIMIT"

# You should see in Terminal 1:
# [INFO] Order received: id=1 client=CLIENT_001 symbol=AAPL
# [INFO] Risk check passed: id=1
# [INFO] Order accepted: id=1 price=150.00 quantity=100
# [INFO] Added to book: AAPL bid at 150.00
```

**What to Point Out:**
- "Order ID 1 assigned"
- "Risk validation took ~1μs"
- "Order added to book at $150.00"
- "No match because no crossing orders"

---

#### **Demo 3.2: Matching Orders**

**What to Say:**
> "Now let me send a crossing order to trigger a trade."

**Actions:**
```bash
# Terminal 2: Send crossing order
./tcp_client --host localhost --port 8888 \
  --order "SELL,AAPL,50,150.00,LIMIT"

# You should see in Terminal 1:
# [INFO] Order received: id=2 client=CLIENT_002 symbol=AAPL
# [INFO] Risk check passed: id=2
# [INFO] Trade executed: buy_id=1 sell_id=2 price=150.00 qty=50
# [INFO] Order partially filled: id=1 remaining=50
# [INFO] Order filled: id=2
# [INFO] BBO update: bid=150.00(50) ask=N/A
```

**What to Point Out:**
- "Order 2 crosses with Order 1"
- "Trade executed at $150.00 (passive order's price)"
- "Order 1 partially filled (50 remaining)"
- "Order 2 fully filled"
- "BBO updated and published via UDP"

---

#### **Demo 3.3: Market Making Strategy**

**What to Say:**
> "Now let me show you a market making strategy that continuously quotes bid/ask prices."

**Actions:**
```bash
# Terminal 2: Run market maker strategy
./client_simulator --strategy market_maker \
  --symbol AAPL --spread 10 --size 100

# You should see in Terminal 1:
# [INFO] Market maker started for AAPL
# [INFO] Order received: BUY AAPL 100 @ 149.90 (bid)
# [INFO] Order received: SELL AAPL 100 @ 150.10 (ask)
# [INFO] Order accepted: bid at 149.90
# [INFO] Order accepted: ask at 150.10
# [INFO] Trade executed: buy_id=1 sell_id=3 price=150.10 qty=50
# [INFO] Ask order partially filled
# [INFO] Canceling existing orders
# [INFO] Updating quotes: bid=149.85, ask=150.05
```

**What to Point Out:**
- "Market maker quotes both sides: bid $149.90, ask $150.10"
- "Spread: 20 cents (10 ticks each side)"
- "When hit, immediately cancels and requotes"
- "Adjusts prices based on recent trades"
- "This is how real market makers provide liquidity"

**Explain the Strategy:**
```cpp
// From strategies.cpp
void MarketMakerStrategy::update_quotes() {
    cancel_existing_orders();  // Cancel old quotes
    
    // Calculate bid/ask around base price
    uint64_t bid_price = base_price_ - spread_ticks_;  // $149.90
    uint64_t ask_price = base_price_ + spread_ticks_;  // $150.10
    
    // Send new quotes
    send_new_order(symbol_, Side::BUY, quote_size_, bid_price);
    send_new_order(symbol_, Side::SELL, quote_size_, ask_price);
}

void MarketMakerStrategy::on_trade(const TradeMessage& trade) {
    base_price_ = trade.price;  // Adjust to market
    cancel_existing_orders();
    update_quotes();  // Requote immediately
}
```

**Key Points:**
- "Continuously provides liquidity on both sides"
- "Captures bid-ask spread as profit"
- "Adjusts to market conditions (base_price updates)"
- "Cancels and requotes when filled (inventory management)"

---

#### **Demo 3.4: High-Frequency Load**

**What to Say:**
> "Now let me show you the system under load - 10,000 orders per second."

**Actions:**
```bash
# Terminal 2: Run load generator
./client_simulator --strategy liquidity_taker \
  --symbol AAPL --orders 10000 --duration 10

# You should see in Terminal 1:
# [INFO] Orders processed: 10000
# [INFO] Trades executed: 4523
# [INFO] Average latency: 8.2μs
# [INFO] P99 latency: 87μs
# [INFO] Throughput: 10234 orders/sec
```

**What to Point Out:**
- "10,000 orders in 10 seconds = 1,000 orders/sec"
- "Average latency: 8.2μs (within 10μs target)"
- "P99 latency: 87μs (within 100μs target)"
- "45% match rate (realistic for market maker)"

---

### **PHASE 4: METRICS DEMONSTRATION (3 minutes)**

**What to Say:**
> "Let me show you the real-time metrics exposed via Prometheus."

**Actions:**
```bash
# Terminal 3: Query metrics
curl -s http://localhost:8080/metrics | grep rtes_

# You should see:
# rtes_orders_received_total 10000
# rtes_orders_accepted_total 9987
# rtes_orders_rejected_total 13
# rtes_trades_executed_total 4523
# rtes_order_latency_seconds{quantile="0.5"} 0.000007
# rtes_order_latency_seconds{quantile="0.99"} 0.000087
# rtes_order_latency_seconds{quantile="0.999"} 0.000432
# rtes_memory_pool_utilization 0.15
# rtes_connections_active 1
```

**What to Point Out:**
- "13 orders rejected (risk checks)"
- "P50 latency: 7μs, P99: 87μs, P999: 432μs"
- "Memory pool: 15% utilized (150K of 1M orders)"
- "All metrics exposed in Prometheus format"

---

### **PHASE 5: MARKET DATA DEMONSTRATION (2 minutes)**

**What to Say:**
> "Let me show you the market data being published via UDP multicast."

**Actions:**
```bash
# Terminal 4: Subscribe to market data
./udp_receiver --multicast 239.0.0.1 --port 9999

# You should see:
# [TRADE] seq=1234 symbol=AAPL buy=1 sell=2 price=150.00 qty=50
# [BBO] seq=1235 symbol=AAPL bid=150.00(50) ask=N/A
# [TRADE] seq=1236 symbol=AAPL buy=3 sell=4 price=150.10 qty=100
# [BBO] seq=1237 symbol=AAPL bid=150.00(50) ask=150.20(200)
```

**What to Point Out:**
- "Sequence numbers for gap detection"
- "Trade reports include both order IDs"
- "BBO updates show best bid/ask with quantities"
- "UDP multicast: one packet reaches all subscribers"

---

### **PHASE 6: CODE WALKTHROUGH (5 minutes)**

#### **Demo 6.1: Lock-Free Queue**

**What to Say:**
> "Let me show you the lock-free SPSC queue implementation."

**Actions:**
```bash
# Open in editor
vim include/rtes/spsc_queue.hpp
```

**What to Point Out:**
```cpp
template<typename T>
class SPSCQueue {
    // Cache-line aligned to prevent false sharing
    alignas(64) std::atomic<size_t> head_;  // Producer
    alignas(64) std::atomic<size_t> tail_;  // Consumer
    
    bool push(const T& item) {
        auto head = head_.load(std::memory_order_relaxed);
        auto next_head = (head + 1) % capacity_;
        
        // Check if queue full
        if (next_head == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        
        buffer_[head] = item;
        head_.store(next_head, std::memory_order_release);  // Publish
        return true;
    }
};
```

**Explain:**
- "alignas(64) prevents false sharing"
- "memory_order_acquire/release creates happens-before"
- "No locks, no syscalls, ~10ns per operation"

---

#### **Demo 6.2: Order Book Matching**

**What to Say:**
> "Let me show you the price-time priority matching algorithm."

**Actions:**
```bash
# Open in editor
vim src/order_book.cpp
# Jump to match_limit_order_safe_optimized
```

**What to Point Out:**
```cpp
Result<void> OrderBook::match_limit_order_safe_optimized(Order* order) {
    auto& opposite_side = (order->side == Side::BUY) ? asks_ : bids_;
    
    // Cache prefetch for performance
    _mm_prefetch(&opposite_side.begin()->second, _MM_HINT_T0);
    
    while (order->remaining_quantity > 0 && !opposite_side.empty()) {
        auto& [price, level] = *opposite_side.begin();
        
        // Check if price crosses
        bool crosses = (order->side == Side::BUY) 
            ? (order->price >= price)  // Buy: willing to pay >= ask
            : (order->price <= price); // Sell: willing to accept <= bid
        
        if (!crosses) break;  // No match, add to book
        
        Order* passive_order = level.orders.front();  // FIFO
        Quantity trade_qty = std::min(order->remaining_quantity, 
                                      passive_order->remaining_quantity);
        
        execute_trade_optimized(order, passive_order, trade_qty, price);
    }
}
```

**Explain:**
- "_mm_prefetch loads next cache line (80ns → 4ns)"
- "Price crossing logic: buy >= ask, sell <= bid"
- "FIFO within price level (time priority)"
- "Partial fills handled with remaining_quantity"

---

#### **Demo 6.3: Memory Pool**

**What to Say:**
> "Let me show you the memory pool that eliminates allocations."

**Actions:**
```bash
vim include/rtes/memory_pool.hpp
```

**What to Point Out:**
```cpp
template<typename T>
class MemoryPool {
    std::vector<T> pool_;           // Pre-allocated at startup
    std::vector<size_t> free_list_; // Available indices
    std::atomic<size_t> free_count_;
    
    T* allocate() {
        auto count = free_count_.load(std::memory_order_acquire);
        while (count > 0) {
            if (free_count_.compare_exchange_weak(count, count - 1)) {
                auto index = free_list_[count - 1];
                return &pool_[index];  // O(1), ~5ns
            }
        }
        return nullptr;  // Pool exhausted
    }
};
```

**Explain:**
- "All memory allocated at startup (1M orders)"
- "O(1) allocation with atomic CAS"
- "No malloc/free in hot path (saves ~100ns per order)"
- "Lock-free: multiple threads can allocate simultaneously"

---

### **PHASE 7: PERFORMANCE ANALYSIS (3 minutes)**

**What to Say:**
> "Let me show you the latency breakdown and where time is spent."

**Actions:**
```bash
# Run benchmark
./bench_matching --orders 100000 --symbols 1

# Output:
# === Latency Breakdown ===
# TCP Gateway:      2.1μs (23%)
# Risk Validation:  0.9μs (10%)
# Order Matching:   4.8μs (53%)
# Market Data:      1.2μs (13%)
# Total:            9.0μs (100%)
#
# === Throughput ===
# Orders/sec:       152,341
# Trades/sec:       68,553
# Memory:           1.5GB stable
```

**What to Point Out:**
- "Matching engine is the bottleneck (53% of time)"
- "Risk validation is fast (10%) due to in-memory checks"
- "Total latency: 9μs (within 10μs target)"
- "Throughput: 152K orders/sec (50% above target)"

---

### **PHASE 8: STRESS TEST (2 minutes)**

**What to Say:**
> "Let me stress test the system with 100 concurrent clients."

**Actions:**
```bash
# Terminal 2: Run stress test
./stress_test --clients 100 --orders 1000 --duration 60

# You should see:
# [INFO] Starting 100 clients...
# [INFO] Each client sending 1000 orders...
# [INFO] Duration: 60 seconds
# 
# === Results ===
# Total orders:     100,000
# Successful:       99,987 (99.99%)
# Rejected:         13 (0.01%)
# Average latency:  8.7μs
# P99 latency:      92μs
# P999 latency:     458μs
# Throughput:       1,666 orders/sec
```

**What to Point Out:**
- "99.99% success rate"
- "Latency stable under load (8.7μs avg)"
- "P99/P999 within targets"
- "System handles 100 concurrent clients"

---

### **PHASE 9: FAILURE SCENARIOS (2 minutes)**

#### **Demo 9.1: Risk Rejection**

**What to Say:**
> "Let me show you risk checks rejecting bad orders."

**Actions:**
```bash
# Send order exceeding size limit
./tcp_client --order "BUY,AAPL,1000000,150.00,LIMIT"

# You should see:
# [WARN] Order rejected: id=1001 reason=SIZE_LIMIT_EXCEEDED
# [INFO] Risk check failed: quantity 1000000 > max 10000
```

---

#### **Demo 9.2: Rate Limiting**

**What to Say:**
> "Let me trigger rate limiting by sending too many orders."

**Actions:**
```bash
# Send 2000 orders in 1 second (limit is 1000)
./tcp_client --burst 2000 --interval 0

# You should see:
# [WARN] Rate limit exceeded: client=CLIENT_001
# [INFO] Orders rejected: 1000 (rate limit)
```

---

#### **Demo 9.3: Pool Exhaustion**

**What to Say:**
> "Let me show what happens when the memory pool is exhausted."

**Actions:**
```bash
# Configure small pool (100 orders)
vim configs/config.json
# Change: "order_pool_size": 100

# Restart and send 200 orders
./trading_exchange ../configs/config.json &
./tcp_client --orders 200

# You should see:
# [ERROR] Order pool exhausted: 100/100 allocated
# [INFO] Order rejected: id=101 reason=POOL_EXHAUSTED
```

---

### **PHASE 10: Q&A PREPARATION (5 minutes)**

**Common Questions & Demos:**

#### **Q: "How do you handle partial fills?"**
**Demo:**
```bash
# Send large buy order
./tcp_client --order "BUY,AAPL,1000,150.00,LIMIT"

# Send multiple small sell orders
./tcp_client --order "SELL,AAPL,100,150.00,LIMIT"
./tcp_client --order "SELL,AAPL,200,150.00,LIMIT"
./tcp_client --order "SELL,AAPL,300,150.00,LIMIT"

# Show partial fills in logs
# [INFO] Trade: buy=1 sell=2 qty=100 (remaining: 900)
# [INFO] Trade: buy=1 sell=3 qty=200 (remaining: 700)
# [INFO] Trade: buy=1 sell=4 qty=300 (remaining: 400)
```

---

#### **Q: "Show me the market making strategy in action"**
**Demo:**
```bash
# Terminal 2: Start market maker
./client_simulator --strategy market_maker --symbol AAPL &

# Terminal 3: Send aggressive orders to hit the market maker
./tcp_client --order "BUY,AAPL,50,150.20,LIMIT"   # Hit ask
./tcp_client --order "SELL,AAPL,50,149.80,LIMIT"  # Hit bid

# Observe market maker behavior:
# 1. Initial quotes: bid=149.90, ask=150.10
# 2. Aggressive buy hits ask at 150.10 (50 shares)
# 3. Market maker cancels remaining orders
# 4. Market maker requotes: bid=150.00, ask=150.20
# 5. Aggressive sell hits bid at 150.00 (50 shares)
# 6. Market maker requotes again
```

**Explain:**
> "The market maker continuously provides liquidity:
> 1. Quotes both sides with a spread (20 cents)
> 2. When filled, immediately cancels and requotes
> 3. Adjusts prices based on recent trades
> 4. Manages inventory by adjusting quotes
> 5. Captures spread as profit (10 cents per round trip)
>
> This demonstrates how the exchange supports systematic market making strategies that need ultra-low latency for requoting."

---

#### **Q: "How do you ensure price-time priority?"**
**Demo:**
```bash
# Send 3 orders at same price
./tcp_client --order "BUY,AAPL,100,150.00,LIMIT"  # Order 1
sleep 0.1
./tcp_client --order "BUY,AAPL,100,150.00,LIMIT"  # Order 2
sleep 0.1
./tcp_client --order "BUY,AAPL,100,150.00,LIMIT"  # Order 3

# Send crossing sell order
./tcp_client --order "SELL,AAPL,100,150.00,LIMIT"

# Show Order 1 matched first (FIFO)
# [INFO] Trade: buy=1 sell=4 qty=100
```

---

#### **Q: "What's your throughput limit?"**
**Demo:**
```bash
# Run throughput benchmark
./bench_throughput --duration 60

# Output:
# === Throughput Test (60 seconds) ===
# Orders/sec:       152,341 (avg)
# Peak:             187,234 (burst)
# Sustained:        148,923 (1 minute)
# Bottleneck:       Matching engine (CPU bound)
```

---

## **TROUBLESHOOTING GUIDE**

### **Issue: Exchange won't start**
```bash
# Check port availability
netstat -tuln | grep 8888
netstat -tuln | grep 8080

# Kill existing process
pkill trading_exchange

# Check logs
tail -f logs/exchange.log
```

### **Issue: Client can't connect**
```bash
# Verify exchange is running
ps aux | grep trading_exchange

# Test TCP connection
telnet localhost 8888

# Check firewall
sudo iptables -L | grep 8888
```

### **Issue: No market data received**
```bash
# Check multicast route
ip route show | grep 239.0.0.1

# Enable multicast
sudo ip route add 239.0.0.0/8 dev lo

# Test UDP receiver
./udp_receiver --multicast 239.0.0.1 --port 9999
```

---

## **INTERVIEW TIPS**

### **DO:**
- ✅ Start with overview, then dive deep
- ✅ Show metrics in real-time
- ✅ Explain trade-offs as you demo
- ✅ Have backup plan if demo fails
- ✅ Relate to real-world systems (NASDAQ, CME)

### **DON'T:**
- ❌ Spend too long on setup
- ❌ Show code without context
- ❌ Ignore questions during demo
- ❌ Claim it's production-ready without caveats
- ❌ Forget to mention limitations

---

## **BACKUP PLAN (If Live Demo Fails)**

### **Option 1: Pre-recorded Video**
- Record demo beforehand
- Show video while narrating
- Have code ready to walk through

### **Option 2: Static Metrics**
- Show saved benchmark results
- Walk through code instead
- Use diagrams to explain flow

### **Option 3: Whiteboard**
- Draw architecture
- Trace order flow
- Explain algorithms

---

## **TIME ALLOCATION**

| Phase | Time | Critical? |
|-------|------|-----------|
| Overview | 2 min | ✅ Yes |
| Architecture | 3 min | ✅ Yes |
| Live Demo | 5 min | ✅ Yes |
| Metrics | 3 min | ⚠️ Important |
| Market Data | 2 min | ⚠️ Important |
| Code Walkthrough | 5 min | ✅ Yes |
| Performance | 3 min | ⚠️ Important |
| Stress Test | 2 min | ❌ Optional |
| Failure Scenarios | 2 min | ❌ Optional |
| Q&A | 5 min | ✅ Yes |
| **TOTAL** | **30 min** | |

**Adjust based on interview length:**
- 15 min: Phases 1-3 only
- 30 min: Phases 1-7
- 45 min: All phases

---

## **FINAL CHECKLIST**

### **Before Interview:**
- [ ] Build system successfully
- [ ] Test all demo commands
- [ ] Render PlantUML diagrams
- [ ] Prepare 4 terminals
- [ ] Have backup plan ready
- [ ] Review common questions
- [ ] Practice timing (30 min)

### **During Interview:**
- [ ] Start with overview
- [ ] Show live system running
- [ ] Explain as you demo
- [ ] Point out key metrics
- [ ] Walk through critical code
- [ ] Handle questions gracefully
- [ ] Summarize achievements

### **After Demo:**
- [ ] Ask for feedback
- [ ] Discuss improvements
- [ ] Show enthusiasm
- [ ] Thank interviewer

---

**Good luck! This system demonstrates world-class systems programming skills.** 🚀
