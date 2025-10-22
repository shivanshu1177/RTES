# Thread Safety and Concurrency Framework

## Overview

The RTES thread safety framework provides comprehensive concurrency control, deadlock prevention, and graceful shutdown coordination for high-performance trading systems.

## Key Components

### 1. Thread Safety Primitives

#### `scoped_lock<Mutexes...>`
- **Purpose**: RAII lock with deadlock detection
- **Features**:
  - Automatic deadlock detection before acquisition
  - Consistent lock ordering to prevent deadlocks
  - Exception-safe lock management
  - Support for multiple mutex types

```cpp
std::mutex order_mutex, trade_mutex;
{
    scoped_lock lock(order_mutex, trade_mutex);
    // Critical section - both mutexes held
} // Automatic unlock
```

#### `atomic_wrapper<T>`
- **Purpose**: Enhanced atomic operations with memory ordering
- **Features**:
  - Explicit memory ordering control
  - Type-safe atomic operations
  - Performance-optimized for trading systems

```cpp
atomic_wrapper<bool> shutdown_requested(false);
atomic_wrapper<uint64_t> order_count(0);

// Memory ordering control
bool is_shutdown = shutdown_requested.load(std::memory_order_acquire);
order_count.store(new_count, std::memory_order_release);
```

#### `condition_variable_safe`
- **Purpose**: Condition variable with predicate validation
- **Features**:
  - Mandatory predicate checking
  - Timeout support with predicates
  - Exception-safe waiting

```cpp
condition_variable_safe cv;
std::mutex mutex;
atomic_wrapper<bool> ready(false);

std::unique_lock lock(mutex);
cv.wait(lock, [&] { return ready.load(); });
```

### 2. Deadlock Detection System

#### `DeadlockDetector`
- **Purpose**: Runtime deadlock detection and prevention
- **Algorithm**: Cycle detection in lock dependency graph
- **Features**:
  - Per-thread lock tracking
  - Circular dependency detection
  - Proactive deadlock prevention

```cpp
// Automatic deadlock detection in scoped_lock
try {
    scoped_lock lock(mutex1, mutex2);
    // Safe critical section
} catch (const std::runtime_error& e) {
    // Deadlock would have occurred
}
```

### 3. Shutdown Coordination

#### `ShutdownManager`
- **Purpose**: Coordinated system shutdown
- **Features**:
  - Component registration
  - Ordered shutdown sequence
  - Graceful resource cleanup
  - Thread-safe shutdown signaling

```cpp
// Component registration
ShutdownManager::instance().register_component(
    "OrderBook", 
    [this]() { shutdown(); }
);

// Initiate shutdown
ShutdownManager::instance().initiate_shutdown();

// Wait for completion
ShutdownManager::instance().wait_for_shutdown();
```

#### `WorkDrainer`
- **Purpose**: Work queue drainage during shutdown
- **Features**:
  - Graceful work completion
  - Queue drainage on shutdown
  - Thread-safe work submission

```cpp
WorkDrainer drainer("TcpGateway");

// Add work items
drainer.add_work_item([this]() { 
    process_order(); 
});

// Process work (in worker thread)
drainer.process_work();
```

### 4. Concurrency Validation

#### `LockOrderValidator`
- **Purpose**: Lock ordering validation
- **Features**:
  - Lock dependency graph maintenance
  - Ordering violation detection
  - Runtime validation

```cpp
auto& validator = LockOrderValidator::instance();

// Register lock ordering
validator.register_lock_order(&mutex1, &mutex2);

// Validate lock sequence
std::vector<void*> locks = {&mutex1, &mutex2};
bool valid = validator.validate_lock_order(locks);
```

#### `RaceDetector`
- **Purpose**: Data race detection
- **Features**:
  - Memory access tracking
  - Concurrent access detection
  - Synchronization point tracking

```cpp
auto& detector = RaceDetector::instance();

// Register memory access
detector.register_memory_access(&shared_data, true); // write
detector.register_memory_access(&shared_data, false); // read

// Check for races
bool has_race = detector.has_race_condition();
```

## Thread Safety Annotations

### Clang Thread Safety Analysis
```cpp
#define GUARDED_BY(x) __attribute__((guarded_by(x)))
#define REQUIRES(x) __attribute__((requires_capability(x)))
#define EXCLUDES(x) __attribute__((locks_excluded(x)))
#define ACQUIRE(x) __attribute__((acquire_capability(x)))
#define RELEASE(x) __attribute__((release_capability(x)))
```

### Usage Examples
```cpp
class OrderBook {
private:
    std::mutex order_mutex_;
    std::unordered_map<OrderID, Order*> orders_ GUARDED_BY(order_mutex_);
    
public:
    void add_order(Order* order) REQUIRES(order_mutex_);
    void remove_order(OrderID id) EXCLUDES(order_mutex_);
};
```

