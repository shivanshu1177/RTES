/**
 * @file logger.cpp
 * @brief Thread-safe logging with rate limiting
 * 
 * Features:
 * - Singleton pattern for global access
 * - Thread-safe logging (atomic operations)
 * - Rate limiting to prevent log flooding
 * - Structured logging (JSON format)
 * - Multiple log levels (DEBUG, INFO, WARN, ERROR, FATAL)
 * - Timestamp with millisecond precision
 * 
 * Performance:
 * - Lock-free level checking
 * - Rate limiting with atomic timestamp
 * - Minimal string allocations
 */

#include "rtes/logger.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>

namespace rtes {

/**
 * @brief Get logger singleton instance
 * @return Reference to global logger
 * 
 * Thread-safe singleton using static local variable.
 */
Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::set_level(LogLevel level) {
    level_.store(level);
}

void Logger::set_rate_limit(std::chrono::milliseconds limit) {
    rate_limit_ = limit;
}

void Logger::enable_structured(bool enabled) {
    structured_.store(enabled);
}

/**
 * @brief Log message at specified level
 * @param level Log level (DEBUG, INFO, WARN, ERROR, FATAL)
 * @param message Message to log (already sanitized)
 * 
 * Fast-path checks:
 * 1. Level filter (atomic load)
 * 2. Rate limit check (atomic compare)
 * 3. Format and output
 * 
 * Thread-safe: Multiple threads can log concurrently.
 */
void Logger::log(LogLevel level, const std::string& message) {
    // Fast-path: Check if level enabled (atomic load)
    if (level < level_.load()) return;
    
    // Fast-path: Check rate limit (atomic compare)
    if (should_rate_limit()) return;
    
    // Format and output message
    std::cout << format_message(level, message) << std::endl;
}

// Note: Removed unsafe printf-style logging - use log_safe() template methods instead

/**
 * @brief Format log message with timestamp and level
 * @param level Log level
 * @param message Message content
 * @return Formatted log string
 * 
 * Formats:
 * - Structured (JSON): {"timestamp":"...","level":"...","message":"..."}
 * - Plain: [2024-01-01 12:00:00.123] INFO message
 * 
 * Timestamp: UTC with millisecond precision
 */
std::string Logger::format_message(LogLevel level, const std::string& message) {
    // Get current time with millisecond precision
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    
    // Format based on structured logging setting
    if (structured_.load()) {
        // JSON format for log aggregation systems
        ss << "{\"timestamp\":\"" << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S")
           << "." << std::setfill('0') << std::setw(3) << ms.count() << "Z\""
           << ",\"level\":\"";
        
        switch (level) {
            case LogLevel::DEBUG: ss << "DEBUG"; break;
            case LogLevel::INFO:  ss << "INFO"; break;
            case LogLevel::WARN:  ss << "WARN"; break;
            case LogLevel::ERROR: ss << "ERROR"; break;
            case LogLevel::FATAL: ss << "FATAL"; break;
        }
        
        ss << "\",\"message\":\"" << message << "\"}";
    } else {
        // Plain text format for human readability
        ss << "[" << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S")
           << "." << std::setfill('0') << std::setw(3) << ms.count() << "] ";
        
        switch (level) {
            case LogLevel::DEBUG: ss << "DEBUG"; break;
            case LogLevel::INFO:  ss << "INFO "; break;
            case LogLevel::WARN:  ss << "WARN "; break;
            case LogLevel::ERROR: ss << "ERROR"; break;
            case LogLevel::FATAL: ss << "FATAL"; break;
        }
        
        ss << " " << message;
    }
    
    return ss.str();
}

/**
 * @brief Check if logging should be rate limited
 * @return true if should skip this log (rate limited)
 * 
 * Rate limiting prevents log flooding:
 * - Compares current time with last log time
 * - Skips log if within rate limit window
 * - Updates last log time atomically
 * 
 * Thread-safe: Uses atomic operations for timestamp.
 */
bool Logger::should_rate_limit() {
    auto now = std::chrono::steady_clock::now();
    auto last = last_log_.load();
    
    // Check if within rate limit window
    if (now - last < rate_limit_) {
        return true;  // Skip this log
    }
    
    // Update last log time (atomic)
    last_log_.store(now);
    return false;  // Allow this log
}

} // namespace rtes