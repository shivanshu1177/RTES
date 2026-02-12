# RTES Interview Playbook — Hedge Fund Demo

## Before You Walk In

### Build (do this the night before)
```bash
cd /Users/shivanshu/Low-latency-trading-exchange-simmulator/RTES
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc) trading_exchange bench_matching bench_memory_pool load_generator client_simulator
```

### Required terminals (open and label them before the interview)
| Terminal | Label | Purpose |
|---|---|---|
| 1 | EXCHANGE | Run the exchange process |
| 2 | BENCH | Run benchmarks |
| 3 | MARKET DATA | UDP multicast receiver |
| 4 | METRICS | Prometheus scraper |
| 5 | CLIENT | Client simulator |

### Environment variables (set in every terminal)
```bash
export RTES_AUTH_MODE=development
export RTES_HMAC_KEY=dev_insecure_hmac_key_32chars!!
```

---

## The Narrative Arc (say this out loud before demos)

> "I built a full exchange stack from scratch — the same components a real venue runs:
> an order gateway, a pre-trade risk manager, a per-symbol matching engine, and a
> market data feed. The design goal was sub-10μs average latency at 100K orders/sec
> on a single host. I'll show you the architecture, then run it live."

---

## Act 1 — Architecture Walk (5 min, no running code)

Open `src/main.cpp` and walk the startup sequence top-to-bottom. Point at each line:

```
Exchange        → order pool + risk manager + matching engines
TcpGateway      → binary protocol, port 8888, poll()-based I/O
UdpPublisher    → multicast 239.0.0.1:9999, BBO + trade events
MonitoringService → Prometheus endpoint, port 8080
```

**Key talking points:**

1. **Single-writer per symbol** — each `MatchingEngine` owns its `OrderBook` exclusively.
   No lock contention between AAPL, MSFT, GOOGL engines. Point at `exchange.cpp:initialize_matching_engines()`.

2. **Lock-free queues between components** — show `spsc_queue.hpp`.
   Gateway → RiskManager is SPSC (one producer, one consumer).
   MatchingEngines → UdpPublisher is MPMC (N producers, one consumer).
   The `alignas(64)` on head/tail prevents false sharing — two threads updating
   adjacent cache lines would otherwise thrash each other's L1.

3. **Memory pool** — show `memory_pool.hpp`. Pre-allocates 1M `Order` objects at startup.
   `allocate()` is a CAS on a free-list index — no `malloc`, no kernel involvement.
   Point at `execute_trade_optimized` in `order_book.cpp` — `pool_.deallocate(passive)`
   is the only "free" in the hot path, and it's O(1).

4. **FlatPriceBook** — show the comment block at the top of `order_book.hpp`.
   Old design: `std::map<Price, PriceLevel>` — each node is a separate heap allocation,
   matching sweep follows pointer chains → cache miss per level.
   New design: `std::vector<FlatLevel>` — all levels contiguous, hardware prefetcher
   streams them automatically. Price lookup is O(1) via `unordered_map<Price, index>`.

   **Anticipate the question**: "Why not a sorted array with binary search?"
   Answer: binary search is O(log N) and branches unpredictably. Hash map is O(1)
   amortised with no branch. For a book with 20 active levels, log₂(20) ≈ 4 comparisons
   vs 1 hash lookup — and the hash result is predictable.

5. **Risk manager pipeline** — show `risk_manager.cpp:validate_new_order()`.
   Seven checks in sequence: symbol whitelist, size, price collar, rate limit,
   duplicate, credit. All synchronous, all on the risk thread — no shared state
   with the matching engine thread. This is the "sub-microsecond" claim; be honest:
   it's plausible on a warm cache but not measured. Say "designed for" not "proven at".

---

## Act 2 — Memory Pool Benchmark (3 min)

**Terminal 2:**
```bash
cd build
./bench_memory_pool
```

Expected output shape:
```
Single-thread benchmark:
  Iterations: 10,000,000
  Avg per op: ~20-50 ns
  Ops/sec: ~20-50M

Multi-thread benchmark (4 threads):
  ...
```

**What to say:**
> "The pool does a CAS on an atomic index — that's the entire allocate path.
> Compare that to `malloc` which takes 50-200ns and can call into the kernel.
> In the matching hot path, the only allocation is this pool — everything else
> is stack or pre-allocated."

**Anticipate**: "Your CAS loop could spin under contention."
Answer: "Yes — that's why the matching engine is single-writer. The pool is
only contended at the gateway (allocate) and matching engine (deallocate) boundary,
and those are on different cores. In practice the CAS succeeds on the first try."

---

## Act 3 — Matching Engine Benchmark (3 min)

**Terminal 2:**
```bash
./bench_matching
```

Expected output shape:
```
Matching Engine Benchmark:
  Orders processed: 50000
  Trades executed: ~15000-25000
  Total time: ~500-2000 ms
  Avg latency: ~10-40 μs/order
  Throughput: ~25K-100K orders/sec
```

