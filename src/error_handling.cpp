#include "rtes/error_handling.hpp"
#include <thread>

namespace rtes {

std::string ErrorCategory::message(int ev) const {
    switch (static_cast<ErrorCode>(ev)) {
        case ErrorCode::SUCCESS:                    return "success";
        case ErrorCode::NETWORK_CONNECTION_FAILED:  return "network connection failed";
        case ErrorCode::NETWORK_TIMEOUT:            return "network timeout";
        case ErrorCode::NETWORK_DISCONNECTED:       return "network disconnected";
        case ErrorCode::NETWORK_INVALID_MESSAGE:    return "invalid message received";
        case ErrorCode::MEMORY_ALLOCATION_FAILED:   return "memory allocation failed";
        case ErrorCode::MEMORY_BUFFER_OVERFLOW:     return "buffer overflow";
        case ErrorCode::MEMORY_POOL_EXHAUSTED:      return "memory pool exhausted";
        case ErrorCode::FILE_NOT_FOUND:             return "file not found";
        case ErrorCode::FILE_PERMISSION_DENIED:     return "file permission denied";
        case ErrorCode::FILE_CORRUPTED:             return "file corrupted";
        case ErrorCode::FILE_DISK_FULL:             return "disk full";
        case ErrorCode::ORDER_INVALID:              return "order invalid";
        case ErrorCode::ORDER_DUPLICATE:            return "duplicate order";
        case ErrorCode::ORDER_NOT_FOUND:            return "order not found";
        case ErrorCode::RISK_LIMIT_EXCEEDED:        return "risk limit exceeded";
        case ErrorCode::SYSTEM_SHUTDOWN:            return "system shutdown";
        case ErrorCode::SYSTEM_OVERLOAD:            return "system overload";
        case ErrorCode::SYSTEM_CORRUPTED_STATE:     return "corrupted system state";
        default:                                    return "unknown error";
    }
}

const ErrorCategory& error_category() {
    static ErrorCategory cat;
    return cat;
}

std::error_code make_error_code(ErrorCode ec) {
    return {static_cast<int>(ec), error_category()};
}

// ---- NetworkErrorRecovery ------------------------------------------------

NetworkErrorRecovery::NetworkErrorRecovery(int max_retries,
                                           std::chrono::milliseconds retry_delay)
    : max_retries_(max_retries), retry_delay_(retry_delay) {}

bool NetworkErrorRecovery::can_recover(ErrorCode error) const {
    return error == ErrorCode::NETWORK_CONNECTION_FAILED ||
           error == ErrorCode::NETWORK_TIMEOUT          ||
           error == ErrorCode::NETWORK_DISCONNECTED;
}

Result<void> NetworkErrorRecovery::attempt_recovery(const ErrorContext& /*context*/) {
    if (circuit_state_ == CircuitState::OPEN) {
        auto elapsed = std::chrono::steady_clock::now() - last_failure_time_;
        if (elapsed < CIRCUIT_TIMEOUT) return Result<void>(ErrorCode::NETWORK_CONNECTION_FAILED);
        circuit_state_ = CircuitState::HALF_OPEN;
    }

    for (int attempt = 0; attempt < max_retries_; ++attempt) {
        if (connection_factory_ && connection_factory_().has_value()) {
            circuit_state_ = CircuitState::CLOSED;
            failure_count_ = 0;
            return Result<void>{};
        }
        std::this_thread::sleep_for(retry_delay_);
    }

    ++failure_count_;
    last_failure_time_ = std::chrono::steady_clock::now();
    if (failure_count_ >= FAILURE_THRESHOLD) circuit_state_ = CircuitState::OPEN;
    return Result<void>(ErrorCode::NETWORK_CONNECTION_FAILED);
}

// ---- FileErrorRecovery ---------------------------------------------------

FileErrorRecovery::FileErrorRecovery(std::vector<std::string> fallback_paths)
    : fallback_paths_(std::move(fallback_paths)) {}

bool FileErrorRecovery::can_recover(ErrorCode error) const {
    return error == ErrorCode::FILE_NOT_FOUND ||
           error == ErrorCode::FILE_CORRUPTED;
}

Result<void> FileErrorRecovery::attempt_recovery(const ErrorContext& /*context*/) {
    if (current_fallback_index_ < fallback_paths_.size()) {
        ++current_fallback_index_;
        return Result<void>{};
    }
    return Result<void>(ErrorCode::FILE_NOT_FOUND);
}

// ---- MemoryErrorRecovery -------------------------------------------------

MemoryErrorRecovery::MemoryErrorRecovery() = default;

bool MemoryErrorRecovery::can_recover(ErrorCode error) const {
    return error == ErrorCode::MEMORY_ALLOCATION_FAILED ||
           error == ErrorCode::MEMORY_POOL_EXHAUSTED;
}

Result<void> MemoryErrorRecovery::attempt_recovery(const ErrorContext& /*context*/) {
    if (!emergency_allocation_mode_) {
        trigger_garbage_collection();
        reduce_cache_size();
        emergency_allocation_mode_ = true;
        return Result<void>{};
    }
    return Result<void>(ErrorCode::MEMORY_ALLOCATION_FAILED);
}

void MemoryErrorRecovery::trigger_garbage_collection() {}
void MemoryErrorRecovery::reduce_cache_size() {}

} // namespace rtes
