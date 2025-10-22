#pragma once

#include <string>
#include <chrono>
#include <atomic>
#include <memory>

namespace rtes {

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3,
    FATAL = 4
};

class Logger {
public:
    static Logger& instance();
    
    void set_level(LogLevel level);
    void set_rate_limit(std::chrono::milliseconds limit);
    void enable_structured(bool enabled);
    
    void log(LogLevel level, const std::string& message);
    void log(LogLevel level, const char* format, ...);
    
    // Convenience methods
    void debug(const std::string& msg) { log(LogLevel::DEBUG, msg); }
    void info(const std::string& msg) { log(LogLevel::INFO, msg); }
    void warn(const std::string& msg) { log(LogLevel::WARN, msg); }
    void error(const std::string& msg) { log(LogLevel::ERROR, msg); }
    void fatal(const std::string& msg) { log(LogLevel::FATAL, msg); }

private:
    Logger() = default;
    
    std::atomic<LogLevel> level_{LogLevel::INFO};
    std::atomic<bool> structured_{false};
    std::chrono::milliseconds rate_limit_{1000};
    std::atomic<std::chrono::steady_clock::time_point> last_log_{};
    
    std::string format_message(LogLevel level, const std::string& message);
    bool should_rate_limit();
};

// Macros for efficient logging
#define LOG_DEBUG(msg) if (rtes::Logger::instance().level_ <= rtes::LogLevel::DEBUG) rtes::Logger::instance().debug(msg)
#define LOG_INFO(msg) if (rtes::Logger::instance().level_ <= rtes::LogLevel::INFO) rtes::Logger::instance().info(msg)
#define LOG_WARN(msg) if (rtes::Logger::instance().level_ <= rtes::LogLevel::WARN) rtes::Logger::instance().warn(msg)
#define LOG_ERROR(msg) if (rtes::Logger::instance().level_ <= rtes::LogLevel::ERROR) rtes::Logger::instance().error(msg)
#define LOG_FATAL(msg) rtes::Logger::instance().fatal(msg)

} // namespace rtes