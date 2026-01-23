#!/bin/bash
set -e

# RTES Throughput Benchmark Script
# Tests maximum sustainable throughput without modifying source code

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build}"
RESULTS_DIR="${RESULTS_DIR:-$PROJECT_ROOT/benchmark_results}"

# Configuration
DURATION=${DURATION:-60}
START_CLIENTS=${START_CLIENTS:-10}
MAX_CLIENTS=${MAX_CLIENTS:-200}
STEP=${STEP:-20}
RATE_PER_CLIENT=${RATE_PER_CLIENT:-1000}

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo "=========================================="
echo "RTES Throughput Benchmark"
echo "=========================================="
echo "Duration: ${DURATION}s per test"
echo "Clients: ${START_CLIENTS} to ${MAX_CLIENTS} (step: ${STEP})"
echo "Rate per client: ${RATE_PER_CLIENT} orders/sec"
echo ""

# Check binaries
if [ ! -f "$BUILD_DIR/trading_exchange" ] || [ ! -f "$BUILD_DIR/load_generator" ]; then
    echo -e "${RED}Error: Required binaries not found${NC}"
    exit 1
fi

# Create results directory
mkdir -p "$RESULTS_DIR"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RESULT_FILE="$RESULTS_DIR/throughput_${TIMESTAMP}.txt"

# Set environment
export RTES_HMAC_KEY=${RTES_HMAC_KEY:-$(openssl rand -hex 32 2>/dev/null || echo "dev_key")}

{
    echo "RTES Throughput Benchmark Results"
    echo "Date: $(date)"
    echo "Target: ≥100,000 orders/sec"
    echo ""
    echo "Clients | Target Rate | Achieved | Rejection % | Avg Latency | P99 Latency | Status"
    echo "--------|-------------|----------|-------------|-------------|-------------|--------"
} | tee "$RESULT_FILE"

# Function to run single test
run_test() {
    local clients=$1
    local target_rate=$((clients * RATE_PER_CLIENT))
    
    echo -e "${BLUE}Testing with $clients clients (target: $target_rate ops/s)...${NC}"
    
    # Start exchange
    cd "$BUILD_DIR"
    ./trading_exchange ../configs/config.json > exchange_${clients}.log 2>&1 &
    local exchange_pid=$!
    sleep 5
    
    if ! kill -0 $exchange_pid 2>/dev/null; then
        echo -e "${RED}Exchange failed to start${NC}"
        return 1
    fi
    
    # Run load generator
    timeout $((DURATION + 10)) ./load_generator \
        --clients $clients \
        --rate $RATE_PER_CLIENT \
        --duration $DURATION \
        > load_${clients}.log 2>&1 || true
    
    # Collect metrics
    local metrics=$(curl -s http://localhost:8080/metrics 2>/dev/null || echo "")
    
    # Parse results
    local received=$(echo "$metrics" | grep 'rtes_orders_received_total' | awk '{print $2}')
    local rejected=$(echo "$metrics" | grep 'rtes_orders_rejected_total' | awk '{print $2}')
    local avg_latency=$(echo "$metrics" | grep 'rtes_order_latency_seconds{quantile="0.5"}' | awk '{print $2 * 1000000}')
    local p99_latency=$(echo "$metrics" | grep 'rtes_order_latency_seconds{quantile="0.99"}' | awk '{print $2 * 1000000}')
    
    local achieved=$((received / DURATION))
    local rejection_pct=0
    if [ ! -z "$received" ] && [ "$received" -gt 0 ]; then
        rejection_pct=$(echo "scale=2; ($rejected / $received) * 100" | bc -l 2>/dev/null || echo "0")
    fi
    
    # Determine status
    local status="✓"
    if [ ! -z "$achieved" ] && [ "$achieved" -lt 100000 ]; then
        status="○"
    fi
    if [ ! -z "$rejection_pct" ] && (( $(echo "$rejection_pct > 1" | bc -l) )); then
        status="✗"
    fi
    
    # Output results
    printf "%7d | %11d | %8d | %11s | %11s | %11s | %s\n" \
        "$clients" "$target_rate" "${achieved:-0}" "${rejection_pct:-0}%" \
        "${avg_latency:-N/A}" "${p99_latency:-N/A}" "$status" | tee -a "$RESULT_FILE"
    
    # Cleanup
    kill $exchange_pid 2>/dev/null || true
    wait $exchange_pid 2>/dev/null || true
    sleep 2
}

# Run tests with increasing load
for clients in $(seq $START_CLIENTS $STEP $MAX_CLIENTS); do
    run_test $clients
done

# Summary
echo ""
echo "=========================================="
echo "Summary"
echo "=========================================="

MAX_THROUGHPUT=$(grep -E "^[[:space:]]*[0-9]+" "$RESULT_FILE" | awk '{print $5}' | sort -n | tail -1)
echo "Maximum sustained throughput: ${MAX_THROUGHPUT:-N/A} orders/sec"

if [ ! -z "$MAX_THROUGHPUT" ] && [ "$MAX_THROUGHPUT" -ge 100000 ]; then
    echo -e "${GREEN}✓ Target achieved (≥100,000 orders/sec)${NC}"
else
    echo -e "${YELLOW}○ Target not achieved${NC}"
fi

echo ""
echo "Results saved to: $RESULT_FILE"
echo ""
echo "Legend:"
echo "  ✓ = Good performance, low rejection rate"
echo "  ○ = Below target throughput"
echo "  ✗ = High rejection rate (>1%)"
