# Contributing to RTES

Thank you for your interest in contributing to the Real-Time Trading Exchange Simulator (RTES)!

## Development Setup

### Prerequisites
- C++20/23 compiler (GCC 11+ or Clang 13+)
- CMake 3.20+
- Docker and Docker Compose
- Git

### Local Development
```bash
# Clone the repository
git clone <repository-url>
cd RTES

# Build and test
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
ctest --output-on-failure

# Run with development config
./trading_exchange ../configs/config.json
```

### Docker Development
```bash
# Build development image
docker build -f docker/Dockerfile.dev -t rtes:dev .

# Run development container
docker run -it --rm -p 8888:8888 -p 8080:8080 -v $(pwd):/workspace rtes:dev

# Inside container
cd /workspace
cmake --build build
./build/trading_exchange configs/config.json
```

## Code Standards

### C++ Guidelines
- Follow C++20/23 best practices
- Use RAII for resource management
- Prefer `std::unique_ptr` over raw pointers
- Use `const` and `constexpr` where appropriate
- Avoid exceptions in hot paths
- Use explicit type conversions

### Performance Requirements
- Hot path must be allocation-free
- Use cache-line alignment for shared data
- Prefer lock-free algorithms where possible
- Profile performance-critical code
- Target: ≤10μs average latency, ≥100K orders/sec

### Code Style
```cpp
// Use snake_case for variables and functions
int order_count = 0;
void process_order();

// Use PascalCase for classes and types
class OrderBook;
enum class OrderType;

// Use UPPER_CASE for constants
constexpr int MAX_ORDER_SIZE = 10000;

// Prefer auto for complex types
auto order_map = std::unordered_map<OrderID, Order*>{};
```

### Memory Safety
- No raw `new`/`delete` - use smart pointers
- Use memory pool for frequent allocations
- Validate all input parameters
- Check array bounds explicitly
- Use sanitizers during development

## Testing

### Unit Tests
```bash
# Run all tests
cd build && ctest --output-on-failure

# Run specific test suite
./rtes_tests --gtest_filter="OrderBookTest.*"

# Run with memory checking
valgrind --tool=memcheck ./rtes_tests
```

### Integration Tests
```bash
# Start exchange
./trading_exchange ../configs/config.json &

# Run integration tests
./tcp_client localhost 8888
./client_simulator --strategy market_maker --symbol AAPL --duration 10
curl http://localhost:8080/metrics
```

### Performance Tests
```bash
# Run benchmarks
./scripts/benchmark.sh --duration 60 --clients 20

# Component benchmarks
./bench_memory_pool
./bench_matching
```

## Submission Guidelines

### Pull Request Process
1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-feature`
3. Make your changes with tests
4. Ensure all tests pass: `ctest --output-on-failure`
5. Run performance benchmarks if applicable
6. Submit pull request with clear description

### Commit Messages
```
feat: add order cancellation support
fix: resolve race condition in matching engine
perf: optimize memory pool allocation
docs: update API documentation
test: add integration tests for TCP gateway
```

### Code Review Checklist
- [ ] Code follows style guidelines
- [ ] All tests pass (unit + integration)
- [ ] Performance requirements met
- [ ] Memory safety verified
- [ ] Documentation updated
- [ ] No breaking changes (or properly documented)

## Architecture Guidelines

### Threading Model
- Single writer per symbol (matching engine)
- Lock-free queues between components
- Dedicated threads for I/O operations
- Avoid shared mutable state

### Error Handling
- Use return codes for expected errors
- Log errors with appropriate severity
- Fail fast on programming errors
- Graceful degradation under load

### Monitoring
- Add metrics for new features
- Use structured logging
- Include performance counters
- Provide health check endpoints

## Release Process

### Version Numbering
- Major: Breaking changes
- Minor: New features (backward compatible)
- Patch: Bug fixes

### Release Checklist
- [ ] All tests pass in CI
- [ ] Performance benchmarks meet SLOs
- [ ] Documentation updated
- [ ] Docker images built and tested
- [ ] Security scan passed
- [ ] Release notes prepared

## Getting Help

### Communication
- GitHub Issues: Bug reports and feature requests
- GitHub Discussions: Questions and general discussion
- Code Review: Pull request comments

### Resources
- [Architecture Documentation](docs/ARCHITECTURE.md)
- [API Reference](docs/API.md)
- [Performance Guide](docs/PERFORMANCE.md)
- [Operations Runbook](docs/RUNBOOK.md)

## License

By contributing to RTES, you agree that your contributions will be licensed under the MIT License.