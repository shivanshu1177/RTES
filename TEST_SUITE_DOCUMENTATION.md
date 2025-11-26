# RTES Test Suite Documentation

## Overview
Comprehensive test suite covering unit tests, integration tests, and performance regression tests for the Real-Time Trading Exchange Simulator.

## Test Categories

### 1. Unit Tests for Utility Functions
**File**: `tests/test_utility_functions.cpp`

Tests low-level utility functions through public interfaces:

#### SecurityUtils Tests
- `SanitizeLogInput`: Validates log injection prevention
- `ValidateFilePath`: Tests path traversal protection
- `NormalizePath`: Verifies path normalization
- `IsValidSymbol`: Symbol format validation
- `IsValidOrderId`: Order ID validation
- `IsSafeString`: String safety checks

#### ProtocolUtils Tests
- `CalculateChecksum`: Checksum computation consistency
- `ValidateChecksum`: Message integrity verification
- `GetTimestamp`: Timestamp generation monotonicity

#### MessageValidator Tests
- `ValidateMessageHeader`: Header validation
- `IsValidMessageType`: Message type enumeration checks
- `IsValidMessageSize`: Size boundary validation

#### FieldValidators Tests
- `RangeValidator`: Numeric range validation
- `PositiveValidator`: Positive number validation
- `LengthValidator`: String length constraints
- `AlphanumericValidator`: Character set validation
- `SymbolValidator`: Trading symbol validation
- `ValidatePriceQuantityRelationship`: Cross-field validation
- `ValidatePortRange`: Network port validation
- `ValidatePercentage`: Percentage range validation
- `SanitizeString`: String sanitization
- `SanitizeSymbol`: Symbol normalization
- `SanitizeClientId`: Client ID sanitization

#### InputSanitizer Tests
- `SanitizeNetworkInput`: Network input cleaning
- `RemoveControlChars`: Control character removal
- `NormalizeWhitespace`: Whitespace normalization

#### ValidationChain Tests
- `AddRuleAndValidate`: Rule-based validation
- `CustomValidator`: Custom validation logic

**Total Tests**: 30+

### 2. Integration Tests
**File**: `tests/test_integration_api.cpp`

Tests component interactions through public APIs:

#### OrderBook + MemoryPool Integration
- `OrderBookMemoryPoolIntegration`: Order allocation and book management
- `OrderBookDepthSnapshot`: Market depth snapshot generation
- `OrderBookCancellation`: Order cancellation workflow

#### Protocol Integration
- `ProtocolMessageRoundTrip`: Message serialization/validation
- `ProtocolCancelOrderMessage`: Cancel message handling

#### Multi-Component Integration
- `MultiSymbolOrderBooks`: Multiple order books isolation
- `OrderBookErrorHandling`: Error propagation testing

#### Matching Engine Integration
- `MarketOrderMatching`: Market order execution across price levels
- `PriceTimePriority`: FIFO order matching at same price
- `PartialFillScenario`: Partial order fills
- `BidAskSpread`: Bid-ask spread calculation

**Total Tests**: 12+

### 3. Performance Regression Tests
**File**: `tests/test_performance_regression.cpp`

Validates performance targets to detect regressions:

#### Memory Pool Performance
- `MemoryPoolAllocationLatency`: Target <1μs
- `MemoryPoolThroughput`: Target >1M ops/sec

#### OrderBook Performance
- `OrderBookAddOrderLatency`: Avg <10μs, P99 <100μs
- `OrderBookCancelOrderLatency`: Target <5μs
- `OrderBookMatchingLatency`: Target <15μs
- `OrderBookDepthSnapshotLatency`: Target <20μs
- `OrderBookThroughput`: Target >100K ops/sec

#### Protocol Performance
- `ProtocolChecksumLatency`: Target <2μs
- `ProtocolValidateChecksumLatency`: Target <2μs

#### Queue Performance
- `SPSCQueueLatency`: Target <1μs

#### End-to-End Performance
- `EndToEndOrderProcessingLatency`: Avg <20μs, P99 <100μs, P999 <500μs

#### Memory Management
- `MemoryPoolFragmentation`: Stress test for memory leaks

**Total Tests**: 12+

## Performance Targets Summary

| Component | Metric | Target | Test |
|-----------|--------|--------|------|
| Memory Pool | Allocation Latency | <1μs | MemoryPoolAllocationLatency |
| Memory Pool | Throughput | >1M ops/s | MemoryPoolThroughput |
| OrderBook | Add Order Avg | <10μs | OrderBookAddOrderLatency |
| OrderBook | Add Order P99 | <100μs | OrderBookAddOrderLatency |
| OrderBook | Cancel Order | <5μs | OrderBookCancelOrderLatency |
| OrderBook | Matching | <15μs | OrderBookMatchingLatency |
| OrderBook | Depth Snapshot | <20μs | OrderBookDepthSnapshotLatency |
| OrderBook | Throughput | >100K ops/s | OrderBookThroughput |
| Protocol | Checksum | <2μs | ProtocolChecksumLatency |
| Queue | SPSC Push/Pop | <1μs | SPSCQueueLatency |
| End-to-End | Avg Latency | <20μs | EndToEndOrderProcessingLatency |
| End-to-End | P99 Latency | <100μs | EndToEndOrderProcessingLatency |
| End-to-End | P999 Latency | <500μs | EndToEndOrderProcessingLatency |

