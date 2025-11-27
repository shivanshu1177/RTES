#include "rtes/matching_engine.hpp"
#include "rtes/memory_pool.hpp"
#include <chrono>
#include <iostream>
#include <random>

namespace rtes {

void benchmark_matching_engine() {
    constexpr size_t pool_size = 100000;
    constexpr size_t num_orders = 50000;
    
    OrderPool pool(pool_size);
    MPMCQueue<MarketDataEvent> market_data_queue(10000);
    MatchingEngine engine("AAPL", pool);
    
    engine.set_market_data_queue(&market_data_queue);
    engine.start();
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> side_dist(1, 2);
    std::uniform_int_distribution<> price_dist(14900, 15100);
    std::uniform_int_distribution<> qty_dist(100, 1000);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Submit orders
    for (size_t i = 0; i < num_orders; ++i) {
        auto* order = pool.allocate();
        if (!order) break;
        
        Side side = (side_dist(gen) == 1) ? Side::BUY : Side::SELL;
        Price price = price_dist(gen);
        Quantity qty = qty_dist(gen);
        
        order->id = i + 1;
        order->client_id.assign("100");
        order->symbol.assign("AAPL");
        order->side = side;
        order->type = OrderType::LIMIT;
        order->quantity = qty;
        order->price = price;
        order->remaining_quantity = qty;
        order->status = OrderStatus::PENDING;
        
        while (!engine.submit_order(order)) {
            std::this_thread::yield();
        }
    }
    
    // Wait for processing
    while (engine.orders_processed() < num_orders) {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    engine.stop();
    
    std::cout << "Matching Engine Benchmark:\n";
    std::cout << "  Orders processed: " << engine.orders_processed() << "\n";
    std::cout << "  Trades executed: " << engine.trades_executed() << "\n";
    std::cout << "  Total time: " << duration.count() / 1000.0 << " ms\n";
    std::cout << "  Avg latency: " << duration.count() / num_orders << " μs/order\n";
    std::cout << "  Throughput: " << (num_orders * 1e6) / duration.count() << " orders/sec\n";
    
    // Drain market data queue
    MarketDataEvent event;
    size_t md_events = 0;
    while (market_data_queue.pop(event)) {
        ++md_events;
    }
    std::cout << "  Market data events: " << md_events << "\n";
}

} // namespace rtes

int main() {
    std::cout << "Matching Engine Benchmarks\n";
    std::cout << "===========================\n\n";
    
    rtes::benchmark_matching_engine();
    
    return 0;
}