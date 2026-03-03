# Goldman Sachs Equity Systematic Market Making - Interview Questions

**Candidate**: Shivanshu Sharma  
**Role**: Equity Systematic Market Making Associate  
**Interview Type**: Exploratory Call  
**Duration**: 45-60 minutes

---

## INTERVIEW STRUCTURE

```
Introduction & Background        (5 min)
Technical Deep-Dive             (25 min)
Market Making & Trading         (10 min)
Behavioral & Fit                (5 min)
Candidate Questions             (5 min)
```

---

## SECTION 1: INTRODUCTION & BACKGROUND (5 min)

### Opening Questions

**Q1.1**: "Walk me through your background and why you're interested in systematic market making at Goldman Sachs."
- **Looking for**: Career narrative, motivation, understanding of the role
- **Red flags**: Generic answer, no research on GS, unclear motivation

**Q1.2**: "You have experience in telecom embedded systems. How does that translate to financial trading systems?"
- **Looking for**: Ability to draw parallels (low-latency, real-time, reliability)
- **Red flags**: Can't connect the dots, defensive about career pivot

**Q1.3**: "What do you know about systematic market making? How is it different from traditional market making?"
- **Looking for**: Understanding of automation, algorithms, quantitative strategies
- **Red flags**: Confuses with HFT, doesn't understand liquidity provision

---

## SECTION 2: TECHNICAL DEEP-DIVE (25 min)

### A. RTES Project - Architecture & Design (10 min)

**Q2.1**: "Your resume mentions a 'lock-free order book.' Walk me through the implementation details."
- **CRITICAL**: This is likely **inaccurate** on resume (mutex-based, not lock-free)
- **Looking for**: Honesty, technical accuracy, ability to explain trade-offs
- **Red flags**: Fabricates details, can't explain lock-free algorithms, defensive

**Q2.2**: "Explain the complete order lifecycle in your RTES system, from TCP reception to market data publication."
- **Looking for**: End-to-end understanding, latency breakdown, component interaction
- **Expected answer**: Gateway (2μs) → Risk (1μs) → Matching (5μs) → Market Data (1μs)

**Q2.3**: "You achieved 8μs average latency. What's the breakdown by component, and where's the bottleneck?"
- **Looking for**: Profiling skills, performance analysis, optimization priorities
- **Expected answer**: 63% in matching engine, cache misses, std::map operations

**Q2.4**: "Your resume says 'kernel bypass techniques.' Which specific techniques did you use?"
- **CRITICAL**: Resume likely overstates (epoll, not DPDK/io_uring)
- **Looking for**: Technical precision, honesty about limitations
- **Red flags**: Claims DPDK without implementation, vague answers

**Q2.5**: "How does your single-writer design per symbol work? What happens if you need multiple writers?"
- **Looking for**: Understanding of concurrency, scalability limitations
- **Expected answer**: One thread per symbol, horizontal scaling by sharding symbols

**Q2.6**: "Walk me through your lock-free SPSC queue implementation. What memory ordering do you use and why?"
- **Looking for**: Deep understanding of memory models, acquire/release semantics
- **Expected answer**: Cache-line alignment, acquire/release ordering, false sharing prevention

**Q2.7**: "You mentioned 'zero allocations in critical path.' How did you achieve this?"
- **Looking for**: Memory pool design, pre-allocation strategy, O(1) allocation
- **Expected answer**: Pre-allocated 1M orders, lock-free free list, fixed-size buffers

**Q2.8**: "How do you handle market data sequencing and gap detection?"
- **Looking for**: Understanding of UDP reliability issues, sequence numbers
- **Expected answer**: Monotonic sequence numbers, client-side gap detection, retransmission

---

### B. Performance Optimization (8 min)

**Q2.9**: "You mentioned cache-line alignment and false sharing elimination. Explain what these are and why they matter."
- **Looking for**: Hardware understanding, cache hierarchy, performance impact
- **Expected answer**: 64-byte cache lines, atomic contention, 50ns → 20ns improvement

**Q2.10**: "What profiling tools did you use to identify bottlenecks? Walk me through your optimization process."
- **Looking for**: Systematic approach, data-driven decisions, perf/valgrind usage
- **Expected answer**: perf record/report, identified cache misses, added prefetching

**Q2.11**: "You used cache prefetching (_mm_prefetch). When is this beneficial and when does it hurt performance?"
- **Looking for**: Deep understanding, not just copy-paste from Stack Overflow
- **Expected answer**: Beneficial for predictable access patterns, hurts with random access

