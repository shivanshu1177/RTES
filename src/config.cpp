#include "rtes/config.hpp"
#include "rtes/logger.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace rtes {

// Simple JSON parser for config (minimal implementation)
class JsonParser {
public:
    static std::unique_ptr<Config> parse(const std::string& content) {
        auto config = std::make_unique<Config>();
        
        // Parse exchange section
        config->exchange.name = extract_string(content, "name");
        config->exchange.tcp_port = extract_uint16(content, "tcp_port");
        config->exchange.udp_multicast_group = extract_string(content, "udp_multicast_group");
        config->exchange.udp_port = extract_uint16(content, "udp_port");
        config->exchange.metrics_port = extract_uint16(content, "metrics_port");
        
        // Parse risk section
        config->risk.max_order_size = extract_uint64(content, "max_order_size");
        config->risk.max_notional_per_client = extract_double(content, "max_notional_per_client");
        config->risk.max_orders_per_second = extract_uint32(content, "max_orders_per_second");
        config->risk.price_collar_enabled = extract_bool(content, "price_collar_enabled");
        
        // Parse performance section
        config->performance.order_pool_size = extract_uint32(content, "order_pool_size");
        config->performance.queue_capacity = extract_uint32(content, "queue_capacity");
        config->performance.enable_cpu_pinning = extract_bool(content, "enable_cpu_pinning");
        config->performance.tcp_nodelay = extract_bool(content, "tcp_nodelay");
        config->performance.udp_buffer_size = extract_uint32(content, "udp_buffer_size");
        
        // Parse logging section
        config->logging.level = extract_string(content, "level");
        config->logging.rate_limit_ms = extract_uint32(content, "rate_limit_ms");
        config->logging.enable_structured = extract_bool(content, "enable_structured");
        
        // Parse persistence section
        config->persistence.enable_event_log = extract_bool(content, "enable_event_log");
        config->persistence.snapshot_interval_ms = extract_uint32(content, "snapshot_interval_ms");
        config->persistence.log_directory = extract_string(content, "log_directory");
        
        // Parse symbols (simplified - assumes 3 symbols)
        config->symbols.push_back({"AAPL", 0.01, 1, 10.0});
        config->symbols.push_back({"MSFT", 0.01, 1, 10.0});
        config->symbols.push_back({"GOOGL", 0.01, 1, 10.0});
        
        return config;
    }

private:
    static std::string extract_string(const std::string& json, const std::string& key) {
        auto pos = json.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";
        
        pos = json.find(":", pos);
        pos = json.find("\"", pos);
        auto end = json.find("\"", pos + 1);
        
        return json.substr(pos + 1, end - pos - 1);
    }
    
    static uint16_t extract_uint16(const std::string& json, const std::string& key) {
        return static_cast<uint16_t>(extract_number(json, key));
    }
    
    static uint32_t extract_uint32(const std::string& json, const std::string& key) {
        return static_cast<uint32_t>(extract_number(json, key));
    }
    
    static uint64_t extract_uint64(const std::string& json, const std::string& key) {
        return static_cast<uint64_t>(extract_number(json, key));
    }
    
    static double extract_double(const std::string& json, const std::string& key) {
        return extract_number(json, key);
    }
    
    static bool extract_bool(const std::string& json, const std::string& key) {
        auto pos = json.find("\"" + key + "\"");
        if (pos == std::string::npos) return false;
        
        pos = json.find(":", pos);
        return json.find("true", pos) < json.find("false", pos);
    }
    
    static double extract_number(const std::string& json, const std::string& key) {
        auto pos = json.find("\"" + key + "\"");
        if (pos == std::string::npos) return 0;
        
        pos = json.find(":", pos);
        while (pos < json.length() && (json[pos] == ':' || json[pos] == ' ')) pos++;
        
        auto end = pos;
        while (end < json.length() && (std::isdigit(json[end]) || json[end] == '.')) end++;
        
        return std::stod(json.substr(pos, end - pos));
    }
};

std::unique_ptr<Config> Config::load_from_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config file: " + path);
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    
    try {
        return JsonParser::parse(buffer.str());
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse config: " + std::string(e.what()));
    }
}

} // namespace rtes