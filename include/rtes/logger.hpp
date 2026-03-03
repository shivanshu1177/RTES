#pragma once

#include <iostream>
#include <string>
#include <mutex>

namespace rtes {

class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }
    
    void info(const std::string& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << "[INFO] " << msg << std::endl;
    }
    
    void warn(const std::string& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << "[WARN] " << msg << std::endl;
    }
    
    void error(const std::string& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cerr << "[ERROR] " << msg << std::endl;
    }

private:
    std::mutex mutex_;
};

#define LOG_INFO(msg) rtes::Logger::instance().info(msg)
#define LOG_WARN(msg) rtes::Logger::instance().warn(msg)
#define LOG_ERROR(msg) rtes::Logger::instance().error(msg)

} // namespace rtes