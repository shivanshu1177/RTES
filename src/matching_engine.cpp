#include "rtes/matching_engine.hpp"
#include "rtes/logger.hpp"
#include <cstring>

namespace rtes {

MatchingEngine::MatchingEngine(const std::string& symbol, OrderPool& pool)
    : symbol_(symbol), pool_(pool),
      input_queue_(std::make_unique<SPSCQueue<OrderRequest>>(65536))
{
    book_ = std::make_unique<OrderBook>(symbol, pool,
        [this](const Trade& trade) { on_trade(trade); });
}

MatchingEngine::~MatchingEngine() {
    stop();
}

void MatchingEngine::start() {
    running_.store(true, std::memory_order_release);
    worker_thread_ = std::thread(&MatchingEngine::run, this);
}

void MatchingEngine::stop() {
    running_.store(false, std::memory_order_release);
    if (worker_thread_.joinable()) worker_thread_.join();
}

bool MatchingEngine::submit_order(Order* order) {
    OrderRequest req;
    req.type      = OrderRequest::NEW_ORDER;
    req.order     = order;
    req.order_id  = 0;
    req.client_id = order->client_id;

    // Spin-retry a bounded number of times before dropping
    for (int i = 0; i < 1000; ++i) {
        if (input_queue_->push(req)) return true;
    }
    LOG_WARN("MatchingEngine[" + symbol_ + "]: input queue full, dropping order");
    return false;
}

bool MatchingEngine::cancel_order(OrderID order_id, ClientID client_id) {
    OrderRequest req;
    req.type      = OrderRequest::CANCEL_ORDER;
    req.order     = nullptr;
    req.order_id  = order_id;
    req.client_id = client_id;

    for (int i = 0; i < 1000; ++i) {
        if (input_queue_->push(req)) return true;
    }
    return false;
}

void MatchingEngine::set_market_data_queue(MPMCQueue<MarketDataEvent>* queue) {
    market_data_queue_ = queue;
}

void MatchingEngine::run() {
    while (running_.load(std::memory_order_acquire)) {
        OrderRequest req;
        if (input_queue_->pop(req)) {
            if (req.type == OrderRequest::NEW_ORDER) {
                process_new_order(req.order);
            } else {
                process_cancel_order(req.order_id, req.client_id);
            }
        }
        // tight spin — no sleep to minimise latency
    }
}

void MatchingEngine::process_new_order(Order* order) {
    book_->add_order(order);
    orders_processed_.fetch_add(1, std::memory_order_relaxed);
    publish_bbo_update();
}

void MatchingEngine::process_cancel_order(OrderID order_id, ClientID /*client_id*/) {
    book_->cancel_order(order_id);
    publish_bbo_update();
}

void MatchingEngine::on_trade(const Trade& trade) {
    // Called while book_->mutex_ is held — only touch lock-free structures here
    trades_executed_.fetch_add(1, std::memory_order_relaxed);
    publish_trade(trade);
}

void MatchingEngine::publish_trade(const Trade& trade) {
    if (!market_data_queue_) return;
    MarketDataEvent ev(trade);
    market_data_queue_->push(ev);
}

void MatchingEngine::publish_bbo_update() {
    if (!market_data_queue_) return;

    MarketDataEvent ev;
    ev.type = MarketDataEvent::BBO_UPDATE;
    std::strncpy(ev.symbol, symbol_.c_str(), sizeof(ev.symbol) - 1);
    ev.symbol[sizeof(ev.symbol) - 1] = '\0';
    ev.bbo.bid_price    = book_->best_bid();
    ev.bbo.bid_quantity = 0;
    ev.bbo.ask_price    = book_->best_ask();
    ev.bbo.ask_quantity = 0;
    market_data_queue_->push(ev);
}

} // namespace rtes
