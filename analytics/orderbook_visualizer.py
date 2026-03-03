#!/usr/bin/env python3
"""Order book visualizer for RTES market data."""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from collections import defaultdict
from typing import List, Tuple


class OrderBookVisualizer:
    """Visualize order book depth and dynamics."""
    
    def __init__(self, csv_file: str = None):
        self.bids = defaultdict(int)
        self.asks = defaultdict(int)
        if csv_file:
            self._load_from_csv(csv_file)
    
    def _load_from_csv(self, file_path: str):
        """Load order book snapshot from CSV."""
        df = pd.read_csv(file_path)
        for _, row in df.iterrows():
            if row['side'] == 'BUY':
                self.bids[row['price']] += row['quantity']
            else:
                self.asks[row['price']] += row['quantity']
    
    def add_order(self, side: str, price: float, quantity: int):
        """Add order to book."""
        if side == 'BUY':
            self.bids[price] += quantity
        else:
            self.asks[price] += quantity
    
    def get_depth_data(self, levels: int = 10) -> Tuple[pd.DataFrame, pd.DataFrame]:
        """Get top N levels of bid/ask depth."""
        bid_prices = sorted(self.bids.keys(), reverse=True)[:levels]
        ask_prices = sorted(self.asks.keys())[:levels]
        
        bid_df = pd.DataFrame([
            {'price': p, 'quantity': self.bids[p], 'cumulative': sum(self.bids[x] for x in bid_prices[:i+1])}
            for i, p in enumerate(bid_prices)
        ])
        
        ask_df = pd.DataFrame([
            {'price': p, 'quantity': self.asks[p], 'cumulative': sum(self.asks[x] for x in ask_prices[:i+1])}
            for i, p in enumerate(ask_prices)
        ])
        
        return bid_df, ask_df
    
    def plot_depth_chart(self, levels: int = 10, output_file: str = 'orderbook_depth.png'):
        """Plot order book depth chart."""
        bid_df, ask_df = self.get_depth_data(levels)
        
        if bid_df.empty or ask_df.empty:
            print("No order book data to plot")
            return
        
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))
        
        # Bid side (left)
        ax1.barh(bid_df['price'], bid_df['quantity'], color='green', alpha=0.6, label='Bids')
        ax1.set_xlabel('Quantity')
        ax1.set_ylabel('Price')
        ax1.set_title('Bid Depth')
        ax1.invert_xaxis()
        ax1.grid(True, alpha=0.3)
        
        # Ask side (right)
        ax2.barh(ask_df['price'], ask_df['quantity'], color='red', alpha=0.6, label='Asks')
        ax2.set_xlabel('Quantity')
        ax2.set_title('Ask Depth')
        ax2.grid(True, alpha=0.3)
        
        plt.tight_layout()
        plt.savefig(output_file, dpi=150)
        plt.close()
    
    def plot_spread_analysis(self, output_file: str = 'spread_analysis.png'):
        """Plot bid-ask spread over time."""
        if not self.bids or not self.asks:
            return
        
        best_bid = max(self.bids.keys())
        best_ask = min(self.asks.keys())
        spread = best_ask - best_bid
        spread_bps = (spread / best_bid) * 10000
        
        fig, ax = plt.subplots(figsize=(10, 6))
        ax.axhline(y=best_bid, color='green', linestyle='--', label=f'Best Bid: {best_bid:.2f}')
        ax.axhline(y=best_ask, color='red', linestyle='--', label=f'Best Ask: {best_ask:.2f}')
        ax.fill_between([0, 1], best_bid, best_ask, alpha=0.3, color='yellow')
        ax.set_ylabel('Price')
        ax.set_title(f'Bid-Ask Spread: {spread:.2f} ({spread_bps:.1f} bps)')
        ax.legend()
        ax.grid(True, alpha=0.3)
        plt.tight_layout()
        plt.savefig(output_file, dpi=150)
        plt.close()
    
    def summary_stats(self) -> str:
        """Generate order book summary statistics."""
        if not self.bids or not self.asks:
            return "Empty order book"
        
        best_bid = max(self.bids.keys())
        best_ask = min(self.asks.keys())
        mid_price = (best_bid + best_ask) / 2
        spread = best_ask - best_bid
        
        total_bid_qty = sum(self.bids.values())
        total_ask_qty = sum(self.asks.values())
        
        report = "=== Order Book Summary ===\n\n"
        report += f"Best Bid: {best_bid:.2f} ({self.bids[best_bid]} qty)\n"
        report += f"Best Ask: {best_ask:.2f} ({self.asks[best_ask]} qty)\n"
        report += f"Mid Price: {mid_price:.2f}\n"
        report += f"Spread: {spread:.2f} ({(spread/mid_price)*10000:.1f} bps)\n"
        report += f"Total Bid Qty: {total_bid_qty}\n"
        report += f"Total Ask Qty: {total_ask_qty}\n"
        report += f"Imbalance: {(total_bid_qty-total_ask_qty)/(total_bid_qty+total_ask_qty)*100:.1f}%\n"
        
        return report


if __name__ == '__main__':
    # Demo with synthetic data
    viz = OrderBookVisualizer()
    
    # Add sample orders
    for i in range(10):
        viz.add_order('BUY', 100.0 - i*0.1, np.random.randint(100, 1000))
        viz.add_order('SELL', 100.1 + i*0.1, np.random.randint(100, 1000))
    
    print(viz.summary_stats())
    viz.plot_depth_chart()
    viz.plot_spread_analysis()
    print("\nVisualizations saved: orderbook_depth.png, spread_analysis.png")
