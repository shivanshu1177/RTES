#pragma once

#include "rtes/types.hpp"
#include <cstdint>
#include <cstring>

namespace rtes {

#pragma pack(push, 1)

// UDP Market Data Message Types
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
    uint8_t aggressor_side;  // 1=Buy, 2=Sell
    
    TradeUpdateMessage() { std::memset(this, 0, sizeof(*this)); }
};

struct DepthLevel {
    uint64_t price;
    uint64_t quantity;
    uint32_t order_count;
};

struct DepthUpdateMessage {
    UdpMessageHeader header;
    char symbol[8];
    uint8_t num_bid_levels;
    uint8_t num_ask_levels;
    DepthLevel levels[20];  // Max 10 bids + 10 asks
    
    DepthUpdateMessage() { std::memset(this, 0, sizeof(*this)); }
};

#pragma pack(pop)

} // namespace rtes