## Integration with RTES Components

### OrderBook Thread Safety
```cpp
class OrderBook {
private:
    mutable std::mutex order_mutex_;
    atomic_wrapper<bool> shutdown_requested_;
    
public:
    Result<void> add_order_safe(Order* order) {
        if (shutdown_requested_.load()) {
            return ErrorCode::SYSTEM_SHUTDOWN;
        }
        
        scoped_lock lock(order_mutex_);
        // Thread-safe order processing
    }
    
    void shutdown() {
        shutdown_requested_.store(true);
        scoped_lock lock(order_mutex_);
        // Clean shutdown
    }
};
```

### TCP Gateway Thread Safety
```cpp
class TcpGateway {
private:
    std::mutex connections_mutex_;
    std::unordered_map<int, std::shared_ptr<ClientConnection>> 
        connections_ GUARDED_BY(connections_mutex_);
    WorkDrainer work_drainer_{"TcpGateway"};
    
public:
    void handle_client_data(int fd) {
        std::shared_ptr<ClientConnection> conn;
        {
            scoped_lock lock(connections_mutex_);
            auto it = connections_.find(fd);
            if (it != connections_.end()) {
                conn = it->second;
            }
        }
        
        if (conn) {
            // Process without holding lock
        }
    }
};
```

## Performance Considerations

### Lock-Free Operations
- Use `atomic_wrapper` for simple counters and flags
- Minimize critical section size
- Prefer read-write locks for read-heavy workloads

### Memory Ordering
```cpp
// Release-acquire ordering for producer-consumer
producer_ready_.store(true, std::memory_order_release);
if (producer_ready_.load(std::memory_order_acquire)) {
    // Consumer can safely read producer data
}
```

### Cache-Friendly Locking
```cpp
// Group related data under same lock
struct OrderBookLevel {
    std::mutex level_mutex_;
    Price price_ GUARDED_BY(level_mutex_);
    Quantity quantity_ GUARDED_BY(level_mutex_);
    std::deque<Order*> orders_ GUARDED_BY(level_mutex_);
};
```

## Error Handling

### Deadlock Recovery
```cpp
try {
    scoped_lock lock(mutex1, mutex2);
    // Critical section
} catch (const std::runtime_error& e) {
    if (std::string(e.what()).find("deadlock") != std::string::npos) {
        // Handle deadlock - retry with backoff
        std::this_thread::sleep_for(std::chrono::microseconds(rand() % 100));
        // Retry operation
    }
}
```

### Race Condition Handling
```cpp
if (RaceDetector::instance().has_race_condition()) {
    LOG_ERROR("Data race detected - system integrity compromised");
    ShutdownManager::instance().initiate_shutdown();
}
```

## Best Practices

### 1. Lock Ordering
- Always acquire locks in consistent order
- Use `LockOrderValidator` to verify ordering
- Document lock hierarchies

### 2. Atomic Operations
- Use appropriate memory ordering
- Prefer `atomic_wrapper` over raw atomics
- Minimize atomic operation overhead

### 3. Shutdown Coordination
- Register all components with `ShutdownManager`
- Implement graceful shutdown in all threads
- Use `WorkDrainer` for work queue cleanup

### 4. Testing
- Use thread sanitizer in debug builds
- Test with high concurrency loads
- Validate shutdown behavior under stress

## Monitoring and Debugging

### Runtime Checks
```cpp
// Enable in debug builds
#ifdef DEBUG
    if (RaceDetector::instance().has_race_condition()) {
        std::abort(); // Fail fast on race detection
    }
#endif
```

### Performance Metrics
```cpp
// Track lock contention
auto start = std::chrono::high_resolution_clock::now();
scoped_lock lock(mutex);
auto duration = std::chrono::high_resolution_clock::now() - start;
if (duration > std::chrono::microseconds(100)) {
    LOG_WARN("High lock contention detected");
}
```

## Configuration

### Compile-Time Options
```cpp
// Enable thread safety annotations
#define THREAD_SAFETY_ANALYSIS 1

// Enable deadlock detection
#define ENABLE_DEADLOCK_DETECTION 1

// Enable race detection
#define ENABLE_RACE_DETECTION 1
```

### Runtime Configuration
```cpp
// Configure deadlock detection sensitivity
DeadlockDetector::instance().set_detection_threshold(
    std::chrono::milliseconds(100)
);

// Configure race detection window
RaceDetector::instance().set_detection_window(
    std::chrono::microseconds(50)
);
```

This thread safety framework ensures the RTES system operates correctly under high concurrency while maintaining the ultra-low latency requirements of high-frequency trading systems.