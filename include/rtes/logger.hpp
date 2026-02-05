#pragma once

#include <string>
#include <chrono>
#include <atomic>
#include <memory>
#include <format>
#include "security_utils.hpp"

namespace rtes {

enum class LogLevel {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3,
    FATAL = 4
};

class Logger {
public:
    static Logger& instance();

    void set_level(LogLevel level);
    LogLevel getLevel() const { return level_.load(); }
    void set_rate_limit(std::chrono::milliseconds limit);
    void enable_structured(bool enabled);

    void log(LogLevel level, const std::string& message);

    // Type-safe logging with automatic sanitization
    template<typename... Args>
    void log_safe(LogLevel level, std::string_view format_str, Args&&... args) {
        auto formatted = SecurityUtils::safe_format(format_str, args...);
        auto sanitized = SecurityUtils::sanitize_log_input(formatted);
        log(level, sanitized);
    }

    // Convenience methods with sanitization
    void debug(const std::string& msg) { log(LogLevel::DEBUG, SecurityUtils::sanitize_log_input(msg)); }
    void info(const std::string& msg)  { log(LogLevel::INFO,  SecurityUtils::sanitize_log_input(msg)); }
    void warn(const std::string& msg)  { log(LogLevel::WARN,  SecurityUtils::sanitize_log_input(msg)); }
    void error(const std::string& msg) { log(LogLevel::ERROR, SecurityUtils::sanitize_log_input(msg)); }
    void fatal(const std::string& msg) { log(LogLevel::FATAL, SecurityUtils::sanitize_log_input(msg)); }

    template<typename... Args>
    void debug_safe(std::string_view fmt, Args&&... args) { log_safe(LogLevel::DEBUG, fmt, args...); }
    template<typename... Args>
    void info_safe(std::string_view fmt, Args&&... args)  { log_safe(LogLevel::INFO,  fmt, args...); }
    template<typename... Args>
    void warn_safe(std::string_view fmt, Args&&... args)  { log_safe(LogLevel::WARN,  fmt, args...); }
    template<typename... Args>
    void error_safe(std::string_view fmt, Args&&... args)  { log_safe(LogLevel::ERROR, fmt, args...); }
    template<typename... Args>
    void fatal_safe(std::string_view fmt, Args&&... args)  { log_safe(LogLevel::FATAL, fmt, args...); }

private:
    Logger() = default;

    std::atomic<LogLevel> level_{LogLevel::INFO};
    std::atomic<bool> structured_{false};
    std::chrono::milliseconds rate_limit_{1000};
    std::atomic<std::chrono::steady_clock::time_point> last_log_{};

    std::string format_message(LogLevel level, const std::string& message);
    bool should_rate_limit();
};

// ---------------------------------------------------------------------------
// Variadic logging macros (support both single-message and formatted calls)
//
//   LOG_INFO("simple message");
//   LOG_INFO("order {} filled at {}", order_id, price);
//
// Uses C++20 __VA_OPT__ to conditionally insert the comma only when extra
// arguments are present, so std::format receives the correct argument list.
// Wrapped in do { } while(0) for safe use in all statement contexts.
// ---------------------------------------------------------------------------

#define LOG_DEBUG(fmt_str, ...)                                          \
    do {                                                                 \
        if (rtes::Logger::instance().getLevel() <= rtes::LogLevel::DEBUG)\
            rtes::Logger::instance().debug(                              \
                std::format(fmt_str __VA_OPT__(,) __VA_ARGS__));        \
    } while (0)

#define LOG_INFO(fmt_str, ...)                                           \
    do {                                                                 \
        if (rtes::Logger::instance().getLevel() <= rtes::LogLevel::INFO) \
            rtes::Logger::instance().info(                               \
                std::format(fmt_str __VA_OPT__(,) __VA_ARGS__));        \
    } while (0)

#define LOG_WARN(fmt_str, ...)                                           \
    do {                                                                 \
        if (rtes::Logger::instance().getLevel() <= rtes::LogLevel::WARN) \
            rtes::Logger::instance().warn(                               \
                std::format(fmt_str __VA_OPT__(,) __VA_ARGS__));        \
    } while (0)

#define LOG_ERROR(fmt_str, ...)                                          \
    do {                                                                 \
        if (rtes::Logger::instance().getLevel() <= rtes::LogLevel::ERROR)\
            rtes::Logger::instance().error(                              \
                std::format(fmt_str __VA_OPT__(,) __VA_ARGS__));        \
    } while (0)

#define LOG_FATAL(fmt_str, ...)                                          \
    do {                                                                 \
        rtes::Logger::instance().fatal(                                  \
            std::format(fmt_str __VA_OPT__(,) __VA_ARGS__));            \
    } while (0)

// Type-safe logging macros (with explicit sanitization via log_safe)
#define LOG_DEBUG_SAFE(fmt_str, ...)                                     \
    do {                                                                 \
        if (rtes::Logger::instance().getLevel() <= rtes::LogLevel::DEBUG)\
            rtes::Logger::instance().debug_safe(fmt_str, __VA_ARGS__);  \
    } while (0)

#define LOG_INFO_SAFE(fmt_str, ...)                                      \
    do {                                                                 \
        if (rtes::Logger::instance().getLevel() <= rtes::LogLevel::INFO) \
            rtes::Logger::instance().info_safe(fmt_str, __VA_ARGS__);   \
    } while (0)

#define LOG_WARN_SAFE(fmt_str, ...)                                      \
    do {                                                                 \
        if (rtes::Logger::instance().getLevel() <= rtes::LogLevel::WARN) \
            rtes::Logger::instance().warn_safe(fmt_str, __VA_ARGS__);   \
    } while (0)

#define LOG_ERROR_SAFE(fmt_str, ...)                                     \
    do {                                                                 \
        if (rtes::Logger::instance().getLevel() <= rtes::LogLevel::ERROR)\
            rtes::Logger::instance().error_safe(fmt_str, __VA_ARGS__);  \
    } while (0)

#define LOG_FATAL_SAFE(fmt_str, ...)                                     \
    do {                                                                 \
        rtes::Logger::instance().fatal_safe(fmt_str, __VA_ARGS__);      \
    } while (0)

} // namespace rtes