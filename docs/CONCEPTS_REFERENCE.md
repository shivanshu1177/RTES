# RTES Concepts Reference - Complete List

## **1. C++ LANGUAGE FEATURES**

### **Modern C++ (C++20/23)**
- `std::atomic` - Lock-free atomic operations
- `std::thread` - Thread management
- `std::mutex` / `std::scoped_lock` - Mutual exclusion
- `std::unique_ptr` / `std::shared_ptr` - Smart pointers (RAII)
- `std::map` / `std::unordered_map` - Associative containers
- `std::deque` - Double-ended queue
- `std::vector` - Dynamic array
- `std::chrono` - Time utilities
- `std::function` - Function objects
- `std::variant` - Type-safe union (for Result<T>)
- `std::optional` - Optional values
- Structured bindings - `auto [key, value] = *it;`
- Lambda expressions - Callbacks
- Move semantics - `std::move()`
- Template metaprogramming - `template<typename T>`
- `constexpr` - Compile-time constants
- `alignas(64)` - Memory alignment
- `__attribute__((packed))` - Struct packing

### **Low-Level Features**
- Pointers and references
- Memory layout control
- Bit manipulation
- Type casting (`reinterpret_cast`, `static_cast`)
- Inline functions
- RAII (Resource Acquisition Is Initialization)

---

## **2. CONCURRENCY & PARALLELISM**

### **Threading Concepts**
- Multi-threading
- Thread pools
- Thread-per-task model
- Single-writer principle
- Actor model (per-symbol threads)
- Thread affinity / CPU pinning
- Context switching
- Thread synchronization

### **Synchronization Primitives**
- Mutexes (mutual exclusion)
- Spinlocks
- Condition variables
- Semaphores
- Read-write locks
- Atomic operations

### **Lock-Free Programming**
- Lock-free data structures
- Wait-free algorithms
- Compare-and-swap (CAS)
- Memory ordering (acquire/release/relaxed)
- ABA problem
- Sequence numbers
- Ring buffers
- SPSC (Single Producer Single Consumer) queues
- MPMC (Multi Producer Multi Consumer) queues

### **Memory Models**
- Sequential consistency
- Memory barriers
- `memory_order_acquire`
- `memory_order_release`
- `memory_order_relaxed`
- `memory_order_seq_cst`
- Happens-before relationship
- Data races
- Race conditions

---

## **3. DATA STRUCTURES & ALGORITHMS**

### **Data Structures**
- Hash tables (`unordered_map`)
- Balanced trees (`std::map` - Red-Black Tree)
- Deque (double-ended queue)
- Ring buffer (circular buffer)
- Free list
- Skip list (alternative considered)
- Priority queue concepts

### **Algorithms**
- Price-time priority matching
- FIFO (First In First Out)
- Binary search (implicit in `std::map`)
- Hash functions
- Sorting (implicit in `std::map`)

### **Complexity Analysis**
- O(1) - Constant time
- O(log n) - Logarithmic time
- O(n) - Linear time
- Amortized complexity
- Space complexity

---

## **4. NETWORKING**

### **Network Protocols**
- TCP (Transmission Control Protocol)
- UDP (User Datagram Protocol)
- IP (Internet Protocol)
- HTTP (for metrics)
- Binary protocols
- Message framing

### **Socket Programming**
- Berkeley sockets API
- `socket()`, `bind()`, `listen()`, `accept()`
- `recv()`, `send()`
- Non-blocking I/O
- Socket options (`SO_REUSEADDR`, `TCP_NODELAY`)
- File descriptors

### **I/O Multiplexing**
- `epoll` (Linux)
- `select()` / `poll()` (alternatives)
- `kqueue` (macOS/BSD)
- Edge-triggered vs level-triggered
- Event-driven architecture

### **Network Concepts**
- Nagle's algorithm
- TCP backlog
- Multicast addressing
- Unicast vs multicast vs broadcast
- Network byte order (endianness)
- MTU (Maximum Transmission Unit)
- Packet loss
- Latency vs throughput

