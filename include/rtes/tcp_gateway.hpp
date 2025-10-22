#pragma once

#include "rtes/protocol.hpp"
#include "rtes/risk_manager.hpp"
#include "rtes/memory_pool.hpp"
#include "rtes/security_utils.hpp"
#include "rtes/memory_safety.hpp"
#include "rtes/network_security.hpp"
#include "rtes/thread_safety.hpp"
#include <thread>
#include <atomic>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <sys/socket.h>
#include <sys/epoll.h>

namespace rtes {

class ClientConnection {
public:
    explicit ClientConnection(int fd);
    ~ClientConnection();
    
    int get_fd() const { return fd_.get(); }
    bool is_connected() const { return connected_.load(); }
    
    // Memory-safe I/O
    ssize_t read_data_safe(void* buffer, size_t size);
    ssize_t write_data_safe(const void* buffer, size_t size);
    
    // Message handling with bounds checking
    bool has_complete_message() const;
    bool read_message_safe(FixedSizeBuffer<8192>& buffer);
    bool write_message_safe(const void* message, size_t size);
    
    void disconnect();

private:
    FileDescriptor fd_;
    std::atomic<bool> connected_{true};
    
    // Memory-safe buffers
    static constexpr size_t MAX_MESSAGE_SIZE = 8192;
    FixedSizeBuffer<MAX_MESSAGE_SIZE> read_buffer_;
    FixedSizeBuffer<MAX_MESSAGE_SIZE> write_buffer_;
    size_t write_offset_{0};
    
    bool setup_socket();
    bool validate_message_header(const void* data, size_t size) const;
};

class TcpGateway {
public:
    explicit TcpGateway(uint16_t port, RiskManager* risk_manager, OrderPool* order_pool);
    ~TcpGateway();
    
    void start();
    void stop();
    
    // Statistics
    uint64_t connections_accepted() const { return connections_accepted_.load(); }
    uint64_t messages_received() const { return messages_received_.load(); }
    uint64_t messages_sent() const { return messages_sent_.load(); }

private:
    uint16_t port_;
    RiskManager* risk_manager_;
    OrderPool* order_pool_;
    std::unique_ptr<SecureNetworkLayer> secure_network_;
    
    // Threading
    std::thread acceptor_thread_;
    std::thread worker_thread_;
    std::atomic<bool> running_{false};
    
    // Network with RAII
    FileDescriptor listen_fd_;
    FileDescriptor epoll_fd_;
    
    // Client connections with thread safety
    std::unordered_map<int, std::shared_ptr<ClientConnection>> connections_ GUARDED_BY(connections_mutex_);
    mutable std::mutex connections_mutex_;
    
    // Work drainage for shutdown
    WorkDrainer work_drainer_{"TcpGateway"};
    
    // Statistics
    std::atomic<uint64_t> connections_accepted_{0};
    std::atomic<uint64_t> messages_received_{0};
    std::atomic<uint64_t> messages_sent_{0};
    std::atomic<uint64_t> next_sequence_{1};
    
    // Network setup
    bool setup_listen_socket();
    bool setup_epoll();
    
    // Thread functions
    void acceptor_loop();
    void worker_loop();
    
    // Connection management
    void accept_connection();
    void handle_client_data(int client_fd);
    void remove_connection(int client_fd) EXCLUDES(connections_mutex_);
    
    // Message processing
    void process_message_safe(ClientConnection* conn, const FixedSizeBuffer<8192>& buffer);
    void handle_new_order(ClientConnection* conn, const NewOrderMessage& msg);
    void handle_cancel_order(ClientConnection* conn, const CancelOrderMessage& msg);
    
    // Secure message processing with authentication
    void handle_new_order_secure(ClientConnection* conn, const NewOrderMessage& msg, const AuthContext& ctx);
    void handle_cancel_order_secure(ClientConnection* conn, const CancelOrderMessage& msg, const AuthContext& ctx);
    
    // Response sending
    void send_order_ack(ClientConnection* conn, uint64_t order_id, uint8_t status, const char* reason);
    void send_trade_report(ClientConnection* conn, const Trade& trade);
};

} // namespace rtes