# RTES Performance Tuning Guide

## System Configuration

### Linux Kernel Parameters
```bash
# Network buffers
echo 'net.core.rmem_max = 268435456' >> /etc/sysctl.conf
echo 'net.core.wmem_max = 268435456' >> /etc/sysctl.conf
echo 'net.core.netdev_max_backlog = 5000' >> /etc/sysctl.conf

# TCP settings
echo 'net.ipv4.tcp_rmem = 4096 87380 268435456' >> /etc/sysctl.conf
echo 'net.ipv4.tcp_wmem = 4096 65536 268435456' >> /etc/sysctl.conf
echo 'net.ipv4.tcp_congestion_control = bbr' >> /etc/sysctl.conf

# Apply settings
sysctl -p
```

### CPU Configuration
```bash
# Disable CPU frequency scaling
echo performance > /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# CPU isolation (reserve cores 2-7 for RTES)
# Add to kernel boot parameters:
isolcpus=2-7 nohz_full=2-7 rcu_nocbs=2-7

# Disable hyperthreading for consistent latency
echo off > /sys/devices/system/cpu/smt/control
```

### Memory Configuration
```bash
# Huge pages for reduced TLB misses
echo 1024 > /proc/sys/vm/nr_hugepages

# Disable swap
swapoff -a

# Memory overcommit
echo 1 > /proc/sys/vm/overcommit_memory
```

## Application Tuning

### Thread Affinity
```json
{
  "performance": {
    "enable_cpu_pinning": true,
    "thread_affinity": {
      "tcp_gateway": 2,
      "risk_manager": 3,
      "matching_aapl": 4,
      "matching_msft": 5,
      "matching_googl": 6,
      "market_data": 7,
      "metrics": 1
    }
  }
}
```

### Memory Pool Sizing
```json
{
  "performance": {
    "order_pool_size": 2000000,    // 2M orders for high throughput
    "queue_capacity": 131072       // 128K queue depth
  }
}
```

### Risk Manager Tuning
```json
{
  "risk": {
    "max_order_size": 100000,
    "max_notional_per_client": 10000000.0,
    "max_orders_per_second": 10000,    // Higher for performance testing
    "price_collar_enabled": false      // Disable for benchmarking
  }
}
```

## Compiler Optimizations

### Release Build Flags
```cmake
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -mtune=native -flto -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fno-omit-frame-pointer")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -funroll-loops")
```

### Profile-Guided Optimization
```bash
# Step 1: Build with profiling
cmake -DCMAKE_CXX_FLAGS="-O3 -march=native -fprofile-generate" ..
make -j$(nproc)

# Step 2: Run representative workload
./trading_exchange configs/config.json &
./load_generator --clients 20 --duration 60
killall trading_exchange

# Step 3: Rebuild with profile data
cmake -DCMAKE_CXX_FLAGS="-O3 -march=native -fprofile-use" ..
make -j$(nproc)
```

## Network Optimization

### NIC Configuration
```bash
# Increase ring buffer sizes
ethtool -G eth0 rx 4096 tx 4096

# Enable receive packet steering
echo 7 > /sys/class/net/eth0/queues/rx-0/rps_cpus

# Disable interrupt coalescing for low latency
ethtool -C eth0 rx-usecs 0 tx-usecs 0

# Enable hardware timestamping
ethtool -K eth0 rx-timestamping on tx-timestamping on
```

### Socket Tuning
```cpp
// In TCP gateway setup
int tcp_nodelay = 1;
setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &tcp_nodelay, sizeof(tcp_nodelay));

int tcp_quickack = 1;
setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &tcp_quickack, sizeof(tcp_quickack));

// Larger socket buffers
int buffer_size = 2 * 1024 * 1024;  // 2MB
setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size));
setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size));
```

## Monitoring and Profiling

