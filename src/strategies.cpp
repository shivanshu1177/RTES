#include "rtes/strategies.hpp"
#include "rtes/logger.hpp"

namespace rtes {

// Market Maker Strategy
MarketMakerStrategy::MarketMakerStrategy(const std::string& host, uint16_t port, uint32_t client_id,
                                       const std::string& symbol, uint64_t spread_ticks)
    : ClientBase(host, port, client_id), symbol_(symbol), spread_ticks_(spread_ticks) {
}

void MarketMakerStrategy::on_start() {
    LOG_INFO("Market maker started for " + symbol_);
    update_quotes();
}

void MarketMakerStrategy::on_tick() {
    // Update quotes periodically
    if (random_int(1, 1000) <= 10) {  // 1% chance per tick
        update_quotes();
    }
}

void MarketMakerStrategy::on_order_ack(const OrderAckMessage& ack) {
    if (ack.status == 1) {  // Accepted
        if (ack.order_id == bid_order_id_) {
            LOG_INFO("Bid order accepted: " + std::to_string(ack.order_id));
        } else if (ack.order_id == ask_order_id_) {
            LOG_INFO("Ask order accepted: " + std::to_string(ack.order_id));
        }
    }
}

void MarketMakerStrategy::on_trade(const TradeMessage& trade) {
    // Adjust base price based on trades
    base_price_ = trade.price;
    
    // Cancel and replace quotes after being hit
    cancel_existing_orders();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    update_quotes();
}

void MarketMakerStrategy::update_quotes() {
    cancel_existing_orders();
    
    // Add some randomness to price
    int price_adjustment = random_int(-5, 5);
    uint64_t adjusted_price = base_price_ + price_adjustment;
    
    uint64_t bid_price = adjusted_price - spread_ticks_;
    uint64_t ask_price = adjusted_price + spread_ticks_;
    
    // Send new quotes
    if (send_new_order(symbol_, Side::BUY, quote_size_, bid_price)) {
        bid_order_id_ = next_order_id_ - 1;
    }
    
    if (send_new_order(symbol_, Side::SELL, quote_size_, ask_price)) {
        ask_order_id_ = next_order_id_ - 1;
    }
}

void MarketMakerStrategy::cancel_existing_orders() {
    if (bid_order_id_ > 0) {
        send_cancel_order(bid_order_id_, symbol_);
        bid_order_id_ = 0;
    }
    
    if (ask_order_id_ > 0) {
        send_cancel_order(ask_order_id_, symbol_);
        ask_order_id_ = 0;
    }
}

// Momentum Strategy
MomentumStrategy::MomentumStrategy(const std::string& host, uint16_t port, uint32_t client_id,
                                 const std::string& symbol)
    : ClientBase(host, port, client_id), symbol_(symbol) {
}

void MomentumStrategy::on_start() {
    LOG_INFO("Momentum strategy started for " + symbol_);
}

void MomentumStrategy::on_tick() {
    if (price_history_.size() >= 10) {
        analyze_momentum();
    }
}

void MomentumStrategy::on_trade(const TradeMessage& trade) {
    last_trade_price_ = trade.price;
    price_history_.push_back(trade.price);
    
    // Keep only recent prices
    if (price_history_.size() > 20) {
        price_history_.pop_front();
    }
}

void MomentumStrategy::analyze_momentum() {
    if (detect_upward_momentum()) {
        // Buy on upward momentum
        uint64_t aggressive_price = last_trade_price_ + 5;  // Pay up
        send_new_order(symbol_, Side::BUY, order_size_, aggressive_price);
    } else if (detect_downward_momentum()) {
        // Sell on downward momentum
        uint64_t aggressive_price = last_trade_price_ - 5;  // Sell down
        send_new_order(symbol_, Side::SELL, order_size_, aggressive_price);
    }
}

bool MomentumStrategy::detect_upward_momentum() {
    if (price_history_.size() < 5) return false;
    
    int rising_count = 0;
    for (size_t i = 1; i < price_history_.size(); ++i) {
        if (price_history_[i] > price_history_[i-1]) {
            rising_count++;
        }
    }
    
    return rising_count >= momentum_threshold_;
}

bool MomentumStrategy::detect_downward_momentum() {
    if (price_history_.size() < 5) return false;
    
    int falling_count = 0;
    for (size_t i = 1; i < price_history_.size(); ++i) {
        if (price_history_[i] < price_history_[i-1]) {
            falling_count++;
        }
    }
    
    return falling_count >= momentum_threshold_;
}

// Arbitrage Strategy
ArbitrageStrategy::ArbitrageStrategy(const std::string& host, uint16_t port, uint32_t client_id,
                                   const std::string& symbol1, const std::string& symbol2)
    : ClientBase(host, port, client_id), symbol1_(symbol1), symbol2_(symbol2) {
}

void ArbitrageStrategy::on_start() {
    LOG_INFO("Arbitrage strategy started for " + symbol1_ + "/" + symbol2_);
}

void ArbitrageStrategy::on_tick() {
    if (symbol1_price_ > 0 && symbol2_price_ > 0) {
        check_arbitrage_opportunity();
    }
}

void ArbitrageStrategy::on_trade(const TradeMessage& trade) {
    if (std::string(trade.symbol) == symbol1_) {
        symbol1_price_ = trade.price;
    } else if (std::string(trade.symbol) == symbol2_) {
        symbol2_price_ = trade.price;
    }
}

void ArbitrageStrategy::check_arbitrage_opportunity() {
    uint64_t price_diff = (symbol1_price_ > symbol2_price_) ? 
                         (symbol1_price_ - symbol2_price_) : 
                         (symbol2_price_ - symbol1_price_);
    
    uint64_t avg_price = (symbol1_price_ + symbol2_price_) / 2;
    uint64_t threshold = (avg_price * spread_threshold_) / 10000;  // Convert basis points
    
    if (price_diff > threshold) {
        if (symbol1_price_ > symbol2_price_) {
            // Sell symbol1, buy symbol2
            send_new_order(symbol1_, Side::SELL, arb_size_, symbol1_price_ - 1);
            send_new_order(symbol2_, Side::BUY, arb_size_, symbol2_price_ + 1);
        } else {
            // Buy symbol1, sell symbol2
            send_new_order(symbol1_, Side::BUY, arb_size_, symbol1_price_ + 1);
            send_new_order(symbol2_, Side::SELL, arb_size_, symbol2_price_ - 1);
        }
    }
}

// Liquidity Taker Strategy
LiquidityTakerStrategy::LiquidityTakerStrategy(const std::string& host, uint16_t port, uint32_t client_id,
                                             const std::string& symbol)
    : ClientBase(host, port, client_id), symbol_(symbol) {
}

void LiquidityTakerStrategy::on_start() {
    LOG_INFO("Liquidity taker started for " + symbol_);
}

void LiquidityTakerStrategy::on_tick() {
    tick_counter_++;
    
    // Send orders at specified frequency
    if (tick_counter_ % (1000 / order_frequency_) == 0) {
        send_random_order();
    }
}

void LiquidityTakerStrategy::send_random_order() {
    Side side = (random_int(0, 1) == 0) ? Side::BUY : Side::SELL;
    
    // Random price around base price
    int price_offset = random_int(-20, 20);
    uint64_t price = base_price_ + price_offset;
    
    // Random size
    uint64_t size = order_size_ + random_int(-25, 25);
    
    send_new_order(symbol_, side, size, price);
}

} // namespace rtes