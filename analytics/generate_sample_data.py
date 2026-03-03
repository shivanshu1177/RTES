#!/usr/bin/env python3
"""Generate sample data for testing analytics suite."""

import pandas as pd
import numpy as np
from datetime import datetime, timedelta


def generate_metrics_file(output_file: str = 'sample_metrics.txt'):
    """Generate sample Prometheus metrics."""
    with open(output_file, 'w') as f:
        f.write("# HELP rtes_metrics RTES Exchange Metrics\n")
        f.write("# TYPE rtes_orders_total counter\n")
        f.write("# TYPE rtes_latency_seconds histogram\n\n")
        
        f.write("rtes_orders_total 150000\n")
        f.write("rtes_trades_total 75000\n")
        f.write("rtes_cancels_total 5000\n\n")
        
        # Latency histogram buckets
        buckets = [
            (0.000001, 10000),
            (0.000005, 50000),
            (0.00001, 120000),
            (0.00005, 145000),
            (0.0001, 148000),
            (0.0005, 149500),
            (0.001, 149900),
            (0.005, 149990),
            (0.01, 150000),
        ]
        
        for le, count in buckets:
            f.write(f'rtes_latency_bucket{{le="{le}"}} {count}\n')
        
        f.write('rtes_latency_bucket{le="+Inf"} 150000\n')
        f.write('rtes_latency_count 150000\n')
        f.write('rtes_latency_sum 1.5\n')
    
    print(f"Generated {output_file}")


def generate_orderbook_csv(output_file: str = 'sample_orderbook.csv'):
    """Generate sample order book snapshot."""
    data = []
    
    # Bids
    for i in range(10):
        data.append({
            'side': 'BUY',
            'price': 100.0 - i * 0.1,
            'quantity': np.random.randint(100, 1000)
        })
    
    # Asks
    for i in range(10):
        data.append({
            'side': 'SELL',
            'price': 100.1 + i * 0.1,
            'quantity': np.random.randint(100, 1000)
        })
    
    df = pd.DataFrame(data)
    df.to_csv(output_file, index=False)
    print(f"Generated {output_file}")


def generate_trades_csv(output_file: str = 'sample_trades.csv'):
    """Generate sample trade data."""
    np.random.seed(42)
    data = []
    
    start_time = datetime.now() - timedelta(hours=1)
    price = 100.0
    
    for i in range(200):
        price += np.random.randn() * 0.2
        data.append({
            'timestamp': start_time + timedelta(seconds=i*10),
            'symbol': 'AAPL',
            'price': round(price, 2),
            'quantity': np.random.randint(50, 500),
            'side': np.random.choice(['BUY', 'SELL'])
        })
    
    df = pd.DataFrame(data)
    df.to_csv(output_file, index=False)
    print(f"Generated {output_file}")


if __name__ == '__main__':
    generate_metrics_file()
    generate_orderbook_csv()
    generate_trades_csv()
    print("\nSample data generated successfully!")
    print("\nTest the analytics suite:")
    print("  python performance_analyzer.py sample_metrics.txt")
    print("  python orderbook_visualizer.py")
    print("  python trade_analyzer.py")
