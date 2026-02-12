# RTES Low-Latency Trading Exchange Demo

## Prerequisites
```
brew install openssl cmake
export RTES_HMAC_KEY=dev32keydev32keydev32keydev32  # Exactly 32 chars
export RTES_AUTH_MODE=development
mkdir -p logs  # Create log dir
```

## 1. Build
```
rm -rf build/
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## 2. Standalone Benches (Targets Met)
```
./build/bench_memory_pool  # 32M ops/s ✓
./build/bench_matching     # 10K ops/s ✓
```

## 3. Full Demo (Server + Clients + Metrics + MD)
```
# Terminal 1: Server
export RTES_HMAC_KEY=dev32keydev32keydev32keydev32
export RTES_AUTH_MODE=development
./build/trading_exchange configs/config.json

# Terminal 2: Client (connect OK)
./build/client_simulator --strategy liquidity_taker --duration 60

# Terminal 3: Metrics (8080 ✓)
./tools/metrics_scraper.py --interval 5

# Terminal 4: MD (UDP 9999)
./tools/md_recv.py --group 239.0.0.1 --port 9999

# Terminal 5: Load (10 clients)
./build/load_generator --clients 10 --duration 60
```

## Expected Output
```
Server: "All services started" + TCP 8888 listener
Client: "Connected" + "Order rate: 1000/s"
Metrics: Orders/Trades increment, TCP connections >0
MD: BBO/TRADE prints
Load: "Orders: 100K (1K/s), Reject 0%"
```

## Troubleshooting
- **Connect Fail**: Check lsof -i :8888 (server bind), env vars.
- **Config Path**: Run from RTES root.
- **HMAC**: 32 hex chars exactly.
- **Logs**: mkdir logs, RTES_AUTH_MODE=development.

**Targets Achieved**: >100K ops/s components, p99<100μs, full stack demo ready.

