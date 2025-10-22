#include "rtes/error_handling.hpp"
#include "rtes/logger.hpp"
#include <thread>
#include <filesystem>
#include <fstream>

namespace rtes {

std::string ErrorCategory::message(int ev) const {
    switch (static_cast<ErrorCode>(ev)) {
        case ErrorCode::SUCCESS: return "Success";
        
        case ErrorCode::NETWORK_CONNECTION_FAILED: return "Network connection failed";
        case ErrorCode::NETWORK_TIMEOUT: return "Network operation timed out";
        case ErrorCode::NETWORK_DISCONNECTED: return "Network disconnected";
        case ErrorCode::NETWORK_INVALID_MESSAGE: return "Invalid network message";
        
        case ErrorCode::MEMORY_ALLOCATION_FAILED: return "Memory allocation failed";
        case ErrorCode::MEMORY_BUFFER_OVERFLOW: return "Buffer overflow detected";
        case ErrorCode::MEMORY_POOL_EXHAUSTED: return "Memory pool exhausted";
        
        case ErrorCode::FILE_NOT_FOUND: return "File not found";
        case ErrorCode::FILE_PERMISSION_DENIED: return "File permission denied";
        case ErrorCode::FILE_CORRUPTED: return "File corrupted";
        case ErrorCode::FILE_DISK_FULL: return "Disk full";
        
        case ErrorCode::ORDER_INVALID: return "Invalid order";
        case ErrorCode::ORDER_DUPLICATE: return "Duplicate order";
        case ErrorCode::ORDER_NOT_FOUND: return "Order not found";
        case ErrorCode::RISK_LIMIT_EXCEEDED: return "Risk limit exceeded";
        
        case ErrorCode::SYSTEM_SHUTDOWN: return "System shutdown";
        case ErrorCode::SYSTEM_OVERLOAD: return "System overload";
        case ErrorCode::SYSTEM_CORRUPTED_STATE: return "System corrupted state";
        
        default: return "Unknown error";
    }
}

const ErrorCategory& error_category() {
    static ErrorCategory instance;
    return instance;
}

std::error_code make_error_code(ErrorCode ec) {
    return {static_cast<int>(ec), error_category()};
}

// NetworkErrorRecovery implementation
NetworkErrorRecovery::NetworkErrorRecovery(int max_retries, std::chrono::milliseconds retry_delay)
    : max_retries_(max_retries), retry_delay_(retry_delay) {}

Result<void> NetworkErrorRecovery::attempt_recovery(const ErrorContext& context) {
    LOG_INFO_SAFE("Attempting network recovery for {}: {}", context.component, context.operation);
    
    // Check circuit breaker
    auto now = std::chrono::steady_clock::now();
    if (circuit_state_ == CircuitState::OPEN) {
        if (now - last_failure_time_ < CIRCUIT_TIMEOUT) {
            return ErrorCode::NETWORK_CONNECTION_FAILED;
        }
        circuit_state_ = CircuitState::HALF_OPEN;
    }
    
    for (int attempt = 0; attempt < max_retries_; ++attempt) {
        if (attempt > 0) {
            std::this_thread::sleep_for(retry_delay_);
        }
        
        if (connection_factory_) {
            auto result = connection_factory_();
            if (result.has_value()) {
                failure_count_ = 0;
                circuit_state_ = CircuitState::CLOSED;
                LOG_INFO_SAFE("Network recovery successful after {} attempts", attempt + 1);
                return Result<void>();
            }
        }
        
        LOG_WARN_SAFE("Network recovery attempt {} failed", attempt + 1);
    }
    
    // Recovery failed - update circuit breaker
    failure_count_++;
    last_failure_time_ = now;
    if (failure_count_ >= FAILURE_THRESHOLD) {
        circuit_state_ = CircuitState::OPEN;
        LOG_ERROR_SAFE("Circuit breaker opened after {} failures", failure_count_);
    }
    
    return ErrorCode::NETWORK_CONNECTION_FAILED;
}

bool NetworkErrorRecovery::can_recover(ErrorCode error) const {
    return error == ErrorCode::NETWORK_CONNECTION_FAILED ||
           error == ErrorCode::NETWORK_TIMEOUT ||
           error == ErrorCode::NETWORK_DISCONNECTED;
}

// FileErrorRecovery implementation
FileErrorRecovery::FileErrorRecovery(std::vector<std::string> fallback_paths)
    : fallback_paths_(std::move(fallback_paths)) {}

Result<void> FileErrorRecovery::attempt_recovery(const ErrorContext& context) {
    LOG_INFO_SAFE("Attempting file recovery for {}: {}", context.component, context.operation);
    
    // Try fallback paths
    for (size_t i = current_fallback_index_; i < fallback_paths_.size(); ++i) {
        const auto& path = fallback_paths_[i];
        
        try {
            if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
                current_fallback_index_ = i;
                LOG_INFO_SAFE("File recovery using fallback path: {}", path);
                return Result<void>();
            }
        } catch (const std::filesystem::filesystem_error& e) {
            LOG_WARN_SAFE("Fallback path {} failed: {}", path, e.what());
            continue;
        }
    }
    
    // Try creating directories
    for (const auto& path : fallback_paths_) {
        try {
            std::filesystem::create_directories(path);
            LOG_INFO_SAFE("Created fallback directory: {}", path);
            return Result<void>();
        } catch (const std::filesystem::filesystem_error& e) {
            LOG_WARN_SAFE("Failed to create directory {}: {}", path, e.what());
        }
    }
    
    return ErrorCode::FILE_NOT_FOUND;
}

bool FileErrorRecovery::can_recover(ErrorCode error) const {
    return error == ErrorCode::FILE_NOT_FOUND ||
           error == ErrorCode::FILE_PERMISSION_DENIED ||
           error == ErrorCode::FILE_DISK_FULL;
}

// MemoryErrorRecovery implementation
MemoryErrorRecovery::MemoryErrorRecovery() {}

Result<void> MemoryErrorRecovery::attempt_recovery(const ErrorContext& context) {
    LOG_INFO_SAFE("Attempting memory recovery for {}: {}", context.component, context.operation);
    
    // Try garbage collection first
    trigger_garbage_collection();
    
    // Reduce cache sizes
    reduce_cache_size();
    
    // Enable emergency allocation mode
    if (!emergency_allocation_mode_) {
        emergency_allocation_mode_ = true;
        LOG_WARN("Enabled emergency allocation mode");
        return Result<void>();
    }
    
    return ErrorCode::MEMORY_ALLOCATION_FAILED;
}

bool MemoryErrorRecovery::can_recover(ErrorCode error) const {
    return error == ErrorCode::MEMORY_ALLOCATION_FAILED ||
           error == ErrorCode::MEMORY_POOL_EXHAUSTED;
}

void MemoryErrorRecovery::trigger_garbage_collection() {
    // Force any pending deallocations
    LOG_DEBUG("Triggering garbage collection");
}

void MemoryErrorRecovery::reduce_cache_size() {
    // Reduce internal caches
    LOG_DEBUG("Reducing cache sizes");
}

} // namespace rtes