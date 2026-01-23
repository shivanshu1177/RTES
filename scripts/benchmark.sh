#!/bin/bash
set -e

# RTES Benchmark Script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Default values
DURATION=60
CLIENTS=10
SYMBOLS="AAPL,MSFT,GOOGL"
HOST="localhost"
PORT=8888
OUTPUT_FILE=""

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --duration)
            DURATION="$2"
            shift 2
            ;;
        --clients)
            CLIENTS="$2"
            shift 2
            ;;
        --symbols)
            SYMBOLS="$2"
            shift 2
            ;;
        --host)
            HOST="$2"
            shift 2
            ;;
        --port)
            PORT="$2"
            shift 2
            ;;
        --output)
            OUTPUT_FILE="$2"
            shift 2
            ;;
        --help)
            echo "Usage: $0 [options]"
            echo "Options:"
            echo "  --duration <sec>     Benchmark duration (default: 60)"
            echo "  --clients <num>      Number of clients (default: 10)"
            echo "  --symbols <list>     Comma-separated symbols (default: AAPL,MSFT,GOOGL)"
            echo "  --host <host>        Exchange host (default: localhost)"
            echo "  --port <port>        Exchange port (default: 8888)"
            echo "  --output <file>      Output file for results"
            echo "  --help               Show this help"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

cd "$PROJECT_DIR"

# Check if exchange is running
echo "Checking exchange connectivity..."
if ! curl -f http://$HOST:8080/health > /dev/null 2>&1; then
    echo "Exchange not reachable at $HOST:8080"
    echo "Please start the exchange first:"
    echo "  ./build/trading_exchange configs/config.json"
    exit 1
fi

echo "=== RTES Performance Benchmark ==="
echo "Duration: ${DURATION}s"
echo "Clients: $CLIENTS"
echo "Symbols: $SYMBOLS"
echo "Target: $HOST:$PORT"
echo

# Prepare output
if [ -n "$OUTPUT_FILE" ]; then
    exec > >(tee "$OUTPUT_FILE")
fi

# Build if needed
if [ ! -f build/load_generator ]; then
    echo "Building benchmark tools..."
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j$(nproc)
fi

# Get baseline metrics
echo "=== Baseline Metrics ==="
curl -s http://$HOST:8080/metrics | grep -E "(rtes_orders_total|rtes_trades_total)" || true
echo

# Run component benchmarks
echo "=== Component Benchmarks ==="
echo "Memory Pool:"
./build/bench_memory_pool | tail -n 5

echo
echo "Matching Engine:"
./build/bench_matching | tail -n 5

echo
echo "=== Load Test ==="
START_TIME=$(date +%s)

# Run load generator
./build/load_generator \
    --host "$HOST" \
    --port "$PORT" \
    --clients "$CLIENTS" \
    --duration "$DURATION" \
    --symbols "$SYMBOLS"

END_TIME=$(date +%s)
ACTUAL_DURATION=$((END_TIME - START_TIME))

echo
echo "=== Final Metrics ==="
curl -s http://$HOST:8080/metrics | grep -E "(rtes_orders_total|rtes_trades_total|rtes_latency)" || true

echo
echo "=== System Resources ==="
echo "CPU Usage:"
top -bn1 | grep "Cpu(s)" || true

echo "Memory Usage:"
free -h || true

echo "Network Connections:"
ss -tuln | grep -E "(8888|8080|9999)" || true

echo
echo "=== Benchmark Summary ==="
echo "Actual Duration: ${ACTUAL_DURATION}s"
echo "Target Throughput: ≥100,000 orders/sec"
echo "Target Latency: avg ≤10μs, p99 ≤100μs"
echo
echo "Check metrics endpoint for detailed latency histograms:"
echo "  curl http://$HOST:8080/metrics | grep latency"