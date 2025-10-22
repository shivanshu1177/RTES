#pragma once

#include <cstdint>
#include <chrono>
#include "memory_safety.hpp"

namespace rtes {

using OrderID = uint64_t;
using ClientID = BoundedString<32>;
using Price = uint64_t;  // Fixed point: price * 10000
using Quantity = uint64_t;
using TradeID = uint64_t;
using SequenceNumber = uint64_t;

enum class Side : uint8_t {
    BUY = 1,
    SELL = 2
};

enum class OrderType : uint8_t {
    MARKET = 1,
    LIMIT = 2
};

enum class OrderStatus : uint8_t {
    PENDING = 0,
    ACCEPTED = 1,
    REJECTED = 2,
    FILLED = 3,
    PARTIALLY_FILLED = 4,
    CANCELLED = 5
};

struct Order {
    OrderID id;
    BoundedString<32> client_id;
    BoundedString<8> symbol;
    Side side;
    OrderType type;
    Quantity quantity;
    Quantity remaining_quantity;
    Price price;
    OrderStatus status;
    std::chrono::steady_clock::time_point timestamp;
    
    Order() = default;
    Order(OrderID id, const BoundedString<32>& client_id, const char* sym, Side s, OrderType t, 
          Quantity qty, Price p) 
        : id(id), client_id(client_id), side(s), type(t), 
          quantity(qty), remaining_quantity(qty), price(p), 
          status(OrderStatus::PENDING), timestamp(std::chrono::steady_clock::now()) {
        symbol.assign(sym);
    }
};

struct Trade {
    TradeID id;
    OrderID buy_order_id;
    OrderID sell_order_id;
    BoundedString<8> symbol;
    Quantity quantity;
    Price price;
    std::chrono::steady_clock::time_point timestamp;
    
    Trade(TradeID id, OrderID buy_id, OrderID sell_id, const char* sym, 
          Quantity qty, Price p)
        : id(id), buy_order_id(buy_id), sell_order_id(sell_id), 
          quantity(qty), price(p), timestamp(std::chrono::steady_clock::now()) {
        symbol.assign(sym);
    }
};

} // namespace rtes