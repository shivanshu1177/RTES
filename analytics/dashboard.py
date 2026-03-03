#!/usr/bin/env python3
"""Comprehensive analytics dashboard for RTES."""

import matplotlib.pyplot as plt
from matplotlib.gridspec import GridSpec
import numpy as np
from performance_analyzer import PerformanceAnalyzer
from orderbook_visualizer import OrderBookVisualizer
from trade_analyzer import TradeAnalyzer


class RTESDashboard:
    """Unified dashboard for RTES analytics."""
    
    def __init__(self, metrics_file: str = None, orderbook_file: str = None, trades_file: str = None):
        self.perf = PerformanceAnalyzer(metrics_file) if metrics_file else None
        self.book = OrderBookVisualizer(orderbook_file) if orderbook_file else None
        self.trades = TradeAnalyzer(trades_file) if trades_file else None
    
    def generate_dashboard(self, output_file: str = 'rtes_dashboard.png'):
        """Generate comprehensive dashboard with all metrics."""
        fig = plt.figure(figsize=(16, 10))
        gs = GridSpec(3, 3, figure=fig, hspace=0.3, wspace=0.3)
        
        # Performance metrics
        if self.perf:
            ax1 = fig.add_subplot(gs[0, :2])
            self._plot_latency_summary(ax1)
            
            ax2 = fig.add_subplot(gs[0, 2])
            self._plot_throughput(ax2)
        
        # Order book
        if self.book:
            ax3 = fig.add_subplot(gs[1, 0])
            ax4 = fig.add_subplot(gs[1, 1])
            self._plot_orderbook_depth(ax3, ax4)
            
            ax5 = fig.add_subplot(gs[1, 2])
            self._plot_spread(ax5)
        
        # Trade analysis
        if self.trades:
            ax6 = fig.add_subplot(gs[2, :2])
            self._plot_price_chart(ax6)
            
            ax7 = fig.add_subplot(gs[2, 2])
            self._plot_volume_summary(ax7)
        
        plt.savefig(output_file, dpi=150, bbox_inches='tight')
        plt.close()
        print(f"Dashboard saved to {output_file}")
    
    def _plot_latency_summary(self, ax):
        """Plot latency percentiles."""
        percentiles = self.perf.calculate_latency_percentiles()
        if not percentiles:
            return
        
        labels = list(percentiles.keys())
        values = list(percentiles.values())
        
        bars = ax.bar(labels, values, color='steelblue', alpha=0.7)
        ax.set_ylabel('Latency (μs)')
        ax.set_title('Latency Percentiles')
        ax.grid(True, alpha=0.3, axis='y')
        
        for bar in bars:
            height = bar.get_height()
            ax.text(bar.get_x() + bar.get_width()/2., height,
                   f'{height:.1f}', ha='center', va='bottom', fontsize=9)
    
    def _plot_throughput(self, ax):
        """Plot throughput gauge."""
        throughput = self.perf.get_throughput()
        target = 100000
        
        ax.text(0.5, 0.6, f'{throughput:,.0f}', ha='center', va='center', 
               fontsize=24, fontweight='bold')
        ax.text(0.5, 0.4, 'ops/sec', ha='center', va='center', fontsize=12)
        ax.text(0.5, 0.2, f'{throughput/target:.1f}x target', ha='center', va='center',
               fontsize=10, color='green' if throughput >= target else 'red')
        ax.set_xlim(0, 1)
        ax.set_ylim(0, 1)
        ax.axis('off')
        ax.set_title('Throughput')
    
    def _plot_orderbook_depth(self, ax_bid, ax_ask):
        """Plot order book depth."""
        bid_df, ask_df = self.book.get_depth_data(10)
        
        if not bid_df.empty:
            ax_bid.barh(bid_df['price'], bid_df['quantity'], color='green', alpha=0.6)
            ax_bid.set_xlabel('Quantity')
            ax_bid.set_ylabel('Price')
            ax_bid.set_title('Bid Depth')
            ax_bid.invert_xaxis()
            ax_bid.grid(True, alpha=0.3)
        
        if not ask_df.empty:
            ax_ask.barh(ask_df['price'], ask_df['quantity'], color='red', alpha=0.6)
            ax_ask.set_xlabel('Quantity')
            ax_ask.set_title('Ask Depth')
            ax_ask.grid(True, alpha=0.3)
    
    def _plot_spread(self, ax):
        """Plot spread metrics."""
        if not self.book.bids or not self.book.asks:
            return
        
        best_bid = max(self.book.bids.keys())
        best_ask = min(self.book.asks.keys())
        spread = best_ask - best_bid
        spread_bps = (spread / best_bid) * 10000
        
        ax.text(0.5, 0.6, f'{spread:.3f}', ha='center', va='center',
               fontsize=20, fontweight='bold')
        ax.text(0.5, 0.4, f'({spread_bps:.1f} bps)', ha='center', va='center', fontsize=12)
        ax.text(0.5, 0.2, f'Bid: {best_bid:.2f}\nAsk: {best_ask:.2f}',
               ha='center', va='center', fontsize=9)
        ax.set_xlim(0, 1)
        ax.set_ylim(0, 1)
        ax.axis('off')
        ax.set_title('Bid-Ask Spread')
    
    def _plot_price_chart(self, ax):
        """Plot price over time."""
        df = self.trades.trades
        if df.empty:
            return
        
        ax.plot(df.index, df['price'], linewidth=1.5, color='blue')
        vwap = self.trades.calculate_vwap()
        ax.axhline(y=vwap, color='orange', linestyle='--', label=f'VWAP: {vwap:.2f}')
        ax.set_xlabel('Trade #')
        ax.set_ylabel('Price')
        ax.set_title('Price Chart')
        ax.legend()
        ax.grid(True, alpha=0.3)
    
    def _plot_volume_summary(self, ax):
        """Plot volume summary."""
        df = self.trades.trades
        if df.empty:
            return
        
        total_vol = df['quantity'].sum()
        buy_vol = df[df['side'] == 'BUY']['quantity'].sum()
        sell_vol = df[df['side'] == 'SELL']['quantity'].sum()
        
        ax.pie([buy_vol, sell_vol], labels=['Buy', 'Sell'],
              colors=['green', 'red'], autopct='%1.1f%%', startangle=90)
        ax.set_title(f'Volume Split\nTotal: {total_vol:,}')
    
    def print_summary(self):
        """Print comprehensive summary."""
        print("=" * 60)
        print("RTES ANALYTICS SUMMARY")
        print("=" * 60)
        
        if self.perf:
            print("\n" + self.perf.summary_report())
        
        if self.book:
            print("\n" + self.book.summary_stats())
        
        if self.trades:
            print("\n" + self.trades.summary_stats())
        
        print("=" * 60)


if __name__ == '__main__':
    # Demo with sample data
    print("Generating sample data...")
    from generate_sample_data import generate_metrics_file, generate_orderbook_csv, generate_trades_csv
    
    generate_metrics_file()
    generate_orderbook_csv()
    generate_trades_csv()
    
    print("\nCreating dashboard...")
    dashboard = RTESDashboard(
        metrics_file='sample_metrics.txt',
        orderbook_file='sample_orderbook.csv',
        trades_file='sample_trades.csv'
    )
    
    dashboard.print_summary()
    dashboard.generate_dashboard()
    print("\nDashboard complete!")
