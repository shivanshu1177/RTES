#include "rtes/monitoring.hpp"
#include "rtes/logger.hpp"
#include <sstream>

namespace rtes {

MonitoringService::MonitoringService(uint16_t port, Exchange* exchange)
    : port_(port), exchange_(exchange) {
    
    http_server_ = std::make_unique<HttpServer>(port);
    
    // Initialize metrics
    orders_total_ = METRICS_COUNTER("rtes_orders_total");
    orders_rejected_ = METRICS_COUNTER("rtes_orders_rejected_total");
    trades_total_ = METRICS_COUNTER("rtes_trades_total");
    tcp_connections_ = METRICS_COUNTER("rtes_tcp_connections_total");
    udp_messages_ = METRICS_COUNTER("rtes_udp_messages_total");
    order_latency_ = METRICS_LATENCY_HISTOGRAM("rtes_order_latency_seconds");
    queue_depth_ = METRICS_HISTOGRAM("rtes_queue_depth", {1, 5, 10, 50, 100, 500, 1000, 5000});
    
    setup_http_handlers();
}

MonitoringService::~MonitoringService() {
    stop();
}

void MonitoringService::start() {
    if (running_.load()) return;
    
    running_.store(true);
    http_server_->start();
    metrics_thread_ = std::thread(&MonitoringService::metrics_collection_loop, this);
    
    LOG_INFO("Monitoring service started on port " + std::to_string(port_));
}

void MonitoringService::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    
    if (metrics_thread_.joinable()) {
        metrics_thread_.join();
    }
    
    http_server_->stop();
    
    LOG_INFO("Monitoring service stopped");
}

void MonitoringService::record_order_latency(double latency_seconds) {
    order_latency_->observe(latency_seconds);
}

void MonitoringService::record_trade_executed() {
    trades_total_->increment();
}

void MonitoringService::record_order_rejected() {
    orders_rejected_->increment();
}

void MonitoringService::setup_http_handlers() {
    http_server_->add_handler("/metrics", 
        [this](const std::string& path, const std::string& query) {
            return handle_metrics(path, query);
        });
    
    http_server_->add_handler("/health",
        [this](const std::string& path, const std::string& query) {
            return handle_health(path, query);
        });
    
    http_server_->add_handler("/ready",
        [this](const std::string& path, const std::string& query) {
            return handle_ready(path, query);
        });
}

void MonitoringService::metrics_collection_loop() {
    while (running_.load()) {
        collect_system_metrics();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

std::string MonitoringService::handle_metrics(const std::string&, const std::string&) {
    return MetricsRegistry::instance().get_prometheus_output();
}

std::string MonitoringService::handle_health(const std::string&, const std::string&) {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"status\": \"healthy\",\n";
    ss << "  \"timestamp\": " << std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() << ",\n";
    ss << "  \"uptime_seconds\": " << orders_total_->get() << "\n";  // Simplified
    ss << "}\n";
    return ss.str();
}

std::string MonitoringService::handle_ready(const std::string&, const std::string&) {
    // Check if exchange components are ready
    bool ready = (exchange_ != nullptr) && 
                 (exchange_->get_risk_manager() != nullptr);
    
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"ready\": " << (ready ? "true" : "false") << ",\n";
    ss << "  \"components\": {\n";
    ss << "    \"risk_manager\": " << (exchange_->get_risk_manager() ? "true" : "false") << ",\n";
    ss << "    \"order_pool\": " << (exchange_->get_order_pool() ? "true" : "false") << "\n";
    ss << "  }\n";
    ss << "}\n";
    return ss.str();
}

void MonitoringService::collect_system_metrics() {
    if (!exchange_) return;
    
    // Collect metrics from exchange components
    auto* risk_manager = exchange_->get_risk_manager();
    if (risk_manager) {
        orders_total_->increment(0);  // Update with actual count
        // Note: In a real implementation, we'd get actual values from components
    }
    
    // Collect queue depth metrics (simplified)
    queue_depth_->observe(10);  // Placeholder - would get actual queue depths
}

} // namespace rtes