### Performance Monitoring
```bash
# CPU utilization per core
mpstat -P ALL 1

# Memory usage
free -h && cat /proc/meminfo | grep Huge

# Network statistics
ss -i | grep rtes
sar -n DEV 1

# Context switches
vmstat 1

# Cache misses
perf stat -e cache-misses,cache-references ./trading_exchange configs/config.json
```

### Latency Profiling
```bash
# Function-level profiling
perf record -g ./trading_exchange configs/config.json
perf report

# Flame graph generation
perf record -F 997 -g ./trading_exchange configs/config.json
perf script | stackcollapse-perf.pl | flamegraph.pl > profile.svg
```

### Memory Profiling
```bash
# Heap profiling with gperftools
LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libprofiler.so \
CPUPROFILE=rtes.prof ./trading_exchange configs/config.json

# Memory leak detection
valgrind --tool=memcheck --leak-check=full ./trading_exchange configs/config.json
```

## Hardware Recommendations

### CPU Requirements
- **Minimum**: 8 cores, 3.0 GHz (Intel Xeon Silver, AMD EPYC 7002)
- **Recommended**: 16+ cores, 3.5+ GHz (Intel Xeon Gold, AMD EPYC 7003)
- **Optimal**: High-frequency cores with large L3 cache

### Memory Requirements
- **Minimum**: 16GB DDR4-2400
- **Recommended**: 32GB DDR4-3200 or faster
- **Optimal**: 64GB with ECC support

### Network Requirements
- **Minimum**: 1Gbps NIC with hardware timestamping
- **Recommended**: 10Gbps NIC with SR-IOV support
- **Optimal**: 25Gbps+ with kernel bypass (DPDK)

### Storage Requirements
- **Minimum**: SSD for logs and configuration
- **Recommended**: NVMe SSD for event logging
- **Optimal**: Persistent memory (Intel Optane) for ultra-low latency

## Benchmark Targets

### Latency Targets
```
Single Order Latency:
- Average: ≤ 5μs
- P99: ≤ 50μs  
- P999: ≤ 200μs
- P9999: ≤ 1ms
```

### Throughput Targets
```
Orders per Second:
- Single client: ≥ 50,000
- 10 clients: ≥ 300,000
- 50 clients: ≥ 1,000,000
- 100 clients: ≥ 1,500,000
```

### Resource Utilization
```
CPU Usage:
- Matching engine: ≤ 80% per core
- Risk manager: ≤ 60% per core
- Network I/O: ≤ 70% per core

Memory Usage:
- Resident set: ≤ 2GB
- Order pool: ≤ 1GB
- Queue buffers: ≤ 512MB
```

## Troubleshooting Performance Issues

### High Latency Symptoms
```bash
# Check CPU frequency scaling
cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq

# Check for thermal throttling
dmesg | grep -i thermal

# Check interrupt distribution
cat /proc/interrupts | grep eth0

# Check for memory pressure
cat /proc/vmstat | grep -E "(pgmajfault|pgpgin|pgpgout)"
```

### Low Throughput Symptoms
```bash
# Check queue depths
curl -s http://localhost:8080/metrics | grep queue_depth

# Check reject rates
curl -s http://localhost:8080/metrics | grep rejected

# Check network buffer overruns
netstat -i | grep -E "(RX-OVR|TX-OVR)"

# Check file descriptor limits
ulimit -n
lsof -p $(pgrep trading_exchange) | wc -l
```

### Memory Issues
```bash
# Check for memory leaks
ps -o pid,vsz,rss,pmem -p $(pgrep trading_exchange)

# Check huge page usage
cat /proc/meminfo | grep -i huge

# Check NUMA topology
numactl --hardware
numastat -p $(pgrep trading_exchange)
```

## Production Deployment Checklist

- [ ] Kernel parameters optimized
- [ ] CPU isolation configured
- [ ] Huge pages enabled
- [ ] Network interface tuned
- [ ] Thread affinity configured
- [ ] Monitoring dashboards setup
- [ ] Alerting thresholds configured
- [ ] Performance baselines established
- [ ] Capacity planning completed
- [ ] Disaster recovery tested