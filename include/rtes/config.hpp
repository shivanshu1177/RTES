#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace rtes {

struct SymbolConfig {
    std::string symbol;
    double tick_size{0.01};
    uint64_t lot_size{1};
    double price_collar_pct{10.0};
};

struct ExchangeConfig {
    std::string name;
    uint16_t tcp_port{0};
    std::string udp_multicast_group;
    uint16_t udp_port{0};
    uint16_t metrics_port{0};
};

struct RiskConfig {
    uint64_t max_order_size{100000};
    double   max_notional_per_client{10000000.0};
    uint32_t max_orders_per_second{1000};
    bool     price_collar_enabled{true};
};

struct PerformanceConfig {
    uint32_t order_pool_size{0};
    uint32_t queue_capacity{0};
    bool     enable_cpu_pinning{false};
    bool     tcp_nodelay{false};
    uint32_t udp_buffer_size{0};
    uint32_t market_data_queue_size{4096};   // ← ADDED (fixes exchange.cpp:101)
};

struct LoggingConfig {
    std::string level;
    uint32_t rate_limit_ms{0};
    bool enable_structured{false};
};

struct PersistenceConfig {
    bool enable_event_log{false};
    uint32_t snapshot_interval_ms{0};
    std::string log_directory;
};

struct Config {
    ExchangeConfig exchange;
    std::vector<SymbolConfig> symbols;
    RiskConfig risk;
    PerformanceConfig performance;
    LoggingConfig logging;
    PersistenceConfig persistence;

    static std::unique_ptr<Config> load_from_file(const std::string& path);
};

} // namespace rtes