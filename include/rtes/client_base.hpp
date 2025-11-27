#pragma once

#include "rtes/protocol.hpp"
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <random>

namespace rtes {

class ClientBase {
public:
    ClientBase(const std::string& host, uint16_t port, uint32_t client_id);
    virtual ~ClientBase();
    
    bool connect();
    void disconnect();
    void run(std::chrono::seconds duration);
    
    // Statistics
    uint64_t orders_sent() const { return orders_sent_.load(); }
    uint64_t orders_acked() const { return orders_acked_.load(); }
    uint64_t orders_rejected() const { return orders_rejected_.load(); }
    uint64_t trades_received() const { return trades_received_.load(); }
    
    // Order management (public for testing tools)
    bool send_new_order(const std::string& symbol, Side side, uint64_t quantity, uint64_t price);
    bool send_cancel_order(uint64_t order_id, const std::string& symbol);

protected:
    std::string host_;
    uint16_t port_;
    uint32_t client_id_;
    int socket_fd_{-1};
    
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> orders_sent_{0};
    std::atomic<uint64_t> orders_acked_{0};
    std::atomic<uint64_t> orders_rejected_{0};
    std::atomic<uint64_t> trades_received_{0};
    
    uint64_t next_order_id_{1};
    uint64_t sequence_{0};
    
    std::mt19937 rng_;
    
    // Virtual strategy methods
    virtual void on_start() {}
    virtual void on_tick() = 0;
    virtual void on_order_ack(const OrderAckMessage& ack) {}
    virtual void on_trade(const TradeMessage& trade) {}
    virtual void on_stop() {}
    
    // Utility methods
    uint64_t generate_order_id() { return next_order_id_++; }
    double random_double(double min, double max);
    int random_int(int min, int max);
    
private:
    std::thread receiver_thread_;
    
    void receiver_loop();
    bool send_message(const void* message, size_t size);
    bool receive_message(void* buffer, size_t size);
};

} // namespace rtes