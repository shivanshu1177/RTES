#include "rtes/order_book.hpp"
#include "rtes/logger.hpp"
#include <chrono>
#include <random>

using namespace rtes;

int main() {
    LOG_INFO("Starting matching engine benchmark");
    
    const size_t num_orders = 10000;
    OrderPool pool(num_orders);
    
    size_t trade_count = 0;
    auto trade_callback = [&trade_count](const Trade&) { trade_count++; };
    
    OrderBook book("AAPL", pool, trade_callback);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> price_dist(14900, 15100);
    std::uniform_int_distribution<> qty_dist(1, 100);
    std::uniform_int_distribution<> side_dist(0, 1);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < num_orders; ++i) {
        auto* order = pool.acquire();
        *order = Order{
            .order_id = i + 1,
            .symbol = "AAPL",
            .side = static_cast<Side>(side_dist(gen)),
            .type = OrderType::LIMIT,
            .price = static_cast<Price>(price_dist(gen)),
            .quantity = static_cast<Quantity>(qty_dist(gen))
        };
        
        book.add_order(order);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    LOG_INFO("Matching engine benchmark completed");
    LOG_INFO("Orders processed: " + std::to_string(num_orders));
    LOG_INFO("Trades executed: " + std::to_string(trade_count));
    LOG_INFO("Total time: " + std::to_string(duration.count()) + " μs");
    LOG_INFO("Average time per order: " + std::to_string(duration.count() / num_orders) + " μs");
    LOG_INFO("Orders/second: " + std::to_string(num_orders * 1000000 / duration.count()));
    
    return 0;
}