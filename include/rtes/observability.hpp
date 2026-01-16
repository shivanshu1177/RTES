#pragma once

#include "rtes/thread_safety.hpp"
#include "rtes/logger.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <memory>
#include <functional>
#include <sstream>

namespace rtes {

// Use LogLevel from logger.hpp

struct LogEntry {
    std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    std::string message;
    std::string component;
    std::string trace_id;
    std::unordered_map<std::string, std::string> fields;
};

class StructuredLogger {
public:
    static StructuredLogger& instance();
    
    void log(LogLevel level, const std::string& component, const std::string& message, 
             const std::unordered_map<std::string, std::string>& fields = {});
    
    void set_trace_id(const std::string& trace_id);
    std::string get_trace_id() const;
    
    void add_global_field(const std::string& key, const std::string& value);
    void set_output_format(bool json_format);

private:
    mutable std::shared_mutex logger_mutex_;
    std::unordered_map<std::string, std::string> global_fields_ GUARDED_BY(logger_mutex_);
    inline static thread_local std::string current_trace_id_;
    atomic_wrapper<bool> json_format_{true};
    
    std::string format_log_entry(const LogEntry& entry) const;
    std::string to_json(const LogEntry& entry) const;
};

// Metrics collection
struct MetricValue {
    double value;
    std::chrono::system_clock::time_point timestamp;
    std::unordered_map<std::string, std::string> labels;
};

class MetricsCollector {
public:
    static MetricsCollector& instance();
    
    void increment_counter(const std::string& name, const std::unordered_map<std::string, std::string>& labels = {});
    void set_gauge(const std::string& name, double value, const std::unordered_map<std::string, std::string>& labels = {});
    void record_histogram(const std::string& name, double value, const std::unordered_map<std::string, std::string>& labels = {});
    
    std::string export_prometheus() const;
    std::unordered_map<std::string, std::vector<MetricValue>> get_all_metrics() const;

private:
    mutable std::shared_mutex metrics_mutex_;
    std::unordered_map<std::string, std::vector<MetricValue>> counters_ GUARDED_BY(metrics_mutex_);
    std::unordered_map<std::string, std::vector<MetricValue>> gauges_ GUARDED_BY(metrics_mutex_);
    std::unordered_map<std::string, std::vector<MetricValue>> histograms_ GUARDED_BY(metrics_mutex_);
};

// Distributed tracing
struct Span {
    std::string trace_id;
    std::string span_id;
    std::string parent_span_id;
    std::string operation_name;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    std::unordered_map<std::string, std::string> tags;
    bool finished{false};
};

class DistributedTracer {
public:
    static DistributedTracer& instance();
    
    std::shared_ptr<Span> start_span(const std::string& operation_name, 
                                    const std::string& parent_span_id = "");
    void finish_span(std::shared_ptr<Span> span);
    void add_tag(std::shared_ptr<Span> span, const std::string& key, const std::string& value);
    
    std::string generate_trace_id();
    std::string generate_span_id();
    
    std::vector<Span> get_trace(const std::string& trace_id) const;

private:
    mutable std::shared_mutex traces_mutex_;
    std::unordered_map<std::string, std::vector<Span>> traces_ GUARDED_BY(traces_mutex_);
    atomic_wrapper<uint64_t> span_counter_{0};
};

// Anomaly detection
struct AnomalyThreshold {
    double mean;
    double std_dev;
    double z_score_threshold{3.0};
    size_t window_size{100};
};

class AnomalyDetector {
public:
    static AnomalyDetector& instance();
    
    void configure_metric(const std::string& metric_name, const AnomalyThreshold& threshold);
    bool is_anomaly(const std::string& metric_name, double value);
    void update_baseline(const std::string& metric_name, double value);

private:
    mutable std::shared_mutex detector_mutex_;
    std::unordered_map<std::string, AnomalyThreshold> thresholds_ GUARDED_BY(detector_mutex_);
    std::unordered_map<std::string, std::vector<double>> history_ GUARDED_BY(detector_mutex_);
    
    void calculate_statistics(const std::string& metric_name);
};

// Alerting system
enum class AlertSeverity {
    INFO,
    WARNING,
    CRITICAL
};

struct Alert {
    std::string name;
    AlertSeverity severity;
    std::string message;
    std::chrono::system_clock::time_point timestamp;
    std::unordered_map<std::string, std::string> labels;
    bool resolved{false};
};

class AlertManager {
public:
    static AlertManager& instance();
    
