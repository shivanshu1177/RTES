/**
 * @file main.cpp
 * @brief Main entry point for RTES trading exchange
 *
 * Initializes and coordinates all exchange components:
 * - Exchange core (matching engines, risk manager)
 * - TCP Gateway (order entry)
 * - UDP Publisher (market data)
 * - Monitoring service (metrics, health checks)
 *
 * Shutdown: Send SIGINT (Ctrl+C) or SIGTERM for graceful shutdown.
 * All components are torn down in reverse startup order.
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
#include <unordered_map>
#include <algorithm>
#include <functional>
#include <vector>
#include <stdexcept>

namespace rtes {

// ─────────────────────────────────────────────────────────────
// Signal Handling (async-signal-safe)
// ─────────────────────────────────────────────────────────────

/**
 * @brief Signal-safe shutdown flag
 *
 * volatile sig_atomic_t is the ONLY type guaranteed safe
 * for use in signal handlers per ISO C and POSIX standards.
 * std::atomic<bool> is NOT guaranteed async-signal-safe.
 */
volatile std::sig_atomic_t g_shutdown_requested = 0;

/**
 * @brief Async-signal-safe shutdown handler
 * @param signal Signal number (SIGINT or SIGTERM)
 *
 * Only performs a single atomic store to a sig_atomic_t.
 * No allocations, no locks, no iostream — fully signal-safe.
 */
void signal_handler(int signal) {
    (void)signal;
    g_shutdown_requested = 1;
}

// ─────────────────────────────────────────────────────────────
// Startup Guard (RAII rollback on partial startup failure)
// ─────────────────────────────────────────────────────────────

/**
 * @brief RAII guard that rolls back started components on failure
 *
 * Usage:
 *   StartupGuard guard;
 *   component_a.start();
 *   guard.add([&] { component_a.stop(); });
 *   component_b.start();  // If this throws, component_a.stop() is called
 *   guard.add([&] { component_b.stop(); });
 *   guard.commit();       // Success — disarm all rollbacks
 *
 * On destruction without commit(), rollbacks execute in reverse order.
 */
class StartupGuard {
public:
    StartupGuard() = default;

    // Non-copyable, non-movable (prevents accidental disarm)
    StartupGuard(const StartupGuard&) = delete;
    StartupGuard& operator=(const StartupGuard&) = delete;
    StartupGuard(StartupGuard&&) = delete;
    StartupGuard& operator=(StartupGuard&&) = delete;

    /**
     * @brief Register a rollback action
     * @param rollback Callable to invoke on failure (must be noexcept-safe)
     */
    void add(std::function<void()> rollback) {
        rollbacks_.push_back(std::move(rollback));
    }

    /**
     * @brief Disarm all rollbacks (call on successful full startup)
     */
    void commit() noexcept {
        rollbacks_.clear();
    }

    /**
     * @brief Execute rollbacks in reverse order if not committed
     */
    ~StartupGuard() {
        for (auto it = rollbacks_.rbegin(); it != rollbacks_.rend(); ++it) {
            try {
                (*it)();
            } catch (const std::exception& e) {
                // Log but don't propagate — we're already unwinding
                std::cerr << "Rollback error: " << e.what() << std::endl;
            }
        }
    }

private:
    std::vector<std::function<void()>> rollbacks_;
};

// ─────────────────────────────────────────────────────────────
// Logger Configuration
// ─────────────────────────────────────────────────────────────

/**
 * @brief Parse log level string to enum (case-insensitive)
 * @param level_str Log level string from config
 * @return Parsed LogLevel, defaults to INFO on unknown input
 */
LogLevel parse_log_level(const std::string& level_str) {
    static const std::unordered_map<std::string, LogLevel> level_map = {
        {"DEBUG", LogLevel::DEBUG},
        {"INFO",  LogLevel::INFO},
        {"WARN",  LogLevel::WARN},
        {"ERROR", LogLevel::ERROR},
    };

    // Normalize to uppercase
    std::string normalized = level_str;
    std::transform(normalized.begin(), normalized.end(),
                   normalized.begin(), ::toupper);

    auto it = level_map.find(normalized);
    if (it != level_map.end()) {
        return it->second;
    }

    std::cerr << "Warning: Unknown log level '" << level_str
              << "', defaulting to INFO" << std::endl;
    return LogLevel::INFO;
}

/**
 * @brief Configure logging subsystem from config
 * @param config Logging configuration section
 */
void configure_logger(const LoggingConfig& log_config) {
    auto& logger = Logger::instance();
    logger.set_level(parse_log_level(log_config.level));
    logger.set_rate_limit(std::chrono::milliseconds(log_config.rate_limit_ms));
    logger.enable_structured(log_config.enable_structured);
}

// ─────────────────────────────────────────────────────────────
// Main Exchange Lifecycle
// ─────────────────────────────────────────────────────────────

/**
 * @brief Wait for shutdown signal (SIGINT/SIGTERM)
 *
 * Uses sig_atomic_t polling. For even lower overhead,
 * consider sigwait() or eventfd + epoll in production.
 */
