#pragma once

#include "rtes/market_data.hpp"
#include "rtes/matching_engine.hpp"
#include "rtes/mpmc_queue.hpp"
#include <thread>
#include <atomic>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>

namespace rtes {

class UdpPublisher {
public:
    UdpPublisher(const std::string& multicast_group, uint16_t port, 
                MPMCQueue<MarketDataEvent>* input_queue);
    ~UdpPublisher();
    
    void start();
    void stop();
    
    // Statistics
    uint64_t messages_sent() const { return messages_sent_.load(); }
    uint64_t bytes_sent() const { return bytes_sent_.load(); }

private:
    std::string multicast_group_;
    uint16_t port_;
    MPMCQueue<MarketDataEvent>* input_queue_;
    
    // Threading
    std::thread worker_thread_;
    std::atomic<bool> running_{false};
    
    // Network
    int socket_fd_{-1};
    struct sockaddr_in multicast_addr_{};
    
    // Statistics
    std::atomic<uint64_t> messages_sent_{0};
    std::atomic<uint64_t> bytes_sent_{0};
    std::atomic<uint64_t> next_sequence_{1};
    
    // Network setup
    bool setup_multicast_socket();
    
    // Worker thread
    void worker_loop();
    
    // Message processing
    void process_market_data_event(const MarketDataEvent& event);
    void send_bbo_update(const MarketDataEvent& event);
    void send_trade_update(const MarketDataEvent& event);
    
    // UDP sending
    bool send_message(const void* message, size_t size);
    uint64_t get_timestamp_ns() const;
};

} // namespace rtes