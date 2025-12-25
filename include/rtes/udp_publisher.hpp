#pragma once

#include "rtes/types.hpp"
#include "rtes/mpmc_queue.hpp"
#include "rtes/matching_engine.hpp"

#include <string>
#include <thread>
#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <unistd.h>

namespace rtes {

// ═══════════════════════════════════════════════════════════════
//  Constants
// ═══════════════════════════════════════════════════════════════

inline constexpr size_t MD_BATCH_SIZE = 64;
inline constexpr size_t SENDMMSG_BATCH = 32;

// ═══════════════════════════════════════════════════════════════
//  UDP Protocol Messages
// ═══════════════════════════════════════════════════════════════

#pragma pack(push, 1)

enum UdpMessageType : uint32_t {
    BBO_UPDATE = 201,
    TRADE_UPDATE = 202,
    DEPTH_UPDATE = 203
};

struct UdpMessageHeader {
    uint32_t type;
    uint32_t length;
    uint64_t sequence;
    uint64_t timestamp_ns;
    
    UdpMessageHeader() = default;
    UdpMessageHeader(uint32_t t, uint32_t len, uint64_t seq, uint64_t ts)
        : type(t), length(len), sequence(seq), timestamp_ns(ts) {}
};

struct BBOUpdateMessage {
    UdpMessageHeader header;
    char symbol[8];
    uint64_t bid_price;
    uint64_t bid_quantity;
    uint64_t ask_price;
    uint64_t ask_quantity;
    
    BBOUpdateMessage() { std::memset(this, 0, sizeof(*this)); }
};

struct TradeUpdateMessage {
    UdpMessageHeader header;
    uint64_t trade_id;
    char symbol[8];
    uint64_t quantity;
    uint64_t price;
    uint8_t aggressor_side;
    
    TradeUpdateMessage() { std::memset(this, 0, sizeof(*this)); }
};

#pragma pack(pop)

// ═══════════════════════════════════════════════════════════════
//  Send Buffer
// ═══════════════════════════════════════════════════════════════

struct SendBuffer {
    uint8_t data[1400];
    size_t length{0};
};

// ═══════════════════════════════════════════════════════════════
//  Socket RAII Wrapper
// ═══════════════════════════════════════════════════════════════

struct UdpSocketWrapper {
    int fd_{-1};
    
    ~UdpSocketWrapper() { close(); }
    
    void reset(int fd) { 
        close(); 
        fd_ = fd; 
    }
    
    int get() const { return fd_; }
    
    void close() { 
        if (fd_ >= 0) { 
            ::close(fd_); 
            fd_ = -1; 
        } 
    }
};

// ═══════════════════════════════════════════════════════════════
//  UDP Publisher Class
// ═══════════════════════════════════════════════════════════════

class UdpPublisher {
public:
    UdpPublisher(const std::string& multicast_group, uint16_t port, 
                 MPMCQueue<MarketDataEvent>* input_queue);
    ~UdpPublisher();

    // Non-copyable
    UdpPublisher(const UdpPublisher&) = delete;
    UdpPublisher& operator=(const UdpPublisher&) = delete;

    void start();
    void stop();

    // Statistics
    size_t messages_sent() const { 
        return stats_atomic_.messages_sent.load(std::memory_order_relaxed); 
    }

private:
    // Configuration
    std::string multicast_group_;
    uint16_t port_;
    MPMCQueue<MarketDataEvent>* input_queue_;
    
    // Threading
    std::atomic<bool> running_{false};
    std::thread worker_thread_;
    
    // Network
    UdpSocketWrapper socket_fd_;
    struct sockaddr_in multicast_addr_{};

    // Local stats (no atomics in hot path)
    struct LocalStats {
        size_t messages_sent{0};
        size_t bytes_sent{0};
        size_t send_failures{0};
        size_t batches_sent{0};
        uint64_t next_sequence{1};
    } local_stats_;

    // Atomic stats (for cross-thread reads)
    struct AtomicStats {
        std::atomic<size_t> messages_sent{0};
        std::atomic<size_t> bytes_sent{0};
        std::atomic<size_t> send_failures{0};
        std::atomic<size_t> batches_sent{0};
    } stats_atomic_;

    // Internal methods
    bool setup_socket();
    void worker_loop();
    
    size_t drain_events(std::array<MarketDataEvent, MD_BATCH_SIZE>& events);
    size_t build_datagrams(const std::array<MarketDataEvent, MD_BATCH_SIZE>& events, 
                           size_t event_count, 
                           std::array<SendBuffer, SENDMMSG_BATCH>& buffers);
    size_t build_bbo_datagram(const MarketDataEvent& event, uint8_t* out);
    size_t build_trade_datagram(const MarketDataEvent& event, uint8_t* out);
    void batch_send(const std::array<SendBuffer, SENDMMSG_BATCH>& buffers, size_t count);
    
    void spin_wait();
    void maybe_flush_stats();
    void flush_stats();
};

} // namespace rtes
