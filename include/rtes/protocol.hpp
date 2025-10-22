#pragma once

#include "rtes/types.hpp"
#include "rtes/memory_safety.hpp"
#include <cstdint>
#include <cstring>

namespace rtes {

#pragma pack(push, 1)

struct MessageHeader {
    uint32_t type;
    uint32_t length;
    uint64_t sequence;
    uint64_t timestamp;
    uint32_t checksum;
    
    MessageHeader() = default;
    MessageHeader(uint32_t t, uint32_t len, uint64_t seq, uint64_t ts)
        : type(t), length(len), sequence(seq), timestamp(ts), checksum(0) {}
};

// Message types
enum MessageType : uint32_t {
    NEW_ORDER = 1,
    CANCEL_ORDER = 2,
    ORDER_ACK = 101,
    TRADE_REPORT = 102,
    HEARTBEAT = 200
};

// Client to Exchange messages
struct NewOrderMessage {
    MessageHeader header;
    uint64_t order_id;
    BoundedString<32> client_id;
    BoundedString<8> symbol;
    uint8_t side;        // 1=Buy, 2=Sell
    uint64_t quantity;
    uint64_t price;      // Fixed point (price * 10000)
    uint8_t order_type;  // 1=Market, 2=Limit
    
    NewOrderMessage() = default;
};

struct CancelOrderMessage {
    MessageHeader header;
    uint64_t order_id;
    BoundedString<32> client_id;
    BoundedString<8> symbol;
    
    CancelOrderMessage() = default;
};

// Exchange to Client messages
struct OrderAckMessage {
    MessageHeader header;
    uint64_t order_id;
    uint8_t status;      // 1=Accepted, 2=Rejected
    BoundedString<32> reason;
    
    OrderAckMessage() = default;
};

struct TradeMessage {
    MessageHeader header;
    uint64_t trade_id;
    uint64_t buy_order_id;
    uint64_t sell_order_id;
    BoundedString<8> symbol;
    uint64_t quantity;
    uint64_t price;
    uint64_t timestamp_ns;
    
    TradeMessage() = default;
};

struct HeartbeatMessage {
    MessageHeader header;
    uint64_t timestamp_ns;
    
    HeartbeatMessage() = default;
};

#pragma pack(pop)

// Protocol utilities
class ProtocolUtils {
public:
    static uint32_t calculate_checksum(const void* data, size_t length);
    static bool validate_checksum(const MessageHeader& header, const void* payload);
    static void set_checksum(MessageHeader& header, const void* payload);
    static uint64_t get_timestamp_ns();
};

} // namespace rtes