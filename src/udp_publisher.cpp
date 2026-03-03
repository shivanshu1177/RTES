#include "rtes/udp_publisher.hpp"
#include "rtes/logger.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>

namespace rtes {

UdpPublisher::UdpPublisher(const std::string& multicast_group, uint16_t port,
                          MPMCQueue<MarketDataEvent>* input_queue)
    : multicast_group_(multicast_group), port_(port), input_queue_(input_queue) {
}

UdpPublisher::~UdpPublisher() {
    stop();
}

void UdpPublisher::start() {
    if (running_.load()) return;
    
    if (!setup_multicast_socket()) {
        LOG_ERROR("Failed to setup multicast socket");
        return;
    }
    
    running_.store(true);
    worker_thread_ = std::thread(&UdpPublisher::worker_loop, this);
    
    LOG_INFO("UDP publisher started on " + multicast_group_ + ":" + std::to_string(port_));
}

void UdpPublisher::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
    
    LOG_INFO("UDP publisher stopped");
}

bool UdpPublisher::setup_multicast_socket() {
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ < 0) {
        LOG_ERROR("Failed to create UDP socket");
        return false;
    }
    
    // Set socket buffer size
    int buffer_size = 262144;  // 256KB
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size)) < 0) {
        LOG_WARN("Failed to set socket send buffer size");
    }
    
    // Setup multicast address
    std::memset(&multicast_addr_, 0, sizeof(multicast_addr_));
    multicast_addr_.sin_family = AF_INET;
    multicast_addr_.sin_port = htons(port_);
    
    if (inet_pton(AF_INET, multicast_group_.c_str(), &multicast_addr_.sin_addr) <= 0) {
        LOG_ERROR("Invalid multicast address: " + multicast_group_);
        return false;
    }
    
    // Set multicast TTL
    int ttl = 1;  // Local network only
    if (setsockopt(socket_fd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
        LOG_WARN("Failed to set multicast TTL");
    }
    
    return true;
}

void UdpPublisher::worker_loop() {
    MarketDataEvent event;
    
    while (running_.load()) {
        if (input_queue_->pop(event)) {
            process_market_data_event(event);
        } else {
            std::this_thread::yield();
        }
    }
}

void UdpPublisher::process_market_data_event(const MarketDataEvent& event) {
    switch (event.type) {
        case MarketDataEvent::BBO_UPDATE:
            send_bbo_update(event);
            break;
            
        case MarketDataEvent::TRADE:
            send_trade_update(event);
            break;
    }
}

void UdpPublisher::send_bbo_update(const MarketDataEvent& event) {
    BBOUpdateMessage msg;
    msg.header = UdpMessageHeader(BBO_UPDATE, sizeof(BBOUpdateMessage),
                                 next_sequence_.fetch_add(1), get_timestamp_ns());
    
    std::strncpy(msg.symbol, event.symbol, sizeof(msg.symbol));
    msg.bid_price = event.bbo.bid_price;
    msg.bid_quantity = event.bbo.bid_quantity;
    msg.ask_price = event.bbo.ask_price;
    msg.ask_quantity = event.bbo.ask_quantity;
    
    send_message(&msg, sizeof(msg));
}

void UdpPublisher::send_trade_update(const MarketDataEvent& event) {
    TradeUpdateMessage msg;
    msg.header = UdpMessageHeader(TRADE_UPDATE, sizeof(TradeUpdateMessage),
                                 next_sequence_.fetch_add(1), get_timestamp_ns());
    
    msg.trade_id = event.trade.id;
    std::strncpy(msg.symbol, event.trade.symbol, sizeof(msg.symbol));
    msg.quantity = event.trade.quantity;
    msg.price = event.trade.price;
    
    // Determine aggressor side (simplified - assume buy order is aggressor)
    msg.aggressor_side = 1;  // BUY
    
    send_message(&msg, sizeof(msg));
}

bool UdpPublisher::send_message(const void* message, size_t size) {
    ssize_t sent = sendto(socket_fd_, message, size, 0,
                         reinterpret_cast<const struct sockaddr*>(&multicast_addr_),
                         sizeof(multicast_addr_));
    
    if (sent == static_cast<ssize_t>(size)) {
        messages_sent_.fetch_add(1);
        bytes_sent_.fetch_add(size);
        return true;
    } else {
        LOG_WARN("Failed to send UDP message");
        return false;
    }
}

uint64_t UdpPublisher::get_timestamp_ns() const {
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
}

} // namespace rtes