# RTES Architecture Documentation

## System Overview

RTES (Real-Time Trading Exchange Simulator) is a high-performance, low-latency trading exchange built with modern C++20/23. The system processes orders, executes trades, and publishes market data with microsecond-level latency.

### Design Principles

1. **Zero-Copy**: Minimize data copying in hot paths
2. **Lock-Free**: Use lock-free data structures where possible
3. **Cache-Friendly**: Optimize for CPU cache efficiency
4. **Allocation-Free Hot Path**: Pre-allocate all memory
5. **Single-Writer**: Per-symbol order books avoid contention

## High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        Clients                               │
└────────────┬────────────────────────────┬───────────────────┘
             │ TCP (Orders)               │ UDP (Market Data)
             │ Port 8888                  │ 239.0.0.1:9999
             ▼                            ▲
┌────────────────────────────────────────────────────────────┐
│                    TCP Order Gateway                        │
│  - Binary Protocol                                          │
│  - TLS/SSL Support                                          │
│  - Authentication & Authorization                           │
│  - Rate Limiting                                            │
└────────────┬───────────────────────────────────────────────┘
             │
             ▼
┌────────────────────────────────────────────────────────────┐
│                     Risk Manager                            │
│  - Pre-trade Validation                                     │
│  - Position Limits                                          │
│  - Credit Checks                                            │
│  - Price Collars                                            │
└────────────┬───────────────────────────────────────────────┘
             │
             ▼
┌────────────────────────────────────────────────────────────┐
│                   Matching Engine                           │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐    │
│  │ OrderBook    │  │ OrderBook    │  │ OrderBook    │    │
│  │   AAPL       │  │   MSFT       │  │   GOOGL      │    │
│  │ (Price-Time) │  │ (Price-Time) │  │ (Price-Time) │    │
│  └──────────────┘  └──────────────┘  └──────────────┘    │
└────────────┬───────────────────────────────────────────────┘
             │
             ▼
┌────────────────────────────────────────────────────────────┐
│                  Market Data Publisher                      │
│  - UDP Multicast                                            │
│  - HMAC Authentication                                      │
│  - BBO, Trades, Depth                                       │
└─────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────┐
│                  Metrics & Monitoring                       │
│  - Prometheus Endpoint (Port 8080)                         │
│  - Health Checks                                            │
│  - Performance Metrics                                      │
└─────────────────────────────────────────────────────────────┘
```

## Component Architecture

### 1. TCP Order Gateway

**Responsibility**: Accept client connections, authenticate, and route orders

**Key Classes**:
- `TcpGateway`: Main gateway orchestrator
- `ClientConnection`: Per-client connection handler
- `SecureNetworkLayer`: TLS/SSL and security

**Threading Model**:
- Acceptor thread: Accepts new connections
- Worker thread: Handles I/O via epoll
- Non-blocking I/O with edge-triggered epoll

**Data Flow**:
```
Client → TCP Socket → TLS Layer → Authentication → 
Message Validation → Risk Manager → Matching Engine
```

**Performance Optimizations**:
- TCP_NODELAY enabled
- Zero-copy message framing
- Pre-allocated buffers (FixedSizeBuffer)
- Epoll edge-triggered mode

### 2. Risk Manager

**Responsibility**: Pre-trade risk validation

**Key Classes**:
- `RiskManager`: Main risk orchestrator
- `RiskLimits`: Per-client limits
- `PositionTracker`: Real-time position tracking

**Validation Checks**:
1. Order size limits
2. Notional value limits
3. Price collar validation
4. Rate limiting (orders/second)
5. Position limits
6. Credit checks

**Threading Model**:
- Single-threaded with lock-free queue
- Processes orders sequentially
- Forwards to matching engine

**Data Structures**:
```cpp
struct RiskLimits {
    uint64_t max_order_size;
    double max_notional;
    uint32_t max_orders_per_second;
    bool price_collar_enabled;
};
```

### 3. Matching Engine

**Responsibility**: Match orders and execute trades

**Key Classes**:
- `MatchingEngine`: Multi-symbol orchestrator
- `OrderBook`: Per-symbol order book
- `PriceLevel`: Price-time priority queue

**Algorithm**: Price-Time Priority
- Orders matched at best price first
- Within same price, FIFO (time priority)
- Market orders execute immediately
- Limit orders rest in book if not filled

**Data Structures**:
```cpp
// Bids: Descending price (highest first)
std::map<Price, PriceLevel, std::greater<Price>> bids_;

// Asks: Ascending price (lowest first)
std::map<Price, PriceLevel> asks_;