**Q2.12**: "How would you scale your system from 150K to 1M orders/sec?"
- **Looking for**: Scalability thinking, horizontal vs vertical scaling
- **Expected answer**: Shard by symbol, CPU pinning, NUMA awareness, io_uring

**Q2.13**: "What's the difference between average latency and tail latency (P99/P999)? Why does tail latency matter more in trading?"
- **Looking for**: Understanding of latency distributions, adverse selection
- **Expected answer**: Tail latency = worst case, matters for market making (adverse selection risk)

---

### C. Concurrency & Threading (7 min)

**Q2.14**: "You have 8 threads in your system. How do they communicate? What synchronization primitives do you use?"
- **Looking for**: Threading model, lock-free queues, synchronization boundaries
- **Expected answer**: SPSC/MPMC queues, mutex only in order book (single-writer)

**Q2.15**: "Explain the difference between std::memory_order_relaxed, acquire, release, and seq_cst. When do you use each?"
- **Looking for**: Deep C++ knowledge, memory model understanding
- **Expected answer**: Relaxed (same thread), acquire/release (cross-thread), seq_cst (rare)

**Q2.16**: "How do you prevent race conditions in your order book? Walk me through a specific scenario."
- **Looking for**: Thread safety reasoning, mutex usage, single-writer design
- **Expected answer**: Single-writer per symbol, mutex for cancel operations

**Q2.17**: "What's the ABA problem in lock-free programming? How do you prevent it?"
- **Looking for**: Advanced concurrency knowledge
- **Expected answer**: CAS sees same value but different state, use tagged pointers/sequence numbers

**Q2.18**: "Your LG Soft experience mentions 'lock-free algorithms.' Give me a specific example from your work."
- **Looking for**: Real production experience, not just side projects
- **Red flags**: Vague answer, can't provide specifics, confuses lock-free with thread-safe

---

## SECTION 3: MARKET MAKING & TRADING KNOWLEDGE (10 min)

### A. Market Microstructure (5 min)

**Q3.1**: "Explain how a market maker makes money. What are the main risks?"
- **Looking for**: Understanding of bid-ask spread, inventory risk, adverse selection
- **Expected answer**: Capture spread, risks = adverse selection, inventory, operational

**Q3.2**: "What is adverse selection in market making? How does latency relate to adverse selection risk?"
- **Looking for**: Deep understanding of market dynamics
- **Expected answer**: Informed traders pick off stale quotes, speed reduces exposure window

**Q3.3**: "Your RTES has a market making strategy. Walk me through how it works."
- **Looking for**: Strategy logic, quoting behavior, inventory management
- **Expected answer**: Continuous bid/ask, spread capture, requote on fills

**Q3.4**: "What's the difference between a maker and a taker? Why do exchanges give rebates to makers?"
- **Looking for**: Understanding of exchange economics, liquidity provision
- **Expected answer**: Makers provide liquidity (passive), takers remove (aggressive), rebates incentivize liquidity

**Q3.5**: "Explain price-time priority. Why is this the standard matching algorithm?"
- **Looking for**: Order book mechanics, fairness considerations
- **Expected answer**: Best price first, then FIFO within level, ensures fairness

**Q3.6**: "What happens to a market maker's P&L if the market suddenly moves 5% in one direction?"
- **Looking for**: Understanding of inventory risk, delta exposure
- **Expected answer**: Loses money on inventory (long in down market, short in up market)

---

### B. Risk Management (5 min)

**Q3.7**: "You implemented 6 pre-trade risk checks. Walk me through each one and why it's necessary."
- **Looking for**: Risk awareness, production thinking
- **Expected answer**: Size, price collar, credit, rate limit, duplicate, symbol validation

**Q3.8**: "What's the difference between pre-trade and post-trade risk? Give examples of each."
- **Looking for**: Risk management knowledge
- **Expected answer**: Pre-trade = prevent bad orders, post-trade = monitor positions/P&L

**Q3.9**: "How would you implement position limits in a market making system? What happens when you hit the limit?"
- **Looking for**: Practical risk implementation
- **Expected answer**: Track net position, stop quoting one side, flatten position

**Q3.10**: "What's a fat-finger error? How do your risk checks prevent it?"
- **Looking for**: Real-world trading knowledge
- **Expected answer**: Accidental large order, prevented by size limits and price collars

---

## SECTION 4: LG SOFT EXPERIENCE (5 min)

**Q4.1**: "You reduced call-setup latency by 20% in the modem handshake. Walk me through the specific optimizations."
- **Looking for**: Real technical work, not just buzzwords
- **Red flags**: Vague answer, can't explain details, takes credit for team work

