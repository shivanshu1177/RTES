#pragma once

#include "rtes/protocol.hpp"
#include "rtes/risk_manager.hpp"
#include "rtes/memory_pool.hpp"
#include "rtes/network_security.hpp"
#include "rtes/thread_safety.hpp"

#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include <cstring>

namespace rtes {

// Forward declarations
struct ConnectionState;

class TcpGateway {
public:
    explicit TcpGateway(uint16_t port, RiskManager* risk_manager, OrderPool* order_pool);
    ~TcpGateway();
    
    void start();
    void stop();
    
    // Statistics
    uint64_t connections_accepted() const { return stats_atomic_.connections_accepted.load(); }
    uint64_t messages_received() const { return stats_atomic_.messages_received.load(); }
    uint64_t messages_sent() const { return stats_atomic_.messages_sent.load(); }

private:
    uint16_t port_;
    RiskManager* risk_manager_;
    OrderPool* order_pool_;
    std::unique_ptr<SecureNetworkLayer> secure_network_;
    
    // Threading
    std::thread acceptor_thread_;
    std::thread worker_thread_;
    std::atomic<bool> running_{false};
    
    // Network File Descriptors
    FileDescriptor listen_fd_;
    FileDescriptor epoll_fd_;
    
    // Client connections (Lock-free vector indexed by FD)
    std::vector<std::unique_ptr<ConnectionState>> connections_;
    
    // ── Local Statistics (No atomics in hot path) ──
    struct LocalStats {
        uint64_t connections_accepted{0};
        uint64_t messages_received{0};
        uint64_t messages_sent{0};
        uint64_t orders_rejected{0};
        uint64_t pool_exhausted{0};
        uint64_t disconnections{0};
        uint64_t next_sequence{1};
    } local_stats_;

    // ── Atomic Statistics (For cross-thread monitoring) ──
    struct AtomicStats {
        std::atomic<uint64_t> connections_accepted{0};
        std::atomic<uint64_t> messages_received{0};
        std::atomic<uint64_t> messages_sent{0};
        std::atomic<uint64_t> disconnections{0};
    } stats_atomic_;
    
    // Network setup
    bool setup_listen_socket();
    bool setup_epoll();
    
    // Thread loops
    void acceptor_loop();
    void worker_loop();
    
    // Connection management
    void handle_client_data(int client_fd);
    void remove_connection(int client_fd);
    
    // Message processing
    bool try_process_message(ConnectionState& conn);
    void process_message(ConnectionState& conn, const uint8_t* data, size_t length);
    void handle_new_order(ConnectionState& conn, const uint8_t* data, size_t length);
    void handle_cancel_order(ConnectionState& conn, const uint8_t* data, size_t length);
    
    // Responses
    void send_ack(ConnectionState& conn, uint64_t order_id, uint8_t status, const char* reason);
    void send_reject(ConnectionState& conn, uint64_t order_id, const char* reason);

    // Maintenance
    void maybe_flush_stats();
    void flush_stats();
};

// ═══════════════════════════════════════════════════════════════
//  Internal Types for TCP Gateway (moved from .cpp)
// ═══════════════════════════════════════════════════════════════

class ReadBuffer {
public:
    ReadBuffer() = default;

    [[nodiscard]] bool append(const uint8_t* data, size_t len) {
        if (used_ + len > CAPACITY) return false;
        std::memcpy(buf_ + used_, data, len);
        used_ += len;
        return true;
    }

    void consume(size_t n) {
        if (n >= used_) {
            used_ = 0;
        } else {
            used_ -= n;
            std::memmove(buf_, buf_ + n, used_);
        }
    }

    [[nodiscard]] const uint8_t* data() const { return buf_; }
    [[nodiscard]] size_t size() const { return used_; }
    [[nodiscard]] size_t remaining() const { return CAPACITY - used_; }
    [[nodiscard]] uint8_t* write_ptr() { return buf_ + used_; }

    void advance_write(size_t n) { used_ += n; }
    void clear() { used_ = 0; }

private:
    static constexpr size_t CAPACITY = 8192; // READ_BUFFER_SIZE
    uint8_t buf_[CAPACITY]{};
    size_t  used_{0};
};

struct ConnectionState {
    FileDescriptor fd;
    ReadBuffer     read_buf;
    std::string    client_id;      
    bool           authenticated{false};
    bool           connected{true};
    Timestamp      connect_time{0};
    uint32_t       messages_received{0};

    explicit ConnectionState(int raw_fd);
    void setup_socket();
    void disconnect();
};

} // namespace rtes