---

## **5. MEMORY MANAGEMENT**

### **Memory Allocation**
- Stack vs heap
- `malloc()` / `free()`
- `new` / `delete`
- Memory pools
- Object pools
- Arena allocators
- Slab allocators

### **Memory Optimization**
- Memory alignment
- Cache-line alignment (64 bytes)
- False sharing
- True sharing
- Memory fragmentation
- Memory leaks
- RAII pattern
- Smart pointers

### **Memory Safety**
- Buffer overflows
- Bounds checking
- Use-after-free
- Double-free
- Memory corruption
- Valgrind / AddressSanitizer

---

## **6. CPU & CACHE OPTIMIZATION**

### **CPU Architecture**
- CPU cache hierarchy (L1/L2/L3)
- Cache lines (64 bytes)
- Cache coherence (MESI protocol)
- Cache misses (cold/capacity/conflict)
- TLB (Translation Lookaside Buffer)
- NUMA (Non-Uniform Memory Access)

### **Performance Optimization**
- Cache prefetching (`_mm_prefetch`)
- Branch prediction
- Branch misprediction penalty
- Conditional moves (`cmov`)
- `__builtin_expect` (likely/unlikely)
- Pipeline stalls
- Instruction-level parallelism (ILP)
- SIMD (Single Instruction Multiple Data)
- Vectorization

### **Profiling & Measurement**
- CPU profiling (`perf`)
- Latency measurement
- Throughput measurement
- Percentiles (P50, P99, P999)
- Microbenchmarking
- Hot path identification

---

## **7. OPERATING SYSTEM CONCEPTS**

### **Process & Thread Management**
- Processes vs threads
- Thread scheduling
- Priority scheduling
- Real-time scheduling (SCHED_FIFO)
- Context switches
- System calls
- User space vs kernel space

### **File Systems**
- File descriptors
- `open()`, `close()`, `read()`, `write()`
- File permissions
- Symbolic links

### **Signals & IPC**
- Signal handling
- Inter-process communication (IPC)
- Shared memory
- Message queues
- Pipes

---

## **8. DESIGN PATTERNS**

### **Creational Patterns**
- Singleton (ShutdownManager)
- Factory pattern
- Object pool pattern

### **Structural Patterns**
- Adapter pattern
- Facade pattern
- Proxy pattern

### **Behavioral Patterns**
- Observer pattern (callbacks)
- Strategy pattern
- Command pattern
- State machine pattern

### **Concurrency Patterns**
- Producer-consumer
- Single-writer principle
- Actor model
- Pipeline pattern
- Work stealing (not used, but alternative)

---

## **9. FINANCIAL CONCEPTS**

### **Trading Terminology**
- Order (buy/sell instruction)
- Trade (executed transaction)
- Order book (collection of orders)
- Bid (buy order)
- Ask (sell order)
- Spread (bid-ask difference)
- Market order (execute at any price)
- Limit order (execute at specific price or better)
- Fill (order execution)
- Partial fill
- Notional value (price × quantity)

### **Market Data**
- BBO (Best Bid and Offer)
- Market depth (top N levels)
- Trade reports
- Price levels
- Tick size
- Lot size

### **Risk Management**
- Position limits
- Credit limits
- Price collars (fat-finger protection)
- Rate limiting
- Pre-trade risk checks
- Post-trade risk monitoring

### **Matching Algorithms**
- Price-time priority
- Pro-rata matching (alternative)
- FIFO (First In First Out)
- Price improvement

---

## **10. SECURITY CONCEPTS**

### **Cryptography**
- HMAC (Hash-based Message Authentication Code)
- SHA-256 hashing
- CRC32 checksum
- Symmetric encryption
- TLS/SSL
- Certificate validation

### **Authentication & Authorization**
- Token-based authentication
- JWT (JSON Web Tokens)
- Role-based access control (RBAC)
- Session management
- API keys