**Q4.2**: "You mention 'lock-free algorithms' at LG Soft. What specific lock-free data structure did you implement?"
- **Looking for**: Concrete examples, implementation details
- **Red flags**: Can't provide specifics, confuses lock-free with thread-safe

**Q4.3**: "You debugged 50+ production crashes. Walk me through your debugging process for a complex race condition."
- **Looking for**: Systematic debugging, tools usage, root cause analysis
- **Expected answer**: Reproduce, logs, core dumps, ThreadSanitizer, fix, verify

**Q4.4**: "How do you handle debugging in production when you can't reproduce the issue locally?"
- **Looking for**: Production debugging skills, logging, telemetry
- **Expected answer**: Enhanced logging, core dumps, statistical analysis, canary deployments

---

## SECTION 5: SYSTEM DESIGN & ARCHITECTURE (5 min)

**Q5.1**: "Design a market data feed handler that consumes 1M messages/sec from multiple exchanges. How do you ensure no data loss?"
- **Looking for**: System design skills, scalability, reliability
- **Expected answer**: UDP multicast, sequence numbers, gap detection, retransmission, sharding

**Q5.2**: "How would you design a risk system that validates 500K orders/sec with sub-microsecond latency?"
- **Looking for**: Scalability thinking, performance optimization
- **Expected answer**: Shard by client, in-memory state, lock-free, pre-computed limits

**Q5.3**: "Your RTES uses TCP for orders and UDP for market data. Why this choice? What are the trade-offs?"
- **Looking for**: Protocol understanding, trade-off analysis
- **Expected answer**: TCP = reliable orders, UDP = low-latency broadcast, trade-offs = reliability vs speed

---

## SECTION 6: C++ TECHNICAL DEPTH (5 min)

**Q6.1**: "What's the difference between std::map and std::unordered_map? When do you use each in your order book?"
- **Looking for**: Data structure knowledge, performance characteristics
- **Expected answer**: map = sorted (O(log n)), unordered_map = hash (O(1)), use map for prices, unordered_map for order lookup

**Q6.2**: "Explain move semantics in C++11. Why is it important for performance?"
- **Looking for**: Modern C++ knowledge
- **Expected answer**: Transfer ownership, avoid copies, RVO, perfect forwarding

**Q6.3**: "What's the difference between std::atomic and volatile? When do you use each?"
- **Looking for**: Concurrency knowledge, common misconceptions
- **Expected answer**: atomic = thread-safe, volatile = prevent optimization, use atomic for concurrency

**Q6.4**: "You use C++20/23. What new features did you leverage in your RTES project?"
- **Looking for**: Modern C++ knowledge, practical usage
- **Expected answer**: Concepts, ranges, coroutines, std::span, designated initializers

**Q6.5**: "Explain RAII. Give me an example from your code."
- **Looking for**: C++ fundamentals, resource management
- **Expected answer**: Resource Acquisition Is Initialization, FileDescriptor wrapper, lock_guard

---

## SECTION 7: PROBLEM-SOLVING & ALGORITHMS (5 min)

**Q7.1**: "How would you detect if an order book is crossed (bid > ask)? What does this indicate?"
- **Looking for**: Order book understanding, error detection
- **Expected answer**: Compare best_bid() > best_ask(), indicates bug or stale data

**Q7.2**: "Design an algorithm to calculate VWAP (Volume-Weighted Average Price) in O(1) time."
- **Looking for**: Algorithm design, running statistics
- **Expected answer**: Maintain running sum of (price × quantity) and total quantity

**Q7.3**: "You have 1M orders in memory. How do you find the top 10 orders by price efficiently?"
- **Looking for**: Data structure knowledge, heap usage
- **Expected answer**: Min-heap of size 10, O(n log k) where k=10

**Q7.4**: "How would you implement a time-based order expiration (e.g., Good-Till-Time orders)?"
- **Looking for**: Practical implementation, timer management
- **Expected answer**: Priority queue sorted by expiration time, timer thread

---

## SECTION 8: BEHAVIORAL & FIT (5 min)

**Q8.1**: "Tell me about a time you had to make a trade-off between performance and code complexity. How did you decide?"
- **Looking for**: Engineering judgment, pragmatism
- **Expected answer**: Lock-free order book decision (0.25% gain, 3 months work, not worth it)

**Q8.2**: "Describe a situation where you had to debug a critical production issue under time pressure."
- **Looking for**: Pressure handling, systematic approach, ownership
- **Expected answer**: LG Soft production crashes, systematic debugging, root cause analysis

