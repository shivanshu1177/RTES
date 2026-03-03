#include "rtes/strategies.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <sstream>

using namespace rtes;

std::vector<std::string> split_symbols(const std::string& symbols_str) {
    std::vector<std::string> symbols;
    std::stringstream ss(symbols_str);
    std::string symbol;
    
    while (std::getline(ss, symbol, ',')) {
        symbols.push_back(symbol);
    }
    
    return symbols;
}

int main(int argc, char* argv[]) {
    std::string symbols_str = "AAPL,MSFT";
    int num_orders = 1000000;
    std::string host = "localhost";
    uint16_t port = 8888;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) {
            std::cerr << "Missing value for " << argv[i] << "\n";
            return 1;
        }
        
        std::string arg = argv[i];
        if (arg == "--symbols") {
            symbols_str = argv[i + 1];
        } else if (arg == "--orders") {
            num_orders = std::stoi(argv[i + 1]);
        } else if (arg == "--host") {
            host = argv[i + 1];
        } else if (arg == "--port") {
            port = std::stoi(argv[i + 1]);
        }
    }
    
    auto symbols = split_symbols(symbols_str);
    if (symbols.empty()) {
        std::cerr << "No symbols specified\n";
        return 1;
    }
    
    std::cout << "Exchange Benchmark\n";
    std::cout << "Symbols: " << symbols_str << "\n";
    std::cout << "Target orders: " << num_orders << "\n";
    std::cout << "Host: " << host << ":" << port << "\n\n";
    
    // Create high-frequency liquidity taker for benchmarking
    LiquidityTakerStrategy client(host, port, 9999, symbols[0]);
    
    if (!client.connect()) {
        std::cerr << "Failed to connect to exchange\n";
        return 1;
    }
    
    std::cout << "Connected to exchange, starting benchmark...\n";
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Run for calculated duration to reach target orders
    int estimated_duration = std::max(1, num_orders / 1000);  // Assume ~1000 orders/sec
    client.run(std::chrono::seconds(estimated_duration));
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    std::cout << "\n=== Benchmark Results ===\n";
    std::cout << "Duration: " << duration.count() / 1000.0 << " ms\n";
    std::cout << "Orders sent: " << client.orders_sent() << "\n";
    std::cout << "Orders acked: " << client.orders_acked() << "\n";
    std::cout << "Orders rejected: " << client.orders_rejected() << "\n";
    std::cout << "Trades received: " << client.trades_received() << "\n";
    
    if (duration.count() > 0) {
        double orders_per_sec = (client.orders_sent() * 1e6) / duration.count();
        double avg_latency_us = duration.count() / static_cast<double>(client.orders_sent());
        
        std::cout << "Throughput: " << orders_per_sec << " orders/sec\n";
        std::cout << "Average latency: " << avg_latency_us << " μs/order\n";
    }
    
    client.disconnect();
    return 0;
}