**What to say:**
> "This is the internal matching benchmark — it bypasses TCP and risk to measure
> the order book in isolation. The number you see includes queue round-trip through
> the SPSC queue and the matching sweep. On Linux with CPU pinning this would be
> significantly faster — we're on macOS right now which adds scheduler jitter."

**If the number is lower than 100K**: Be honest.
> "On macOS the scheduler isn't real-time and `steady_clock` has higher overhead.
> The architecture is designed for Linux with `taskset` CPU pinning and
> `SCHED_FIFO` priority. The design decisions — flat book, pool allocator,
> SPSC queues — are what matter for the interview, not the macOS number."

---

## Act 4 — Live Exchange Demo (10 min, the centrepiece)

### Step 1 — Start the exchange
**Terminal 1:**
```bash
cd build
./trading_exchange ../configs/config.json
```

You should see:
```
[INFO] Initialized order pool with 1000000 orders
[INFO] Initialized matching engine for symbol: AAPL
[INFO] Initialized matching engine for symbol: MSFT
[INFO] Initialized matching engine for symbol: GOOGL
[INFO] TCP gateway started on port 8888
[INFO] UDP publisher started on 239.0.0.1:9999
[INFO] All services started successfully
```

### Step 2 — Start the market data receiver
**Terminal 3:**
```bash
cd tools
python3 md_recv.py --group 239.0.0.1 --port 9999
```

### Step 3 — Start the metrics scraper
**Terminal 4:**
```bash
cd tools
python3 metrics_scraper.py --host localhost --port 8080 --interval 3
```

Or raw Prometheus format (more impressive visually):
```bash
watch -n 2 'curl -s localhost:8080/metrics'
```

### Step 4 — Run a single market maker
**Terminal 5:**
```bash
cd build
./client_simulator --strategy market_maker --symbol AAPL --duration 30
```

**Point at Terminal 3** — you should see BBO updates streaming:
```
BBO AAPL Bid:14.99x100 Ask:15.01x100 Seq:1
BBO AAPL Bid:14.98x100 Ask:15.02x100 Seq:2
```

**What to say:**
> "The market maker is posting two-sided quotes. Every time it updates,
> the matching engine detects the BBO changed and publishes a UDP multicast
> packet. That packet is 56 bytes — header plus symbol, bid price/qty, ask price/qty.
> Any subscriber on the multicast group gets it with no unicast overhead."

### Step 5 — Add a liquidity taker (creates trades)
**Terminal 5 (new tab):**
```bash
./client_simulator --strategy liquidity_taker --symbol AAPL --duration 30
```

**Point at Terminal 3** — you should now see TRADE events:
```
TRADE AAPL ID:1 75@15.01 BUY Seq:5
TRADE AAPL ID:2 75@14.99 SELL Seq:6
```

**What to say:**
> "The liquidity taker is crossing the spread — sending market orders that
> match against the resting quotes. The matching engine executes at the passive
> order's price (price-time priority), publishes the trade to the MPMC queue,
> and the UDP publisher picks it up and multicasts it. The whole path from
> order submission to market data publication is in-process — no network hop
> between components."

### Step 6 — Load test
**Terminal 5:**
```bash
./load_generator --clients 20 --duration 30 --symbols AAPL,MSFT,GOOGL
```

Watch the metrics terminal. Every 10 seconds you'll see:
```
[10s] Orders: 12450 (1245/s), Acked: 11200, Rejected: 1250 (10%), Trades: 3400
[20s] Orders: 25100 (1255/s), Acked: 22600, Rejected: 2500 (10%), Trades: 6900
```

**What to say:**
> "20 clients, mix of market makers, momentum traders, arbitrageurs, and
> liquidity takers — same distribution you'd see on a real venue. The reject
> rate is from the risk manager: rate limits, price collars, credit limits.
> That's intentional — the risk manager is the first line of defence."

---

## Act 5 — Code Deep-Dives (answer-driven, only if asked)

### "Show me the matching algorithm"
Open `src/order_book.cpp`, go to `match_limit_order_safe_optimized`:

```cpp
while (order->remaining_quantity > 0 && !opposite.empty()) {
    FlatLevel& level = opposite[0];          // best price — O(1)
    const bool crosses = (order->side == Side::BUY)
        ? (order->price >= level.price)
        : (order->price <= level.price);
    if (!crosses) break;                     // price-time priority stop
    ...
    Quantity qty = std::min(order->remaining_quantity,
                            passive->remaining_quantity);
    execute_trade_optimized(order, passive, qty, level.price);
```

Key points:
- `opposite[0]` is always the best price — no search, no comparison
- Price crossing is a single integer compare (prices are fixed-point `uint64_t * 10000`)
- Execution price is always the passive order's price — rewards liquidity providers
- FIFO within a level: `FlatLevel::push` appends, `pop_front` increments `head_` cursor