**Q8.3**: "You're working on a market making strategy that's losing money. How do you approach debugging it?"
- **Looking for**: Analytical thinking, systematic approach
- **Expected answer**: Check logs, analyze trades, verify risk limits, compare to backtest, isolate variables

**Q8.4**: "Tell me about a time you disagreed with a technical decision. How did you handle it?"
- **Looking for**: Communication, collaboration, technical reasoning
- **Red flags**: Arrogant, can't compromise, poor communication

**Q8.5**: "Why Goldman Sachs? Why systematic market making specifically?"
- **Looking for**: Research, genuine interest, career goals
- **Red flags**: Generic answer, only interested in compensation, no research

**Q8.6**: "You're relatively early in your career (2 years). Why should we hire you over someone with 5+ years in finance?"
- **Looking for**: Self-awareness, value proposition, learning agility
- **Expected answer**: Fresh perspective, strong technical foundation, fast learner, proven performance

---

## SECTION 9: SCENARIO-BASED QUESTIONS (5 min)

**Q9.1**: "Your market making strategy suddenly starts losing money. Walk me through your debugging process."
- **Looking for**: Systematic thinking, root cause analysis
- **Expected answer**: Check positions, review recent trades, verify risk limits, analyze market conditions

**Q9.2**: "You notice your P99 latency spiked from 85μs to 500μs. How do you investigate?"
- **Looking for**: Performance debugging, systematic approach
- **Expected answer**: Check metrics, profile hot path, look for GC/allocations, check system load

**Q9.3**: "A client complains their orders are being rejected. How do you debug this?"
- **Looking for**: Customer focus, debugging skills
- **Expected answer**: Check logs, verify risk limits, test with sample order, reproduce issue

**Q9.4**: "Your system is processing 100K orders/sec, but suddenly drops to 10K. What do you check?"
- **Looking for**: Production debugging, systematic approach
- **Expected answer**: Check CPU/memory, queue depths, network, logs, recent deployments

---

## SECTION 10: TECHNICAL CHALLENGES & RED FLAGS (5 min)

### Questions to Expose Resume Inaccuracies

**Q10.1**: "You mentioned 'kernel bypass techniques.' Explain the difference between epoll, io_uring, and DPDK. Which did you use?"
- **CRITICAL**: Resume likely overstates (epoll, not true kernel bypass)
- **Looking for**: Honesty, technical precision
- **Red flags**: Claims DPDK without details, defensive, vague

**Q10.2**: "Walk me through the CAS (Compare-And-Swap) loop in your lock-free order book."
- **CRITICAL**: Order book is likely mutex-based, not lock-free
- **Looking for**: Honesty, ability to admit mistakes
- **Red flags**: Fabricates implementation, defensive, can't explain

**Q10.3**: "You achieved 10μs average latency. Show me the measurement code. How do you ensure accurate timing?"
- **Looking for**: Measurement methodology, high-resolution timers
- **Expected answer**: std::chrono::steady_clock, TSC, avoid system calls in measurement

**Q10.4**: "Your resume says 'production-grade.' What does that mean to you? Is your RTES actually production-ready?"
- **Looking for**: Honesty, understanding of production requirements
- **Expected answer**: It's a simulator demonstrating production techniques, not actually production-ready

---

## SECTION 11: MARKET MAKING SPECIFIC (5 min)

**Q11.1**: "How would you adjust your market making strategy during high volatility?"
- **Looking for**: Risk awareness, adaptive strategies
- **Expected answer**: Widen spreads, reduce size, increase requote frequency

**Q11.2**: "What's the difference between market making on NASDAQ vs NYSE? How would your system need to change?"
- **Looking for**: Market structure knowledge
- **Expected answer**: NASDAQ = electronic, NYSE = hybrid with DMM, different protocols

**Q11.3**: "Explain the concept of 'inventory skew' in market making. How do you manage it?"
- **Looking for**: Advanced market making knowledge
- **Expected answer**: Adjust quotes to lean against inventory (wider on long side, tighter on short side)

**Q11.4**: "What's the difference between statistical arbitrage and market making?"
- **Looking for**: Strategy understanding
- **Expected answer**: Stat arb = directional bets on mean reversion, MM = liquidity provision for spread

---

## SECTION 12: CLOSING QUESTIONS (5 min)

**Q12.1**: "What questions do you have for me about the role or Goldman Sachs?"
- **Looking for**: Thoughtful questions, genuine interest
- **Red flags**: No questions, only asks about compensation

**Q12.2**: "What are you looking for in your next role?"
- **Looking for**: Career goals, fit with role
- **Red flags**: Misaligned expectations, short-term thinking

