#include "rtes/logger.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>

namespace rtes {

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

void Logger::log(LogLevel level, const std::string& message) {
    if (level < level_.load()) return;
    if (should_rate_limit()) return;
    
    std::cout << format_message(level, message) << std::endl;
}

// Removed unsafe printf-style logging - use log_safe() template methods instead

std::string Logger::format_message(LogLevel level, const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    
    if (structured_.load()) {
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

bool Logger::should_rate_limit() {
    auto now = std::chrono::steady_clock::now();
    auto last = last_log_.load();
    
    if (now - last < rate_limit_) {
        return true;
    }
    
    last_log_.store(now);
    return false;
}

} // namespace rtes