### **Security Best Practices**
- Input validation
- Input sanitization
- SQL injection prevention
- Log injection prevention
- Buffer overflow prevention
- Timing attack prevention
- Constant-time comparison
- Defense in depth

---

## **11. PROTOCOL DESIGN**

### **Message Format**
- Binary protocols
- Text protocols (JSON, XML)
- Message framing
- Fixed-size headers
- Variable-length payloads
- Struct packing (`__attribute__((packed))`)

### **Protocol Features**
- Sequence numbers
- Checksums (CRC32)
- Timestamps
- Message types
- Version negotiation
- Heartbeats

### **Serialization**
- Binary serialization
- Endianness (byte order)
- Alignment
- Padding

---

## **12. ERROR HANDLING**

### **Error Handling Strategies**
- Return codes
- Exceptions (not used in hot path)
- Result/Option types (monads)
- Error propagation
- Fail-fast principle
- Graceful degradation

### **Error Types**
- System errors
- Network errors
- Validation errors
- Business logic errors
- Resource exhaustion

### **Debugging & Diagnostics**
- Logging
- Structured logging
- Log levels (DEBUG, INFO, WARN, ERROR)
- Assertions
- Core dumps
- Stack traces

---

## **13. TESTING & QUALITY**

### **Testing Types**
- Unit testing
- Integration testing
- Performance testing
- Stress testing
- Load testing
- Chaos engineering
- Fuzz testing

### **Testing Tools**
- Google Test (gtest)
- Benchmarking frameworks
- Valgrind (memory checking)
- AddressSanitizer (ASAN)
- ThreadSanitizer (TSAN)
- UndefinedBehaviorSanitizer (UBSAN)

---

## **14. OBSERVABILITY & MONITORING**

### **Metrics**
- Counters (monotonic increasing)
- Gauges (point-in-time values)
- Histograms (distribution)
- Percentiles (P50, P99, P999)
- Throughput (ops/sec)
- Latency (microseconds)

### **Monitoring Systems**
- Prometheus (metrics collection)
- Grafana (visualization)
- OpenTelemetry (distributed tracing)
- Health checks
- Alerting

### **Logging**
- Asynchronous logging
- Log rotation
- Log aggregation
- Structured logging (JSON)
- Log levels

---

## **15. BUILD & DEPLOYMENT**

### **Build Systems**
- CMake
- Make
- Compiler flags (`-O3`, `-march=native`)
- Link-time optimization (LTO)
- Static vs dynamic linking

### **Compilers**
- GCC (GNU Compiler Collection)
- Clang/LLVM
- Compiler optimizations
- Compiler intrinsics (`_mm_prefetch`)

### **Deployment**
- Docker containers
- Docker Compose
- Configuration management
- Environment variables
- Service discovery

---

## **16. SYSTEM DESIGN PRINCIPLES**

### **Performance Principles**
- Zero-copy
- Zero-allocation (in hot path)
- Lock-free programming
- Single-writer principle
- Batching
- Pipelining
- Prefetching

### **Scalability Principles**
- Horizontal scaling (sharding)
- Vertical scaling (bigger machines)
- Partitioning (by symbol)
- Load balancing
- Replication
- Caching

### **Reliability Principles**
- Fault tolerance
- Graceful degradation
- Circuit breakers
- Retry logic
- Idempotency
- Crash recovery

### **Design Trade-offs**
- Latency vs throughput
- Memory vs speed
- Complexity vs performance
- Portability vs optimization
- Consistency vs availability (CAP theorem)

---

## **17. DOMAIN-SPECIFIC KNOWLEDGE**

### **Exchange Architecture**
- Order entry gateway
- Risk management system
- Matching engine
- Market data distribution
- Clearing and settlement (not implemented)

### **Market Microstructure**
- Order types
- Order lifecycle
- Trade execution
- Market making
- Liquidity provision
- Adverse selection

---

## **18. MATHEMATICAL CONCEPTS**

### **Numerical Methods**
- Fixed-point arithmetic
- Floating-point arithmetic
- Rounding errors
- Precision vs accuracy