**Q12.3**: "Walk me through your ideal project at Goldman Sachs in the first 6 months."
- **Looking for**: Realistic expectations, understanding of role
- **Expected answer**: Learn systems, contribute to optimization, build features

---

## EVALUATION CRITERIA

### Technical Depth (40%)
- [ ] Deep C++ knowledge (memory model, concurrency, performance)
- [ ] Low-latency systems expertise (cache, lock-free, profiling)
- [ ] Honest about limitations (lock-free order book, kernel bypass)
- [ ] Can explain trade-offs (performance vs complexity)

### Market Making Knowledge (20%)
- [ ] Understands market microstructure (bid-ask, adverse selection)
- [ ] Risk management awareness (position limits, credit)
- [ ] Strategy understanding (liquidity provision, inventory)
- [ ] Exchange mechanics (price-time priority, maker-taker)

### Problem-Solving (20%)
- [ ] Systematic debugging approach
- [ ] Data-driven optimization
- [ ] Scalability thinking
- [ ] Algorithm design

### Communication & Fit (20%)
- [ ] Clear technical explanations
- [ ] Honest about mistakes
- [ ] Collaborative mindset
- [ ] Genuine interest in role

---

## RED FLAGS TO WATCH FOR

### Technical Red Flags 🚩
- ❌ Can't explain "lock-free order book" (likely inaccurate)
- ❌ Vague about "kernel bypass techniques" (likely epoll, not DPDK)
- ❌ Can't provide specifics on LG Soft work
- ❌ Memorized answers without understanding
- ❌ Defensive when challenged on technical details

### Behavioral Red Flags 🚩
- ❌ Takes credit for team work
- ❌ Can't admit mistakes or limitations
- ❌ Arrogant or dismissive
- ❌ No questions about the role
- ❌ Only interested in compensation

### Market Making Red Flags 🚩
- ❌ Confuses market making with HFT
- ❌ Doesn't understand adverse selection
- ❌ No awareness of risk management
- ❌ Can't explain bid-ask spread economics

---

## RECOMMENDED INTERVIEW FLOW

### Phase 1: Warm-up (5 min)
- Background, motivation, role understanding
- Build rapport, assess communication

### Phase 2: Technical Deep-Dive (25 min)
- RTES architecture (10 min)
- Performance optimization (8 min)
- Concurrency & threading (7 min)
- **Challenge on "lock-free order book" and "kernel bypass"**

### Phase 3: Market Making (10 min)
- Market microstructure (5 min)
- Risk management (5 min)

### Phase 4: Behavioral & Fit (5 min)
- Trade-off decisions
- Production debugging
- Why Goldman Sachs

### Phase 5: Closing (5 min)
- Candidate questions
- Next steps

---

## FOLLOW-UP QUESTIONS (If Time Permits)

**F1**: "How would you implement a circuit breaker in your trading system?"
**F2**: "Explain the difference between latency and jitter. Which matters more?"
**F3**: "How do you handle clock synchronization across multiple servers?"
**F4**: "What's the CAP theorem? How does it apply to trading systems?"
**F5**: "Design a system to detect market manipulation (e.g., spoofing)."

---

## SCORING RUBRIC

| Category | Weight | Score (1-5) | Notes |
|----------|--------|-------------|-------|
| Technical Depth | 40% | | C++, low-latency, concurrency |
| Market Making | 20% | | Microstructure, risk, strategy |
| Problem-Solving | 20% | | Debugging, optimization, design |
| Communication | 10% | | Clarity, honesty, collaboration |
| Fit & Motivation | 10% | | Interest, research, culture fit |
| **Total** | **100%** | | |

**Hiring Bar**: ≥4.0 average (Strong Hire)

---

## FINAL RECOMMENDATION FRAMEWORK

### Strong Hire (4.5-5.0)
- Deep technical expertise
- Strong market making knowledge
- Excellent problem-solving
- Great communication and fit
- Honest about limitations

### Hire (4.0-4.4)
- Solid technical skills
- Good market making understanding
- Competent problem-solving
- Clear communication
- Some areas for growth

### Maybe (3.5-3.9)
- Adequate technical skills
- Basic market making knowledge
- Needs development
- Communication concerns
- Fit questions

### No Hire (<3.5)
- Weak technical skills
- Poor market making knowledge
- Fabricated resume claims
- Communication issues
- Poor fit

---

**Document Version**: 1.0  
**Last Updated**: 2024  
**Interviewer Guide**: Goldman Sachs Equity Systematic Market Making  
**Candidate**: Shivanshu Sharma
