#!/usr/bin/env python3
"""
Simple metrics scraper for RTES Prometheus endpoint
Usage: python metrics_scraper.py --host localhost --port 8080
"""

import requests
import argparse
import time
import json
from datetime import datetime

def scrape_metrics(host, port):
    """Scrape metrics from RTES exchange"""
    try:
        response = requests.get(f"http://{host}:{port}/metrics", timeout=5)
        response.raise_for_status()
        return response.text
    except requests.RequestException as e:
        print(f"Error scraping metrics: {e}")
        return None

def scrape_health(host, port):
    """Check health status"""
    try:
        response = requests.get(f"http://{host}:{port}/health", timeout=5)
        response.raise_for_status()
        return json.loads(response.text)
    except requests.RequestException as e:
        print(f"Error checking health: {e}")
        return None

def parse_metrics(metrics_text):
    """Parse Prometheus metrics into a dictionary"""
    metrics = {}
    
    for line in metrics_text.split('\n'):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
            
        parts = line.split(' ', 1)
        if len(parts) == 2:
            metric_name = parts[0]
            metric_value = parts[1]
            
            try:
                # Handle histogram buckets and other labeled metrics
                if '{' in metric_name:
                    base_name = metric_name.split('{')[0]
                    if base_name not in metrics:
                        metrics[base_name] = {}
                    metrics[base_name][metric_name] = float(metric_value)
                else:
                    metrics[metric_name] = float(metric_value)
            except ValueError:
                pass  # Skip non-numeric values
    
    return metrics

def display_key_metrics(metrics):
    """Display key exchange metrics in a readable format"""
    print(f"\n=== RTES Exchange Metrics ({datetime.now().strftime('%H:%M:%S')}) ===")
    
    # Orders
    orders_total = metrics.get('rtes_orders_total', 0)
    orders_rejected = metrics.get('rtes_orders_rejected_total', 0)
    print(f"Orders: {orders_total:,} total, {orders_rejected:,} rejected")
    
    # Trades
    trades_total = metrics.get('rtes_trades_total', 0)
    print(f"Trades: {trades_total:,} executed")
    
    # Connections
    tcp_connections = metrics.get('rtes_tcp_connections_total', 0)
    udp_messages = metrics.get('rtes_udp_messages_total', 0)
    print(f"Network: {tcp_connections:,} TCP connections, {udp_messages:,} UDP messages")
    
    # Latency (if available)
    latency_metrics = metrics.get('rtes_order_latency_seconds', {})
    if latency_metrics:
        count_key = 'rtes_order_latency_seconds_count'
        sum_key = 'rtes_order_latency_seconds_sum'
        
        if count_key in latency_metrics and sum_key in latency_metrics:
            count = latency_metrics[count_key]
            total_time = latency_metrics[sum_key]
            if count > 0:
                avg_latency_us = (total_time / count) * 1_000_000
                print(f"Latency: {avg_latency_us:.1f}μs average")
    
    print("-" * 50)

def main():
    parser = argparse.ArgumentParser(description='RTES Metrics Scraper')
    parser.add_argument('--host', default='localhost', help='Exchange host')
    parser.add_argument('--port', type=int, default=8080, help='Metrics port')
    parser.add_argument('--interval', type=int, default=5, help='Scrape interval (seconds)')
    parser.add_argument('--once', action='store_true', help='Scrape once and exit')
    parser.add_argument('--raw', action='store_true', help='Show raw Prometheus output')
    parser.add_argument('--health', action='store_true', help='Check health only')
    args = parser.parse_args()
    
    if args.health:
        health = scrape_health(args.host, args.port)
        if health:
            print(json.dumps(health, indent=2))
        return
    
    try:
        while True:
            metrics_text = scrape_metrics(args.host, args.port)
            
            if metrics_text:
                if args.raw:
                    print(metrics_text)
                else:
                    metrics = parse_metrics(metrics_text)
                    display_key_metrics(metrics)
            else:
                print(f"Failed to scrape metrics from {args.host}:{args.port}")
            
            if args.once:
                break
                
            time.sleep(args.interval)
            
    except KeyboardInterrupt:
        print("\nStopping metrics scraper...")

if __name__ == '__main__':
    main()