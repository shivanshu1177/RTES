# Goldman Sachs Exploratory Call - Complete Prep Guide

**Role**: Equity Systematic Market Making Associate  
**Round**: Exploratory Call (Screening)  
**Duration**: 60 minutes  
**Format**: Video (Zoom)  
**Interviewer**: VP  
**Time Until Interview**: 40 hours  
**Strategy**: Defend resume as-is (HIGH RISK)

---

## ⚠️ CRITICAL WARNING

You're defending **two inaccurate claims**:
1. "Lock-free order book" (actually mutex-based)
2. "Kernel bypass techniques" (actually epoll)

**Risk Level**: 🔴 **HIGH** - If VP is technical, you may be caught  
**Mitigation**: Strong technical defense + pivot to what matters

---

## 📋 INTERVIEW STRUCTURE (60 MIN)

```
00:00-05:00  Opening & Rapport Building
05:00-10:00  Your Background (LG Soft + RTES)
10:00-35:00  RTES Technical Deep-Dive ⚠️ HIGH RISK
35:00-45:00  Market Making Knowledge
45:00-50:00  Why Goldman Sachs
50:00-55:00  Your Questions
55:00-60:00  Closing & Next Steps
```

---

## 🎯 OPENING SCRIPT (0-5 MIN)

### **When VP Joins:**

> "Good morning/afternoon [VP Name], thank you for taking the time today. I'm excited to discuss the Equity Systematic Market Making role and share how my experience building low-latency trading systems aligns with what Goldman Sachs is looking for."

### **If Asked "Tell me about yourself":**

> "I'm a software engineer with 2 years of experience building high-performance C++ systems. Currently at LG Soft, I work on 5G protocol stacks where I've optimized critical path latency by 20% using lock-free algorithms and cache-aware data structures.
>
> Outside of work, I built a trading exchange simulator that processes 150,000 orders per second with 8 microsecond latency—this project taught me about market microstructure, adverse selection, and why latency matters in systematic market making.
>
> I'm drawn to Goldman Sachs because you're at the intersection of cutting-edge technology and financial markets, and I want to apply my low-latency systems expertise to real-world trading infrastructure."

**Time**: 90 seconds max

---

## 💼 YOUR STORY: RTES PROJECT (5-10 MIN)

### **When Asked "Walk me through your RTES project":**

**Structure** (3 minutes):

> "I built a high-performance trading exchange simulator to understand how market makers interact with exchanges at a fundamental level.
>
> **Architecture**: The system has four main components:
> 1. TCP Gateway receives orders via epoll-based I/O
> 2. Risk Manager validates with 6 pre-trade checks
> 3. Matching Engine executes trades using price-time priority
> 4. Market Data publishes via UDP multicast
>
> **Performance**: I achieved 150,000 orders per second with 8 microsecond average latency—50% above my initial target. The key optimizations were:
> - Lock-free SPSC/MPMC queues for inter-thread communication
> - Pre-allocated memory pool eliminating heap allocations
> - Cache-line alignment preventing false sharing
> - Single-writer design per symbol eliminating lock contention
>
> **Market Making**: I implemented a market maker strategy that continuously quotes bid/ask, captures the spread, and manages inventory—demonstrating the infrastructure needed for systematic market making.
>
> **Learning**: This project taught me that 'perfect is the enemy of good'—I initially wanted a fully lock-free order book, but realized the single-writer design was sufficient and saved months of complexity."

---

## 🔥 DEFENDING RESUME CLAIMS (10-35 MIN)

### **CRITICAL QUESTION #1: "Lock-Free Order Book"**

**VP**: "Your resume says lock-free order book. Walk me through the implementation."

**YOUR DEFENSE** (Confident, technical):