// Fast order lookup for cancellation
std::unordered_map<OrderID, Order*> order_lookup_;
```

**Threading Model**:
- Per-symbol single-writer thread
- No locks in hot path
- Lock-free queues for order submission

**Performance Optimizations**:
- Cache prefetching (`_mm_prefetch`)
- Memory pool for orders (no allocations)
- Inline trade execution
- Optimized price level management

### 4. Market Data Publisher

**Responsibility**: Publish market data to subscribers

**Key Classes**:
- `UdpPublisher`: UDP multicast sender
- `AuthenticatedUdpBroadcast`: HMAC-authenticated messages
- `MarketDataSnapshot`: BBO and depth

**Message Types**:
1. **BBO (Best Bid/Offer)**: Top of book
2. **Trade Reports**: Executed trades
3. **Depth Updates**: Top N price levels

**Protocol**:
```
[Sequence][HMAC_Size][Payload][HMAC]
```

**Threading Model**:
- Dedicated publisher thread
- Lock-free queue from matching engine
- Batching for efficiency

### 5. Memory Management

**Key Classes**:
- `OrderPool`: Pre-allocated order pool
- `FixedSizeBuffer`: Stack-based buffers
- `BoundedString`: Fixed-size strings

**Strategy**:
- All memory pre-allocated at startup
- No allocations in hot path
- RAII for resource management
- Memory pools for orders

**Order Pool**:
```cpp
class OrderPool {
    std::vector<Order> pool_;      // Pre-allocated
    std::vector<Order*> free_list_; // Available orders
    std::mutex mutex_;
};
```

### 6. Thread Safety

**Key Classes**:
- `RaceDetector`: Detect data races
- `ShutdownManager`: Coordinated shutdown
- `WorkDrainer`: Drain work before shutdown
- `atomic_wrapper`: Type-safe atomics

**Synchronization Primitives**:
- `std::mutex` for coarse-grained locks
- `std::atomic` for lock-free operations
- Lock-free queues (SPSC, MPMC)
- Thread annotations (GUARDED_BY, REQUIRES)

**Lock Hierarchy**:
```
Level 1: Configuration locks
Level 2: Connection management locks
Level 3: Order book locks (per-symbol)
Level 4: Memory pool locks
```

### 7. Performance Optimization

**Key Classes**:
- `PerformanceOptimizer`: Metrics collection
- `LatencyTracker`: Latency measurement
- `ThroughputTracker`: Throughput tracking
- `CacheOptimizer`: Cache-friendly layouts

**Techniques**:
1. **Cache Prefetching**: `_mm_prefetch` for next orders
2. **Data Alignment**: 64-byte cache line alignment
3. **Branch Prediction**: Likely/unlikely hints
4. **SIMD**: Vectorized operations where applicable
5. **Hot/Cold Splitting**: Separate hot and cold data

**Latency Measurement**:
```cpp
class LatencyTracker {
    void record(std::chrono::nanoseconds latency);
    double avg_latency_ns();
    double p99_latency_ns();
    double p999_latency_ns();
};
```

## Data Flow

### Order Submission Flow

```
1. Client sends NewOrderMessage via TCP
2. TcpGateway receives and validates message
3. Authentication & authorization check
4. Rate limiting check
5. RiskManager validates order
6. MatchingEngine receives order
7. OrderBook attempts matching
8. If matched: Execute trade, publish market data
9. If not matched: Add to order book
10. Send OrderAckMessage to client
```

### Trade Execution Flow

```
1. Aggressive order crosses with passive order
2. Calculate trade quantity (min of both)
3. Update order quantities
4. Update order statuses
5. Create Trade object
6. Invoke trade callback
7. Publish trade to market data
8. Update metrics
9. If order filled: Remove from book
10. If order partial: Keep in book
```

### Market Data Flow

```
1. Trade executed in matching engine
2. Trade added to market data queue
3. Publisher thread dequeues trade
4. Create market data message
5. Compute HMAC for authentication
6. Send via UDP multicast
7. Subscribers receive and validate HMAC
```

## Configuration

### Configuration Hierarchy

```
config.json
├── exchange (ports, name)
├── symbols (tick size, lot size)
├── risk (limits, collars)
├── performance (pools, queues)
├── logging (level, format)
└── persistence (snapshots, logs)
```

### Environment Variables

```bash
# Security (Required)
RTES_HMAC_KEY=<64-char-hex>
RTES_TLS_CERT=/path/to/cert.pem
RTES_TLS_KEY=/path/to/key.pem
RTES_CA_CERT=/path/to/ca.pem

