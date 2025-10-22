#pragma once

#include <system_error>
#include <string>
#include <optional>
#include <functional>
#include <chrono>
#include <memory>

namespace rtes {

// Unified error categories
enum class ErrorCode {
    SUCCESS = 0,
    
    // Network errors
    NETWORK_CONNECTION_FAILED = 1000,
    NETWORK_TIMEOUT,
    NETWORK_DISCONNECTED,
    NETWORK_INVALID_MESSAGE,
    
    // Memory errors
    MEMORY_ALLOCATION_FAILED = 2000,
    MEMORY_BUFFER_OVERFLOW,
    MEMORY_POOL_EXHAUSTED,
    
    // File I/O errors
    FILE_NOT_FOUND = 3000,
    FILE_PERMISSION_DENIED,
    FILE_CORRUPTED,
    FILE_DISK_FULL,
    
    // Business logic errors
    ORDER_INVALID = 4000,
    ORDER_DUPLICATE,
    ORDER_NOT_FOUND,
    RISK_LIMIT_EXCEEDED,
    
    // System errors
    SYSTEM_SHUTDOWN = 5000,
    SYSTEM_OVERLOAD,
    SYSTEM_CORRUPTED_STATE
};

class ErrorCategory : public std::error_category {
public:
    const char* name() const noexcept override { return "rtes"; }
    std::string message(int ev) const override;
};

const ErrorCategory& error_category();
std::error_code make_error_code(ErrorCode ec);

// Result monad for error propagation
template<typename T>
class Result {
public:
    Result(T&& value) : value_(std::move(value)), has_value_(true) {}
    Result(const T& value) : value_(value), has_value_(true) {}
    Result(ErrorCode error) : error_(make_error_code(error)), has_value_(false) {}
    Result(std::error_code error) : error_(error), has_value_(false) {}
    
    bool has_value() const noexcept { return has_value_; }
    bool has_error() const noexcept { return !has_value_; }
    
    const T& value() const& { 
        if (!has_value_) throw std::runtime_error("Accessing value of error result");
        return value_; 
    }
    
    T&& value() && { 
        if (!has_value_) throw std::runtime_error("Accessing value of error result");
        return std::move(value_); 
    }
    
    const std::error_code& error() const { return error_; }
    
    template<typename F>
    auto map(F&& func) -> Result<decltype(func(value_))> {
        if (has_value_) {
            return Result<decltype(func(value_))>(func(value_));
        }
        return Result<decltype(func(value_))>(error_);
    }
    
    template<typename F>
    Result<T> or_else(F&& func) {
        if (has_error()) {
            return func(error_);
        }
        return *this;
    }
    
private:
    T value_{};
    std::error_code error_;
    bool has_value_;
};

// Specialization for void
template<>
class Result<void> {
public:
    Result() : has_value_(true) {}
    Result(ErrorCode error) : error_(make_error_code(error)), has_value_(false) {}
    Result(std::error_code error) : error_(error), has_value_(false) {}
    
    bool has_value() const noexcept { return has_value_; }
    bool has_error() const noexcept { return !has_value_; }
    const std::error_code& error() const { return error_; }
    
private:
    std::error_code error_;
    bool has_value_;
};

// Error context for detailed error information
struct ErrorContext {
    std::string component;
    std::string operation;
    std::string details;
    std::chrono::steady_clock::time_point timestamp;
    
    ErrorContext(std::string comp, std::string op, std::string det = "")
        : component(std::move(comp)), operation(std::move(op)), 
          details(std::move(det)), timestamp(std::chrono::steady_clock::now()) {}
};

// Base recovery strategy
class RecoveryStrategy {
public:
    virtual ~RecoveryStrategy() = default;
    virtual Result<void> attempt_recovery(const ErrorContext& context) = 0;
    virtual bool can_recover(ErrorCode error) const = 0;
};

// Network error recovery with circuit breaker
class NetworkErrorRecovery : public RecoveryStrategy {
public:
    NetworkErrorRecovery(int max_retries = 3, std::chrono::milliseconds retry_delay = std::chrono::milliseconds(1000));
    
    Result<void> attempt_recovery(const ErrorContext& context) override;
    bool can_recover(ErrorCode error) const override;
    
    void set_connection_factory(std::function<Result<void>()> factory) { connection_factory_ = factory; }
    
private:
    int max_retries_;
    std::chrono::milliseconds retry_delay_;
    std::function<Result<void>()> connection_factory_;
    
    // Circuit breaker state
    enum class CircuitState { CLOSED, OPEN, HALF_OPEN };
    CircuitState circuit_state_ = CircuitState::CLOSED;
    int failure_count_ = 0;
    std::chrono::steady_clock::time_point last_failure_time_;
    static constexpr int FAILURE_THRESHOLD = 5;
    static constexpr auto CIRCUIT_TIMEOUT = std::chrono::seconds(30);
};

// File error recovery with fallback paths
class FileErrorRecovery : public RecoveryStrategy {
public:
    FileErrorRecovery(std::vector<std::string> fallback_paths = {});
    
    Result<void> attempt_recovery(const ErrorContext& context) override;
    bool can_recover(ErrorCode error) const override;
    
    void add_fallback_path(const std::string& path) { fallback_paths_.push_back(path); }
    
private:
    std::vector<std::string> fallback_paths_;
    size_t current_fallback_index_ = 0;
};

// Memory error recovery with allocation fallbacks
class MemoryErrorRecovery : public RecoveryStrategy {
public:
    MemoryErrorRecovery();
    
    Result<void> attempt_recovery(const ErrorContext& context) override;
    bool can_recover(ErrorCode error) const override;
    
private:
    void trigger_garbage_collection();
    void reduce_cache_size();
    bool emergency_allocation_mode_ = false;
};

} // namespace rtes

// Make ErrorCode compatible with std::error_code
namespace std {
template<>
struct is_error_code_enum<rtes::ErrorCode> : true_type {};
}