### "How do you prevent false sharing?"
Show `spsc_queue.hpp`:
```cpp
alignas(64) std::atomic<size_t> head_{0};
alignas(64) std::atomic<size_t> tail_{0};
```
> "head_ is written by the producer, tail_ by the consumer. If they shared a
> cache line, every write by one thread would invalidate the other's L1 entry —
> that's a ~60ns penalty per operation. `alignas(64)` puts them on separate
> cache lines. Same pattern in LatencyTracker and MemoryMonitor."

### "What's the latency breakdown?"
Walk through the order path:
```
TCP recv         → ~1-2μs  (kernel → userspace, non-blocking read)
Message parse    → ~100ns  (fixed-size binary struct, no parsing)
Auth check       → ~200ns  (token lookup, rate limit check)
Risk validation  → ~500ns  (7 hash map lookups, one clock read)
SPSC push        → ~50ns   (atomic store, no contention)
SPSC pop         → ~50ns   (atomic load)
Order book match → ~1-3μs  (FlatPriceBook sweep, pool dealloc)
Trade callback   → ~200ns  (MPMC push)
Total            → ~3-7μs  (in-process, no network between components)
```

### "Why not use io_uring or epoll?"
Be direct:
> "epoll is Linux-only and I developed on macOS, so I used `poll()` for portability.
> In production on Linux I'd switch the gateway to edge-triggered epoll or io_uring
> for the acceptor loop — that eliminates the 50ms poll timeout latency on new
> connections. For the worker loop, io_uring with registered buffers would reduce
> syscall count further. The architecture supports that swap — it's one function
> in `tcp_gateway.cpp`."

### "How do you handle a slow consumer on the market data queue?"
> "The MPMC queue between matching engines and the UDP publisher is bounded at
> 65K entries. If the UDP publisher falls behind, `push()` returns false and the
> matching engine drops the market data event — it logs a warning but doesn't block.
> This is the right trade-off: order processing must never stall waiting for
> market data. In production you'd add a sequence number gap detector on the
> subscriber side — which `md_recv.py` already does."

### "What would you change for production?"
Be honest and specific — this shows maturity:
1. **epoll/io_uring** on Linux instead of `poll()`
2. **CPU pinning** (`taskset`, `SCHED_FIFO`) — eliminates scheduler jitter
3. **Huge pages** for the order pool — reduces TLB misses on 1M-entry pool
4. **Remove `TransactionScope` string allocation** from `add_order_safe` — it
   constructs a `std::string` on every order, which is a heap allocation in the hot path
5. **Measured latency percentiles** — the `LatencyTracker` tracks avg/min/max but
   not p99/p999. Need a histogram (already have `Histogram` in `metrics.cpp`) wired
   into the order book path
6. **Order book mutex → seqlock** — the current `std::mutex` in `OrderBook` is the
   main bottleneck for multi-reader scenarios (market data readers vs writer)

---

## Shutdown (clean every time)

**Terminal 1:** `Ctrl+C`

You should see:
```
[INFO] Shutdown signal received
[INFO] Initiating graceful shutdown
[INFO] Exchange stopped
```

If it hangs: `kill -9 $(lsof -ti:8888)` — then explain the macOS `poll()` timeout
is 50ms so shutdown takes up to 50ms, not a hang.

---

## Likely Hard Questions and Honest Answers

| Question | Answer |
|---|---|
| "Have you measured 100K orders/sec?" | "The architecture is designed for it. The bench_matching tool measures the book in isolation. End-to-end on Linux with CPU pinning is the right environment — I haven't run that yet." |
| "Is the order book lock-free?" | "No — it uses a mutex. The *queues between components* are lock-free SPSC/MPMC. The book itself is single-writer by design, so the mutex is uncontested in the steady state." |
| "What's your p99 latency?" | "The LatencyTracker records avg/min/max. I have the histogram infrastructure in metrics.cpp but haven't wired it into the order path yet — that's the next thing I'd add." |
| "Why C++20 and not Rust?" | "C++ gives me direct control over memory layout, SIMD intrinsics, and cache alignment without a borrow checker fighting me on the shared pool. Rust would be a valid choice — the ownership model maps well to the single-writer design." |
| "How does the risk manager know the current market price for price collars?" | "Right now it uses the order's own price as the reference — that's a simplification I'd call out. In production you'd feed the last trade price from the matching engine back to the risk manager via a separate read path." |

---

## What NOT to Say

- Don't say "lock-free order book" — it's not. Say "lock-free inter-thread queues"
- Don't claim measured p99 < 100μs — you haven't measured it
- Don't say "production-ready" — say "production-architecture, simulator-grade"
- Don't apologise for macOS numbers — explain the Linux difference confidently
