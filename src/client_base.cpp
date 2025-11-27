#include "rtes/client_base.hpp"
#include "rtes/logger.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

namespace rtes {

ClientBase::ClientBase(const std::string& host, uint16_t port, uint32_t client_id)
    : host_(host), port_(port), client_id_(client_id), rng_(std::random_device{}()) {
}

ClientBase::~ClientBase() {
    disconnect();
}

bool ClientBase::connect() {
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        return false;
    }
    
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    
    if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    
    if (::connect(socket_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    
    return true;
}

void ClientBase::disconnect() {
    running_.store(false);
    
    if (receiver_thread_.joinable()) {
        receiver_thread_.join();
    }
    
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

void ClientBase::run(std::chrono::seconds duration) {
    if (socket_fd_ < 0) return;
    
    running_.store(true);
    receiver_thread_ = std::thread(&ClientBase::receiver_loop, this);
    
    on_start();
    
    auto start_time = std::chrono::steady_clock::now();
    auto end_time = start_time + duration;
    
    while (running_.load() && std::chrono::steady_clock::now() < end_time) {
        on_tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    on_stop();
    running_.store(false);
    
    if (receiver_thread_.joinable()) {
        receiver_thread_.join();
    }
}

bool ClientBase::send_new_order(const std::string& symbol, Side side, uint64_t quantity, uint64_t price) {
    NewOrderMessage msg;
    msg.header = MessageHeader(NEW_ORDER, sizeof(NewOrderMessage), ++sequence_,
                              ProtocolUtils::get_timestamp_ns());
    msg.order_id = generate_order_id();
    msg.client_id = std::to_string(client_id_).c_str();
    msg.symbol = symbol.c_str();
    msg.side = static_cast<uint8_t>(side);
    msg.quantity = quantity;
    msg.price = price;
    msg.order_type = static_cast<uint8_t>(OrderType::LIMIT);
    
    ProtocolUtils::set_checksum(msg.header, &msg.order_id);
    
    if (send_message(&msg, sizeof(msg))) {
        orders_sent_.fetch_add(1);
        return true;
    }
    
    return false;
}

bool ClientBase::send_cancel_order(uint64_t order_id, const std::string& symbol) {
    CancelOrderMessage msg;
    msg.header = MessageHeader(CANCEL_ORDER, sizeof(CancelOrderMessage), ++sequence_,
                              ProtocolUtils::get_timestamp_ns());
    msg.order_id = order_id;
    msg.client_id = std::to_string(client_id_).c_str();
    msg.symbol = symbol.c_str();
    
    ProtocolUtils::set_checksum(msg.header, &msg.order_id);
    
    return send_message(&msg, sizeof(msg));
}

double ClientBase::random_double(double min, double max) {
    std::uniform_real_distribution<double> dist(min, max);
    return dist(rng_);
}

int ClientBase::random_int(int min, int max) {
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng_);
}

void ClientBase::receiver_loop() {
    while (running_.load()) {
        MessageHeader header;
        if (!receive_message(&header, sizeof(header))) {
            continue;
        }
        
        if (header.type == ORDER_ACK) {
            OrderAckMessage ack;
            std::memcpy(&ack.header, &header, sizeof(header));
            
            size_t remaining = sizeof(ack) - sizeof(header);
            if (receive_message(&ack.order_id, remaining)) {
                if (ack.status == 1) {
                    orders_acked_.fetch_add(1);
                } else {
                    orders_rejected_.fetch_add(1);
                }
                on_order_ack(ack);
            }
        } else if (header.type == TRADE_REPORT) {
            TradeMessage trade;
            std::memcpy(&trade.header, &header, sizeof(header));
            
            size_t remaining = sizeof(trade) - sizeof(header);
            if (receive_message(&trade.trade_id, remaining)) {
                trades_received_.fetch_add(1);
                on_trade(trade);
            }
        }
    }
}

bool ClientBase::send_message(const void* message, size_t size) {
    if (socket_fd_ < 0) return false;
    
    ssize_t sent = send(socket_fd_, message, size, 0);
    return sent == static_cast<ssize_t>(size);
}

bool ClientBase::receive_message(void* buffer, size_t size) {
    if (socket_fd_ < 0) return false;
    
    size_t total_received = 0;
    while (total_received < size && running_.load()) {
        ssize_t received = recv(socket_fd_, static_cast<char*>(buffer) + total_received,
                              size - total_received, 0);
        if (received <= 0) {
            return false;
        }
        total_received += received;
    }
    
    return total_received == size;
}

} // namespace rtes