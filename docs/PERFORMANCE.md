# RTES Performance Guide

## Target SLOs
- **Throughput**: ≥100,000 orders/sec (single host)
- **Latency**: avg ≤10μs, p99 ≤100μs, p999 ≤500μs
- **Deterministic**: Zero allocation hot path, clean backpressure

## System Requirements

### Hardware
- CPU: 8+ cores, 3.0+ GHz (Intel Xeon/AMD EPYC recommended)
- RAM: 16GB+ (32GB for production)
- Network: 10Gbps+ NIC with SR-IOV support
- Storage: NVMe SSD for logging (optional)

### OS Configuration
```bash
# Kernel parameters
echo 'net.core.rmem_max = 268435456' >> /etc/sysctl.conf
echo 'net.core.wmem_max = 268435456' >> /etc/sysctl.conf
echo 'net.core.netdev_max_backlog = 5000' >> /etc/sysctl.conf

# CPU isolation (cores 2-7 for exchange)
echo 'isolcpus=2-7 nohz_full=2-7 rcu_nocbs=2-7' >> /boot/grub/grub.cfg

# Disable CPU frequency scaling
echo performance > /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# Huge pages
echo 1024 > /proc/sys/vm/nr_hugepages
```

### Thread Pinning
```json
{
  "performance": {
    "enable_cpu_pinning": true,
    "thread_affinity": {
      "tcp_gateway": 2,
      "risk_manager": 3,
      "matching_aapl": 4,
      "matching_msft": 5,
      "market_data": 6,
      "metrics": 7
    }
  }
}
```

## Benchmarking

### Load Test Setup
```bash
# Build optimized binary
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-march=native"
make -j$(nproc)

# Run benchmark
./bench_exchange --symbols AAPL,MSFT --orders 1000000 --clients 10
```

### Expected Results
```
Symbol: AAPL
Orders processed: 1,000,000
Throughput: 125,847 orders/sec
Latency (μs): avg=7.2, p50=6.8, p90=12.1, p99=45.3, p999=156.2
Memory: 0 allocations in hot path
```

## Optimization Techniques

### Memory Layout
```cpp
// Cache-line aligned structures
struct alignas(64) OrderBookLevel {
    std::atomic<uint64_t> price;
    std::deque<OrderID> orders;
    char padding[64 - sizeof(price) - sizeof(orders)];
};
```

### Branch Prediction
```cpp
// Hot path optimization
if ([[likely]] order->quantity <= available_quantity) {
    // Full fill path (common case)
    execute_trade(order, available_quantity);
} else {
    // Partial fill path (rare)
    execute_partial_trade(order, available_quantity);
}
```

### Prefetching
```cpp
// Prefetch next order in queue
if (orders.size() > 1) {
    __builtin_prefetch(&orders[1], 0, 3);
}
```

## Monitoring

### Key Metrics
- `rtes_orders_total{status="accepted|rejected"}`
- `rtes_trades_total{symbol}`
- `rtes_latency_seconds{quantile="0.5|0.9|0.99|0.999"}`
- `rtes_queue_depth{queue="risk|matching|market_data"}`

### Alerting Thresholds
- Latency p99 > 100μs
- Queue depth > 80% capacity
- Reject rate > 1%
- Memory usage > 90%

## Troubleshooting

### High Latency
1. Check CPU frequency scaling disabled
2. Verify thread pinning active
3. Monitor queue depths for bottlenecks
4. Check network buffer sizes

### Low Throughput  
1. Increase queue capacities
2. Add more matching engine threads
3. Optimize risk check logic
4. Profile with perf/vtune

### Memory Issues
1. Monitor order pool utilization
2. Check for memory leaks with valgrind
3. Verify RAII order lifecycle
4. Tune pool size in config