### **Statistics**
- Mean (average)
- Median
- Percentiles
- Standard deviation
- Distribution analysis

---

## **19. SOFTWARE ENGINEERING**

### **Code Quality**
- Code reviews
- Static analysis (clang-tidy)
- Code formatting (clang-format)
- Documentation
- Comments
- Naming conventions

### **Version Control**
- Git
- Branching strategies
- Pull requests
- CI/CD pipelines

### **Architecture Patterns**
- Layered architecture
- Microservices (not used, but alternative)
- Event-driven architecture
- Message-passing architecture

---

## **20. ADVANCED TOPICS**

### **Hardware Acceleration**
- FPGA (Field-Programmable Gate Array)
- RDMA (Remote Direct Memory Access)
- Kernel bypass (DPDK)
- Hardware timestamping

### **Distributed Systems**
- Consensus algorithms (Raft, Paxos)
- Distributed transactions
- Eventual consistency
- Clock synchronization (NTP, PTP)

### **Real-Time Systems**
- Hard real-time vs soft real-time
- Deterministic latency
- Jitter
- Deadline scheduling

---

## **CONCEPT CATEGORIES BY IMPORTANCE**

### **Critical (Must Know)**
1. Lock-free programming (SPSC/MPMC queues)
2. Memory pools (zero-allocation)
3. Cache optimization (prefetching, alignment)
4. Threading model (single-writer)
5. Data structures (map, deque, unordered_map)
6. TCP/UDP networking
7. epoll I/O multiplexing
8. Price-time priority algorithm

### **Important (Should Know)**
9. Memory ordering (acquire/release)
10. Binary protocols
11. Error handling (Result<T>)
12. RAII pattern
13. Atomic operations
14. Branch prediction
15. Fixed-point arithmetic

### **Good to Know**
16. HMAC authentication
17. Prometheus metrics
18. Docker deployment
19. CMake build system
20. Performance profiling

---

## **LEARNING PATH**

### **Beginner → Intermediate**
1. C++ basics → Modern C++20
2. Data structures → Advanced algorithms
3. Single-threading → Multi-threading
4. Mutexes → Lock-free programming
5. malloc/free → Memory pools

### **Intermediate → Advanced**
6. Basic networking → epoll/high-performance I/O
7. Simple protocols → Binary protocols
8. Generic code → Cache-optimized code
9. Exceptions → Result types
10. Monolithic → Component-based architecture

### **Advanced → Expert**
11. Software optimization → Hardware optimization
12. Single-host → Distributed systems
13. Best-effort → Real-time guarantees
14. General-purpose → Domain-specific (trading)
15. Prototype → Production-ready

---

## **INTERVIEW PREPARATION CHECKLIST**

### **Must Explain Confidently**
- ✅ Lock-free SPSC/MPMC queues
- ✅ Memory pool design
- ✅ Single-writer per symbol
- ✅ Price-time priority matching
- ✅ Cache prefetching
- ✅ epoll I/O multiplexing
- ✅ Binary protocol design
- ✅ Memory ordering (acquire/release)

### **Should Be Able to Discuss**
- ✅ Trade-offs for each design decision
- ✅ Alternative approaches considered
- ✅ Performance optimization techniques
- ✅ Scalability limitations
- ✅ Error handling strategy
- ✅ Testing approach

### **Nice to Mention**
- ✅ Future improvements (RDMA, FPGA)
- ✅ Production considerations (monitoring, deployment)
- ✅ Real-world trading systems
- ✅ Industry standards (FIX protocol)

---

## **TOTAL CONCEPT COUNT: 200+**

This system demonstrates mastery of:
- **20+ C++ features**
- **15+ concurrency concepts**
- **10+ data structures**
- **10+ networking protocols**
- **15+ optimization techniques**
- **10+ design patterns**
- **20+ financial concepts**
- **10+ security practices**
- **And 100+ more supporting concepts**

**This is a comprehensive low-latency systems engineering project!** 🚀
