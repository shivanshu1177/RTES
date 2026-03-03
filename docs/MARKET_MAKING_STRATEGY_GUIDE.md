# Market Making Strategy - Complete Lifecycle Guide

**Purpose**: Detailed explanation of market making strategy with code walkthrough  
**Usage**: Demonstrate systematic market making during Goldman Sachs interview  
**Duration**: 5-7 minute explanation

---

## 💹 MARKET MAKING OVERVIEW

### **What is Market Making?**

Market making is a trading strategy that provides liquidity by continuously quoting bid and ask prices:

```
Market Maker Quotes:
├── BID:  $149.90 (100 shares)  ← Willing to BUY
└── ASK:  $150.10 (100 shares)  ← Willing to SELL

Spread: $0.20 (20 cents)
Profit: Capture spread when both sides fill
```

**Economics**:
- **Revenue**: Bid-ask spread ($0.20 per round-trip)
- **Costs**: Adverse selection, inventory risk, operational costs
- **Goal**: Maximize spread capture while managing risk

---

## 🔄 MARKET MAKER LIFECYCLE

```
┌─────────────────────────────────────────────────────────────┐
│  MARKET MAKER LIFECYCLE                                      │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  1. INITIALIZATION        Set base price, spread, size      │
│  2. QUOTE BOTH SIDES      Send bid + ask orders             │
│  3. WAIT FOR FILLS        Monitor order book                │
│  4. ON FILL               Cancel opposite side              │
│  5. ADJUST PRICE          Update base price (price discovery)│
│  6. REQUOTE               Send new bid + ask                │
│  7. REPEAT                Continuous loop                   │
│                                                              │
│  RISK MANAGEMENT:                                           │
│  - Position limits        Stop quoting if inventory too high│
│  - Spread widening        Increase spread during volatility │
│  - Rate limiting          Avoid exchange penalties          │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

---

## 📝 STEP-BY-STEP MARKET MAKING SEQUENCE

### **STEP 1: Initialization** (0ms)

**File**: `include/rtes/strategies.hpp`  
**Lines**: 9-30

```cpp
class MarketMakerStrategy : public ClientBase {
public:
    MarketMakerStrategy(const std::string& host, uint16_t port, uint32_t client_id,
                       const std::string& symbol, uint64_t spread_ticks = 10);

private:
    std::string symbol_;           // "AAPL"
    uint64_t spread_ticks_;        // 10 ticks = $0.10
    uint64_t base_price_{150000};  // $150.00 (fixed-point: price * 1000)
    uint64_t quote_size_{100};     // 100 shares per side
    
    uint64_t bid_order_id_{0};     // Track active bid
    uint64_t ask_order_id_{0};     // Track active ask
};
```

**What Happens**:
- ✅ Sets symbol (AAPL)
- ✅ Sets spread (10 ticks = $0.10 each side)
- ✅ Sets base price ($150.00)
- ✅ Sets quote size (100 shares)

**Interview Talking Point**:
> "The market maker initializes with a base price of $150.00 and a spread of 10 ticks ($0.10) on each side. This means we'll quote bid at $149.90 and ask at $150.10, capturing a 20-cent spread when both sides fill."

---

### **STEP 2: Start Strategy** (10ms)

**File**: `src/strategies.cpp`  
**Lines**: 12-15

```cpp
void MarketMakerStrategy::on_start() {
    LOG_INFO("Market maker started for " + symbol_);
    update_quotes();  // Send initial quotes
}
```

**What Happens**:
- ✅ Logs startup message
- ✅ Calls `update_quotes()` to send initial bid/ask

**Interview Talking Point**:
> "When the strategy starts, it immediately calls update_quotes() to send the initial bid and ask orders. This establishes our presence in the order book."

---

### **STEP 3: Send Initial Quotes** (50μs)

**File**: `src/strategies.cpp`  
**Lines**: 43-61

```cpp
void MarketMakerStrategy::update_quotes() {
    cancel_existing_orders();  // Cancel any stale quotes
    
    // Add some randomness to price (simulate market movement)
    int price_adjustment = random_int(-5, 5);
    uint64_t adjusted_price = base_price_ + price_adjustment;
    
    // Calculate bid/ask around adjusted price
    uint64_t bid_price = adjusted_price - spread_ticks_;  // $149.90
    uint64_t ask_price = adjusted_price + spread_ticks_;  // $150.10
    
    // Send new quotes
    if (send_new_order(symbol_, Side::BUY, quote_size_, bid_price)) {
        bid_order_id_ = next_order_id_ - 1;  // Track bid order ID
    }
    
    if (send_new_order(symbol_, Side::SELL, quote_size_, ask_price)) {
        ask_order_id_ = next_order_id_ - 1;  // Track ask order ID
    }
}
```

**What Happens**:
1. ✅ Cancels any existing orders (clean slate)
2. ✅ Adds random price adjustment (-5 to +5 ticks)
3. ✅ Calculates bid = base - spread ($149.90)
4. ✅ Calculates ask = base + spread ($150.10)
5. ✅ Sends BUY order at $149.90 (100 shares)
6. ✅ Sends SELL order at $150.10 (100 shares)
7. ✅ Tracks order IDs for later cancellation

**Order Book State After Quoting**:
```
Order Book (AAPL):
├── ASK: $150.10 (100 shares) ← Our ask order
├── ASK: $150.15 (200 shares)
├── ASK: $150.20 (150 shares)
├── ─────────────────────────
├── BID: $149.90 (100 shares) ← Our bid order
├── BID: $149.85 (150 shares)
└── BID: $149.80 (200 shares)

