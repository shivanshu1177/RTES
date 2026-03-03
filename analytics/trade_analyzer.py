#!/usr/bin/env python3
"""Trade analyzer for RTES execution data."""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from datetime import datetime


class TradeAnalyzer:
    """Analyze trade execution data."""
    
    def __init__(self, csv_file: str = None):
        self.trades = pd.DataFrame(columns=['timestamp', 'symbol', 'price', 'quantity', 'side'])
        if csv_file:
            self.trades = pd.read_csv(csv_file)
            self.trades['timestamp'] = pd.to_datetime(self.trades['timestamp'])
    
    def add_trade(self, symbol: str, price: float, quantity: int, side: str):
        """Add trade to dataset."""
        new_trade = pd.DataFrame([{
            'timestamp': datetime.now(),
            'symbol': symbol,
            'price': price,
            'quantity': quantity,
            'side': side
        }])
        self.trades = pd.concat([self.trades, new_trade], ignore_index=True)
    
    def calculate_vwap(self, symbol: str = None) -> float:
        """Calculate Volume-Weighted Average Price."""
        df = self.trades if symbol is None else self.trades[self.trades['symbol'] == symbol]
        if df.empty:
            return 0.0
        return (df['price'] * df['quantity']).sum() / df['quantity'].sum()
    
    def get_volume_profile(self, symbol: str = None, bins: int = 20) -> pd.DataFrame:
        """Get volume distribution by price level."""
        df = self.trades if symbol is None else self.trades[self.trades['symbol'] == symbol]
        if df.empty:
            return pd.DataFrame()
        
        df['price_bin'] = pd.cut(df['price'], bins=bins)
        volume_profile = df.groupby('price_bin')['quantity'].sum().reset_index()
        volume_profile['price_mid'] = volume_profile['price_bin'].apply(lambda x: x.mid)
        return volume_profile
    
    def plot_price_volume(self, symbol: str = None, output_file: str = 'price_volume.png'):
        """Plot price and volume over time."""
        df = self.trades if symbol is None else self.trades[self.trades['symbol'] == symbol]
        if df.empty:
            return
        
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 8), sharex=True)
        
        # Price chart
        ax1.plot(df.index, df['price'], marker='o', markersize=3, linestyle='-', linewidth=1)
        vwap = self.calculate_vwap(symbol)
        ax1.axhline(y=vwap, color='orange', linestyle='--', label=f'VWAP: {vwap:.2f}')
        ax1.set_ylabel('Price')
        ax1.set_title(f'Trade Price {"- " + symbol if symbol else ""}')
        ax1.legend()
        ax1.grid(True, alpha=0.3)
        
        # Volume chart
        colors = ['green' if s == 'BUY' else 'red' for s in df['side']]
        ax2.bar(df.index, df['quantity'], color=colors, alpha=0.6)
        ax2.set_xlabel('Trade #')
        ax2.set_ylabel('Quantity')
        ax2.set_title('Trade Volume')
        ax2.grid(True, alpha=0.3)
        
        plt.tight_layout()
        plt.savefig(output_file, dpi=150)
        plt.close()
    
    def plot_volume_profile(self, symbol: str = None, output_file: str = 'volume_profile.png'):
        """Plot volume profile by price level."""
        volume_profile = self.get_volume_profile(symbol)
        if volume_profile.empty:
            return
        
        plt.figure(figsize=(10, 8))
        plt.barh(volume_profile['price_mid'], volume_profile['quantity'], alpha=0.7, color='blue')
        plt.xlabel('Volume')
        plt.ylabel('Price')
        plt.title(f'Volume Profile {"- " + symbol if symbol else ""}')
        plt.grid(True, alpha=0.3)
        plt.tight_layout()
        plt.savefig(output_file, dpi=150)
        plt.close()
    
    def summary_stats(self, symbol: str = None) -> str:
        """Generate trade summary statistics."""
        df = self.trades if symbol is None else self.trades[self.trades['symbol'] == symbol]
        if df.empty:
            return "No trades"
        
        report = f"=== Trade Summary {'- ' + symbol if symbol else ''} ===\n\n"
        report += f"Total Trades: {len(df)}\n"
        report += f"Total Volume: {df['quantity'].sum():,}\n"
        report += f"VWAP: {self.calculate_vwap(symbol):.2f}\n"
        report += f"Price Range: {df['price'].min():.2f} - {df['price'].max():.2f}\n"
        report += f"Avg Trade Size: {df['quantity'].mean():.0f}\n"
        report += f"Buy/Sell Ratio: {len(df[df['side']=='BUY'])/len(df[df['side']=='SELL']):.2f}\n"
        
        return report


if __name__ == '__main__':
    # Demo with synthetic data
    analyzer = TradeAnalyzer()
    
    # Generate sample trades
    np.random.seed(42)
    for i in range(100):
        price = 100 + np.random.randn() * 0.5
        qty = np.random.randint(50, 500)
        side = np.random.choice(['BUY', 'SELL'])
        analyzer.add_trade('AAPL', price, qty, side)
    
    print(analyzer.summary_stats('AAPL'))
    analyzer.plot_price_volume('AAPL')
    analyzer.plot_volume_profile('AAPL')
    print("\nVisualizations saved: price_volume.png, volume_profile.png")
