# C++ Keywords Reference - By Version with RTES Usage

**Purpose**: Complete C++ keywords guide for Goldman Sachs interview  
**Coverage**: C++98 through C++23  
**Focus**: General functionality + RTES-specific examples

---

## 📋 TABLE OF CONTENTS

1. [C++98/03 Keywords](#c9803-keywords-85-keywords)
2. [C++11 Keywords](#c11-keywords-11-new)
3. [C++14 Keywords](#c14-keywords-0-new)
4. [C++17 Keywords](#c17-keywords-0-new)
5. [C++20 Keywords](#c20-keywords-8-new)
6. [C++23 Keywords](#c23-keywords-0-new)

---

## C++98/03 KEYWORDS (85 keywords)

### **Storage & Type Keywords**

#### **`auto` (C++98 meaning, changed in C++11)**
- **General**: Storage class specifier (rarely used in C++98)
- **RTES**: Not used in C++98 sense

#### **`const`**
- **General**: Declares immutable variables
- **RTES**: 
```cpp
const size_t QUEUE_CAPACITY = 100000;  // Config constants
Price best_bid() const { return bids_.rbegin()->first; }  // Const methods
```

#### **`static`**
- **General**: Static storage duration, internal linkage
- **RTES**:
```cpp
static constexpr size_t MAX_ORDER_SIZE = 10000;  // Class constants
static Logger& instance() { return logger_; }     // Singleton
```

#### **`extern`**
- **General**: External linkage declaration
- **RTES**:
```cpp
extern std::atomic<bool> shutdown_requested;  // Global shutdown flag
```

#### **`volatile`**
- **General**: Prevents compiler optimization (NOT for threading!)
- **RTES**: ❌ Not used (use std::atomic instead)

#### **`mutable`**
- **General**: Allows modification in const methods
- **RTES**:
```cpp
mutable std::mutex order_mutex_;  // Can lock in const methods
Price best_bid() const {
    std::lock_guard<std::mutex> lock(order_mutex_);  // OK with mutable
    return bids_.rbegin()->first;
}
```

---

### **Type Keywords**

#### **`bool`, `char`, `int`, `float`, `double`**
- **General**: Fundamental types
- **RTES**:
```cpp
bool running_{false};           // Thread control
uint64_t order_id;              // Order ID (not int!)
double notional_exposure;       // Risk calculation
```

#### **`short`, `long`, `signed`, `unsigned`**
- **General**: Type modifiers
- **RTES**:
```cpp
uint64_t price;        // Unsigned 64-bit for price
int64_t position;      // Signed for long/short position
```

#### **`void`**
- **General**: No return type
- **RTES**:
```cpp
void start();          // No return value
void* buffer;          // Generic pointer (rare)
```

#### **`wchar_t`**
- **General**: Wide character type
- **RTES**: ❌ Not used (ASCII only)

---

### **Control Flow Keywords**

#### **`if`, `else`**
- **General**: Conditional execution
- **RTES**:
```cpp
if (order->side == Side::BUY) {
    match_against_asks(order);
} else {
    match_against_bids(order);
}
```

#### **`switch`, `case`, `default`**
- **General**: Multi-way branch
- **RTES**:
```cpp
switch (msg.header.type) {
    case NEW_ORDER:
        handle_new_order(msg);
        break;
    case CANCEL_ORDER:
        handle_cancel_order(msg);
        break;
    default:
        LOG_WARN("Unknown message type");
}
```

#### **`for`, `while`, `do`**
- **General**: Loops
- **RTES**:
```cpp
// Range-based for (C++11)
for (auto& [symbol, engine] : matching_engines_) {
    engine->start();
}

// While loop
while (running_) {
    if (input_queue_->pop(order)) {
        process_order(order);
    }
}
```

#### **`break`, `continue`**
- **General**: Loop control
- **RTES**:
```cpp
for (auto it = asks_.begin(); it != asks_.end(); ++it) {
    if (!crosses(order->price, it->first)) break;  // Stop matching
    // ... match logic
}
```

#### **`goto`**
- **General**: Unconditional jump
- **RTES**: ❌ Not used (considered harmful)

#### **`return`**
- **General**: Exit function with value
- **RTES**:
```cpp
bool push(const T& item) {
    if (queue_full()) return false;  // Early return
    // ... push logic
    return true;
}
```

---

### **Class & Object Keywords**

#### **`class`, `struct`**
- **General**: Define types
- **RTES**:
```cpp
class OrderBook {  // Complex type with private members
    // ...
};

struct Order {     // POD type with public members
    OrderID id;
    Price price;
    // ...
};
```

#### **`public`, `private`, `protected`**
- **General**: Access specifiers
- **RTES**:
```cpp
class OrderBook {
public:
    bool add_order(Order* order);  // Public interface
private:
    std::map<Price, PriceLevel> bids_;  // Private implementation
protected:
    // Not used in RTES (no inheritance)
};
```

#### **`friend`**
- **General**: Grant access to private members
- **RTES**: ❌ Not used (prefer public interface)

#### **`this`**
- **General**: Pointer to current object
- **RTES**:
```cpp
void TcpGateway::start() {
    worker_thread_ = std::thread(&TcpGateway::worker_loop, this);
}
```

#### **`virtual`, `override` (C++11)**
- **General**: Polymorphism
- **RTES**:
```cpp
class TradingStrategy {
public:
    virtual void on_start() = 0;  // Pure virtual
    virtual void on_tick() = 0;
};

class MarketMakerStrategy : public TradingStrategy {
    void on_start() override {    // Override in C++11
        update_quotes();
    }
};
```

---

### **Memory Management Keywords**

#### **`new`, `delete`**
- **General**: Dynamic allocation
- **RTES**: ❌ Avoided in hot path (use memory pool)
```cpp
// BAD: Dynamic allocation
Order* order = new Order();  // Can trigger page faults

// GOOD: Memory pool
Order* order = pool_.allocate();  // Pre-allocated, O(1)
```

#### **`sizeof`**
- **General**: Size of type/object
- **RTES**:
```cpp
static_assert(sizeof(MessageHeader) == 32, "Header must be 32 bytes");
buffer_.resize(sizeof(NewOrderMessage));
```

---

### **Casting Keywords**

#### **`static_cast`**
- **General**: Compile-time type conversion
- **RTES**:
```cpp
order->side = static_cast<Side>(msg.side);  // uint8_t → enum
auto index = static_cast<size_t>(ptr - pool_.data());
```

#### **`dynamic_cast`**
- **General**: Runtime polymorphic cast
- **RTES**: ❌ Not used (no RTTI, -fno-rtti flag)

#### **`const_cast`**
- **General**: Remove const qualifier
- **RTES**: ❌ Not used (indicates design flaw)

#### **`reinterpret_cast`**
- **General**: Low-level type reinterpretation
- **RTES**:
```cpp
MessageHeader* header = reinterpret_cast<MessageHeader*>(buffer.data());
```

---

### **Exception Keywords**

#### **`try`, `catch`, `throw`**
- **General**: Exception handling
- **RTES**: ⚠️ Limited use (exceptions disabled in hot path with -fno-exceptions)
```cpp
// Only in initialization code
try {
    auto config = Config::load_from_file(argv[1]);
} catch (const std::exception& e) {
    std::cerr << "Fatal error: " << e.what() << std::endl;
    return 1;
}
```

---

### **Namespace Keywords**

#### **`namespace`, `using`**
- **General**: Organize code, import names
- **RTES**:
```cpp
namespace rtes {
    class OrderBook { /* ... */ };
}

using OrderID = uint64_t;  // Type alias
using namespace std;       // ❌ Never in headers!
```

---

### **Template Keywords**

#### **`template`, `typename`**
- **General**: Generic programming
- **RTES**:
```cpp
template<typename T>
class SPSCQueue {
    std::unique_ptr<T[]> buffer_;
    // ...
};

// Usage
SPSCQueue<OrderRequest> input_queue_;
```

---

### **Other C++98 Keywords**

#### **`typedef`**
- **General**: Type alias (old style)
- **RTES**: ⚠️ Prefer `using` (C++11)
```cpp
// Old style
typedef uint64_t OrderID;

// New style (C++11)
using OrderID = uint64_t;  // ✅ Preferred
```

#### **`enum`**
- **General**: Enumeration
- **RTES**: ⚠️ Prefer `enum class` (C++11)
```cpp
// Old style (not type-safe)
enum Side { BUY = 1, SELL = 2 };

// New style (type-safe)
enum class Side : uint8_t { BUY = 1, SELL = 2 };  // ✅ Preferred
```

#### **`operator`**
- **General**: Operator overloading
- **RTES**:
```cpp
bool operator==(const Order& lhs, const Order& rhs) {
    return lhs.id == rhs.id;
}
```

#### **`explicit`**
- **General**: Prevent implicit conversions
- **RTES**:
```cpp
explicit SPSCQueue(size_t capacity);  // Prevent SPSCQueue q = 100;
```

#### **`inline`**
- **General**: Hint to inline function
- **RTES**:
```cpp
inline Price best_bid() const { return bids_.rbegin()->first; }
```

#### **`asm`**
- **General**: Inline assembly
- **RTES**:
```cpp
_mm_prefetch(&(*std::next(it)), _MM_HINT_T0);  // Intrinsic, not asm
```

---

## C++11 KEYWORDS (11 new)

### **`auto` (new meaning)**
- **General**: Type deduction
- **RTES**:
```cpp
auto head = head_.load(std::memory_order_relaxed);  // Deduced as size_t
auto it = order_lookup_.find(order_id);             // Deduced as iterator
```

### **`decltype`**
- **General**: Deduce type of expression
- **RTES**:
```cpp
decltype(bids_.begin()) it = bids_.begin();  // Rarely used (auto is better)
```

### **`nullptr`**
- **General**: Null pointer literal
- **RTES**:
```cpp
Order* order = pool_.allocate();
if (order == nullptr) {  // ✅ Type-safe (vs NULL)
    return false;
}
```

### **`constexpr`**
- **General**: Compile-time constants
- **RTES**:
```cpp
static constexpr size_t CACHE_LINE_SIZE = 64;
constexpr Price calculate_spread(Price base) { return base * 0.001; }
```

### **`static_assert`**
- **General**: Compile-time assertion
- **RTES**:
```cpp
static_assert(sizeof(MessageHeader) == 32, "Header size mismatch");
static_assert(alignof(std::atomic<size_t>) == 64, "Cache line alignment");
```

### **`alignas`, `alignof`**
- **General**: Control alignment
- **RTES**:
```cpp
alignas(64) std::atomic<size_t> head_{0};  // Cache-line aligned
static_assert(alignof(head_) == 64, "Must be 64-byte aligned");
```

### **`noexcept`**
- **General**: Exception specification
- **RTES**:
```cpp
void deallocate(T* ptr) noexcept {  // Guarantees no exceptions
    // ...
}
```

### **`thread_local`**
- **General**: Thread-local storage
- **RTES**:
```cpp
thread_local uint64_t thread_id = get_thread_id();  // Per-thread variable
```

### **`override`**
- **General**: Mark virtual function override
- **RTES**:
```cpp
class MarketMakerStrategy : public TradingStrategy {
    void on_start() override {  // Compiler checks base class has this
        update_quotes();
    }
};
```

### **`final`**
- **General**: Prevent inheritance/override
- **RTES**:
```cpp
class OrderBook final {  // Cannot be inherited
    // ...
};

virtual void process() final {  // Cannot be overridden
    // ...
}
```

### **`default`, `delete` (function)**
- **General**: Control special members
- **RTES**:
```cpp
class FileDescriptor {
    FileDescriptor(const FileDescriptor&) = delete;  // No copy
    FileDescriptor(FileDescriptor&&) = default;      // Default move
};
```

---

## C++14 KEYWORDS (0 new)

**Note**: C++14 added no new keywords, only library features

**RTES Usage**:
```cpp
// Generic lambdas (C++14)
auto print = [](auto x) { std::cout << x; };

// Binary literals (C++14)
uint8_t flags = 0b1010'0101;  // Also digit separators
```

---

## C++17 KEYWORDS (0 new)

**Note**: C++17 added no new keywords, but added contextual keywords

### **Contextual Keywords**

#### **`if constexpr`**
- **General**: Compile-time if
- **RTES**:
```cpp
template<typename T>
void process(T value) {
    if constexpr (std::is_integral_v<T>) {
        // Compile-time branch for integers
    } else {
        // Compile-time branch for others
    }
}
```

#### **Structured bindings**
- **General**: Decompose objects
- **RTES**:
```cpp
for (auto& [symbol, engine] : matching_engines_) {
    engine->start();  // symbol and engine are references
}

auto [it, inserted] = order_lookup_.insert({id, order});
```

---

## C++20 KEYWORDS (8 new)

### **`concept`, `requires`**
- **General**: Constraints on templates
- **RTES**:
```cpp
template<typename T>
concept Numeric = std::is_arithmetic_v<T>;

template<Numeric T>
T add(T a, T b) requires (sizeof(T) <= 8) {
    return a + b;
}
```

### **`co_await`, `co_yield`, `co_return`**
- **General**: Coroutines
- **RTES**: ❌ Not used (adds overhead, not suitable for low-latency)

### **`consteval`**
- **General**: Immediate functions (must evaluate at compile-time)
- **RTES**:
```cpp
consteval size_t cache_line_size() {
    return 64;  // Must be compile-time constant
}
```

### **`constinit`**
- **General**: Constant initialization
- **RTES**:
```cpp
constinit std::atomic<bool> shutdown_requested{false};
```

### **`char8_t`**
- **General**: UTF-8 character type
- **RTES**: ❌ Not used (ASCII only)

---

## C++23 KEYWORDS (0 new)

**Note**: C++23 added no new keywords, only library features

**RTES Potential Usage**:
```cpp
// std::expected (C++23) - like Result<T>
std::expected<Order*, Error> allocate_order() {
    Order* order = pool_.allocate();
    if (!order) return std::unexpected(Error::POOL_EXHAUSTED);
    return order;
}
```

---

## 🎯 ESSENTIAL KEYWORDS FOR RTES

### **Most Used (Top 20)**

| Keyword | Usage Count | Critical For |
|---------|-------------|--------------|
| `const` | 500+ | Immutability, const-correctness |
| `auto` | 400+ | Type deduction, readability |
| `class` | 50+ | Core abstractions |
| `template` | 30+ | Generic containers (queues, pools) |
| `static` | 100+ | Singletons, constants |
| `inline` | 80+ | Performance (hot path) |
| `constexpr` | 60+ | Compile-time constants |
| `noexcept` | 40+ | Exception guarantees |
| `nullptr` | 200+ | Type-safe null pointers |
| `alignas` | 10+ | Cache-line alignment |
| `override` | 20+ | Virtual function safety |
| `delete` | 15+ | Disable copy constructors |
| `explicit` | 30+ | Prevent implicit conversions |
| `mutable` | 5+ | Const method mutations |
| `volatile` | 0 | ❌ Never (use atomic) |
| `virtual` | 10+ | Strategy pattern |
| `enum class` | 15+ | Type-safe enums |
| `using` | 50+ | Type aliases |
| `namespace` | 1 | Code organization |
| `static_assert` | 20+ | Compile-time checks |

---

## 🚫 KEYWORDS TO AVOID IN RTES

| Keyword | Why Avoid | Alternative |
|---------|-----------|-------------|
| `volatile` | Not for threading | `std::atomic` |
| `new`/`delete` | Heap allocations | Memory pool |
| `dynamic_cast` | RTTI overhead | `static_cast` |
| `throw` | Exception overhead | `Result<T>` |
| `goto` | Spaghetti code | Structured control flow |
| `friend` | Breaks encapsulation | Public interface |
| `asm` | Non-portable | Compiler intrinsics |

---

## 📊 KEYWORD USAGE BY COMPONENT

### **Lock-Free Queue (SPSC)**
```cpp
template<typename T>           // template, typename
class SPSCQueue {              // class
public:                        // public
    explicit SPSCQueue(size_t capacity)  // explicit
        : capacity_(capacity + 1),
          buffer_(std::make_unique<T[]>(capacity_)) {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }
    
    bool push(const T& item) {  // const, bool
        auto head = head_.load(std::memory_order_relaxed);  // auto
        auto next_head = (head + 1) % capacity_;
        
        if (next_head == tail_.load(std::memory_order_acquire)) {  // if
            return false;        // return
        }
        
        buffer_[head] = item;
        head_.store(next_head, std::memory_order_release);
        return true;
    }
    
private:                        // private
    const size_t capacity_;     // const
    std::unique_ptr<T[]> buffer_;
    
    alignas(64) std::atomic<size_t> head_{0};  // alignas
    alignas(64) std::atomic<size_t> tail_{0};
};
```

**Keywords Used**: `template`, `typename`, `class`, `public`, `explicit`, `const`, `bool`, `auto`, `if`, `return`, `private`, `alignas`

---

### **Order Book**
```cpp
class OrderBook {              // class
public:                        // public
    explicit OrderBook(const std::string& symbol, OrderPool& pool)  // explicit, const
        : symbol_(symbol), pool_(pool) {}
    
    bool add_order(Order* order) {  // bool
        std::lock_guard<std::mutex> lock(order_mutex_);  // template
        
        match_order(order);
        
        if (order->remaining_quantity > 0) {  // if
            add_to_book(order);
        }
        
        return true;            // return
    }
    
    Price best_bid() const {    // const
        return bids_.empty() ? 0 : bids_.rbegin()->first;
    }
    
private:                        // private
    std::string symbol_;
    OrderPool& pool_;
    
    std::map<Price, PriceLevel, std::greater<Price>> bids_;  // template
    std::map<Price, PriceLevel> asks_;
    
    std::unordered_map<OrderID, Order*> order_lookup_;
    
    mutable std::mutex order_mutex_;  // mutable
};
```

**Keywords Used**: `class`, `public`, `explicit`, `const`, `bool`, `template`, `if`, `return`, `private`, `mutable`

---

## 🎤 INTERVIEW TALKING POINTS

### **"What C++ features do you use most?"**

> "In RTES, I heavily use C++11/14/17/20 features:
>
> **C++11**: `auto` for type deduction, `nullptr` for type safety, `constexpr` for compile-time constants, `alignas` for cache-line alignment, `override` for virtual function safety, and `std::atomic` for lock-free programming.
>
> **C++14**: Generic lambdas and binary literals for readability.
>
> **C++17**: Structured bindings for cleaner code when iterating maps.
>
> **C++20**: `concept` and `requires` for template constraints (though limited use).
>
> I avoid `volatile` (use `atomic` instead), `new`/`delete` (use memory pools), and exceptions in hot paths (use `Result<T>`)."

---

### **"Why avoid volatile for threading?"**

> "`volatile` prevents compiler optimization but provides NO atomicity or memory ordering guarantees. It's for memory-mapped I/O, not threading.
>
> For threading, use `std::atomic` which provides:
> - Atomic operations (no torn reads/writes)
> - Memory ordering (acquire/release semantics)
> - Compiler and hardware barriers
>
> Example:
> ```cpp
> // BAD
> volatile bool running = true;  // Not thread-safe!
>
> // GOOD
> std::atomic<bool> running{true};  // Thread-safe
> ```"

---

**Document Version**: 1.0  
**Last Updated**: 2024  
**Purpose**: C++ keywords reference for Goldman Sachs interview  
**Status**: Ready for discussion 📚
