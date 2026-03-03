#!/usr/bin/env python3
"""Performance analyzer for RTES metrics data."""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from typing import Dict, List
import re


class PerformanceAnalyzer:
    """Analyze RTES performance metrics from Prometheus format."""
    
    def __init__(self, metrics_file: str):
        self.df = self._parse_metrics(metrics_file)
    
    def _parse_metrics(self, file_path: str) -> pd.DataFrame:
        """Parse Prometheus metrics into DataFrame."""
        data = []
        with open(file_path, 'r') as f:
            for line in f:
                if line.startswith('#') or not line.strip():
                    continue
                match = re.match(r'(\S+)(?:\{([^}]+)\})?\s+(\S+)', line)
                if match:
                    name, labels, value = match.groups()
                    label_dict = {}
                    if labels:
                        for label in labels.split(','):
                            k, v = label.split('=')
                            label_dict[k] = v.strip('"')
                    data.append({'metric': name, **label_dict, 'value': float(value)})
        return pd.DataFrame(data)
    
    def calculate_latency_percentiles(self, metric_prefix: str = 'rtes_latency') -> Dict[str, float]:
        """Calculate latency percentiles from histogram buckets."""
        buckets = self.df[self.df['metric'] == f'{metric_prefix}_bucket'].copy()
        if buckets.empty:
            return {}
        
        buckets['le'] = pd.to_numeric(buckets['le'].replace('+Inf', np.inf))
        buckets = buckets.sort_values('le')
        
        total = buckets['value'].iloc[-1]
        percentiles = {}
        
        for p in [50, 95, 99, 99.9]:
            target = total * (p / 100)
            bucket = buckets[buckets['value'] >= target].iloc[0]
            percentiles[f'p{p}'] = bucket['le'] * 1e6  # Convert to microseconds
        
        return percentiles
    
    def get_throughput(self, metric: str = 'rtes_orders_total', window_sec: float = 1.0) -> float:
        """Calculate throughput (ops/sec)."""
        row = self.df[self.df['metric'] == metric]
        if row.empty:
            return 0.0
        return row['value'].iloc[0] / window_sec
    
    def plot_latency_distribution(self, output_file: str = 'latency_dist.png'):
        """Plot latency distribution histogram."""
        buckets = self.df[self.df['metric'].str.contains('latency_bucket')].copy()
        if buckets.empty:
            return
        
        buckets['le'] = pd.to_numeric(buckets['le'].replace('+Inf', np.inf))
        buckets = buckets[buckets['le'] != np.inf].sort_values('le')
        
        plt.figure(figsize=(10, 6))
        plt.bar(buckets['le'] * 1e6, buckets['value'], width=5, alpha=0.7)
        plt.xlabel('Latency (μs)')
        plt.ylabel('Count')
        plt.title('Latency Distribution')
        plt.grid(True, alpha=0.3)
        plt.tight_layout()
        plt.savefig(output_file, dpi=150)
        plt.close()
    
    def summary_report(self) -> str:
        """Generate performance summary report."""
        percentiles = self.calculate_latency_percentiles()
        throughput = self.get_throughput()
        
        report = "=== RTES Performance Summary ===\n\n"
        report += f"Throughput: {throughput:,.0f} ops/sec\n\n"
        report += "Latency Percentiles (μs):\n"
        for k, v in percentiles.items():
            report += f"  {k}: {v:.2f}\n"
        
        return report


if __name__ == '__main__':
    import sys
    if len(sys.argv) < 2:
        print("Usage: performance_analyzer.py <metrics_file>")
        sys.exit(1)
    
    analyzer = PerformanceAnalyzer(sys.argv[1])
    print(analyzer.summary_report())
    analyzer.plot_latency_distribution()
    print("\nLatency distribution saved to latency_dist.png")
