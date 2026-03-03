#include "rtes/types.hpp"
#include "rtes/logger.hpp"
#include "rtes/memory_pool.hpp"
#include "rtes/order_book.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace rtes;

void trade_callback(const Trade& trade) {
    LOG_INFO("Trade executed: " + std::to_string(trade.quantity) + 
             " @ " + std::to_string(trade.price) + 
             " for " + trade.symbol);
}

int main() {
    LOG_INFO("Starting RTES Trading Exchange");
    
    OrderPool pool(1000);
    OrderBook book("AAPL", pool, trade_callback);
    
    // Create some test orders
    auto* buy_order = pool.acquire();
    *buy_order = Order{
        .order_id = 1,
        .symbol = "AAPL",
        .side = Side::BUY,
        .type = OrderType::LIMIT,
        .price = 15000, // $150.00
        .quantity = 100
    };
    
    auto* sell_order = pool.acquire();
    *sell_order = Order{
        .order_id = 2,
        .symbol = "AAPL",
        .side = Side::SELL,
        .type = OrderType::LIMIT,
        .price = 14950, // $149.50
        .quantity = 50
    };
    
    book.add_order(buy_order);
    book.add_order(sell_order);
    
    LOG_INFO("Best bid: " + std::to_string(book.best_bid()));
    LOG_INFO("Best ask: " + std::to_string(book.best_ask()));
    LOG_INFO("Orders in book: " + std::to_string(book.order_count()));
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    LOG_INFO("RTES Trading Exchange stopped");
    return 0;
}