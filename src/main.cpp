/**
 * @file main.cpp
 * @brief Main entry point for RTES trading exchange
 * 
 * Initializes and coordinates all exchange components:
 * - Exchange core (matching engines, risk manager)
 * - TCP Gateway (order entry)
 * - UDP Publisher (market data)
 * - Monitoring service (metrics, health checks)
 */

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

// Global shutdown flag for graceful termination
std::atomic<bool> shutdown_requested{false};

/**
 * @brief Signal handler for graceful shutdown
 * @param signal Signal number (SIGINT, SIGTERM)
 * 
 * Sets shutdown flag to trigger graceful component shutdown
 */
void signal_handler(int signal) {
    LOG_INFO("Received signal " + std::to_string(signal) + ", initiating graceful shutdown");
    shutdown_requested.store(true);
}

/**
 * @brief Main exchange runtime loop
 * @param config_ptr Unique pointer to configuration (ownership transferred)
 * 
 * Lifecycle:
 * 1. Initialize exchange core (matching engines, risk manager)
 * 2. Start TCP gateway for order entry
 * 3. Start UDP publisher for market data
 * 4. Start monitoring service for metrics/health
 * 5. Run until shutdown signal received
 * 6. Graceful shutdown in reverse order
 */
void run_exchange(std::unique_ptr<Config> config_ptr) {
    // Keep config reference for components (exchange takes ownership)
    const Config& config = *config_ptr;
    
    // Create and start exchange core (matching engines, risk manager, order pool)
    Exchange exchange(std::move(config_ptr));
    exchange.start();
    
    // Create and start TCP gateway for order entry (port 8888)
    TcpGateway gateway(config.exchange.tcp_port, exchange.get_risk_manager(), 
                      exchange.get_order_pool());
    gateway.start();
    
    // Create and start UDP publisher for market data multicast (port 9999)
    UdpPublisher udp_publisher(config.exchange.udp_multicast_group, 
                              config.exchange.udp_port,
                              exchange.get_market_data_queue());
    udp_publisher.start();
    
    // Create and start monitoring service for Prometheus metrics (port 8080)
    MonitoringService monitoring(config.exchange.metrics_port, &exchange);
    monitoring.start();
    
    LOG_INFO("All services started successfully");
    
    // Main event loop - wait for shutdown signal
    while (!shutdown_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Graceful shutdown in reverse order of startup
    LOG_INFO("Initiating graceful shutdown");
    monitoring.stop();      // Stop metrics collection first
    udp_publisher.stop();   // Stop market data publishing
    gateway.stop();         // Stop accepting new orders
    exchange.stop();        // Stop matching engines and risk manager
    LOG_INFO("Exchange shutdown complete");
}

} // namespace rtes

/**
 * @brief Main entry point
 * @param argc Argument count
 * @param argv Argument vector (expects config file path)
 * @return 0 on success, 1 on error
 * 
 * Startup sequence:
 * 1. Validate command line arguments
 * 2. Register signal handlers for graceful shutdown
 * 3. Load and validate configuration
 * 4. Configure logging subsystem
 * 5. Run exchange until shutdown
 */
int main(int argc, char* argv[]) {
    // Validate command line arguments
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <config_file>" << std::endl;
        return 1;
    }
    
    // Setup signal handlers for graceful shutdown (Ctrl+C, kill)
    std::signal(SIGINT, rtes::signal_handler);
    std::signal(SIGTERM, rtes::signal_handler);
    
    try {
        // Load configuration from JSON file
        auto config = rtes::Config::load_from_file(argv[1]);
        
        // Configure logger based on config settings
        auto& logger = rtes::Logger::instance();
        if (config->logging.level == "DEBUG") logger.set_level(rtes::LogLevel::DEBUG);
        else if (config->logging.level == "INFO") logger.set_level(rtes::LogLevel::INFO);
        else if (config->logging.level == "WARN") logger.set_level(rtes::LogLevel::WARN);
        else if (config->logging.level == "ERROR") logger.set_level(rtes::LogLevel::ERROR);
        
        // Set rate limiting to prevent log flooding
        logger.set_rate_limit(std::chrono::milliseconds(config->logging.rate_limit_ms));
        logger.enable_structured(config->logging.enable_structured);
        
        // Run exchange (blocks until shutdown signal)
        rtes::run_exchange(std::move(config));
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}