# Real-Time Trading Exchange Simulator (RTES)

A high-performance, low-latency trading exchange simulator built with modern C++20/23.

## Performance Targets
- **Throughput**: ≥100,000 orders/sec (single host)
- **Latency**: avg ≤10μs, p99 ≤100μs, p999 ≤500μs
- **Deterministic**: Zero data races, allocation-free hot path

## Architecture
- **TCP Order Gateway** (port 8888): Binary protocol with robust framing
- **Risk Manager**: Pre-trade validation (size, price, credit limits)
- **Matching Engine**: Per-symbol single-writer, price-time priority
- **Market Data**: UDP multicast (239.0.0.1:9999) BBO/trades/depth
- **Metrics**: Prometheus endpoint (port 8080)

## Quick Start

```bash
# Build
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Run tests
ctest --output-on-failure

# Start exchange
./trading_exchange ../configs/config.json

# Run client simulator
./client_simulator --strategy market_maker --symbol AAPL

# Monitor metrics
curl -s localhost:8080/metrics | head
```

## Repository Structure
```
├── include/           # Public headers
├── src/              # Implementation
├── tests/            # Unit & integration tests
├── tools/            # Load generators, utilities
├── docs/             # Architecture & performance guides
├── configs/          # Configuration files
├── docker/           # Container definitions
└── .github/workflows/ # CI/CD
```

## Security Features
- **Input Sanitization**: All user inputs validated and sanitized
- **Authentication**: Role-based access control with token validation
- **Authorization**: Operation-level permission checks
- **Secure Logging**: Log injection prevention with type-safe formatting
- **Path Validation**: Protection against path traversal attacks

See [SECURITY.md](docs/SECURITY.md) for detailed security implementation.

## Requirements
- C++20/23 compiler (GCC 11+, Clang 13+)
- CMake 3.20+
- Linux (recommended for performance)

## License
MIT License - see LICENSE file