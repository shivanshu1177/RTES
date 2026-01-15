#pragma once

#include "rtes/observability.hpp"
#include "rtes/thread_safety.hpp"
#include <string>
#include <vector>
#include <memory>

namespace rtes {

struct DashboardWidget {
    std::string id;
    std::string title;
    std::string type; // "metric", "chart", "table", "alert"
    std::string query;
    std::unordered_map<std::string, std::string> config;
};

struct DashboardPanel {
    std::string id;
    std::string title;
    std::vector<DashboardWidget> widgets;
    int grid_x{0};
    int grid_y{0};
    int grid_w{12};
    int grid_h{8};
};

class OperationalDashboard {
public:
    static OperationalDashboard& instance();
    
    void initialize();
    void start_server(uint16_t port = 3000);
    void stop_server();
    
    void add_panel(const DashboardPanel& panel);
    std::string render_dashboard() const;
    std::string get_metrics_data() const;
    std::string get_alerts_data() const;

private:
    atomic_wrapper<bool> server_running_{false};
    std::thread server_thread_;
    uint16_t port_{3000};
    
    mutable std::shared_mutex panels_mutex_;
    std::vector<DashboardPanel> panels_ GUARDED_BY(panels_mutex_);
    
    void server_loop();
    void setup_default_panels();
    std::string render_panel(const DashboardPanel& panel) const;
    std::string render_widget(const DashboardWidget& widget) const;
    std::string handle_api_request(const std::string& path) const;
};

class AlertingDashboard {
public:
    static AlertingDashboard& instance();
    
    std::string render_alerts_page() const;
    std::string get_alert_history() const;
    void acknowledge_alert(const std::string& alert_id);
    void silence_alert(const std::string& alert_id, std::chrono::minutes duration);

private:
    std::string format_alert_table(const std::vector<Alert>& alerts) const;
    std::string get_alert_severity_color(AlertSeverity severity) const;
};

} // namespace rtes