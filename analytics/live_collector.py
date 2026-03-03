#!/usr/bin/env python3
"""Live metrics collector for RTES exchange."""

import requests
import time
import argparse
from datetime import datetime


class LiveMetricsCollector:
    """Collect metrics from running RTES exchange."""
    
    def __init__(self, host: str = 'localhost', port: int = 8080):
        self.url = f'http://{host}:{port}/metrics'
    
    def collect_snapshot(self, output_file: str = None):
        """Collect single metrics snapshot."""
        try:
            response = requests.get(self.url, timeout=5)
            response.raise_for_status()
            
            if output_file:
                with open(output_file, 'w') as f:
                    f.write(response.text)
                print(f"Metrics saved to {output_file}")
            
            return response.text
        
        except requests.exceptions.RequestException as e:
            print(f"Error collecting metrics: {e}")
            return None
    
    def collect_continuous(self, interval: int = 10, duration: int = 60, output_prefix: str = 'metrics'):
        """Collect metrics continuously."""
        print(f"Collecting metrics every {interval}s for {duration}s...")
        
        start_time = time.time()
        count = 0
        
        while time.time() - start_time < duration:
            timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
            output_file = f'{output_prefix}_{timestamp}.txt'
            
            self.collect_snapshot(output_file)
            count += 1
            
            time.sleep(interval)
        
        print(f"\nCollected {count} snapshots")


def main():
    parser = argparse.ArgumentParser(description='Collect metrics from RTES exchange')
    parser.add_argument('--host', default='localhost', help='Exchange host')
    parser.add_argument('--port', type=int, default=8080, help='Metrics port')
    parser.add_argument('--output', default='live_metrics.txt', help='Output file')
    parser.add_argument('--continuous', action='store_true', help='Continuous collection')
    parser.add_argument('--interval', type=int, default=10, help='Collection interval (seconds)')
    parser.add_argument('--duration', type=int, default=60, help='Collection duration (seconds)')
    
    args = parser.parse_args()
    
    collector = LiveMetricsCollector(args.host, args.port)
    
    if args.continuous:
        collector.collect_continuous(args.interval, args.duration)
    else:
        collector.collect_snapshot(args.output)
        print(f"\nAnalyze with: python performance_analyzer.py {args.output}")


if __name__ == '__main__':
    main()
