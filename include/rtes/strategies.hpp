#pragma once

#include "rtes/client_base.hpp"
#include <unordered_map>
#include <deque>

namespace rtes {

class MarketMakerStrategy : public ClientBase {
public:
    MarketMakerStrategy(const std::string& host, uint16_t port, uint32_t client_id,
                       const std::string& symbol, uint64_t spread_ticks = 10);

protected:
    void on_start() override;
    void on_tick() override;
    void on_order_ack(const OrderAckMessage& ack) override;
    void on_trade(const TradeMessage& trade) override;

private:
    std::string symbol_;
    uint64_t spread_ticks_;
    uint64_t base_price_{150000};  // $15.00
    uint64_t quote_size_{100};
    
    uint64_t bid_order_id_{0};
    uint64_t ask_order_id_{0};
    
    void update_quotes();
    void cancel_existing_orders();
};

class MomentumStrategy : public ClientBase {
public:
    MomentumStrategy(const std::string& host, uint16_t port, uint32_t client_id,
                    const std::string& symbol);

protected:
    void on_start() override;
    void on_tick() override;
    void on_trade(const TradeMessage& trade) override;

private:
    std::string symbol_;
    std::deque<uint64_t> price_history_;
    uint64_t last_trade_price_{0};
    int momentum_threshold_{5};  // Price ticks
    uint64_t order_size_{50};
    
    void analyze_momentum();
    bool detect_upward_momentum();
    bool detect_downward_momentum();
};

class ArbitrageStrategy : public ClientBase {
public:
    ArbitrageStrategy(const std::string& host, uint16_t port, uint32_t client_id,
                     const std::string& symbol1, const std::string& symbol2);

protected:
    void on_start() override;
    void on_tick() override;
    void on_trade(const TradeMessage& trade) override;

private:
    std::string symbol1_;
    std::string symbol2_;
    
    uint64_t symbol1_price_{0};
    uint64_t symbol2_price_{0};
    uint64_t spread_threshold_{50};  // 0.5% in basis points
    uint64_t arb_size_{25};
    
    void check_arbitrage_opportunity();
};

class LiquidityTakerStrategy : public ClientBase {
public:
    LiquidityTakerStrategy(const std::string& host, uint16_t port, uint32_t client_id,
                          const std::string& symbol);

protected:
    void on_start() override;
    void on_tick() override;

private:
    std::string symbol_;
    uint64_t base_price_{150000};
    uint64_t order_size_{75};
    int order_frequency_{100};  // Orders per second
    int tick_counter_{0};
    
    void send_random_order();
};

} // namespace rtes