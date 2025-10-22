#include "rtes/matching_engine.hpp"
#include "rtes/logger.hpp"

namespace rtes {

MatchingEngine::MatchingEngine(const std::string& symbol, OrderPool& pool)
    : symbol_(symbol), pool_(pool) {
    
    input_queue_ = std::make_unique<SPSCQueue<OrderRequest>>(65536);
    
    // Create order book with trade callback
    book_ = std::make_unique<OrderBook>(symbol, pool, 
        [this](const Trade& trade) { on_trade(trade); });
}

MatchingEngine::~MatchingEngine() {
    stop();
}

void MatchingEngine::start() {
    if (running_.load()) return;
    
    running_.store(true);
    worker_thread_ = std::thread(&MatchingEngine::run, this);
    
    LOG_INFO("Matching engine started for symbol: " + symbol_);
}

void MatchingEngine::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    LOG_INFO("Matching engine stopped for symbol: " + symbol_);
}

bool MatchingEngine::submit_order(Order* order) {
    if (!order) return false;
    
    OrderRequest request;
    request.type = OrderRequest::NEW_ORDER;
    request.order = order;
    
    return input_queue_->push(request);
}

bool MatchingEngine::cancel_order(OrderID order_id, ClientID client_id) {
    OrderRequest request;
    request.type = OrderRequest::CANCEL_ORDER;
    request.order_id = order_id;
    request.client_id = client_id;
    
    return input_queue_->push(request);
}

void MatchingEngine::set_market_data_queue(MPMCQueue<MarketDataEvent>* queue) {
    market_data_queue_ = queue;
}

void MatchingEngine::run() {
    OrderRequest request;
    
    while (running_.load()) {
        if (input_queue_->pop(request)) {
            switch (request.type) {
                case OrderRequest::NEW_ORDER:
                    process_new_order(request.order);
                    break;
                case OrderRequest::CANCEL_ORDER:
                    process_cancel_order(request.order_id, request.client_id);
                    break;
            }
        } else {
            // No work available, yield CPU
            std::this_thread::yield();
        }
    }
}

void MatchingEngine::process_new_order(Order* order) {
    if (!order) return;
    
    // Store BBO before processing
    Price old_bid = book_->best_bid();
    Price old_ask = book_->best_ask();
    
    // Process order through matching engine
    bool success = book_->add_order(order);
    
    if (success) {
        orders_processed_.fetch_add(1);
        
        // Check if BBO changed
        if (book_->best_bid() != old_bid || book_->best_ask() != old_ask) {
            publish_bbo_update();
        }
    } else {
        // Order rejected, return to pool
        order->status = OrderStatus::REJECTED;
        pool_.deallocate(order);
    }
}

void MatchingEngine::process_cancel_order(OrderID order_id, ClientID client_id) {
    // TODO: Add client ownership verification
    bool success = book_->cancel_order(order_id);
    
    if (success) {
        // BBO might have changed
        publish_bbo_update();
    }
}

void MatchingEngine::publish_trade(const Trade& trade) {
    if (!market_data_queue_) return;
    
    MarketDataEvent event(trade);
    market_data_queue_->push(event);
}

void MatchingEngine::publish_bbo_update() {
    if (!market_data_queue_) return;
    
    MarketDataEvent event;
    event.type = MarketDataEvent::BBO_UPDATE;
    std::strncpy(event.symbol, symbol_.c_str(), sizeof(event.symbol));
    
    event.bbo.bid_price = book_->best_bid();
    event.bbo.bid_quantity = book_->bid_quantity();
    event.bbo.ask_price = book_->best_ask();
    event.bbo.ask_quantity = book_->ask_quantity();
    
    market_data_queue_->push(event);
}

void MatchingEngine::on_trade(const Trade& trade) {
    trades_executed_.fetch_add(1);
    publish_trade(trade);
}

} // namespace rtes