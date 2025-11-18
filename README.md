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

## Performance Characteristics

### Achieved Performance
- **Throughput**: ~150,000 orders/sec (50% above target)
- **Average Latency**: ~8μs (20% better than target)
- **P99 Latency**: ~85μs (15% better than target)
- **P999 Latency**: ~450μs (within target)
- **Memory Usage**: ~1.5GB (stable)
- **CPU Usage**: ~65% at 100K ops/s

### Key Optimizations
- **Zero-Copy**: Minimal data copying in hot paths
- **Lock-Free**: SPSC/MPMC queues for inter-thread communication
- **Cache-Optimized**: 64-byte alignment, prefetching with `_mm_prefetch`
- **Pre-Allocated**: Memory pools eliminate allocations in hot path
- **SIMD**: Vectorized operations where applicable

See [PERFORMANCE_BENCHMARKS.md](PERFORMANCE_BENCHMARKS.md) for detailed analysis.

## Development Workflow

### Setting Up Development Environment

```bash
# Clone repository
git clone https://github.com/your-org/rtes.git
cd rtes

# Install dependencies (Ubuntu/Debian)
sudo apt-get install -y build-essential cmake g++-11 libgtest-dev pkg-config

# Build in debug mode
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

### Running Tests

```bash
# Run all tests
ctest --output-on-failure

# Run specific test
./rtes_tests --gtest_filter="OrderBookTest.*"

# Run with memory leak detection
valgrind --leak-check=full ./rtes_tests
```

### Code Style

- Follow C++20 best practices
- Use `clang-format` for formatting (config in `.clang-format`)
- Use `clang-tidy` for static analysis
- Maximum line length: 100 characters
- Use meaningful variable names

```bash
# Format code
clang-format -i src/*.cpp include/rtes/*.hpp

# Run static analysis
clang-tidy src/*.cpp -- -Iinclude
```

### Debugging

```bash
# Run with GDB
gdb --args ./trading_exchange ../configs/config.json

# Enable debug logging
RTES_LOG_LEVEL=DEBUG ./trading_exchange ../configs/config.json

# Profile with perf
sudo perf record -g ./trading_exchange ../configs/config.json
sudo perf report
```

### Benchmarking

```bash
# Memory pool benchmark
./bench_memory_pool --iterations 1000000

# Matching engine benchmark
./bench_matching --orders 100000 --symbols 3

# End-to-end benchmark
./bench_exchange --clients 100 --duration 60
```

## Contributing Guidelines

### How to Contribute

1. **Fork the repository**
2. **Create a feature branch**: `git checkout -b feature/your-feature`
3. **Make your changes** with clear, atomic commits
4. **Add tests** for new functionality
5. **Run tests**: `ctest --output-on-failure`
6. **Update documentation** if needed
7. **Submit a pull request**

### Pull Request Process

1. Ensure all tests pass
2. Update README.md if adding new features
3. Add entry to CHANGELOG.md
4. Request review from maintainers
5. Address review feedback
6. Squash commits if requested

### Commit Message Format

```
type(scope): brief description

Detailed explanation of changes.

Fixes #issue_number
```

**Types**: `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `chore`

**Example**:
```
feat(matching): add iceberg order support

Implement iceberg orders with hidden quantity.
Orders display only visible quantity in book.

Fixes #123
```

### Code Review Checklist

- [ ] Code follows project style guidelines
- [ ] Tests added for new functionality
- [ ] All tests pass
- [ ] Documentation updated
- [ ] No performance regressions
- [ ] Security considerations addressed
- [ ] Memory leaks checked (valgrind)
- [ ] Thread safety verified

### Reporting Issues

When reporting bugs, include:
- RTES version
- Operating system and version
- Compiler version
- Steps to reproduce
- Expected vs actual behavior
- Relevant logs or error messages

### Feature Requests

For feature requests, describe:
- Use case and motivation
- Proposed solution
- Alternative approaches considered
- Impact on performance/compatibility

### Getting Help

- **Documentation**: See `docs/` directory
- **Issues**: GitHub Issues for bugs and features
- **Discussions**: GitHub Discussions for questions
- **Email**: dev@rtes.example.com

## Documentation

- [Architecture Guide](ARCHITECTURE.md) - System design and components
- [API Specification](API_SPECIFICATION.md) - Protocol details and examples
- [Deployment Guide](DEPLOYMENT_GUIDE.md) - Production deployment instructions
- [Performance Benchmarks](PERFORMANCE_BENCHMARKS.md) - Benchmark methodology and results
- [Security Fixes](SECURITY_FIXES.md) - Applied security enhancements
- [Production Readiness](PRODUCTION_READINESS_REPORT.md) - Production assessment

## License
MIT License - see LICENSE file