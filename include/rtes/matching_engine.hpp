#pragma once

#include "rtes/order_book.hpp"
#include "rtes/spsc_queue.hpp"
#include "rtes/mpmc_queue.hpp"
#include "rtes/types.hpp"
#include <thread>
#include <atomic>
#include <unordered_map>

namespace rtes {

struct OrderRequest {
    enum Type { NEW_ORDER, CANCEL_ORDER } type;
    Order* order;  // For NEW_ORDER
    OrderID order_id;  // For CANCEL_ORDER
    ClientID client_id;  // For ownership verification
};

struct MarketDataEvent {
    enum Type { TRADE, BBO_UPDATE } type;
    char symbol[8];
    union {
        Trade trade;
        struct {
            Price bid_price;
            Quantity bid_quantity;
            Price ask_price;
            Quantity ask_quantity;
        } bbo;
    };
    
    MarketDataEvent() = default;
    MarketDataEvent(const Trade& t) : type(TRADE) {
        trade = t;
        std::strncpy(symbol, t.symbol, sizeof(symbol));
    }
};

class MatchingEngine {
public:
    explicit MatchingEngine(const std::string& symbol, OrderPool& pool);
    ~MatchingEngine();
    
    void start();
    void stop();
    
    // Thread-safe order submission
    bool submit_order(Order* order);
    bool cancel_order(OrderID order_id, ClientID client_id);
    
    // Market data subscription
    void set_market_data_queue(MPMCQueue<MarketDataEvent>* queue);
    
    // Statistics
    uint64_t orders_processed() const { return orders_processed_.load(); }
    uint64_t trades_executed() const { return trades_executed_.load(); }

private:
    std::string symbol_;
    OrderPool& pool_;
    std::unique_ptr<OrderBook> book_;
    
    // Threading
    std::thread worker_thread_;
    std::atomic<bool> running_{false};
    
    // Input queue (SPSC from risk manager)
    std::unique_ptr<SPSCQueue<OrderRequest>> input_queue_;
    
    // Output queue (MPMC to market data publisher)
    MPMCQueue<MarketDataEvent>* market_data_queue_{nullptr};
    
    // Statistics
    std::atomic<uint64_t> orders_processed_{0};
    std::atomic<uint64_t> trades_executed_{0};
    
    // Worker thread main loop
    void run();
    
    // Order processing
    void process_new_order(Order* order);
    void process_cancel_order(OrderID order_id, ClientID client_id);
    
    // Market data publishing
    void publish_trade(const Trade& trade);
    void publish_bbo_update();
    
    // Trade callback from order book
    void on_trade(const Trade& trade);
};

} // namespace rtes