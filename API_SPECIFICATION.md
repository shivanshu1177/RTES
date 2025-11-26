# RTES API Specification

## Protocol Overview

RTES uses a binary protocol over TCP for order submission and a UDP multicast protocol for market data distribution.

## TCP Order Protocol

### Connection

- **Host**: Configurable (default: 0.0.0.0)
- **Port**: 8888 (configurable)
- **Protocol**: TCP with TLS 1.2+
- **Framing**: Length-prefixed binary messages

### Message Structure

All messages follow this structure:

```
┌──────────────────────────────────────────────────────┐
│                   Message Header                      │
│  - type (4 bytes)                                     │
│  - length (4 bytes)                                   │
│  - sequence (8 bytes)                                 │
│  - timestamp (8 bytes)                                │
│  - checksum (4 bytes)                                 │
├──────────────────────────────────────────────────────┤
│                   Message Payload                     │
│  (variable length, depends on message type)           │
└──────────────────────────────────────────────────────┘
```

### Message Header

```cpp
struct MessageHeader {
    uint32_t type;       // Message type identifier
    uint32_t length;     // Total message length (header + payload)
    uint64_t sequence;   // Sequence number
    uint64_t timestamp;  // Nanoseconds since epoch
    uint32_t checksum;   // CRC32 checksum of payload
};
// Size: 28 bytes
```

### Message Types

| Type | Value | Direction | Description |
|------|-------|-----------|-------------|
| NEW_ORDER | 1 | Client → Exchange | Submit new order |
| CANCEL_ORDER | 2 | Client → Exchange | Cancel existing order |
| ORDER_ACK | 101 | Exchange → Client | Order acknowledgment |
| TRADE_REPORT | 102 | Exchange → Client | Trade execution report |
| HEARTBEAT | 200 | Bidirectional | Keep-alive message |

## Client to Exchange Messages

### 1. NEW_ORDER (Type: 1)

Submit a new order to the exchange.

```cpp
struct NewOrderMessage {
    MessageHeader header;
    uint64_t order_id;           // Unique order ID (client-generated)
    char client_id[32];          // Client identifier
    char symbol[8];              // Trading symbol (e.g., "AAPL")
    uint8_t side;                // 1=Buy, 2=Sell
    uint64_t quantity;           // Order quantity
    uint64_t price;              // Price in fixed-point (price * 10000)
    uint8_t order_type;          // 1=Market, 2=Limit
};
// Total size: 28 (header) + 81 = 109 bytes
```

**Fields**:
- `order_id`: Must be unique per client, used for cancellation
- `client_id`: Authenticated client identifier (max 31 chars + null)
- `symbol`: Trading symbol, uppercase alphanumeric (max 7 chars + null)
- `side`: 1 for Buy, 2 for Sell
- `quantity`: Number of shares/contracts (1 to 10,000,000)
- `price`: Fixed-point price (multiply by 10,000). For $100.50, send 1005000
- `order_type`: 1 for Market order, 2 for Limit order

**Example** (Buy 100 AAPL at $150.25):
```
order_id: 12345
client_id: "trader001"
symbol: "AAPL"
side: 1 (Buy)
quantity: 100
price: 1502500 (150.25 * 10000)
order_type: 2 (Limit)
```

**Validation Rules**:
- `order_id` must be non-zero and unique
- `symbol` must be configured in exchange
- `quantity` must be > 0 and ≤ max_order_size
- `price` must be > 0 for limit orders
- `price` must be within price collar (±10% of last trade)

### 2. CANCEL_ORDER (Type: 2)

Cancel an existing order.

```cpp
struct CancelOrderMessage {
    MessageHeader header;
    uint64_t order_id;           // Order ID to cancel
    char client_id[32];          // Client identifier
    char symbol[8];              // Trading symbol
};
// Total size: 28 (header) + 48 = 76 bytes
```

**Fields**:
- `order_id`: ID of order to cancel (must exist)
- `client_id`: Must match order owner
- `symbol`: Must match order symbol

**Validation Rules**:
- Order must exist in order book
- Client must own the order
- Order must not be already filled or cancelled

## Exchange to Client Messages

### 3. ORDER_ACK (Type: 101)

Acknowledgment of order submission or cancellation.

```cpp
struct OrderAckMessage {
    MessageHeader header;
    uint64_t order_id;           // Order ID being acknowledged
    uint8_t status;              // 1=Accepted, 2=Rejected
    char reason[32];             // Reason for rejection (if applicable)
};
// Total size: 28 (header) + 41 = 69 bytes
```

**Status Codes**:
- `1`: Accepted - Order accepted and entered into book
- `2`: Rejected - Order rejected (see reason)

**Common Rejection Reasons**:
- "Invalid order ID"
- "Duplicate order"
- "Symbol not found"
- "Risk limit exceeded"
- "Price collar violation"
- "Invalid quantity"
- "Rate limit exceeded"
- "Order pool exhausted"

### 4. TRADE_REPORT (Type: 102)

Report of executed trade.

