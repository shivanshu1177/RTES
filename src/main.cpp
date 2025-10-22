#include "rtes/exchange.hpp"
#include "rtes/tcp_gateway.hpp"
#include "rtes/udp_publisher.hpp"
#include "rtes/monitoring.hpp"
#include "rtes/config.hpp"
#include "rtes/logger.hpp"
#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>

namespace rtes {

std::atomic<bool> shutdown_requested{false};

void signal_handler(int signal) {
    LOG_INFO("Received signal " + std::to_string(signal) + ", initiating graceful shutdown");
    shutdown_requested.store(true);
}

void run_exchange(std::unique_ptr<Config> config_ptr) {
    // Keep config reference for components
    const Config& config = *config_ptr;
    
    // Create and start exchange
    Exchange exchange(std::move(config_ptr));
    exchange.start();
    
    // Create and start TCP gateway
    TcpGateway gateway(config.exchange.tcp_port, exchange.get_risk_manager(), 
                      exchange.get_order_pool());
    gateway.start();
    
    // Create and start UDP publisher
    UdpPublisher udp_publisher(config.exchange.udp_multicast_group, 
                              config.exchange.udp_port,
                              exchange.get_market_data_queue());
    udp_publisher.start();
    
    // Create and start monitoring service
    MonitoringService monitoring(config.exchange.metrics_port, &exchange);
    monitoring.start();
    
    LOG_INFO("All services started successfully");
    
    // Main event loop
    while (!shutdown_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Graceful shutdown
    monitoring.stop();
    udp_publisher.stop();
    gateway.stop();
    exchange.stop();
    LOG_INFO("Exchange shutdown complete");
}

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
        
        // Run exchange
        rtes::run_exchange(std::move(config));
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}