## Running Tests

### Run All Tests
```bash
cd build
ctest --output-on-failure
```

### Run Specific Test Suite
```bash
# Unit tests only
./rtes_tests --gtest_filter="SecurityUtilsTest.*"
./rtes_tests --gtest_filter="ProtocolUtilsTest.*"
./rtes_tests --gtest_filter="FieldValidatorsTest.*"

# Integration tests only
./rtes_tests --gtest_filter="IntegrationAPITest.*"

# Performance regression tests only
./rtes_tests --gtest_filter="PerformanceRegressionTest.*"
```

### Run with Verbose Output
```bash
./rtes_tests --gtest_filter="*" --gtest_print_time=1
```

### Run Specific Test
```bash
./rtes_tests --gtest_filter="PerformanceRegressionTest.OrderBookAddOrderLatency"
```

## Test Execution Time

- **Unit Tests**: ~1-2 seconds
- **Integration Tests**: ~2-3 seconds
- **Performance Regression Tests**: ~10-15 seconds (includes latency measurements)
- **Total Suite**: ~15-20 seconds

## Continuous Integration

Tests are automatically run in CI/CD pipeline:
- On every push to `main` and `develop` branches
- On every pull request
- With multiple compilers (GCC 11, Clang 13)
- In both Debug and Release modes

See `.github/workflows/ci.yml` for CI configuration.

## Test Coverage

### Components Covered
- ✅ Security utilities (input validation, sanitization)
- ✅ Protocol utilities (checksums, timestamps)
- ✅ Message validation
- ✅ Field validators
- ✅ OrderBook operations
- ✅ Memory pool management
- ✅ Queue operations
- ✅ End-to-end order processing

### Components Not Covered (Require Production Code)
- TCP Gateway (requires network setup)
- UDP Publisher (requires multicast setup)
- HTTP Server (requires HTTP client)
- Matching Engine (requires full system)
- Risk Manager (requires configuration)

## Adding New Tests

### Unit Test Template
```cpp
TEST(ComponentTest, FeatureName) {
    // Arrange
    auto component = create_component();
    
    // Act
    auto result = component.method();
    
    // Assert
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(result.value(), expected_value);
}
```

### Integration Test Template
```cpp
TEST_F(IntegrationAPITest, FeatureIntegration) {
    // Setup components
    OrderBook book("SYMBOL", *pool);
    
    // Execute workflow
    auto* order = pool->allocate();
    // ... configure order
    book.add_order(order);
    
    // Verify integration
    EXPECT_EQ(book.order_count(), 1);
}
```

### Performance Test Template
```cpp
TEST_F(PerformanceRegressionTest, FeaturePerformance) {
    constexpr double TARGET_LATENCY_US = 10.0;
    
    auto latency = measure_latency_us([&]() {
        // Operation to measure
    }, ITERATIONS);
    
    EXPECT_LT(latency, TARGET_LATENCY_US);
}
```

## Test Maintenance

### When to Update Tests
- When adding new utility functions
- When modifying public APIs
- When changing performance targets
- When fixing bugs (add regression test)

### Test Naming Convention
- Test suite: `ComponentTest` or `ComponentIntegrationTest`
- Test case: `FeatureDescription` (descriptive, no underscores)
- Use GIVEN-WHEN-THEN structure in comments if complex

### Performance Test Guidelines
- Use realistic data sizes
- Warm up before measurement
- Measure multiple iterations
- Report avg, P99, P999 latencies
- Set targets based on system requirements

## Debugging Failed Tests

### View Test Output
```bash
./rtes_tests --gtest_filter="FailingTest.*" --gtest_print_time=1
```

### Run with GDB
```bash
gdb --args ./rtes_tests --gtest_filter="FailingTest.*"
```

### Run with Valgrind
```bash
valgrind --leak-check=full ./rtes_tests --gtest_filter="FailingTest.*"
```

### Enable Debug Logging
```bash
RTES_LOG_LEVEL=DEBUG ./rtes_tests
```

## Test Metrics

### Current Status
- **Total Tests**: 54+
- **Pass Rate**: 100% (target)
- **Execution Time**: ~15-20 seconds
- **Code Coverage**: ~75% (utility functions and core components)

### Quality Gates
- All tests must pass before merge
- No performance regressions >10%
- No memory leaks detected by Valgrind
- No undefined behavior detected by sanitizers

## Future Test Additions

### Planned Tests
- [ ] Stress tests for concurrent operations
- [ ] Chaos testing for failure scenarios
- [ ] Fuzz testing for protocol parsing
- [ ] Load testing with realistic workloads
- [ ] Endurance testing for memory leaks

### Test Infrastructure Improvements
- [ ] Test fixtures for common setups
- [ ] Mock objects for external dependencies
- [ ] Performance baseline tracking
- [ ] Automated regression detection
- [ ] Test result visualization
