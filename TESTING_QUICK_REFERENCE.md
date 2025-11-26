# RTES Testing Quick Reference

## Quick Start

```bash
# Build and run all tests
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
ctest --output-on-failure
```

## Test Runner Commands

```bash
# Run all tests
./scripts/run_tests.sh --all

# Run by category
./scripts/run_tests.sh --unit           # Unit tests only
./scripts/run_tests.sh --integration    # Integration tests only
./scripts/run_tests.sh --performance    # Performance tests only

# Run specific tests
./scripts/run_tests.sh --filter "OrderBook*"
./scripts/run_tests.sh --filter "Memory*"

# Run with options
./scripts/run_tests.sh --verbose        # Verbose output
./scripts/run_tests.sh --memcheck       # With valgrind
```

## Direct Test Execution

```bash
cd build

# Run all tests
./rtes_tests

# Run specific test suite
./rtes_tests --gtest_filter="SecurityUtilsTest.*"
./rtes_tests --gtest_filter="IntegrationAPITest.*"
./rtes_tests --gtest_filter="PerformanceRegressionTest.*"

# Run specific test
./rtes_tests --gtest_filter="PerformanceRegressionTest.OrderBookAddOrderLatency"

# Run with verbose output
./rtes_tests --gtest_print_time=1 --gtest_color=yes
```

## Test Categories

### Unit Tests (30+ tests)
```bash
./scripts/run_tests.sh --unit
```
- SecurityUtils (log sanitization, path validation)
- ProtocolUtils (checksums, timestamps)
- MessageValidator (header/message validation)
- FieldValidators (range, length, format validation)
- InputSanitizer (input cleaning)

### Integration Tests (12+ tests)
```bash
./scripts/run_tests.sh --integration
```
- OrderBook + MemoryPool integration
- Protocol message handling
- Multi-symbol order books
- Matching engine workflows
- Partial fills and cancellations

### Performance Tests (12+ tests)
```bash
./scripts/run_tests.sh --performance
```
- Memory pool: <1μs allocation, >1M ops/s
- OrderBook: <10μs avg, <100μs P99
- Protocol: <2μs checksum
- End-to-end: <20μs avg, <100μs P99, <500μs P999

## Performance Targets

| Component | Target | Command |
|-----------|--------|---------|
| Memory Pool Alloc | <1μs | `--filter "MemoryPoolAllocationLatency"` |
| OrderBook Add | <10μs avg | `--filter "OrderBookAddOrderLatency"` |
| OrderBook Cancel | <5μs | `--filter "OrderBookCancelOrderLatency"` |
| OrderBook Match | <15μs | `--filter "OrderBookMatchingLatency"` |
| End-to-End | <20μs avg | `--filter "EndToEndOrderProcessingLatency"` |

## Debugging Tests

```bash
# Run with GDB
gdb --args ./rtes_tests --gtest_filter="FailingTest.*"

# Run with Valgrind
valgrind --leak-check=full ./rtes_tests --gtest_filter="FailingTest.*"

# Run with sanitizers (Debug build)
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
./rtes_tests
```

## Common Filters

```bash
# All OrderBook tests
./rtes_tests --gtest_filter="*OrderBook*"

# All Memory tests
./rtes_tests --gtest_filter="*Memory*"

# All Protocol tests
./rtes_tests --gtest_filter="*Protocol*"

# All Security tests
./rtes_tests --gtest_filter="*Security*"

# All Validation tests
./rtes_tests --gtest_filter="*Validator*"
```

## Test Files

- `tests/test_utility_functions.cpp` - Unit tests for utilities
- `tests/test_integration_api.cpp` - Integration tests
- `tests/test_performance_regression.cpp` - Performance tests

## Documentation

- `TEST_SUITE_DOCUMENTATION.md` - Comprehensive test guide
- `TEST_SUITE_ADDED.md` - Summary of additions
- `TESTING_QUICK_REFERENCE.md` - This file

## CI/CD

Tests run automatically on:
- Push to `main` or `develop`
- Pull requests
- Multiple compilers (GCC 11, Clang 13)
- Debug and Release modes

## Troubleshooting

### Tests not found
```bash
# Rebuild tests
cd build
make -j$(nproc)
```

### Valgrind not found
```bash
sudo apt-get install valgrind
```

### Test failures
```bash
# Run specific failing test with verbose output
./rtes_tests --gtest_filter="FailingTest.*" --gtest_print_time=1
```

### Performance test failures
- Check system load (other processes)
- Run in Release mode
- Disable CPU frequency scaling
- Close other applications

## Adding New Tests

### Unit Test
```cpp
TEST(ComponentTest, FeatureName) {
    // Arrange
    auto component = create_component();
    
    // Act
    auto result = component.method();
    
    // Assert
    EXPECT_TRUE(result.is_ok());
}
```

### Integration Test
```cpp
TEST_F(IntegrationAPITest, FeatureIntegration) {
    OrderBook book("SYMBOL", *pool);
    auto* order = pool->allocate();
    // ... configure and test
    EXPECT_EQ(book.order_count(), 1);
}
```

### Performance Test
```cpp
TEST_F(PerformanceRegressionTest, FeaturePerformance) {
    constexpr double TARGET_US = 10.0;
    auto latency = measure_latency_us([&]() {
        // Operation to measure
    }, ITERATIONS);
    EXPECT_LT(latency, TARGET_US);
}
```

## Useful Commands

```bash
# List all tests
./rtes_tests --gtest_list_tests

# Count tests
./rtes_tests --gtest_list_tests | grep -c "^  "

# Run tests in parallel (CTest)
ctest -j$(nproc)

# Run tests with timeout
timeout 60s ./rtes_tests

# Generate test report
./rtes_tests --gtest_output=xml:test_results.xml
```

## Performance Optimization Tips

For accurate performance measurements:
1. Build in Release mode: `cmake .. -DCMAKE_BUILD_TYPE=Release`
2. Close other applications
3. Disable CPU frequency scaling: `sudo cpupower frequency-set -g performance`
4. Run multiple times and average results
5. Check for thermal throttling

## Memory Leak Detection

```bash
# Quick check
./scripts/run_tests.sh --memcheck

# Detailed check
valgrind --leak-check=full --show-leak-kinds=all \
         --track-origins=yes --verbose \
         ./rtes_tests --gtest_filter="*"

# Specific test
valgrind --leak-check=full \
         ./rtes_tests --gtest_filter="MemoryPoolTest.*"
```

## Test Metrics

- **Total Tests**: 54+
- **Execution Time**: ~15-20 seconds
- **Pass Rate**: 100% (target)
- **Coverage**: ~75% (core components)

## Support

- Documentation: `docs/` directory
- Issues: GitHub Issues
- Test docs: `TEST_SUITE_DOCUMENTATION.md`
