#include "rtes/dashboard.hpp"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

namespace rtes {

// OperationalDashboard implementation
OperationalDashboard& OperationalDashboard::instance() {
    static OperationalDashboard instance;
    return instance;
}

void OperationalDashboard::initialize() {
    setup_default_panels();
    LOG_STRUCTURED(LogLevel::INFO, "Dashboard", "Initialized operational dashboard");
}

void OperationalDashboard::start_server(uint16_t port) {
    if (server_running_.exchange(true)) {
        return; // Already running
    }
    
    port_ = port;
    server_thread_ = std::thread(&OperationalDashboard::server_loop, this);
    
    LOG_STRUCTURED(LogLevel::INFO, "Dashboard", "Started dashboard server",
                  {"port", std::to_string(port)});
}

void OperationalDashboard::stop_server() {
    server_running_.store(false);
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

void OperationalDashboard::add_panel(const DashboardPanel& panel) {
    std::unique_lock lock(panels_mutex_);
    panels_.push_back(panel);
}

std::string OperationalDashboard::render_dashboard() const {
    std::ostringstream html;
    
    html << R"HTML(<!DOCTYPE html>
<html>
<head>
    <title>RTES Operational Dashboard</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background: #f5f5f5; }
        .header { background: #2c3e50; color: white; padding: 20px; margin: -20px -20px 20px -20px; }
        .grid { display: grid; grid-template-columns: repeat(12, 1fr); gap: 20px; }
        .panel { background: white; border-radius: 8px; padding: 20px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        .widget { margin-bottom: 20px; }
        .metric { font-size: 2em; font-weight: bold; color: #27ae60; }
        .alert { background: #e74c3c; color: white; padding: 10px; border-radius: 4px; margin: 5px 0; }
        .warning { background: #f39c12; }
        .info { background: #3498db; }
        .chart { height: 200px; background: #ecf0f1; border-radius: 4px; display: flex; align-items: center; justify-content: center; }
        .refresh { position: fixed; top: 20px; right: 20px; background: #3498db; color: white; border: none; padding: 10px 20px; border-radius: 4px; cursor: pointer; }
    </style>
    <script>
        function refreshDashboard() {
            location.reload();
        }
        setInterval(refreshDashboard, 30000); // Auto-refresh every 30 seconds
    </script>
</head>
<body>
    <div class="header">
        <h1>RTES Operational Dashboard</h1>
        <p>Real-Time Trading Exchange Simulator - System Status</p>
    </div>
    <button class="refresh" onclick="refreshDashboard()">Refresh</button>
    <div class="grid">
)HTML";
    
    std::shared_lock lock(panels_mutex_);
    for (const auto& panel : panels_) {
        html << render_panel(panel);
    }
    
    html << R"HTML(
    </div>
</body>
</html>)HTML";
    
    return html.str();
}

std::string OperationalDashboard::get_metrics_data() const {
    auto& metrics = MetricsCollector::instance();
    return metrics.export_prometheus();
}

std::string OperationalDashboard::get_alerts_data() const {
    auto& alert_manager = AlertManager::instance();
    auto alerts = alert_manager.get_active_alerts();
    
    std::ostringstream json;
    json << "{\"alerts\":[";
    
    bool first = true;
    for (const auto& alert : alerts) {
        if (!first) json << ",";
        json << "{";
        json << "\"name\":\"" << alert.name << "\",";
        json << "\"severity\":\"" << static_cast<int>(alert.severity) << "\",";
        json << "\"message\":\"" << alert.message << "\",";
        json << "\"timestamp\":\"" << std::chrono::duration_cast<std::chrono::seconds>(
            alert.timestamp.time_since_epoch()).count() << "\"";
        json << "}";
        first = false;
    }
    
    json << "]}";
    return json.str();
}

void OperationalDashboard::server_loop() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        LOG_STRUCTURED(LogLevel::ERROR, "Dashboard", "Failed to create dashboard server socket");
        return;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);
    
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        LOG_STRUCTURED(LogLevel::ERROR, "Dashboard", "Failed to bind dashboard server",
                      {"port", std::to_string(port_)});
        close(server_fd);
        return;
    }
    
    if (listen(server_fd, 10) < 0) {
        LOG_STRUCTURED(LogLevel::ERROR, "Dashboard", "Failed to listen on dashboard server");
        close(server_fd);
        return;
    }
    
    while (server_running_.load()) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (server_running_.load()) {
                LOG_STRUCTURED(LogLevel::ERROR, "Dashboard", "Failed to accept dashboard connection");
            }
            continue;
        }
        
        // Simple HTTP request handling
        char buffer[4096] = {0};
        read(client_fd, buffer, sizeof(buffer) - 1);
        
        std::string response;
        std::string http_response;
        
        if (strstr(buffer, "GET / ") || strstr(buffer, "GET /dashboard")) {
            response = render_dashboard();
            http_response = "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/html\r\n"
                           "Content-Length: " + std::to_string(response.length()) + "\r\n"
                           "\r\n" + response;
        } else if (strstr(buffer, "GET /api/metrics")) {
            response = get_metrics_data();
            http_response = "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/plain\r\n"
                           "Content-Length: " + std::to_string(response.length()) + "\r\n"
                           "\r\n" + response;
        } else if (strstr(buffer, "GET /api/alerts")) {
            response = get_alerts_data();
            http_response = "HTTP/1.1 200 OK\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: " + std::to_string(response.length()) + "\r\n"
                           "\r\n" + response;
        } else {
            response = "404 Not Found";
            http_response = "HTTP/1.1 404 Not Found\r\n"
                           "Content-Type: text/plain\r\n"
                           "Content-Length: " + std::to_string(response.length()) + "\r\n"
                           "\r\n" + response;
        }
        
        write(client_fd, http_response.c_str(), http_response.length());
        close(client_fd);
    }
    
    close(server_fd);
}

void OperationalDashboard::setup_default_panels() {
    // System Health Panel
    DashboardPanel health_panel;
    health_panel.id = "system_health";
    health_panel.title = "System Health";
    health_panel.grid_x = 0;
    health_panel.grid_y = 0;
    health_panel.grid_w = 6;
    health_panel.grid_h = 4;
    
    health_panel.widgets = {
        {"cpu_usage", "CPU Usage", "metric", "system_cpu_percent", {}},
        {"memory_usage", "Memory Usage", "metric", "system_memory_percent", {}},
        {"active_connections", "Active Connections", "metric", "tcp_connections_active", {}}
    };
    
    // Performance Panel
    DashboardPanel perf_panel;
    perf_panel.id = "performance";
    perf_panel.title = "Performance Metrics";
    perf_panel.grid_x = 6;
    perf_panel.grid_y = 0;
    perf_panel.grid_w = 6;
    perf_panel.grid_h = 4;
    
    perf_panel.widgets = {
        {"avg_latency", "Average Latency (μs)", "metric", "order_processing_latency_us", {}},
        {"throughput", "Orders/sec", "metric", "orders_per_second", {}},
        {"error_rate", "Error Rate %", "metric", "error_rate_percent", {}}
    };
    
    // Business Metrics Panel
    DashboardPanel business_panel;
    business_panel.id = "business";
    business_panel.title = "Business Metrics";
    business_panel.grid_x = 0;
    business_panel.grid_y = 4;
    business_panel.grid_w = 8;
    business_panel.grid_h = 4;
    
    business_panel.widgets = {
        {"total_volume", "Total Volume", "metric", "total_trading_volume", {}},
        {"active_symbols", "Active Symbols", "metric", "active_symbols_count", {}},
        {"client_count", "Active Clients", "metric", "active_clients_count", {}}
    };
    
    // Alerts Panel
    DashboardPanel alerts_panel;
    alerts_panel.id = "alerts";
    alerts_panel.title = "Active Alerts";
    alerts_panel.grid_x = 8;
    alerts_panel.grid_y = 4;
    alerts_panel.grid_w = 4;
    alerts_panel.grid_h = 4;
    
    alerts_panel.widgets = {
        {"active_alerts", "Active Alerts", "alert", "active_alerts", {}}
    };
    
    std::unique_lock lock(panels_mutex_);
    panels_ = {health_panel, perf_panel, business_panel, alerts_panel};
}

std::string OperationalDashboard::render_panel(const DashboardPanel& panel) const {
    std::ostringstream html;
    
    html << "<div class=\"panel\" style=\"grid-column: span " << panel.grid_w 
         << "; grid-row: span " << panel.grid_h << ";\">";
    html << "<h2>" << panel.title << "</h2>";
    
    for (const auto& widget : panel.widgets) {
        html << render_widget(widget);
    }
    
    html << "</div>";
    return html.str();
}

std::string OperationalDashboard::render_widget(const DashboardWidget& widget) const {
    std::ostringstream html;
    
    html << "<div class=\"widget\">";
    html << "<h3>" << widget.title << "</h3>";
    
    if (widget.type == "metric") {
        // Get actual metric value
        auto& metrics = MetricsCollector::instance();
        auto all_metrics = metrics.get_all_metrics();
        
        double value = 0.0;
        auto it = all_metrics.find(widget.query + "_gauge");
        if (it != all_metrics.end() && !it->second.empty()) {
            value = it->second.back().value;
        }
        
        html << "<div class=\"metric\">" << std::fixed << std::setprecision(2) << value << "</div>";
        
    } else if (widget.type == "alert") {
        auto& alert_manager = AlertManager::instance();
        auto alerts = alert_manager.get_active_alerts();
        
        if (alerts.empty()) {
            html << "<div class=\"info\">No active alerts</div>";
        } else {
            for (const auto& alert : alerts) {
                std::string css_class = "alert";
                if (alert.severity == AlertSeverity::WARNING) css_class += " warning";
                else if (alert.severity == AlertSeverity::INFO) css_class += " info";
                
                html << "<div class=\"" << css_class << "\">";
                html << "<strong>" << alert.name << "</strong><br>";
                html << alert.message;
                html << "</div>";
            }
        }
        
    } else if (widget.type == "chart") {
        html << "<div class=\"chart\">Chart: " << widget.query << "</div>";
    }
    
    html << "</div>";
    return html.str();
}

// AlertingDashboard implementation
AlertingDashboard& AlertingDashboard::instance() {
    static AlertingDashboard instance;
    return instance;
}

std::string AlertingDashboard::render_alerts_page() const {
    auto& alert_manager = AlertManager::instance();
    auto alerts = alert_manager.get_active_alerts();
    
    std::ostringstream html;
    
    html << R"HTML(<!DOCTYPE html>
<html>
<head>
    <title>RTES Alerts</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        .header { background: #e74c3c; color: white; padding: 20px; margin: -20px -20px 20px -20px; }
        .alert-table { width: 100%; border-collapse: collapse; margin-top: 20px; }
        .alert-table th, .alert-table td { border: 1px solid #ddd; padding: 12px; text-align: left; }
        .alert-table th { background-color: #f2f2f2; }
        .severity-critical { background-color: #e74c3c; color: white; }
        .severity-warning { background-color: #f39c12; color: white; }
        .severity-info { background-color: #3498db; color: white; }
        .actions button { margin: 2px; padding: 5px 10px; border: none; border-radius: 3px; cursor: pointer; }
        .ack-btn { background: #27ae60; color: white; }
        .silence-btn { background: #95a5a6; color: white; }
    </style>
</head>
<body>
    <div class="header">
        <h1>RTES Alert Management</h1>
        <p>Active alerts and incident management</p>
    </div>
)HTML";
    
    html << format_alert_table(alerts);
    
    html << R"HTML(
</body>
</html>)HTML";
    
    return html.str();
}

std::string AlertingDashboard::format_alert_table(const std::vector<Alert>& alerts) const {
    std::ostringstream html;
    
    html << "<table class=\"alert-table\">";
    html << "<tr><th>Alert Name</th><th>Severity</th><th>Message</th><th>Timestamp</th><th>Actions</th></tr>";
    
    for (const auto& alert : alerts) {
        html << "<tr>";
        html << "<td>" << alert.name << "</td>";
        html << "<td class=\"" << get_alert_severity_color(alert.severity) << "\">";
        
        switch (alert.severity) {
            case AlertSeverity::CRITICAL: html << "CRITICAL"; break;
            case AlertSeverity::WARNING: html << "WARNING"; break;
            case AlertSeverity::INFO: html << "INFO"; break;
        }
        
        html << "</td>";
        html << "<td>" << alert.message << "</td>";
        
        auto time_t = std::chrono::system_clock::to_time_t(alert.timestamp);
        html << "<td>" << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S") << "</td>";
        
        html << "<td class=\"actions\">";
        html << "<button class=\"ack-btn\" onclick=\"acknowledgeAlert('" << alert.name << "')\">Acknowledge</button>";
        html << "<button class=\"silence-btn\" onclick=\"silenceAlert('" << alert.name << "')\">Silence 1h</button>";
        html << "</td>";
        
        html << "</tr>";
    }
    
    html << "</table>";
    
    if (alerts.empty()) {
        html << "<p>No active alerts. System is healthy!</p>";
    }
    
    return html.str();
}

std::string AlertingDashboard::get_alert_severity_color(AlertSeverity severity) const {
    switch (severity) {
        case AlertSeverity::CRITICAL: return "severity-critical";
        case AlertSeverity::WARNING: return "severity-warning";
        case AlertSeverity::INFO: return "severity-info";
        default: return "";
    }
}

std::string AlertingDashboard::get_alert_history() const {
    // Implementation would query historical alert data
    return R"({"history": []})";
}

void AlertingDashboard::acknowledge_alert(const std::string& alert_id) {
    AlertManager::instance().resolve_alert(alert_id);
    
    LOG_STRUCTURED(LogLevel::INFO, "AlertingDashboard", "Alert acknowledged",
                  {"alert_id", alert_id});
}

void AlertingDashboard::silence_alert(const std::string& alert_id, std::chrono::minutes duration) {
    // Implementation would silence alert for specified duration
    LOG_STRUCTURED(LogLevel::INFO, "AlertingDashboard", "Alert silenced",
                  {"alert_id", alert_id},
                  {"duration_minutes", std::to_string(duration.count())});
}

} // namespace rtes