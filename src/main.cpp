#include "rtes/config.hpp"
#include "rtes/logger.hpp"
#include <iostream>
#include <csignal>
#include <atomic>

namespace rtes {

std::atomic<bool> shutdown_requested{false};

void signal_handler(int signal) {
    LOG_INFO("Received signal " + std::to_string(signal) + ", initiating graceful shutdown");
    shutdown_requested.store(true);
}

class Exchange {
public:
    explicit Exchange(std::unique_ptr<Config> config) 
        : config_(std::move(config)) {
        LOG_INFO("Initializing RTES Exchange: " + config_->exchange.name);
    }
    
    void run() {
        LOG_INFO("Starting exchange on TCP port " + std::to_string(config_->exchange.tcp_port));
        LOG_INFO("Market data multicast: " + config_->exchange.udp_multicast_group + 
                 ":" + std::to_string(config_->exchange.udp_port));
        LOG_INFO("Metrics endpoint: http://localhost:" + std::to_string(config_->exchange.metrics_port));
        
        // Main event loop
        while (!shutdown_requested.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        LOG_INFO("Exchange shutdown complete");
    }

private:
    std::unique_ptr<Config> config_;
};

} // namespace rtes

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <config_file>" << std::endl;
        return 1;
    }
    
    // Setup signal handlers
    std::signal(SIGINT, rtes::signal_handler);
    std::signal(SIGTERM, rtes::signal_handler);
    
    try {
        // Load configuration
        auto config = rtes::Config::load_from_file(argv[1]);
        
        // Configure logger
        auto& logger = rtes::Logger::instance();
        if (config->logging.level == "DEBUG") logger.set_level(rtes::LogLevel::DEBUG);
        else if (config->logging.level == "INFO") logger.set_level(rtes::LogLevel::INFO);
        else if (config->logging.level == "WARN") logger.set_level(rtes::LogLevel::WARN);
        else if (config->logging.level == "ERROR") logger.set_level(rtes::LogLevel::ERROR);
        
        logger.set_rate_limit(std::chrono::milliseconds(config->logging.rate_limit_ms));
        logger.enable_structured(config->logging.enable_structured);
        
        // Create and run exchange
        rtes::Exchange exchange(std::move(config));
        exchange.run();
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}