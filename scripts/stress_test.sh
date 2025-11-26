#!/bin/bash
set -e

# RTES Stress Test Script
# Tests system stability under extreme load without modifying source code

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build}"
RESULTS_DIR="${RESULTS_DIR:-$PROJECT_ROOT/benchmark_results}"

# Configuration
DURATION=${DURATION:-300}
MAX_CLIENTS=${MAX_CLIENTS:-500}
RATE_PER_CLIENT=${RATE_PER_CLIENT:-2000}
RAMP_UP_TIME=${RAMP_UP_TIME:-60}

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
NC='\033[0m'

echo "=========================================="
echo "RTES Stress Test"
echo "=========================================="
echo "Duration: ${DURATION}s"
echo "Max Clients: ${MAX_CLIENTS}"
echo "Rate per client: ${RATE_PER_CLIENT} orders/sec"
echo "Ramp-up time: ${RAMP_UP_TIME}s"
echo ""
echo -e "${YELLOW}WARNING: This test will push the system to its limits${NC}"
echo ""

# Check binaries
if [ ! -f "$BUILD_DIR/trading_exchange" ] || [ ! -f "$BUILD_DIR/load_generator" ]; then
    echo -e "${RED}Error: Required binaries not found${NC}"
    exit 1
fi

# Create results directory
mkdir -p "$RESULTS_DIR"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RESULT_FILE="$RESULTS_DIR/stress_test_${TIMESTAMP}.txt"
METRICS_FILE="$RESULTS_DIR/stress_metrics_${TIMESTAMP}.csv"

# Set environment
export RTES_HMAC_KEY=${RTES_HMAC_KEY:-$(openssl rand -hex 32 2>/dev/null || echo "dev_key")}

{
    echo "RTES Stress Test Results"
    echo "Date: $(date)"
    echo "Duration: ${DURATION}s"
    echo "Max Clients: ${MAX_CLIENTS}"
    echo "Target Rate: $((MAX_CLIENTS * RATE_PER_CLIENT)) orders/sec"
    echo ""
} | tee "$RESULT_FILE"

# Start exchange
echo "Starting exchange..."
cd "$BUILD_DIR"
./trading_exchange ../configs/config.json > stress_exchange.log 2>&1 &
EXCHANGE_PID=$!
sleep 5

if ! kill -0 $EXCHANGE_PID 2>/dev/null; then
    echo -e "${RED}Error: Exchange failed to start${NC}"
    cat stress_exchange.log
    exit 1
fi

echo -e "${GREEN}Exchange started (PID: $EXCHANGE_PID)${NC}"

# Cleanup function
cleanup() {
    echo ""
    echo "Cleaning up..."
    if [ ! -z "$EXCHANGE_PID" ]; then
        kill $EXCHANGE_PID 2>/dev/null || true
        wait $EXCHANGE_PID 2>/dev/null || true
    fi
    
    # Kill any remaining load generators
    pkill -f load_generator 2>/dev/null || true
}
trap cleanup EXIT

