# RTES Analytics Suite

Python-based analytics for RTES performance analysis and order book visualization.

## Installation

```bash
pip install -r requirements.txt
```

## Modules

### 1. Performance Analyzer
Analyze latency and throughput metrics from Prometheus format.

```bash
# Scrape metrics from running exchange
curl -s localhost:8080/metrics > metrics.txt

# Analyze performance
python performance_analyzer.py metrics.txt
```

**Features:**
- Latency percentiles (p50, p95, p99, p99.9)
- Throughput calculation
- Latency distribution histogram

### 2. Order Book Visualizer
Visualize order book depth and spread dynamics.

```bash
# From CSV file
python orderbook_visualizer.py orderbook.csv

# Or use programmatically
from orderbook_visualizer import OrderBookVisualizer
viz = OrderBookVisualizer()
viz.add_order('BUY', 100.0, 500)
viz.add_order('SELL', 100.1, 300)
viz.plot_depth_chart()
```

**Features:**
- Depth chart (bid/ask levels)
- Spread analysis
- Order book imbalance
- Summary statistics

### 3. Trade Analyzer
Analyze trade execution data.

```bash
# From CSV file
python trade_analyzer.py trades.csv

# Or use programmatically
from trade_analyzer import TradeAnalyzer
analyzer = TradeAnalyzer()
analyzer.add_trade('AAPL', 100.5, 200, 'BUY')
analyzer.plot_price_volume()
```

**Features:**
- VWAP calculation
- Volume profile by price
- Price/volume charts
- Trade statistics

## Example Workflow

```python
# 1. Analyze performance
from performance_analyzer import PerformanceAnalyzer
perf = PerformanceAnalyzer('metrics.txt')
print(perf.summary_report())
perf.plot_latency_distribution()

# 2. Visualize order book
from orderbook_visualizer import OrderBookVisualizer
book = OrderBookVisualizer('orderbook.csv')
print(book.summary_stats())
book.plot_depth_chart()

# 3. Analyze trades
from trade_analyzer import TradeAnalyzer
trades = TradeAnalyzer('trades.csv')
print(trades.summary_stats('AAPL'))
trades.plot_price_volume('AAPL')
```

## Data Formats

### Metrics (Prometheus format)
```
rtes_orders_total 150000
rtes_latency_bucket{le="0.00001"} 120000
rtes_latency_bucket{le="0.0001"} 148000
rtes_latency_count 150000
rtes_latency_sum 1.5
```

### Order Book CSV
```
side,price,quantity
BUY,99.90,500
BUY,99.80,300
SELL,100.10,400
SELL,100.20,600
```

### Trades CSV
```
timestamp,symbol,price,quantity,side
2024-01-01 10:00:00,AAPL,100.5,200,BUY
2024-01-01 10:00:01,AAPL,100.6,150,SELL
```