Spread: $0.20 (we're at the inside market)
```

**Interview Talking Point**:
> "update_quotes() calculates bid and ask prices around the base price. For a base of $150.00 and spread of 10 ticks, we quote bid at $149.90 and ask at $150.10. We send both orders and track their IDs so we can cancel them later when we need to requote."

---

### **STEP 4: Order Acknowledgment** (100μs)

**File**: `src/strategies.cpp`  
**Lines**: 23-31

```cpp
void MarketMakerStrategy::on_order_ack(const OrderAckMessage& ack) {
    if (ack.status == 1) {  // Accepted
        if (ack.order_id == bid_order_id_) {
            LOG_INFO("Bid order accepted: " + std::to_string(ack.order_id));
        } else if (ack.order_id == ask_order_id_) {
            LOG_INFO("Ask order accepted: " + std::to_string(ack.order_id));
        }
    }
}
```

**What Happens**:
- ✅ Receives acknowledgment from exchange
- ✅ Confirms bid order accepted (ID 1001)
- ✅ Confirms ask order accepted (ID 1002)
- ✅ Orders now live in order book

**Interview Talking Point**:
> "The exchange sends back acknowledgments confirming our orders are accepted. At this point, we're live in the order book, providing liquidity on both sides."

---

### **STEP 5: Wait for Fills** (Variable)

**Market Maker is Now Passive**:
```
Time: 10:00:00.000  → Quotes sent (bid $149.90, ask $150.10)
Time: 10:00:00.100  → Orders acknowledged
Time: 10:00:05.234  → Waiting... (no fills yet)
Time: 10:00:12.567  → Waiting... (still no fills)
Time: 10:00:18.891  → BID FILLED! (someone sold to us at $149.90)
```

**What Happens**:
- ✅ Market maker waits passively
- ✅ Orders sit in book providing liquidity
- ✅ Other traders can hit our quotes
- ✅ We earn rebates for providing liquidity (maker fee)

**Interview Talking Point**:
> "After quoting, we wait passively. Our orders sit in the book providing liquidity. When an aggressive trader sends a market order or crossing limit order, they'll hit our quotes. We earn maker rebates for providing this liquidity."

---

### **STEP 6: Bid Order Filled** (Event-Driven)

**Scenario**: Aggressive seller hits our bid at $149.90

```
Incoming Order:
├── Type: MARKET SELL
├── Quantity: 50 shares
├── Matches: Our bid at $149.90
└── Result: We BUY 50 shares at $149.90

Our Position:
├── Before: 0 shares (flat)
├── After: +50 shares (long)
└── Inventory Risk: Now exposed to price drops
```

**Trade Notification**:

**File**: `src/strategies.cpp`  
**Lines**: 33-41

```cpp
void MarketMakerStrategy::on_trade(const TradeMessage& trade) {
    // Adjust base price based on trades (price discovery)
    base_price_ = trade.price;
    
    // Cancel and replace quotes after being hit
    cancel_existing_orders();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    update_quotes();
}
```

**What Happens**:
1. ✅ Receives trade notification (filled at $149.90)
2. ✅ Updates base_price to $149.90 (price discovery)
3. ✅ Cancels existing orders (both bid and ask)
4. ✅ Waits 10ms (avoid quote spam)
5. ✅ Calls update_quotes() to requote

**Why Cancel Both Sides?**
- Our bid was hit → we're now LONG 50 shares
- If ask also fills → we'd be flat (ideal)
- But if market drops → we lose money on long position
- Cancel ask, requote with inventory adjustment

**Interview Talking Point**:
> "When our bid is hit, we immediately cancel both sides and requote. This is inventory management—we adjust our quotes based on our position. The on_trade() callback updates our base price to the last trade price, which is price discovery."

---

### **STEP 7: Cancel Existing Orders** (200μs)

**File**: `src/strategies.cpp`  
**Lines**: 63-73

```cpp
void MarketMakerStrategy::cancel_existing_orders() {
    if (bid_order_id_ > 0) {
        send_cancel_order(bid_order_id_, symbol_);
        bid_order_id_ = 0;  // Clear tracked ID
    }
    
    if (ask_order_id_ > 0) {
        send_cancel_order(ask_order_id_, symbol_);
        ask_order_id_ = 0;  // Clear tracked ID
    }
}
```

**What Happens**:
- ✅ Sends cancel for bid order (ID 1001)
- ✅ Sends cancel for ask order (ID 1002)
- ✅ Clears tracked order IDs
- ✅ Prepares for new quotes

**Order Book State After Cancel**:
```
Order Book (AAPL):
├── ASK: $150.15 (200 shares)  ← Our ask removed
├── ASK: $150.20 (150 shares)
├── ─────────────────────────
├── BID: $149.85 (150 shares)  ← Our bid removed
└── BID: $149.80 (200 shares)

We're no longer in the book (temporarily)
```

**Interview Talking Point**:
> "cancel_existing_orders() sends cancel requests for both our bid and ask. This removes us from the order book temporarily while we calculate new quotes. The 10ms sleep prevents us from spamming the exchange with cancel-replace cycles."

---

### **STEP 8: Requote with Adjusted Price** (50μs)

**Price Discovery**: Base price updated from $150.00 → $149.90

```cpp
// In update_quotes(), called after on_trade()
uint64_t adjusted_price = base_price_;  // Now $149.90 (not $150.00)

uint64_t bid_price = adjusted_price - spread_ticks_;  // $149.80
uint64_t ask_price = adjusted_price + spread_ticks_;  // $150.00
```

**New Quotes**:
```
Old Quotes (before fill):
├── BID: $149.90 (100 shares)
└── ASK: $150.10 (100 shares)

New Quotes (after fill):
├── BID: $149.80 (100 shares)  ← Adjusted down
└── ASK: $150.00 (100 shares)  ← Adjusted down

Base price moved from $150.00 → $149.90
```

**What Happens**:
- ✅ Base price now $149.90 (last trade price)
- ✅ New bid: $149.80 (base - spread)
- ✅ New ask: $150.00 (base + spread)
- ✅ Spread still 20 cents
- ✅ Quotes adjusted to market

**Interview Talking Point**:
> "After the fill, we requote with an adjusted base price. The base price is updated to the last trade price ($149.90), so our new quotes are bid $149.80 and ask $150.00. This is price discovery—we're adjusting to market conditions."

---

### **STEP 9: Inventory Management** (Conceptual)

**Current Position**: +50 shares (long)

**Inventory Skew Strategy**:
```cpp
// Conceptual code (not in current implementation)
void update_quotes_with_inventory() {
    int64_t position = get_current_position();  // +50 shares
    
    // If long, widen ask (encourage selling), tighten bid (discourage buying)
    uint64_t bid_price = base_price_ - spread_ticks_ - (position > 0 ? 5 : 0);
    uint64_t ask_price = base_price_ + spread_ticks_ + (position > 0 ? 0 : 5);
    
    // If position too large, stop quoting one side
    if (position > MAX_POSITION) {
        // Only quote ask side (to reduce position)
        send_new_order(symbol_, Side::SELL, quote_size_, ask_price);
    } else if (position < -MAX_POSITION) {
        // Only quote bid side (to reduce position)
        send_new_order(symbol_, Side::BUY, quote_size_, bid_price);
    } else {
        // Quote both sides normally
        send_new_order(symbol_, Side::BUY, quote_size_, bid_price);
        send_new_order(symbol_, Side::SELL, quote_size_, ask_price);
    }
}
```

**Inventory Management Strategies**:
1. **Skew quotes**: Widen side we want to discourage
2. **Stop quoting**: If position too large, only quote one side
3. **Adjust size**: Reduce quote size when inventory high
4. **Spread widening**: Increase spread to reduce fill rate

**Interview Talking Point**:
> "In production, we'd implement inventory management. If we're long 50 shares, we'd widen the ask to encourage selling and tighten the bid to discourage more buying. If position exceeds limits, we'd stop quoting one side entirely to force position flattening."

---

### **STEP 10: Continuous Loop** (Ongoing)

**Market Maker Event Loop**:
```
Time: 10:00:18.891  → Bid filled at $149.90
Time: 10:00:18.892  → Cancel existing orders
Time: 10:00:18.902  → Requote (bid $149.80, ask $150.00)
Time: 10:00:18.903  → Orders acknowledged
Time: 10:00:25.123  → Ask filled at $150.00
Time: 10:00:25.124  → Cancel existing orders
Time: 10:00:25.134  → Requote (bid $149.90, ask $150.10)
Time: 10:00:25.135  → Orders acknowledged
... (repeat forever)
```

**P&L Calculation**:
```
Trade 1: BUY 50 @ $149.90  (cost: $7,495)
Trade 2: SELL 50 @ $150.00 (revenue: $7,500)
─────────────────────────────────────────
Gross P&L: $5.00 (10 cents per share)
Fees: -$1.00 (exchange fees)
Net P&L: $4.00

Spread captured: $0.10 per share × 50 shares = $5.00
```

**Interview Talking Point**:
> "The market maker runs in a continuous loop: quote, wait for fill, cancel, requote. Each time both sides fill, we capture the spread. In this example, we bought at $149.90 and sold at $150.00, capturing 10 cents per share on 50 shares = $5.00 gross profit."

---

## 📊 PERFORMANCE METRICS

### **Requote Latency** (Critical for Adverse Selection)

```
Latency Breakdown (Total: ~100μs):
├── Trade notification received:     0μs
├── on_trade() callback:            10μs
├── cancel_existing_orders():       20μs
├── Sleep (anti-spam):          10,000μs (10ms)
├── update_quotes():                50μs
├── send_new_order() × 2:           20μs
└── Total:                      10,100μs (10.1ms)
```

**Why Latency Matters**:
- Slow requote → stale quotes → adverse selection
- Fast requote → avoid being picked off by informed traders
- Target: <100μs (without sleep)

**Interview Talking Point**:
> "Requote latency is critical. The 10ms sleep is artificial—in production, we'd aim for sub-100μs requote latency. Every microsecond of delay increases adverse selection risk, where informed traders can pick off our stale quotes."

---

## 🎯 RISK MANAGEMENT

### **1. Position Limits**

```cpp
const int64_t MAX_POSITION = 500;  // Max 500 shares long/short

if (position > MAX_POSITION) {
    // Stop quoting bid (only quote ask to reduce position)
    send_new_order(symbol_, Side::SELL, quote_size_, ask_price);
} else if (position < -MAX_POSITION) {
    // Stop quoting ask (only quote bid to reduce position)
    send_new_order(symbol_, Side::BUY, quote_size_, bid_price);
}
```

**Why**: Limits inventory risk (market moves against position)

---

### **2. Spread Widening During Volatility**

```cpp
// Detect volatility (price moved >1% in last minute)
if (is_high_volatility()) {
    spread_ticks_ = 20;  // Widen from 10 to 20 ticks
} else {
    spread_ticks_ = 10;  // Normal spread
}
```

**Why**: Wider spread compensates for increased risk

---

### **3. Rate Limiting**

```cpp
// Avoid exchange penalties for excessive cancel-replace
if (requote_count_last_second > 100) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
```

**Why**: Exchanges penalize quote spam (can lead to ban)

---

### **4. Credit Limits**

```cpp
// Check notional exposure before quoting
double notional = (bid_price + ask_price) * quote_size_ / 2;
if (total_exposure + notional > CREDIT_LIMIT) {
    // Reduce quote size or stop quoting
    quote_size_ = std::min(quote_size_, remaining_credit / price);
}
```

**Why**: Prevents overexposure beyond credit limits

---

## 💡 INTERVIEW TALKING POINTS

### **3-Minute Market Making Explanation**:

> "Let me walk you through the market making strategy:
>
> **Initialization**: We start with a base price of $150.00 and a spread of 10 ticks ($0.10) on each side. This means we'll quote bid at $149.90 and ask at $150.10.
>
> **Quoting**: The update_quotes() function sends both orders simultaneously. We track the order IDs so we can cancel them later.
>
> **Waiting**: We sit passively in the order book, providing liquidity. When an aggressive trader hits our quotes, we get filled.
>
> **Requoting**: When filled, the on_trade() callback fires. We immediately cancel both sides and requote with an adjusted base price. This is price discovery—we're adjusting to the last trade price.
>
> **Inventory Management**: In production, we'd implement inventory skew. If we're long, we'd widen the ask to encourage selling. If position exceeds limits, we'd stop quoting one side.
>
> **P&L**: Each time both sides fill, we capture the spread. Buy at $149.90, sell at $150.10 = 20 cents per share profit.
>
> **Risk Management**: We implement position limits, spread widening during volatility, rate limiting to avoid exchange penalties, and credit limits to prevent overexposure.
>
> **Latency**: Requote latency is critical—we aim for sub-100μs to avoid adverse selection. The current implementation has a 10ms sleep for anti-spam, but in production we'd optimize this."

---

## 🔍 DEEP-DIVE QUESTIONS & ANSWERS

### **Q: "What is adverse selection in market making?"**

**Answer**:
> "Adverse selection is when informed traders trade against you because they know something you don't.
>
> **Example**: You're quoting bid $100.00, ask $100.20. News breaks that the company beat earnings. Informed traders immediately buy at your $100.20 ask. Stock jumps to $101.00. You're stuck long at $100.20, losing $0.80 per share.
>
> **Defense**: Fast requoting (<100μs) after fills, widen spreads during high volatility, monitor order flow for toxicity, position limits to cap exposure."

---

### **Q: "How do you handle inventory risk?"**

**Answer**:
> "Three strategies:
> 1. **Inventory skew**: If long, widen ask and tighten bid to encourage selling
> 2. **Position limits**: Stop quoting one side when limit reached
> 3. **Dynamic spread**: Increase spread with inventory size
>
> **Example**: If we're long 200 shares and limit is 500, we'd widen the ask by 5 ticks and tighten the bid by 5 ticks. This makes it more attractive for others to buy from us (reducing our position) and less attractive for us to buy more."

---

### **Q: "Why cancel both sides when only one is filled?"**

**Answer**:
> "Two reasons:
> 1. **Inventory management**: If bid fills, we're long. We need to adjust quotes based on new position.
> 2. **Price discovery**: The fill indicates market moved. We update base_price to last trade and requote around new price.
>
> **Example**: Bid fills at $149.90. This suggests market is trading at $149.90, not $150.00. We cancel ask at $150.10 and requote at $150.00 (adjusted to new market level)."

---

### **Q: "What's the difference between market making and HFT?"**

**Answer**:
> "**Market Making**: Provides liquidity (passive orders), captures bid-ask spread, inventory risk is main concern.
>
> **HFT**: Broader category including market making, arbitrage, momentum, stat arb. May take liquidity (aggressive orders).
>
> **Overlap**: Many HFT firms do market making, but not all market makers are HFT. Goldman's systematic market making is HFT-style market making—automated, quantitative, low-latency."

---

**Document Version**: 1.0  
**Last Updated**: 2024  
**Purpose**: Market making strategy explanation for Goldman Sachs interview  
**Status**: Ready for discussion 💹