```cpp
struct TradeMessage {
    MessageHeader header;
    uint64_t trade_id;           // Unique trade ID
    uint64_t buy_order_id;       // Buy order ID
    uint64_t sell_order_id;      // Sell order ID
    char symbol[8];              // Trading symbol
    uint64_t quantity;           // Traded quantity
    uint64_t price;              // Execution price (fixed-point)
    uint64_t timestamp_ns;       // Trade timestamp (nanoseconds)
};
// Total size: 28 (header) + 56 = 84 bytes
```

**Fields**:
- `trade_id`: Unique identifier for this trade
- `buy_order_id`: Order ID of buyer
- `sell_order_id`: Order ID of seller
- `quantity`: Number of shares/contracts traded
- `price`: Execution price in fixed-point format
- `timestamp_ns`: Nanosecond timestamp of execution

### 5. HEARTBEAT (Type: 200)

Keep-alive message to maintain connection.

```cpp
struct HeartbeatMessage {
    MessageHeader header;
    uint64_t timestamp_ns;       // Current timestamp
};
// Total size: 28 (header) + 8 = 36 bytes
```

**Behavior**:
- Client should send heartbeat every 30 seconds
- Exchange responds with heartbeat
- Connection closed if no heartbeat for 60 seconds

## UDP Market Data Protocol

### Connection

- **Multicast Group**: 239.0.0.1 (configurable)
- **Port**: 9999 (configurable)
- **Protocol**: UDP multicast
- **Authentication**: HMAC-SHA256

### Message Structure

```
┌──────────────────────────────────────────────────────┐
│              Authenticated Header                     │
│  - sequence (8 bytes)                                 │
│  - hmac_size (4 bytes)                                │
├──────────────────────────────────────────────────────┤
│                   Market Data Payload                 │
│  (variable length)                                    │
├──────────────────────────────────────────────────────┤
│                   HMAC-SHA256                         │
│  (32 bytes)                                           │
└──────────────────────────────────────────────────────┘
```

### Market Data Types

#### 1. Best Bid/Offer (BBO)

```json
{
  "type": "BBO",
  "symbol": "AAPL",
  "bid_price": 150.25,
  "bid_quantity": 1000,
  "ask_price": 150.26,
  "ask_quantity": 500,
  "timestamp_ns": 1234567890123456789
}
```

#### 2. Trade Report

```json
{
  "type": "TRADE",
  "symbol": "AAPL",
  "trade_id": 12345,
  "price": 150.25,
  "quantity": 100,
  "side": "BUY",
  "timestamp_ns": 1234567890123456789
}
```

#### 3. Depth Update (Top 10 Levels)

```json
{
  "type": "DEPTH",
  "symbol": "AAPL",
  "bids": [
    {"price": 150.25, "quantity": 1000, "orders": 5},
    {"price": 150.24, "quantity": 2000, "orders": 8}
  ],
  "asks": [
    {"price": 150.26, "quantity": 500, "orders": 3},
    {"price": 150.27, "quantity": 1500, "orders": 6}
  ],
  "timestamp_ns": 1234567890123456789
}
```

## HTTP REST API

### Metrics Endpoint

**GET /metrics**

Returns Prometheus-formatted metrics.

```
# HELP rtes_orders_received_total Total orders received
# TYPE rtes_orders_received_total counter
rtes_orders_received_total 123456

# HELP rtes_order_latency_seconds Order processing latency
# TYPE rtes_order_latency_seconds summary
rtes_order_latency_seconds{quantile="0.5"} 0.000008
rtes_order_latency_seconds{quantile="0.99"} 0.000085
rtes_order_latency_seconds{quantile="0.999"} 0.000450
```

### Health Check Endpoint

**GET /health**

Returns system health status.

```json
{
  "status": "healthy",
  "timestamp": "2024-01-15T10:30:00Z",
  "components": {
    "tcp_gateway": "healthy",
    "risk_manager": "healthy",
    "matching_engine": "healthy",
    "market_data": "healthy"
  },
  "metrics": {
    "connections": 42,
    "orders_per_second": 15000,
    "avg_latency_us": 8.5
  }
}
```

**Status Values**:
- `healthy`: All systems operational
- `degraded`: Some non-critical issues
- `unhealthy`: Critical issues, not accepting orders

### Dashboard Endpoint

**GET /dashboard**

Returns HTML dashboard with real-time metrics.

## Authentication

### TLS Client Certificates

1. Client connects with TLS certificate
2. Exchange validates certificate chain
3. Extract client ID from certificate Common Name (CN)
4. Create session for authenticated client

### Session Tokens

For non-TLS connections (development only):

```
Authorization: Bearer <session_token>
```

Include in first 32 bytes of each message payload.

## Error Codes

### System Errors (1000-1999)

| Code | Name | Description |
|------|------|-------------|
| 1000 | SYSTEM_SHUTDOWN | Exchange is shutting down |
| 1001 | SYSTEM_CORRUPTED_STATE | Internal state corruption |
| 1002 | MEMORY_ALLOCATION_FAILED | Out of memory |

### Network Errors (2000-2999)