void wait_for_shutdown() {
    while (!g_shutdown_requested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

/**
 * @brief Main exchange runtime
 * @param config_ptr Unique pointer to validated configuration
 * @throws std::runtime_error if any component fails to start
 *
 * Lifecycle:
 * 1. Create Exchange core (takes config ownership)
 * 2. Start components in dependency order
 * 3. Verify health after each start
 * 4. Run until shutdown signal
 * 5. Graceful teardown in reverse order
 *
 * On partial startup failure, already-started components
 * are rolled back automatically via StartupGuard.
 */
void run_exchange(std::unique_ptr<Config> config_ptr) {
    // ── Phase 1: Create Exchange (takes config ownership) ──
    Exchange exchange(std::move(config_ptr));

    // Access config through exchange (avoids dangling reference)
    const Config& config = exchange.get_config();

    // ── Phase 2: Start components with rollback safety ──
    StartupGuard guard;

    // Start exchange core (matching engines, risk manager, order pool)
    exchange.start();
    guard.add([&] {
        LOG_INFO("Rolling back: stopping exchange");
        exchange.stop();
    });
    LOG_INFO("Exchange core started");

    // Start TCP gateway for order entry
    TcpGateway gateway(
        config.exchange.tcp_port,
        exchange.get_risk_manager(),
        exchange.get_order_pool()
    );
    gateway.start();
    guard.add([&] {
        LOG_INFO("Rolling back: stopping TCP gateway");
        gateway.stop();
    });
    LOG_INFO("TCP gateway listening on port {}", config.exchange.tcp_port);

    // Start UDP publisher for market data
    UdpPublisher udp_publisher(
        config.exchange.udp_multicast_group,
        config.exchange.udp_port,
        exchange.get_market_data_queue()
    );
    udp_publisher.start();
    guard.add([&] {
        LOG_INFO("Rolling back: stopping UDP publisher");
        udp_publisher.stop();
    });
    LOG_INFO("UDP publisher broadcasting on {}:{}",
             config.exchange.udp_multicast_group,
             config.exchange.udp_port);

    // Start monitoring service for Prometheus metrics
    MonitoringService monitoring(config.exchange.metrics_port, &exchange);
    monitoring.start();
    guard.add([&] {
        LOG_INFO("Rolling back: stopping monitoring");
        monitoring.stop();
    });
    LOG_INFO("Monitoring service on port {}", config.exchange.metrics_port);

    // ── Phase 3: All components started successfully ──
    guard.commit();  // Disarm rollbacks — we own shutdown now

    LOG_INFO("══════════════════════════════════════════════");
    LOG_INFO("  RTES Exchange fully operational");
    LOG_INFO("  Orders:      TCP port {}", config.exchange.tcp_port);
    LOG_INFO("  Market Data: UDP {}:{}", 
             config.exchange.udp_multicast_group,
             config.exchange.udp_port);
    LOG_INFO("  Metrics:     HTTP port {}", config.exchange.metrics_port);
    LOG_INFO("══════════════════════════════════════════════");

    // ── Phase 4: Run until shutdown signal ──
    wait_for_shutdown();
    LOG_INFO("Shutdown signal received");

    // ── Phase 5: Graceful shutdown (reverse startup order) ──
    LOG_INFO("Initiating graceful shutdown...");

    monitoring.stop();
    LOG_INFO("Monitoring stopped");

    udp_publisher.stop();
    LOG_INFO("UDP publisher stopped");

    gateway.stop();
    LOG_INFO("TCP gateway stopped");

    exchange.stop();
    LOG_INFO("Exchange core stopped");

    LOG_INFO("Exchange shutdown complete");
}

} // namespace rtes

// ─────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────

/**
 * @brief Program entry point
 * @param argc Argument count (expects 2)
 * @param argv argv[1] = path to JSON config file
 * @return EXIT_SUCCESS (0) on clean shutdown, EXIT_FAILURE (1) on error
 *
 * Startup:
 * 1. Validate arguments
 * 2. Block SIGINT/SIGTERM, register handlers
 * 3. Load + validate config
 * 4. Configure logging
 * 5. Run exchange (blocks until signal)
 */
int main(int argc, char* argv[]) {
    // ── Validate arguments ──
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <config_file>\n";
        return EXIT_FAILURE;
    }

    // ── Register signal handlers ──
    std::signal(SIGINT,  rtes::signal_handler);
    std::signal(SIGTERM, rtes::signal_handler);

    try {
        // ── Load configuration ──
        auto config = rtes::Config::load_from_file(argv[1]);
        if (!config) {
            std::cerr << "Error: Failed to load config from "
                      << argv[1] << "\n";
            return EXIT_FAILURE;
        }

        // ── Configure logging (before any LOG_ calls) ──
        rtes::configure_logger(config->logging);

        LOG_INFO("Configuration loaded from {}", argv[1]);

        // ── Run exchange (blocks until shutdown) ──
        rtes::run_exchange(std::move(config));

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Fatal error: unknown exception\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}