    void register_threshold_alert(const std::string& metric_name, double threshold, 
                                 AlertSeverity severity, const std::string& message);
    void register_anomaly_alert(const std::string& metric_name, AlertSeverity severity);
    
    void check_alerts();
    void fire_alert(const Alert& alert);
    void resolve_alert(const std::string& alert_name);
    
    std::vector<Alert> get_active_alerts() const;

private:
    mutable std::shared_mutex alerts_mutex_;
    std::vector<Alert> active_alerts_ GUARDED_BY(alerts_mutex_);
    std::unordered_map<std::string, std::function<bool()>> alert_conditions_ GUARDED_BY(alerts_mutex_);
    
    void escalate_alert(const Alert& alert);
};

// Performance counters
class PerformanceCounters {
public:
    static PerformanceCounters& instance();
    
    void record_latency(const std::string& operation, std::chrono::nanoseconds latency);
    void record_throughput(const std::string& operation, uint64_t count = 1);
    void record_error(const std::string& operation, const std::string& error_type);
    
    double get_avg_latency(const std::string& operation) const;
    uint64_t get_throughput(const std::string& operation) const;
    double get_error_rate(const std::string& operation) const;

private:
    mutable std::shared_mutex counters_mutex_;
    std::unordered_map<std::string, std::vector<double>> latencies_ GUARDED_BY(counters_mutex_);
    std::unordered_map<std::string, atomic_wrapper<uint64_t>> throughput_counters_;
    std::unordered_map<std::string, atomic_wrapper<uint64_t>> error_counters_;
    std::unordered_map<std::string, atomic_wrapper<uint64_t>> total_counters_;
};

// Business metrics
class BusinessMetrics {
public:
    static BusinessMetrics& instance();
    
    void record_order_placed(const std::string& symbol, double notional);
    void record_trade_executed(const std::string& symbol, uint64_t quantity, double price);
    void record_client_activity(const std::string& client_id, const std::string& activity);
    
    double get_total_volume(const std::string& symbol) const;
    uint64_t get_trade_count(const std::string& symbol) const;
    double get_client_pnl(const std::string& client_id) const;

private:
    mutable std::shared_mutex metrics_mutex_;
    std::unordered_map<std::string, atomic_wrapper<double>> symbol_volumes_;
    std::unordered_map<std::string, atomic_wrapper<uint64_t>> trade_counts_;
    std::unordered_map<std::string, atomic_wrapper<double>> client_pnls_;
};

// Security audit trail
struct SecurityEvent {
    std::chrono::system_clock::time_point timestamp;
    std::string event_type;
    std::string user_id;
    std::string resource;
    std::string action;
    bool success;
    std::string details;
    std::string source_ip;
};

class SecurityAuditTrail {
public:
    static SecurityAuditTrail& instance();
    
    void log_security_event(const std::string& event_type, const std::string& user_id,
                           const std::string& resource, const std::string& action,
                           bool success, const std::string& details = "",
                           const std::string& source_ip = "");
    
    std::vector<SecurityEvent> get_events(const std::chrono::system_clock::time_point& from,
                                         const std::chrono::system_clock::time_point& to) const;
    
    void detect_suspicious_activity();

private:
    mutable std::shared_mutex audit_mutex_;
    std::vector<SecurityEvent> events_ GUARDED_BY(audit_mutex_);
    
    void cleanup_old_events();
};

// Main observability stack
class ObservabilityStack {
public:
    static ObservabilityStack& instance();
    
    void initialize();
    void start_monitoring();
    void stop_monitoring();
    
    void set_trace_context(const std::string& trace_id);
    std::shared_ptr<Span> start_operation(const std::string& operation_name);
    
    template<typename T>
    void log_with_metrics(LogLevel level, const std::string& component, 
                         const std::string& message, const T& metric_value);

private:
    atomic_wrapper<bool> monitoring_active_{false};
    std::thread monitoring_thread_;
    
    void monitoring_loop();
};

// Convenience macros
#define LOG_STRUCTURED(level, component, message, ...) \
    StructuredLogger::instance().log(level, component, message, {__VA_ARGS__})

#define TRACE_OPERATION(name) \
    auto _span = ObservabilityStack::instance().start_operation(name)

#define RECORD_METRIC(name, value) \
    MetricsCollector::instance().set_gauge(name, value)

#define SECURITY_AUDIT(event_type, user_id, resource, action, success) \
    SecurityAuditTrail::instance().log_security_event(event_type, user_id, resource, action, success)

} // namespace rtes