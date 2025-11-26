#!/bin/bash
set -e

# RTES Latency Benchmark Script
# Tests order processing latency without modifying source code

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build}"
RESULTS_DIR="${RESULTS_DIR:-$PROJECT_ROOT/benchmark_results}"

# Configuration
DURATION=${DURATION:-60}
CLIENTS=${CLIENTS:-10}
RATE=${RATE:-100}
SYMBOLS="AAPL,MSFT,GOOGL"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "=========================================="
echo "RTES Latency Benchmark"
echo "=========================================="
echo "Duration: ${DURATION}s"
echo "Clients: ${CLIENTS}"
echo "Rate: ${RATE} orders/sec per client"
echo "Symbols: ${SYMBOLS}"
echo ""

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${RED}Error: Build directory not found: $BUILD_DIR${NC}"
    echo "Run: cmake -B build && cmake --build build"
    exit 1
fi

# Check if binaries exist
if [ ! -f "$BUILD_DIR/trading_exchange" ]; then
    echo -e "${RED}Error: trading_exchange binary not found${NC}"
    exit 1
fi

if [ ! -f "$BUILD_DIR/client_simulator" ]; then
    echo -e "${RED}Error: client_simulator binary not found${NC}"
    exit 1
fi

# Create results directory
mkdir -p "$RESULTS_DIR"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RESULT_FILE="$RESULTS_DIR/latency_${TIMESTAMP}.txt"

# Set environment variables
export RTES_HMAC_KEY=${RTES_HMAC_KEY:-$(openssl rand -hex 32 2>/dev/null || echo "dev_key_for_testing_only_not_secure")}

# Start exchange
echo "Starting exchange..."
cd "$BUILD_DIR"
./trading_exchange ../configs/config.json > exchange.log 2>&1 &
EXCHANGE_PID=$!

# Wait for startup
sleep 5

# Check if exchange is running
if ! kill -0 $EXCHANGE_PID 2>/dev/null; then
    echo -e "${RED}Error: Exchange failed to start${NC}"
    cat exchange.log
    exit 1
fi

echo -e "${GREEN}Exchange started (PID: $EXCHANGE_PID)${NC}"

# Function to cleanup
cleanup() {
    echo ""
    echo "Cleaning up..."
    if [ ! -z "$EXCHANGE_PID" ]; then
        kill $EXCHANGE_PID 2>/dev/null || true
        wait $EXCHANGE_PID 2>/dev/null || true
    fi
}
trap cleanup EXIT

# Run latency benchmark
echo ""
echo "Running latency benchmark..."
echo "=========================================="

{
    echo "RTES Latency Benchmark Results"
    echo "Date: $(date)"
    echo "Duration: ${DURATION}s"
    echo "Clients: ${CLIENTS}"
    echo "Rate: ${RATE} orders/sec per client"
    echo ""
    echo "=========================================="
    echo ""
} | tee "$RESULT_FILE"

# Run multiple clients
for i in $(seq 1 $CLIENTS); do
    ./client_simulator \
        --strategy latency_test \
        --symbol AAPL \
        --rate $RATE \
        --duration $DURATION \
        >> client_${i}.log 2>&1 &
    CLIENT_PIDS[$i]=$!
done

# Wait for clients to finish
echo "Waiting for clients to complete..."
for pid in "${CLIENT_PIDS[@]}"; do
    wait $pid 2>/dev/null || true
done

# Collect metrics from exchange
echo ""
echo "Collecting metrics..."
METRICS=$(curl -s http://localhost:8080/metrics 2>/dev/null || echo "")

if [ -z "$METRICS" ]; then
    echo -e "${YELLOW}Warning: Could not fetch metrics${NC}"
else
    {
        echo "Latency Metrics:"
        echo "----------------"
        echo "$METRICS" | grep -E "rtes_order_latency|rtes_orders_" || echo "No latency metrics found"
        echo ""
        echo "Throughput Metrics:"
        echo "-------------------"
        echo "$METRICS" | grep -E "rtes_orders_received_total|rtes_trades_executed_total" || echo "No throughput metrics found"
    } | tee -a "$RESULT_FILE"
fi

# Parse and display results
echo ""
echo "=========================================="
echo "Summary"
echo "=========================================="

AVG_LATENCY=$(echo "$METRICS" | grep 'rtes_order_latency_seconds{quantile="0.5"}' | awk '{print $2 * 1000000}')
P99_LATENCY=$(echo "$METRICS" | grep 'rtes_order_latency_seconds{quantile="0.99"}' | awk '{print $2 * 1000000}')
P999_LATENCY=$(echo "$METRICS" | grep 'rtes_order_latency_seconds{quantile="0.999"}' | awk '{print $2 * 1000000}')

if [ ! -z "$AVG_LATENCY" ]; then
    echo "Average Latency (P50): ${AVG_LATENCY} μs"
    if (( $(echo "$AVG_LATENCY < 10" | bc -l) )); then
        echo -e "${GREEN}✓ Target met (< 10μs)${NC}"
    else
        echo -e "${RED}✗ Target missed (< 10μs)${NC}"
    fi
fi

if [ ! -z "$P99_LATENCY" ]; then
    echo "P99 Latency: ${P99_LATENCY} μs"
    if (( $(echo "$P99_LATENCY < 100" | bc -l) )); then
        echo -e "${GREEN}✓ Target met (< 100μs)${NC}"
    else
        echo -e "${RED}✗ Target missed (< 100μs)${NC}"
    fi
fi

if [ ! -z "$P999_LATENCY" ]; then
    echo "P999 Latency: ${P999_LATENCY} μs"
    if (( $(echo "$P999_LATENCY < 500" | bc -l) )); then
        echo -e "${GREEN}✓ Target met (< 500μs)${NC}"
    else
        echo -e "${RED}✗ Target missed (< 500μs)${NC}"
    fi
fi

echo ""
echo "Results saved to: $RESULT_FILE"
echo "Exchange log: $BUILD_DIR/exchange.log"