| Code | Name | Description |
|------|------|-------------|
| 2000 | NETWORK_CONNECTION_FAILED | Connection failed |
| 2001 | NETWORK_DISCONNECTED | Connection lost |
| 2002 | NETWORK_TIMEOUT | Operation timed out |

### Order Errors (3000-3999)

| Code | Name | Description |
|------|------|-------------|
| 3000 | ORDER_INVALID | Invalid order parameters |
| 3001 | ORDER_DUPLICATE | Duplicate order ID |
| 3002 | ORDER_NOT_FOUND | Order not found |
| 3003 | ORDER_ALREADY_FILLED | Order already filled |

### Risk Errors (4000-4999)

| Code | Name | Description |
|------|------|-------------|
| 4000 | RISK_LIMIT_EXCEEDED | Risk limit exceeded |
| 4001 | RISK_PRICE_COLLAR | Price outside collar |
| 4002 | RISK_POSITION_LIMIT | Position limit exceeded |
| 4003 | RISK_CREDIT_LIMIT | Credit limit exceeded |

### Validation Errors (6000-6999)

| Code | Name | Description |
|------|------|-------------|
| 6000 | INVALID_MESSAGE_TYPE | Unknown message type |
| 6001 | INVALID_MESSAGE_SIZE | Invalid message size |
| 6002 | INVALID_FIELD_VALUE | Invalid field value |
| 6003 | INVALID_FIELD_FORMAT | Invalid field format |

## Rate Limiting

### Per-Client Limits

- **Orders per second**: 1000 (configurable)
- **Connections per IP**: 10
- **Message size**: 8192 bytes max

### Backoff Strategy

When rate limited:
1. First violation: Warning in ORDER_ACK
2. Subsequent violations: Reject orders
3. Persistent violations: Disconnect client

## Checksum Calculation

CRC32 checksum of message payload:

```cpp
uint32_t calculate_checksum(const void* data, size_t length) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFF;
    
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return ~crc;
}
```

## HMAC Authentication (UDP)

HMAC-SHA256 of market data payload:

```cpp
HMAC(SHA256, secret_key, payload) → 32 bytes
```

Clients must:
1. Receive UDP message
2. Extract payload and HMAC
3. Compute HMAC of payload with shared secret
4. Compare with received HMAC (constant-time comparison)
5. Reject if mismatch

## Example Client Implementation

### Python Client

```python
import socket
import struct
import time

class RTESClient:
    def __init__(self, host='localhost', port=8888):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((host, port))
        self.sequence = 1
    
    def send_order(self, order_id, symbol, side, quantity, price, order_type):
        # Build message header
        msg_type = 1  # NEW_ORDER
        timestamp = int(time.time() * 1e9)
        
        # Build payload
        payload = struct.pack(
            'Q32s8sBQQB',
            order_id,
            b'client001',
            symbol.encode('utf-8'),
            side,
            quantity,
            int(price * 10000),
            order_type
        )
        
        # Calculate checksum
        checksum = self.crc32(payload)
        
        # Build header
        header = struct.pack(
            'IIQQI',
            msg_type,
            28 + len(payload),
            self.sequence,
            timestamp,
            checksum
        )
        
        # Send message
        self.sock.sendall(header + payload)
        self.sequence += 1
    
    def receive_ack(self):
        # Receive header
        header = self.sock.recv(28)
        msg_type, length, seq, ts, checksum = struct.unpack('IIQQI', header)
        
        # Receive payload
        payload_len = length - 28
        payload = self.sock.recv(payload_len)
        
        # Parse ORDER_ACK
        order_id, status = struct.unpack('QB', payload[:9])
        reason = payload[9:41].decode('utf-8').rstrip('\x00')
        
        return {
            'order_id': order_id,
            'status': status,
            'reason': reason
        }
    
    @staticmethod
    def crc32(data):
        crc = 0xFFFFFFFF
        for byte in data:
            crc ^= byte
            for _ in range(8):
                if crc & 1:
                    crc = (crc >> 1) ^ 0xEDB88320
                else:
                    crc >>= 1
        return ~crc & 0xFFFFFFFF

# Usage
client = RTESClient()
client.send_order(
    order_id=12345,
    symbol='AAPL',
    side=1,  # Buy
    quantity=100,
    price=150.25,
    order_type=2  # Limit
)
ack = client.receive_ack()
print(f"Order {ack['order_id']}: {ack['status']} - {ack['reason']}")
```

### C++ Client

See `tools/tcp_client.cpp` for full implementation.

## Performance Considerations

### Message Batching

For high-frequency trading:
- Batch multiple orders in single TCP send
- Use TCP_NODELAY to disable Nagle's algorithm
- Pre-allocate message buffers

### Connection Pooling

- Maintain persistent connections
- Implement connection health checks
- Automatic reconnection with exponential backoff

### Market Data Subscription

- Join UDP multicast group
- Set receive buffer size: `SO_RCVBUF = 262144`
- Use non-blocking I/O
- Process messages in separate thread

## Versioning

Current API version: **1.0.0**

Version included in:
- User-Agent header (HTTP)
- TLS certificate metadata
- Configuration file

Breaking changes will increment major version.