# Optional
RTES_API_KEYS_FILE=/path/to/keys.conf
RTES_AUTH_MODE=development  # Dev only
```

## Error Handling

### Error Code Hierarchy

```cpp
enum class ErrorCode {
    // System errors (1000-1999)
    SYSTEM_SHUTDOWN = 1000,
    SYSTEM_CORRUPTED_STATE = 1001,
    
    // Network errors (2000-2999)
    NETWORK_CONNECTION_FAILED = 2000,
    NETWORK_DISCONNECTED = 2001,
    
    // Order errors (3000-3999)
    ORDER_INVALID = 3000,
    ORDER_DUPLICATE = 3001,
    ORDER_NOT_FOUND = 3002,
    
    // Risk errors (4000-4999)
    RISK_LIMIT_EXCEEDED = 4000,
    RISK_PRICE_COLLAR = 4001,
};
```

### Error Propagation

```cpp
// Result<T> monad for error handling
Result<void> add_order_safe(Order* order) {
    if (!order) return ErrorCode::ORDER_INVALID;
    
    auto result = validate_order(order);
    if (result.has_error()) return result.error();
    
    return Result<void>();
}
```

## Monitoring & Observability

### Metrics Exposed

**Prometheus Endpoint**: `http://localhost:8080/metrics`

```
# Throughput
rtes_orders_received_total
rtes_orders_accepted_total
rtes_orders_rejected_total
rtes_trades_executed_total

# Latency
rtes_order_latency_seconds{quantile="0.5"}
rtes_order_latency_seconds{quantile="0.99"}
rtes_order_latency_seconds{quantile="0.999"}

# System
rtes_connections_active
rtes_memory_pool_utilization
rtes_queue_depth
```

### Health Checks

**Endpoint**: `http://localhost:8080/health`

```json
{
  "status": "healthy",
  "components": {
    "tcp_gateway": "healthy",
    "risk_manager": "healthy",
    "matching_engine": "healthy",
    "market_data": "healthy"
  }
}
```

## Scalability

### Horizontal Scaling

- **Symbol Sharding**: Distribute symbols across instances
- **Client Sharding**: Partition clients by ID
- **Read Replicas**: Market data can be replicated

### Vertical Scaling

- **CPU Pinning**: Pin threads to specific cores
- **NUMA Awareness**: Allocate memory on local NUMA node
- **Huge Pages**: Use 2MB pages for memory pools

### Performance Targets

| Metric | Target | Achieved |
|--------|--------|----------|
| Throughput | ≥100K orders/sec | ~150K |
| Avg Latency | ≤10μs | ~8μs |
| P99 Latency | ≤100μs | ~85μs |
| P999 Latency | ≤500μs | ~450μs |

## Security Architecture

### Defense in Depth

1. **Network Layer**: TLS 1.2+, certificate validation
2. **Application Layer**: Authentication, authorization
3. **Input Layer**: Validation, sanitization
4. **Data Layer**: Encryption at rest (planned)

### Authentication Flow

```
1. Client connects with TLS certificate
2. Extract client ID from certificate CN
3. Validate certificate chain
4. Create session token
5. Include token in all subsequent messages
6. Validate token on each request
```

### Authorization Model

```
Roles:
- ADMIN: All operations
- TRADER: Place/cancel orders, view positions
- VIEWER: Read-only access

Permissions checked per operation
```

## Deployment Architecture

### Single-Host Deployment

```
┌─────────────────────────────────────┐
│         Host (Linux)                │
│  ┌──────────────────────────────┐  │
│  │   trading_exchange process   │  │
│  │   - TCP Gateway (8888)       │  │
│  │   - UDP Publisher (9999)     │  │
│  │   - Metrics (8080)           │  │
│  └──────────────────────────────┘  │
└─────────────────────────────────────┘
```

### Multi-Host Deployment

```
┌──────────────┐  ┌──────────────┐  ┌──────────────┐
│   Gateway    │  │   Gateway    │  │   Gateway    │
│   (AAPL)     │  │   (MSFT)     │  │   (GOOGL)    │
└──────┬───────┘  └──────┬───────┘  └──────┬───────┘
       │                 │                 │
       └─────────────────┴─────────────────┘
                         │
                    ┌────▼────┐
                    │  Market │
                    │  Data   │
                    │  Fanout │
                    └─────────┘
```

## Future Enhancements

1. **Persistence**: Add database for order/trade history
2. **Replication**: Multi-region deployment
3. **Advanced Orders**: Stop-loss, iceberg, etc.
4. **Market Making**: Built-in market maker strategies
5. **Analytics**: Real-time analytics engine
6. **WebSocket**: WebSocket API for web clients
7. **FIX Protocol**: Support FIX 4.2/4.4
8. **Blockchain**: Settlement on blockchain