> "Let me clarify the architecture—the **order book itself uses a mutex with single-writer optimization**, while the **communication between components is lock-free** via SPSC/MPMC queues.
>
> Here's why I designed it this way:
>
> **Analysis**:
> - Each symbol has one dedicated matching thread (single-writer)
> - Mutex overhead with single-writer: ~20 nanoseconds (uncontended)
> - Total latency: 8000 nanoseconds
> - Potential improvement with lock-free: 0.25%
> - Implementation complexity: 2-3 months (ABA problem, memory reclamation)
>
> **Decision**: The mutex is effectively uncontended, so the overhead is negligible. I chose simplicity over marginal gains.
>
> **Lock-Free Components**:
> - SPSC queues (Gateway → Risk → Matching): 20ns latency
> - MPMC queue (Matching → Market Data): Sequence-based tickets
> - Memory pool: CAS-based allocation
>
> These are where lock-free really matters—the inter-thread communication hot path.
>
> **Trade-off**: If I needed multiple writers per symbol, I'd implement a lock-free skip list. But for current requirements, single-writer is the right choice."

**Why This Works**:
- ✅ Shows engineering judgment (trade-off analysis)
- ✅ Demonstrates you understand lock-free (even if you didn't implement it)
- ✅ Pivots to what you DID implement (lock-free queues)
- ✅ Shows maturity (knowing when NOT to optimize)

---

### **CRITICAL QUESTION #2: "Kernel Bypass Techniques"**

**VP**: "You mentioned kernel bypass techniques. Which ones did you use?"

**YOUR DEFENSE** (Precise, technical):

> "I used **syscall minimization techniques** with epoll—let me be precise about what I mean:
>
> **What I Implemented**:
> - **epoll** for O(1) I/O multiplexing (vs select/poll O(n))
> - **Non-blocking sockets** with TCP_NODELAY
> - **Batched I/O**: Read/write multiple messages per syscall
> - **Edge-triggered epoll**: Reduces syscall frequency
>
> **Why Not True Kernel Bypass**:
> - **DPDK**: Requires dedicated NICs, kernel modules—overkill for simulator
> - **io_uring**: Requires Linux 5.1+, wanted broader compatibility
> - **Current performance**: 2μs gateway latency, sufficient for targets
>
> **Terminology Clarification**: You're right to push on this—'kernel bypass' typically means DPDK or io_uring. What I did is more accurately 'syscall minimization' or 'efficient kernel I/O.'
>
> **Next Step**: If I needed sub-5μs total latency, io_uring would save ~2μs. But for demonstrating market making infrastructure, epoll was the right choice—mature, portable, and fast enough."

**Why This Works**:
- ✅ Acknowledges imprecise terminology
- ✅ Shows you know what REAL kernel bypass is
- ✅ Explains pragmatic engineering choice
- ✅ Demonstrates growth mindset (knows next optimization)

---

### **FOLLOW-UP: "Show me the CAS loop in your lock-free order book"**

**VP**: "Walk me through the Compare-And-Swap loop in your order book."

**YOUR RESPONSE** (Honest pivot):

> "The order book doesn't use CAS—it uses a mutex with single-writer optimization. Let me show you where I DO use CAS:
>
> **Memory Pool Allocation** (lock-free):
> ```cpp
> T* allocate() {
>     auto count = free_count_.load(std::memory_order_acquire);
>     while (count > 0) {
>         // CAS loop for lock-free allocation
>         if (free_count_.compare_exchange_weak(
>             count, count - 1, 
>             std::memory_order_acq_rel)) {
>             return &pool_[free_list_[count - 1]];
>         }
>         // count updated by CAS failure, retry
>     }
>     return nullptr;  // Pool exhausted
> }
> ```
>
> **Why This Works**:
> - Multiple threads can allocate concurrently
> - CAS ensures atomic decrement of free_count
> - Weak CAS is faster (allows spurious failures)
> - Acquire/release ordering ensures visibility
>
> **Order Book Design**: I chose mutex + single-writer because:
> 1. Only one thread writes per symbol (no contention)
> 2. Mutex overhead: 20ns (0.25% of total latency)
> 3. Lock-free order book would take 2-3 months
> 4. Not worth the complexity for marginal gain
>
> This is a classic engineering trade-off—optimize where it matters (queues, memory pool), keep it simple elsewhere (order book)."

**Why This Works**:
- ✅ Doesn't fabricate implementation
- ✅ Shows you DO understand CAS (memory pool example)
- ✅ Explains design rationale
- ✅ Demonstrates engineering judgment

---

## 📊 TECHNICAL DEEP-DIVE Q&A

### **Q: "What's your latency breakdown by component?"**

**Answer**:
> "Total: 8μs end-to-end
> - TCP Gateway: 2μs (25%) - epoll, parse, validate
> - Risk Manager: 1μs (12%) - 6 validation checks
> - Matching Engine: 5μs (63%) - order book matching
> - Market Data: 1μs (12%) - UDP multicast
>
> The bottleneck is matching (63%). I optimized with cache prefetching, reducing memory latency from 100ns to 4ns, saving 2μs."

---

### **Q: "How do you handle order cancellations?"**

**Answer**:
> "O(1) cancellation via unordered_map:
> ```cpp
> std::unordered_map<OrderID, Order*> order_lookup_;
> ```
> When cancel request arrives:
> 1. Lookup order by ID (O(1))
> 2. Verify client ownership (security)
> 3. Remove from price level deque
> 4. Update total quantity
> 5. Publish BBO update if best price changed
>
> Average cancellation latency: ~500ns."

---

### **Q: "Explain your memory pool design."**

**Answer**:
> "Pre-allocated 1M orders at startup (~200MB):
> - **Allocation**: O(1) via free list, lock-free CAS
> - **Deallocation**: O(1) return to free list
> - **Benefit**: Zero heap allocations in hot path
> - **Impact**: P99 latency dropped 40% (140μs → 85μs)
>
> This is critical for deterministic latency—heap allocations can trigger page faults or allocator contention."

---

### **Q: "How do you prevent false sharing?"**

**Answer**:
> "Cache-line alignment (64 bytes):
> ```cpp
> alignas(64) std::atomic<size_t> head_;  // Producer cache line
> alignas(64) std::atomic<size_t> tail_;  // Consumer cache line
> ```
>
> Without alignment, producer and consumer would share a cache line. Every write would invalidate the other thread's cache, causing ~50ns overhead.
>
> With alignment, each thread has its own cache line. Queue latency: 20ns."

---

### **Q: "What profiling tools did you use?"**

**Answer**:
> "Three-phase approach:
> 1. **perf record/report**: Identified 63% time in matching
> 2. **perf stat**: Found cache misses when traversing price levels
> 3. **Optimization**: Added _mm_prefetch for next level
>
> Result: 2μs reduction in matching latency (15% improvement).
>
> Also used:
> - **Valgrind**: Zero memory leaks
> - **ThreadSanitizer**: Zero data races
> - **Prometheus**: Real-time latency histograms"

---

### **Q: "How would you scale to 1M orders/sec?"**

**Answer**:
> "Three approaches:
>
> **1. Vertical Scaling** (2 weeks):
> - CPU pinning: Reduce context switches (+30%)
> - NUMA awareness: Local memory access (+20%)
> - Huge pages: Reduce TLB misses (+10%)
> - Expected: 150K → 240K orders/sec
>
> **2. Horizontal Scaling** (1 month):
> - Shard symbols across machines
> - Partition risk manager by client
> - Replicate market data publishers
> - Expected: 240K → 1M+ orders/sec
>
> **3. Hardware Acceleration** (3 months):
> - io_uring: -2μs latency
> - DPDK: -3μs latency
> - FPGA matching: -5μs latency
> - Expected: 1M+ orders/sec, <3μs latency
>
> For Goldman's scale, likely approach #2 + #3."

---

## 💹 MARKET MAKING KNOWLEDGE (35-45 MIN)

### **Q: "Explain how a market maker makes money."**

**Answer**:
> "Market makers profit from the bid-ask spread:
>
> **Example**:
> - Quote: Bid $149.90 (buy), Ask $150.10 (sell)
> - Spread: $0.20
> - If both sides fill: Buy at $149.90, sell at $150.10 = $0.20 profit
>
> **Risks**:
> 1. **Adverse selection**: Informed traders pick off stale quotes
> 2. **Inventory risk**: Market moves against position
> 3. **Operational risk**: System failures, fat-fingers
>
> **Why latency matters**: Every microsecond increases adverse selection risk. If you're slow to requote after a fill, informed traders will hit your other side."

---

### **Q: "What is adverse selection?"**

**Answer**:
> "Adverse selection is when informed traders trade against you because they know something you don't.
>
> **Example**:
> - You're quoting: Bid $100.00, Ask $100.20
> - News breaks: Company beats earnings
> - Informed traders buy at your $100.20 ask
> - Stock jumps to $101.00
> - You're stuck long at $100.20, losing $0.80
>
> **Defense**:
> - Fast requoting (<100μs) after fills
> - Widen spreads during high volatility
> - Monitor order flow for toxicity
> - Position limits to cap exposure
>
> This is why my RTES has <100μs requote latency—fast enough to avoid being picked off."

---

### **Q: "How do you manage inventory risk?"**

**Answer**:
> "Three strategies:
>
> **1. Inventory Skew**:
> - If long: Widen ask, tighten bid (encourage selling)
> - If short: Widen bid, tighten ask (encourage buying)
>
> **2. Position Limits**:
> - Stop quoting one side when limit reached
> - Force position flattening
>
> **3. Dynamic Spread**:
> - Increase spread with inventory size
> - Compensate for risk
>
> **My RTES Implementation**:
> ```cpp
> void update_quotes() {
>     Price bid = base_price_ - spread_ticks_ - inventory_skew_;
>     Price ask = base_price_ + spread_ticks_ + inventory_skew_;
>     
>     if (position > MAX_POSITION) {
>         // Stop quoting ask side
>         send_new_order(symbol_, Side::BUY, quote_size_, bid);
>     }
> }
> ```"

---

### **Q: "What's the difference between market making and HFT?"**

**Answer**:
> "**Market Making**:
> - Provides liquidity (passive orders)
> - Captures bid-ask spread
> - Inventory risk is main concern
> - Typically registered with exchanges
> - Example: Citadel Securities, Virtu
>
> **HFT (High-Frequency Trading)**:
> - Broader category including market making
> - Also includes: arbitrage, momentum, stat arb
> - May take liquidity (aggressive orders)
> - Speed is competitive advantage
> - Example: Jump Trading, Tower Research
>
> **Overlap**: Many HFT firms do market making, but not all market makers are HFT.
>
> **Goldman's Role**: Systematic market making is HFT-style market making—automated, quantitative, low-latency."

---

### **Q: "Explain price-time priority."**

**Answer**:
> "Price-time priority is the standard matching algorithm:
>
> **1. Price Priority**: Best price matches first
> - Bids: Highest price first
> - Asks: Lowest price first
>
> **2. Time Priority**: Within same price, FIFO
> - First order at price level matches first
>
> **Example**:
> ```
> Order Book:
> Ask: $100.20 (Order A, 100 shares) [first]
> Ask: $100.20 (Order B, 200 shares) [second]
> Ask: $100.30 (Order C, 150 shares)
>
> Incoming: Buy 250 shares at $100.25
>
> Matching:
> 1. Match 100 shares with Order A at $100.20 (best price, first in time)
> 2. Match 150 shares with Order B at $100.20 (same price, second in time)
> 3. Order B has 50 shares remaining
> ```
>
> **Why This Matters**: Market makers need to be fast to get queue position. Being first at a price level means you fill first."

---

## 🎯 WHY GOLDMAN SACHS (45-50 MIN)

### **Q: "Why Goldman Sachs?"**

**Answer** (2 minutes):
> "Three reasons:
>
> **1. Technology Leadership**: Goldman is unique among banks in building proprietary technology. SecDB, Slang, Marcus—you're not just using vendor software, you're building cutting-edge systems. That's where I want to be.
>
> **2. Systematic Market Making**: This role combines my two interests—low-latency systems and financial markets. I built RTES specifically to understand this space, and I want to apply that knowledge to real-world trading infrastructure at scale.
>
> **3. Impact & Ownership**: At 2 years experience, I want to own components and see my work directly impact P&L. Goldman's culture of ownership and meritocracy aligns with how I want to grow my career.
>
> I'm not just looking for any trading firm—I specifically want Goldman because you're at the intersection of technology innovation and financial markets."

---

### **Q: "Why systematic market making specifically?"**

**Answer**:
> "Market making is the perfect intersection of technology and markets:
>
> **Technology Challenge**: Sub-microsecond latency, lock-free algorithms, cache optimization—this is the hardest systems engineering problem in finance.
>
> **Market Challenge**: Adverse selection, inventory risk, market microstructure—you need to understand both code AND markets.
>
> **Measurable Impact**: Every optimization directly impacts P&L. Reduce latency by 1μs? Capture more spread. Improve risk model? Reduce losses.
>
> I built RTES because I wanted to understand this problem deeply. Now I want to work on production systems at Goldman's scale."

---

### **Q: "What are your career goals?"**

**Answer**:
> "**2-3 years**: Own critical components of the market making infrastructure. Mentor new engineers. Become the go-to person for low-latency optimization.
>
> **5 years**: Lead a team building next-generation trading systems. Contribute to architectural decisions. Bridge the gap between quants and engineers.
>
> **Long-term**: I want to be a technical leader who understands both systems engineering and market microstructure—someone who can design trading infrastructure that's fast, correct, and profitable."

---

## ❓ YOUR QUESTIONS TO ASK (50-55 MIN)

### **Technical Questions** (Pick 3-4):

1. **"What's the current tech stack for the equity market making desk? Languages, frameworks, infrastructure?"**

2. **"What are the biggest technical challenges the team is facing right now?"**

3. **"How do you balance latency optimization with code maintainability?"**

4. **"What's your approach to testing low-latency systems? How do you prevent regressions?"**

5. **"How does the team stay current with hardware advances—new CPUs, NICs, kernel features?"**

### **Role-Specific Questions** (Pick 2-3):

6. **"What would my first project be? What would success look like in the first 6 months?"**

7. **"How does the team collaborate with quants? What's the engineer-to-quant ratio?"**

8. **"What's the on-call rotation like? How do you handle production incidents?"**

9. **"How do you measure performance—latency, throughput, P&L impact?"**

### **Career Questions** (Pick 1-2):

10. **"What does the career progression look like for engineers on this team?"**

11. **"How does Goldman support continuous learning—conferences, courses, research time?"**

12. **"What makes someone successful in this role? What separates good from great?"**

---

## 🏁 CLOSING SCRIPT (55-60 MIN)

### **When VP Asks "Any final thoughts?"**

> "I'm very excited about this opportunity. Building RTES taught me that I love working on low-latency systems where every microsecond matters, and I want to apply that to real-world market making at Goldman Sachs.
>
> I know I'm early in my career, but I'm a fast learner with a strong technical foundation. I'm ready to own components, contribute to the team, and grow into a technical leader.
>
> What are the next steps in the process?"

---

## 📝 DAY-BEFORE CHECKLIST

### **Technical Prep** (2 hours):
- [ ] Review RTES architecture diagram
- [ ] Practice "lock-free order book" defense (5 times)
- [ ] Practice "kernel bypass" defense (5 times)
- [ ] Review memory pool CAS loop code
- [ ] Review market making strategy code

### **Market Making Prep** (1 hour):
- [ ] Review adverse selection explanation
- [ ] Review inventory risk management
- [ ] Review price-time priority example
- [ ] Review bid-ask spread economics

### **Behavioral Prep** (30 min):
- [ ] Practice "Why Goldman Sachs" (2 min)
- [ ] Practice "Why market making" (90 sec)
- [ ] Practice "Career goals" (90 sec)
- [ ] Prepare 5 questions to ask

### **Logistics** (30 min):
- [ ] Test Zoom (camera, mic, internet)
- [ ] Quiet room, good lighting
- [ ] Resume printed (for reference)
- [ ] Water, notebook, pen
- [ ] Join 5 minutes early

---

## 🎭 MOCK INTERVIEW SCRIPT

### **Practice This Flow** (60 min):

**0-5 min**: Opening
- "Tell me about yourself" (90 sec)

**5-10 min**: RTES Overview
- "Walk me through your RTES project" (3 min)

**10-20 min**: Lock-Free Defense
- "Explain your lock-free order book" (5 min)
- "Show me the CAS loop" (3 min)

**20-25 min**: Kernel Bypass Defense
- "What kernel bypass techniques?" (3 min)

**25-35 min**: Technical Deep-Dive
- Latency breakdown (2 min)
- Memory pool design (2 min)
- Profiling approach (2 min)
- Scaling strategy (3 min)

**35-45 min**: Market Making
- How market makers make money (2 min)
- Adverse selection (3 min)
- Inventory risk (2 min)
- Price-time priority (2 min)

**45-50 min**: Why Goldman Sachs
- Why GS? (2 min)
- Why market making? (2 min)
- Career goals (1 min)

**50-55 min**: Your Questions
- Ask 3-5 questions

**55-60 min**: Closing
- Final thoughts (1 min)

---

## ⚠️ FINAL WARNINGS

### **High-Risk Areas**:

1. **"Lock-free order book"**: If VP is technical, they'll know it's mutex-based. Your defense MUST be confident and pivot to trade-offs.

2. **"Kernel bypass"**: If VP knows DPDK/io_uring, they'll catch the imprecision. Acknowledge and explain pragmatic choice.

3. **LG Soft specifics**: Be ready to explain exact optimizations (not just "lock-free algorithms").

### **If Caught**:

**DON'T**:
- ❌ Fabricate implementation details
- ❌ Get defensive or argumentative
- ❌ Blame resume writer or recruiter

**DO**:
- ✅ Acknowledge imprecise terminology
- ✅ Explain what you actually did
- ✅ Show engineering judgment (trade-offs)
- ✅ Demonstrate you know the "right" way

### **Fallback Position**:

If VP pushes hard on inaccuracies:

> "You're right to push on this—I should have been more precise with terminology. The order book uses a mutex with single-writer optimization, not true lock-free. The lock-free components are the queues and memory pool. I chose this design because the mutex overhead was negligible (20ns) and saved months of complexity. This taught me an important lesson about engineering trade-offs—optimize where it matters, keep it simple elsewhere."

---

## 🎯 SUCCESS CRITERIA

### **You'll Know It Went Well If**:
- ✅ VP engaged and asked follow-up questions
- ✅ Conversation felt collaborative, not interrogative
- ✅ You explained trade-offs confidently
- ✅ VP shared details about the role/team
- ✅ Clear next steps discussed

### **Red Flags**:
- ❌ VP seemed skeptical of technical claims
- ❌ Short interview (<45 min)
- ❌ No discussion of next steps
- ❌ VP didn't engage with your questions

---

## 📞 POST-INTERVIEW

### **Within 2 Hours**:
- [ ] Send thank-you email to VP
- [ ] Mention specific topics discussed
- [ ] Reiterate interest in role
- [ ] Reference next steps

### **Email Template**:

```
Subject: Thank you - Equity Systematic Market Making Discussion

Dear [VP Name],

Thank you for taking the time to discuss the Equity Systematic Market Making 
Associate role today. I enjoyed learning about [specific topic discussed] and 
how the team approaches [specific challenge mentioned].

Our conversation reinforced my excitement about the opportunity to apply my 
low-latency systems experience to Goldman's market making infrastructure. 
I'm particularly interested in [specific project or challenge mentioned].

I look forward to the next steps in the process. Please let me know if you 
need any additional information.

Best regards,
Shivanshu Sharma
```

---

## 🚀 FINAL PEP TALK

You have **40 hours**. Here's how to use them:

**Today** (4 hours):
- Read this document 3 times
- Practice "lock-free" defense 10 times
- Practice "kernel bypass" defense 10 times
- Review RTES code (order_book.cpp, spsc_queue.hpp)

**Tomorrow** (4 hours):
- Mock interview with friend (60 min)
- Review market making concepts
- Prepare questions to ask
- Test Zoom setup

**Interview Day** (2 hours before):
- Review this document one final time
- Practice opening script
- Deep breaths, stay confident

**Remember**:
- You DID build a 150K orders/sec system
- You DO understand low-latency optimization
- You DO know market making basics
- You ARE qualified for this role

The resume inaccuracies are risky, but your defense strategy is solid. Be confident, be honest about trade-offs, and show engineering judgment.

**You've got this. Good luck! 🚀**

---

**Document Version**: 1.0  
**Last Updated**: 2024  
**Interview Date**: T-40 hours  
**Status**: READY FOR BATTLE ⚔️