# Function to collect metrics
collect_metrics() {
    local timestamp=$1
    local metrics=$(curl -s http://localhost:8080/metrics 2>/dev/null || echo "")
    
    local received=$(echo "$metrics" | grep 'rtes_orders_received_total' | awk '{print $2}')
    local accepted=$(echo "$metrics" | grep 'rtes_orders_accepted_total' | awk '{print $2}')
    local rejected=$(echo "$metrics" | grep 'rtes_orders_rejected_total' | awk '{print $2}')
    local trades=$(echo "$metrics" | grep 'rtes_trades_executed_total' | awk '{print $2}')
    local connections=$(echo "$metrics" | grep 'rtes_connections_active' | awk '{print $2}')
    local avg_latency=$(echo "$metrics" | grep 'rtes_order_latency_seconds{quantile="0.5"}' | awk '{print $2 * 1000000}')
    local p99_latency=$(echo "$metrics" | grep 'rtes_order_latency_seconds{quantile="0.99"}' | awk '{print $2 * 1000000}')
    
    echo "$timestamp,${received:-0},${accepted:-0},${rejected:-0},${trades:-0},${connections:-0},${avg_latency:-0},${p99_latency:-0}" >> "$METRICS_FILE"
}

# Initialize metrics file
echo "Timestamp,Received,Accepted,Rejected,Trades,Connections,AvgLatency,P99Latency" > "$METRICS_FILE"

# Start monitoring in background
{
    while kill -0 $EXCHANGE_PID 2>/dev/null; do
        collect_metrics $(date +%s)
        sleep 5
    done
} &
MONITOR_PID=$!

# Phase 1: Ramp up
echo ""
echo -e "${BLUE}Phase 1: Ramp-up (${RAMP_UP_TIME}s)${NC}"
echo "Gradually increasing load to $MAX_CLIENTS clients..."

./load_generator \
    --clients $MAX_CLIENTS \
    --rate $RATE_PER_CLIENT \
    --duration $RAMP_UP_TIME \
    --ramp-up \
    > ramp_up.log 2>&1 &
RAMP_PID=$!

sleep $RAMP_UP_TIME
wait $RAMP_PID 2>/dev/null || true

echo -e "${GREEN}Ramp-up complete${NC}"

# Phase 2: Sustained load
echo ""
echo -e "${BLUE}Phase 2: Sustained Load (${DURATION}s)${NC}"
echo "Running at maximum capacity..."

./load_generator \
    --clients $MAX_CLIENTS \
    --rate $RATE_PER_CLIENT \
    --duration $DURATION \
    > sustained.log 2>&1 &
LOAD_PID=$!

# Monitor progress
ELAPSED=0
while [ $ELAPSED -lt $DURATION ]; do
    sleep 10
    ELAPSED=$((ELAPSED + 10))
    PROGRESS=$((ELAPSED * 100 / DURATION))
    echo -ne "\rProgress: ${PROGRESS}% (${ELAPSED}/${DURATION}s)"
done
echo ""

wait $LOAD_PID 2>/dev/null || true

echo -e "${GREEN}Sustained load test complete${NC}"

# Phase 3: Spike test
echo ""
echo -e "${BLUE}Phase 3: Spike Test (30s)${NC}"
echo "Sudden burst of traffic..."

./load_generator \
    --clients $((MAX_CLIENTS * 2)) \
    --rate $((RATE_PER_CLIENT * 2)) \
    --duration 30 \
    > spike.log 2>&1 &
SPIKE_PID=$!

sleep 30
wait $SPIKE_PID 2>/dev/null || true

echo -e "${GREEN}Spike test complete${NC}"

# Stop monitoring
kill $MONITOR_PID 2>/dev/null || true

# Collect final metrics
echo ""
echo "Collecting final metrics..."
sleep 2

FINAL_METRICS=$(curl -s http://localhost:8080/metrics 2>/dev/null || echo "")

# Parse results
TOTAL_RECEIVED=$(echo "$FINAL_METRICS" | grep 'rtes_orders_received_total' | awk '{print $2}')
TOTAL_ACCEPTED=$(echo "$FINAL_METRICS" | grep 'rtes_orders_accepted_total' | awk '{print $2}')
TOTAL_REJECTED=$(echo "$FINAL_METRICS" | grep 'rtes_orders_rejected_total' | awk '{print $2}')
TOTAL_TRADES=$(echo "$FINAL_METRICS" | grep 'rtes_trades_executed_total' | awk '{print $2}')

AVG_LATENCY=$(echo "$FINAL_METRICS" | grep 'rtes_order_latency_seconds{quantile="0.5"}' | awk '{print $2 * 1000000}')
P99_LATENCY=$(echo "$FINAL_METRICS" | grep 'rtes_order_latency_seconds{quantile="0.99"}' | awk '{print $2 * 1000000}')
P999_LATENCY=$(echo "$FINAL_METRICS" | grep 'rtes_order_latency_seconds{quantile="0.999"}' | awk '{print $2 * 1000000}')

# Calculate rates
TOTAL_TIME=$((RAMP_UP_TIME + DURATION + 30))
AVG_THROUGHPUT=$((TOTAL_RECEIVED / TOTAL_TIME))
REJECTION_RATE=0
if [ ! -z "$TOTAL_RECEIVED" ] && [ "$TOTAL_RECEIVED" -gt 0 ]; then
    REJECTION_RATE=$(echo "scale=2; ($TOTAL_REJECTED / $TOTAL_RECEIVED) * 100" | bc -l 2>/dev/null || echo "0")
fi

# Display results
{
    echo ""
    echo "=========================================="
    echo "Stress Test Results"
    echo "=========================================="
    echo ""
    echo "Total Orders Received: ${TOTAL_RECEIVED:-0}"
    echo "Total Orders Accepted: ${TOTAL_ACCEPTED:-0}"
    echo "Total Orders Rejected: ${TOTAL_REJECTED:-0}"
    echo "Total Trades Executed: ${TOTAL_TRADES:-0}"
    echo ""
    echo "Average Throughput: ${AVG_THROUGHPUT:-0} orders/sec"
    echo "Rejection Rate: ${REJECTION_RATE}%"
    echo ""
    echo "Latency:"
    echo "  Average (P50): ${AVG_LATENCY:-N/A} μs"
    echo "  P99: ${P99_LATENCY:-N/A} μs"
    echo "  P999: ${P999_LATENCY:-N/A} μs"
    echo ""
    echo "=========================================="
    echo "System Stability Assessment"
    echo "=========================================="
    echo ""
} | tee -a "$RESULT_FILE"

# Assess stability
STABLE=true

if [ ! -z "$REJECTION_RATE" ] && (( $(echo "$REJECTION_RATE > 5" | bc -l) )); then
    echo -e "${RED}✗ High rejection rate (${REJECTION_RATE}%)${NC}" | tee -a "$RESULT_FILE"
    STABLE=false
else
    echo -e "${GREEN}✓ Acceptable rejection rate (${REJECTION_RATE}%)${NC}" | tee -a "$RESULT_FILE"
fi

if [ ! -z "$P99_LATENCY" ] && (( $(echo "$P99_LATENCY > 200" | bc -l) )); then
    echo -e "${YELLOW}⚠ Elevated P99 latency (${P99_LATENCY} μs)${NC}" | tee -a "$RESULT_FILE"
else
    echo -e "${GREEN}✓ P99 latency within acceptable range${NC}" | tee -a "$RESULT_FILE"
fi

# Check for crashes
if ! kill -0 $EXCHANGE_PID 2>/dev/null; then
    echo -e "${RED}✗ Exchange crashed during test${NC}" | tee -a "$RESULT_FILE"
    STABLE=false
else
    echo -e "${GREEN}✓ Exchange remained stable${NC}" | tee -a "$RESULT_FILE"
fi

# Check logs for errors
ERROR_COUNT=$(grep -i "error\|fatal\|crash" stress_exchange.log 2>/dev/null | wc -l || echo "0")
if [ "$ERROR_COUNT" -gt 10 ]; then
    echo -e "${YELLOW}⚠ ${ERROR_COUNT} errors found in logs${NC}" | tee -a "$RESULT_FILE"
else
    echo -e "${GREEN}✓ Minimal errors in logs (${ERROR_COUNT})${NC}" | tee -a "$RESULT_FILE"
fi

echo "" | tee -a "$RESULT_FILE"

if [ "$STABLE" = true ]; then
    echo -e "${GREEN}Overall: STABLE - System handled stress test successfully${NC}" | tee -a "$RESULT_FILE"
else
    echo -e "${RED}Overall: UNSTABLE - System showed signs of instability${NC}" | tee -a "$RESULT_FILE"
fi

echo ""
echo "Results saved to: $RESULT_FILE"
echo "Metrics timeline: $METRICS_FILE"
echo "Exchange log: $BUILD_DIR/stress_exchange.log"
