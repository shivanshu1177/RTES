#include "rtes/matching_engine.hpp"
#include "rtes/memory_pool.hpp"
#include <chrono>
#include <iostream>
#include <random>

namespace rtes {

void benchmark_matching_engine() {
    constexpr size_t num_orders = 50000;
    // Worst case: every order rests with no matches → need num_orders pool slots.
    constexpr size_t pool_size = num_orders + 1024;
    // Each order can generate at most one trade + one BBO event.
    constexpr size_t md_queue_size = num_orders * 2 + 1024;

    OrderPool pool(pool_size);
    MPMCQueue<MarketDataEvent> market_data_queue(md_queue_size);
    MatchingEngine engine("AAPL", pool);

    engine.set_market_data_queue(&market_data_queue);
    engine.start();

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> side_dist(1, 2);
    std::uniform_int_distribution<> price_dist(14900, 15100);
    std::uniform_int_distribution<> qty_dist(100, 1000);

    auto start = std::chrono::high_resolution_clock::now();

    size_t submitted = 0;
    size_t push_fails = 0;
    for (size_t i = 0; i < num_orders; ++i) {
        auto* order = pool.allocate();
        if (!order) {
            std::cerr << "POOL EXHAUSTED at " << i << std::endl;
            while (!(order = pool.allocate()))
                std::this_thread::sleep_for(std::chrono::microseconds(10));
        }

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

        int retries = 0;
        while (!engine.submit_order(order)) {
            ++push_fails;
            ++retries;
            std::this_thread::sleep_for(std::chrono::microseconds(1));
            if (retries > 1000) {
                std::cerr << "PUSH FAILED after 1000 retries at order " << i << std::endl;
                break;
            }
        }

        ++submitted;
    }
    std::cout << "Submitted: " << submitted << ", Push fails: " << push_fails << std::endl;

    // Wait for the engine to process every submitted order (with timeout)
    auto wait_start = std::chrono::high_resolution_clock::now();
    size_t processed = engine.orders_processed();
    while (engine.orders_processed() < submitted) {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
        auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - wait_start);
        if (waited > std::chrono::milliseconds(5000)) {
            std::cerr << "TIMEOUT waiting for processing. Processed: " 
                      << engine.orders_processed() << "/" << submitted << std::endl;
            break;
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    engine.stop();

    std::cout << "Matching Engine Benchmark:\n";
    std::cout << "  Orders submitted:   " << submitted << "\n";
    std::cout << "  Orders processed:   " << engine.orders_processed() << "\n";
    std::cout << "  Trades executed:    " << engine.trades_executed() << "\n";
    std::cout << "  Total time:         " << duration.count() / 1000.0 << " ms\n";
    std::cout << "  Avg latency:        " << duration.count() / (double)submitted << " μs/order\n";
    std::cout << "  Throughput:         " << (submitted * 1e6) / duration.count() << " orders/sec\n";

    MarketDataEvent event;
    size_t md_events = 0;
    while (market_data_queue.pop(event)) ++md_events;
    std::cout << "  Market data events: " << md_events << "\n";
}

} // namespace rtes

int main() {
    std::cout << "Matching Engine Benchmarks\n";
    std::cout << "===========================\n\n";

    rtes::benchmark_matching_engine();

    return 0;
}
