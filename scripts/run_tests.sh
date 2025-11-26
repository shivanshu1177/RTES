#!/bin/bash
# Test runner script for RTES test suite

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_ROOT}/build"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_header() {
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}"
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

usage() {
    cat << EOF
Usage: $0 [OPTIONS]

Run RTES test suite with various options.

OPTIONS:
    -a, --all               Run all tests (default)
    -u, --unit              Run unit tests only
    -i, --integration       Run integration tests only
    -p, --performance       Run performance regression tests only
    -f, --filter PATTERN    Run tests matching PATTERN
    -v, --verbose           Verbose output
    -m, --memcheck          Run with valgrind memory check
    -c, --coverage          Generate coverage report
    -h, --help              Show this help message

EXAMPLES:
    $0 --all                        # Run all tests
    $0 --unit                       # Run unit tests only
    $0 --filter "OrderBook*"        # Run OrderBook tests
    $0 --performance --verbose      # Run performance tests with verbose output
    $0 --memcheck --filter "Memory*" # Run memory tests with valgrind

EOF
}

# Default options
TEST_FILTER="*"
VERBOSE=""
MEMCHECK=false
COVERAGE=false
TEST_TYPE="all"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -a|--all)
            TEST_TYPE="all"
            shift
            ;;
        -u|--unit)
            TEST_TYPE="unit"
            TEST_FILTER="SecurityUtilsTest.*:ProtocolUtilsTest.*:MessageValidatorTest.*:FieldValidatorsTest.*:InputSanitizerTest.*:ValidationChainTest.*"
            shift
            ;;
        -i|--integration)
            TEST_TYPE="integration"
            TEST_FILTER="IntegrationAPITest.*"
            shift
            ;;
        -p|--performance)
            TEST_TYPE="performance"
            TEST_FILTER="PerformanceRegressionTest.*"
            shift
            ;;
        -f|--filter)
            TEST_FILTER="$2"
            shift 2
            ;;
        -v|--verbose)
            VERBOSE="--gtest_print_time=1"
            shift
            ;;
        -m|--memcheck)
            MEMCHECK=true
            shift
            ;;
        -c|--coverage)
            COVERAGE=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    print_error "Build directory not found: $BUILD_DIR"
    echo "Please build the project first:"
    echo "  mkdir -p build && cd build"
    echo "  cmake .. -DCMAKE_BUILD_TYPE=Release"
    echo "  make -j\$(nproc)"
    exit 1
fi

# Check if test binary exists
if [ ! -f "$BUILD_DIR/rtes_tests" ]; then
    print_error "Test binary not found: $BUILD_DIR/rtes_tests"
    echo "Please build the tests first:"
    echo "  cd build && make -j\$(nproc)"
    exit 1
fi

cd "$BUILD_DIR"

print_header "RTES Test Suite Runner"
echo "Test Type: $TEST_TYPE"
echo "Filter: $TEST_FILTER"
echo "Verbose: $([ -n "$VERBOSE" ] && echo "Yes" || echo "No")"
echo "Memcheck: $([ "$MEMCHECK" = true ] && echo "Yes" || echo "No")"
echo ""

# Run tests
if [ "$MEMCHECK" = true ]; then
    print_header "Running Tests with Valgrind"
    
    if ! command -v valgrind &> /dev/null; then
        print_error "Valgrind not found. Please install it:"
        echo "  sudo apt-get install valgrind"
        exit 1
    fi
    
    valgrind \
        --tool=memcheck \
        --leak-check=full \
        --show-leak-kinds=all \
        --track-origins=yes \
        --error-exitcode=1 \
        ./rtes_tests --gtest_filter="$TEST_FILTER" $VERBOSE
    
    if [ $? -eq 0 ]; then
        print_success "All tests passed with no memory leaks"
    else
        print_error "Tests failed or memory leaks detected"
        exit 1
    fi
else
    print_header "Running Tests"
    
    ./rtes_tests --gtest_filter="$TEST_FILTER" $VERBOSE --gtest_color=yes
    
    TEST_RESULT=$?
    
    if [ $TEST_RESULT -eq 0 ]; then
        print_success "All tests passed"
    else
        print_error "Some tests failed"
        exit 1
    fi
fi

# Generate coverage report if requested
if [ "$COVERAGE" = true ]; then
    print_header "Generating Coverage Report"
    
    if ! command -v gcov &> /dev/null; then
        print_warning "gcov not found. Skipping coverage report."
    else
        gcov ../src/*.cpp
        print_success "Coverage report generated"
    fi
fi

# Print summary
echo ""
print_header "Test Summary"

# Count tests
TOTAL_TESTS=$(./rtes_tests --gtest_list_tests --gtest_filter="$TEST_FILTER" | grep -c "^  ")
echo "Total tests run: $TOTAL_TESTS"

# Performance summary for performance tests
if [ "$TEST_TYPE" = "performance" ] || [ "$TEST_TYPE" = "all" ]; then
    echo ""
    print_header "Performance Targets"
    cat << EOF
Memory Pool Allocation:     <1μs
Memory Pool Throughput:     >1M ops/s
OrderBook Add Order (avg):  <10μs
OrderBook Add Order (P99):  <100μs
OrderBook Cancel:           <5μs
OrderBook Matching:         <15μs
OrderBook Depth:            <20μs
OrderBook Throughput:       >100K ops/s
Protocol Checksum:          <2μs
SPSC Queue:                 <1μs
End-to-End (avg):           <20μs
End-to-End (P99):           <100μs
End-to-End (P999):          <500μs
EOF
fi

echo ""
print_success "Test run completed successfully"
