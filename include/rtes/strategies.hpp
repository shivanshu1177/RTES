#pragma once

#include "rtes/protocol.hpp"
#include <string>
#include <atomic>
#include <chrono>
#include <random>
#include <cstdint>

namespace rtes {

class LiquidityTakerStrategy {
public:
    LiquidityTakerStrategy(std::string host, uint16_t port, std::string symbol);
    // Overload accepting an ignored UDP port (for bench_exchange compatibility)
    LiquidityTakerStrategy(std::string host, uint16_t port, uint16_t /*udp_port*/, std::string symbol)
        : LiquidityTakerStrategy(std::move(host), port, std::move(symbol)) {}
    ~LiquidityTakerStrategy();

    bool connect();
    void disconnect();
    void run(std::chrono::seconds duration);

    uint64_t orders_sent()     const { return orders_sent_.load(); }
    uint64_t orders_acked()    const { return orders_acked_.load(); }
    uint64_t orders_rejected() const { return orders_rejected_.load(); }
    uint64_t trades_received() const { return trades_received_.load(); }
    double   avg_latency_us()  const;

private:
    std::string host_;
    uint16_t    port_;
    std::string symbol_;
    int         sock_fd_{-1};
    uint64_t    next_order_id_{1};
    uint64_t    next_sequence_{1};

    std::atomic<uint64_t> orders_sent_{0};
    std::atomic<uint64_t> orders_acked_{0};
    std::atomic<uint64_t> orders_rejected_{0};
    std::atomic<uint64_t> trades_received_{0};
    std::atomic<uint64_t> total_latency_us_{0};

    std::mt19937 rng_{std::random_device{}()};

    bool send_order(uint8_t side, uint64_t price, uint32_t quantity);
    bool recv_ack();
    static uint64_t now_ns();
};

class MarketMakerStrategy {
public:
    MarketMakerStrategy(std::string host, uint16_t port, std::string symbol,
                        uint64_t spread_ticks, uint32_t quote_size);
    ~MarketMakerStrategy();

    bool connect();
    void disconnect();
    void run(std::chrono::seconds duration);
    void on_trade(uint64_t price);

    uint64_t orders_sent()     const { return orders_sent_.load(); }
    uint64_t orders_acked()    const { return orders_acked_.load(); }
    uint64_t orders_rejected() const { return orders_rejected_.load(); }
    uint64_t trades_received() const { return trades_received_.load(); }

private:
    std::string host_;
    uint16_t    port_;
    std::string symbol_;
    uint64_t    spread_ticks_;
    uint32_t    quote_size_;
    int         sock_fd_{-1};
    uint64_t    next_order_id_{1};
    uint64_t    next_sequence_{1};
    uint64_t    base_price_{15000 * 100}; // $150.00 in fixed-point (*100 for cents, *100 for ticks)
    uint64_t    bid_order_id_{0};
    uint64_t    ask_order_id_{0};

    std::atomic<uint64_t> orders_sent_{0};
    std::atomic<uint64_t> orders_acked_{0};
    std::atomic<uint64_t> orders_rejected_{0};
    std::atomic<uint64_t> trades_received_{0};

    void update_quotes();
    void cancel_order(uint64_t order_id);
    bool send_order(uint8_t side, uint64_t price, uint32_t quantity);
    bool recv_ack(uint64_t& assigned_order_id);
    bool recv_message(MessageHeader& hdr);
    static uint64_t now_ns();
};

} // namespace rtes
