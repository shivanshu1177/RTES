#!/bin/bash
# Quick start script for RTES Analytics Suite

echo "=== RTES Analytics Suite Setup ==="
echo ""

# Check Python version
python3 --version || { echo "Python 3 not found!"; exit 1; }

# Install dependencies
echo "Installing dependencies..."
pip3 install -q pandas numpy matplotlib

echo ""
echo "=== Generating Sample Data ==="
python3 generate_sample_data.py

echo ""
echo "=== Running Performance Analysis ==="
python3 performance_analyzer.py sample_metrics.txt

echo ""
echo "=== Running Order Book Visualization ==="
python3 orderbook_visualizer.py

echo ""
echo "=== Running Trade Analysis ==="
python3 trade_analyzer.py

echo ""
echo "=== Generating Comprehensive Dashboard ==="
python3 dashboard.py

echo ""
echo "=== Analytics Complete ==="
echo "Generated files:"
ls -lh *.png *.csv *.txt 2>/dev/null | grep -v "^total"

echo ""
echo "View visualizations:"
echo "  - latency_dist.png"
echo "  - orderbook_depth.png"
echo "  - spread_analysis.png"
echo "  - price_volume.png"
echo "  - volume_profile.png"
echo "  - rtes_dashboard.png"
