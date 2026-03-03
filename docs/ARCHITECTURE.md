# RTES Architecture

## Threading Model

### Core Threads
1. **TCP Gateway Thread**: Handles incoming order connections, frame parsing
2. **Risk Thread**: Single-actor validation of all orders  
3. **Matching Threads**: One per symbol, single-writer matching engine
4. **Market Data Thread**: UDP multicast publisher
5. **Metrics Thread**: Prometheus endpoint and monitoring

### Queue Architecture
- **SPSC Queues**: Fixed thread pairs (Gateway→Risk, Risk→Matching)
- **MPMC Queue**: Shared fan-in for market data publishing
- **Memory Layout**: Cache-line padding, acquire/release semantics

## Data Flow

```
TCP Client → Gateway → Risk → Matching → Market Data → UDP Multicast
                ↓         ↓        ↓
              Metrics   Metrics  Metrics
```

## Order Lifecycle

1. **Ingress**: TCP frame → parse → validate format
2. **Risk**: Size/price/credit checks → approve/reject  
3. **Matching**: Price-time priority → fill/partial/rest
4. **Egress**: Trade/ack → UDP multicast + TCP response

## Memory Management

### Order Pool
- Fixed-size pool (1M orders default)
- O(1) allocation via free-list
- RAII lifetime management
- Pool returns on fill/cancel

### Order Book Structure
```cpp
struct OrderBook {
    std::map<Price, std::deque<OrderID>> bids;  // descending
    std::map<Price, std::deque<OrderID>> asks;  // ascending  
    std::unordered_map<OrderID, OrderPtr> lookup; // O(1) cancel
};
```

## Network Protocols

### TCP Order Entry (Port 8888)
```cpp
struct MessageHeader {
    uint32_t type;
    uint32_t length;
    uint64_t sequence;
    uint64_t timestamp;
    uint32_t checksum;
} __attribute__((packed));
```

### UDP Market Data (239.0.0.1:9999)
- BBO updates
- Trade notifications  
- Top-N depth snapshots
- Monotonic sequence numbers

## Concurrency Design

### Lock-Free Components
- SPSC/MPMC queues (sequence tickets)
- Memory pool free-list
- Atomic counters for metrics

### Synchronization Points
- Risk state (confined to risk thread)
- Order book (single writer per symbol)
- Market data aggregation (MPMC queue)

## Performance Optimizations

### Hot Path
- Zero allocations in matching
- Branch prediction hints
- Cache prefetching
- Explicit inlining

### Memory Layout
- Cache-line alignment
- False sharing avoidance
- Contiguous data structures