#pragma once

#include "rtes/http_server.hpp"
#include "rtes/metrics.hpp"
#include "rtes/exchange.hpp"
#include <thread>
#include <atomic>
#include <chrono>

namespace rtes {

class MonitoringService {
public:
    explicit MonitoringService(uint16_t port, Exchange* exchange);
    ~MonitoringService();
    
    void start();
    void stop();
    
    // Metric collection
    void record_order_latency(double latency_seconds);
    void record_trade_executed();
    void record_order_rejected();

private:
    uint16_t port_;
    Exchange* exchange_;
    
    std::unique_ptr<HttpServer> http_server_;
    std::thread metrics_thread_;
    std::atomic<bool> running_{false};
    
    // Metrics
    Counter* orders_total_;
    Counter* orders_rejected_;
    Counter* trades_total_;
    Counter* tcp_connections_;
    Counter* udp_messages_;
    Histogram* order_latency_;
    Histogram* queue_depth_;
    
    void setup_http_handlers();
    void metrics_collection_loop();
    
    // HTTP handlers
    std::string handle_metrics(const std::string& path, const std::string& query);
    std::string handle_health(const std::string& path, const std::string& query);
    std::string handle_ready(const std::string& path, const std::string& query);
    
    void collect_system_metrics();
};

} // namespace rtes