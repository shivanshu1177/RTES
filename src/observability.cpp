#include "rtes/observability.hpp"
#include <iostream>
#include <iomanip>
#include <random>
#include <algorithm>
#include <cmath>

namespace rtes {

// StructuredLogger implementation
StructuredLogger& StructuredLogger::instance() {
    static StructuredLogger instance;
    return instance;
}

void StructuredLogger::log(LogLevel level, const std::string& component, const std::string& message,
                          const std::unordered_map<std::string, std::string>& fields) {
    LogEntry entry;
    entry.timestamp = std::chrono::system_clock::now();
    entry.level = level;
    entry.component = component;
    entry.message = message;
    entry.trace_id = current_trace_id_;
    entry.fields = fields;
    
    // Add global fields
    {
        std::shared_lock lock(logger_mutex_);
        for (const auto& [key, value] : global_fields_) {
            if (entry.fields.find(key) == entry.fields.end()) {
                entry.fields[key] = value;
            }
        }
    }
    
    std::cout << format_log_entry(entry) << std::endl;
}

void StructuredLogger::set_trace_id(const std::string& trace_id) {
    current_trace_id_ = trace_id;
}

std::string StructuredLogger::get_trace_id() const {
    return current_trace_id_;
}

void StructuredLogger::add_global_field(const std::string& key, const std::string& value) {
    std::unique_lock lock(logger_mutex_);
    global_fields_[key] = value;
}

void StructuredLogger::set_output_format(bool json_format) {
    json_format_.store(json_format);
}

std::string StructuredLogger::format_log_entry(const LogEntry& entry) const {
    if (json_format_.load()) {
        return to_json(entry);
    }
    
    // Plain text format
    std::ostringstream oss;
    auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
    oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    
    const char* level_str[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL"};
    oss << " [" << level_str[static_cast<int>(entry.level)] << "]";
    oss << " [" << entry.component << "]";
    if (!entry.trace_id.empty()) {
        oss << " [" << entry.trace_id << "]";
    }
    oss << " " << entry.message;
    
    for (const auto& [key, value] : entry.fields) {
        oss << " " << key << "=" << value;
    }
    
    return oss.str();
}

std::string StructuredLogger::to_json(const LogEntry& entry) const {
    std::ostringstream json;
    json << "{";
    
    auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
    json << "\"timestamp\":\"" << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ") << "\",";
    
    const char* level_str[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL"};
    json << "\"level\":\"" << level_str[static_cast<int>(entry.level)] << "\",";
    json << "\"component\":\"" << entry.component << "\",";
    json << "\"message\":\"" << entry.message << "\"";
    
    if (!entry.trace_id.empty()) {
        json << ",\"trace_id\":\"" << entry.trace_id << "\"";
    }
    
    if (!entry.fields.empty()) {
        json << ",\"fields\":{";
        bool first = true;
        for (const auto& [key, value] : entry.fields) {
            if (!first) json << ",";
            json << "\"" << key << "\":\"" << value << "\"";
            first = false;
        }
        json << "}";
    }
    
    json << "}";
    return json.str();
}

// MetricsCollector implementation
MetricsCollector& MetricsCollector::instance() {
    static MetricsCollector instance;
    return instance;
}

void MetricsCollector::increment_counter(const std::string& name, 
                                        const std::unordered_map<std::string, std::string>& labels) {
    std::unique_lock lock(metrics_mutex_);
    
    MetricValue value;
    value.value = 1.0;
    value.timestamp = std::chrono::system_clock::now();
    value.labels = labels;
    
    counters_[name].push_back(value);
}

void MetricsCollector::set_gauge(const std::string& name, double value,
                                const std::unordered_map<std::string, std::string>& labels) {
    std::unique_lock lock(metrics_mutex_);
    
    MetricValue metric;
    metric.value = value;
    metric.timestamp = std::chrono::system_clock::now();
    metric.labels = labels;
    
    gauges_[name].push_back(metric);
    
    // Keep only recent values (last 1000)
    if (gauges_[name].size() > 1000) {
        gauges_[name].erase(gauges_[name].begin(), gauges_[name].begin() + 500);
    }
}

void MetricsCollector::record_histogram(const std::string& name, double value,
                                       const std::unordered_map<std::string, std::string>& labels) {
    std::unique_lock lock(metrics_mutex_);
    
    MetricValue metric;
    metric.value = value;
    metric.timestamp = std::chrono::system_clock::now();
    metric.labels = labels;
    
    histograms_[name].push_back(metric);
    
    // Keep only recent values
    if (histograms_[name].size() > 10000) {
        histograms_[name].erase(histograms_[name].begin(), histograms_[name].begin() + 5000);
    }
}

std::string MetricsCollector::export_prometheus() const {
    std::shared_lock lock(metrics_mutex_);
    std::ostringstream prometheus;
    
    // Export counters
    for (const auto& [name, values] : counters_) {
        prometheus << "# TYPE " << name << " counter\n";
        double total = 0;
        for (const auto& value : values) {
            total += value.value;
        }
        prometheus << name << " " << total << "\n";
    }
    
    // Export gauges
    for (const auto& [name, values] : gauges_) {
        if (!values.empty()) {
            prometheus << "# TYPE " << name << " gauge\n";
            prometheus << name << " " << values.back().value << "\n";
        }
    }
    
    // Export histograms
    for (const auto& [name, values] : histograms_) {
        if (!values.empty()) {
            prometheus << "# TYPE " << name << " histogram\n";
            
            // Calculate percentiles
            std::vector<double> sorted_values;
            for (const auto& value : values) {
                sorted_values.push_back(value.value);
            }
            std::sort(sorted_values.begin(), sorted_values.end());
            
            if (!sorted_values.empty()) {
                size_t p50_idx = sorted_values.size() * 0.5;
                size_t p95_idx = sorted_values.size() * 0.95;
                size_t p99_idx = sorted_values.size() * 0.99;
                
                prometheus << name << "_p50 " << sorted_values[p50_idx] << "\n";
                prometheus << name << "_p95 " << sorted_values[p95_idx] << "\n";
                prometheus << name << "_p99 " << sorted_values[p99_idx] << "\n";
            }
        }
    }
    
    return prometheus.str();
}

std::unordered_map<std::string, std::vector<MetricValue>> MetricsCollector::get_all_metrics() const {
    std::shared_lock lock(metrics_mutex_);
    
    std::unordered_map<std::string, std::vector<MetricValue>> all_metrics;
    
    for (const auto& [name, values] : counters_) {
        all_metrics[name + "_counter"] = values;
    }
    
    for (const auto& [name, values] : gauges_) {
        all_metrics[name + "_gauge"] = values;
    }
    
    for (const auto& [name, values] : histograms_) {
        all_metrics[name + "_histogram"] = values;
    }
    
    return all_metrics;
}

// DistributedTracer implementation
DistributedTracer& DistributedTracer::instance() {
    static DistributedTracer instance;
    return instance;
}

std::shared_ptr<Span> DistributedTracer::start_span(const std::string& operation_name,
                                                   const std::string& parent_span_id) {
    auto span = std::make_shared<Span>();
    
    if (parent_span_id.empty()) {
        span->trace_id = generate_trace_id();
    } else {
        // Find parent span to get trace_id
        std::shared_lock lock(traces_mutex_);
        for (const auto& [trace_id, spans] : traces_) {
            for (const auto& s : spans) {
                if (s.span_id == parent_span_id) {
                    span->trace_id = trace_id;
                    break;
                }
            }
        }
        if (span->trace_id.empty()) {
            span->trace_id = generate_trace_id();
        }
    }
    
    span->span_id = generate_span_id();
    span->parent_span_id = parent_span_id;
    span->operation_name = operation_name;
    span->start_time = std::chrono::system_clock::now();
    
    // Set trace context in logger
    StructuredLogger::instance().set_trace_id(span->trace_id);
    
    return span;
}

void DistributedTracer::finish_span(std::shared_ptr<Span> span) {
    if (!span || span->finished) return;
    
    span->end_time = std::chrono::system_clock::now();
    span->finished = true;
    
    std::unique_lock lock(traces_mutex_);
    traces_[span->trace_id].push_back(*span);
}

void DistributedTracer::add_tag(std::shared_ptr<Span> span, const std::string& key, const std::string& value) {
    if (span) {
        span->tags[key] = value;
    }
}

std::string DistributedTracer::generate_trace_id() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    
    uint64_t id = gen();
    std::ostringstream oss;
    oss << std::hex << id;
    return oss.str();
}

std::string DistributedTracer::generate_span_id() {
    uint64_t id = span_counter_.exchange(span_counter_.load() + 1);
    std::ostringstream oss;
    oss << std::hex << id;
    return oss.str();
}

std::vector<Span> DistributedTracer::get_trace(const std::string& trace_id) const {
    std::shared_lock lock(traces_mutex_);
    
    auto it = traces_.find(trace_id);
    if (it != traces_.end()) {
        return it->second;
    }
    
    return {};
}

// AnomalyDetector implementation
AnomalyDetector& AnomalyDetector::instance() {
    static AnomalyDetector instance;
    return instance;
}

void AnomalyDetector::configure_metric(const std::string& metric_name, const AnomalyThreshold& threshold) {
    std::unique_lock lock(detector_mutex_);
    thresholds_[metric_name] = threshold;
}

bool AnomalyDetector::is_anomaly(const std::string& metric_name, double value) {
    std::shared_lock lock(detector_mutex_);
    
    auto it = thresholds_.find(metric_name);
    if (it == thresholds_.end()) {
        return false;
    }
    
    const auto& threshold = it->second;
    double z_score = std::abs(value - threshold.mean) / threshold.std_dev;
    
    return z_score > threshold.z_score_threshold;
}

void AnomalyDetector::update_baseline(const std::string& metric_name, double value) {
    std::unique_lock lock(detector_mutex_);
    
    history_[metric_name].push_back(value);
    
    // Keep window size
    auto& hist = history_[metric_name];
    if (hist.size() > thresholds_[metric_name].window_size) {
        hist.erase(hist.begin());
    }
    
    calculate_statistics(metric_name);
}

void AnomalyDetector::calculate_statistics(const std::string& metric_name) {
    auto& hist = history_[metric_name];
    if (hist.size() < 10) return;  // Need minimum samples
    
    // Calculate mean
    double sum = 0;
    for (double val : hist) {
        sum += val;
    }
    double mean = sum / hist.size();
    
    // Calculate standard deviation
    double variance = 0;
    for (double val : hist) {
        variance += (val - mean) * (val - mean);
    }
    double std_dev = std::sqrt(variance / hist.size());
    
    thresholds_[metric_name].mean = mean;
    thresholds_[metric_name].std_dev = std_dev;
}

// AlertManager implementation
AlertManager& AlertManager::instance() {
    static AlertManager instance;
    return instance;
}

void AlertManager::register_threshold_alert(const std::string& metric_name, double threshold,
                                           AlertSeverity severity, const std::string& message) {
    std::unique_lock lock(alerts_mutex_);
    
    alert_conditions_[metric_name] = [metric_name, threshold]() {
        auto& metrics = MetricsCollector::instance();
        auto all_metrics = metrics.get_all_metrics();
        
        auto it = all_metrics.find(metric_name + "_gauge");
        if (it != all_metrics.end() && !it->second.empty()) {
            return it->second.back().value > threshold;
        }
        return false;
    };
}

void AlertManager::register_anomaly_alert(const std::string& metric_name, AlertSeverity severity) {
    std::unique_lock lock(alerts_mutex_);
    
    alert_conditions_[metric_name + "_anomaly"] = [metric_name]() {
        auto& detector = AnomalyDetector::instance();
        auto& metrics = MetricsCollector::instance();
        auto all_metrics = metrics.get_all_metrics();
        
        auto it = all_metrics.find(metric_name + "_gauge");
        if (it != all_metrics.end() && !it->second.empty()) {
            return detector.is_anomaly(metric_name, it->second.back().value);
        }
        return false;
    };
}

void AlertManager::check_alerts() {
    std::unique_lock lock(alerts_mutex_);
    
    for (const auto& [name, condition] : alert_conditions_) {
        if (condition()) {
            // Check if alert already active
            bool already_active = false;
            for (const auto& alert : active_alerts_) {
                if (alert.name == name && !alert.resolved) {
                    already_active = true;
                    break;
                }
            }
            
            if (!already_active) {
                Alert alert;
                alert.name = name;
                alert.severity = AlertSeverity::WARNING;
                alert.message = "Alert condition triggered: " + name;
                alert.timestamp = std::chrono::system_clock::now();
                
                fire_alert(alert);
            }
        }
    }
}

void AlertManager::fire_alert(const Alert& alert) {
    active_alerts_.push_back(alert);
    
    // Log alert
    LOG_STRUCTURED(LogLevel::ERROR, "AlertManager", alert.message,
                  {"alert_name", alert.name},
                  {"severity", std::to_string(static_cast<int>(alert.severity))});
    
    escalate_alert(alert);
}

void AlertManager::resolve_alert(const std::string& alert_name) {
    std::unique_lock lock(alerts_mutex_);
    
    for (auto& alert : active_alerts_) {
        if (alert.name == alert_name && !alert.resolved) {
            alert.resolved = true;
            
            LOG_STRUCTURED(LogLevel::INFO, "AlertManager", "Alert resolved",
                          {"alert_name", alert_name});
            break;
        }
    }
}

std::vector<Alert> AlertManager::get_active_alerts() const {
    std::shared_lock lock(alerts_mutex_);
    
    std::vector<Alert> active;
    for (const auto& alert : active_alerts_) {
        if (!alert.resolved) {
            active.push_back(alert);
        }
    }
    
    return active;
}

void AlertManager::escalate_alert(const Alert& alert) {
    // Simple escalation - could integrate with PagerDuty, Slack, etc.
    if (alert.severity == AlertSeverity::CRITICAL) {
        std::cout << "CRITICAL ALERT: " << alert.message << std::endl;
    }
}

// PerformanceCounters implementation
PerformanceCounters& PerformanceCounters::instance() {
    static PerformanceCounters instance;
    return instance;
}

void PerformanceCounters::record_latency(const std::string& operation, std::chrono::nanoseconds latency) {
    std::unique_lock lock(counters_mutex_);
    
    double latency_us = latency.count() / 1000.0;
    latencies_[operation].push_back(latency_us);
    
    // Keep only recent measurements
    if (latencies_[operation].size() > 10000) {
        latencies_[operation].erase(latencies_[operation].begin(), 
                                   latencies_[operation].begin() + 5000);
    }
    
    // Update metrics
    MetricsCollector::instance().record_histogram(operation + "_latency_us", latency_us);
}

void PerformanceCounters::record_throughput(const std::string& operation, uint64_t count) {
    throughput_counters_[operation].store(throughput_counters_[operation].load() + count);
    total_counters_[operation].store(total_counters_[operation].load() + count);
    
    MetricsCollector::instance().increment_counter(operation + "_throughput");
}

void PerformanceCounters::record_error(const std::string& operation, const std::string& error_type) {
    error_counters_[operation].store(error_counters_[operation].load() + 1);
    total_counters_[operation].store(total_counters_[operation].load() + 1);
    
    MetricsCollector::instance().increment_counter(operation + "_errors", {{"type", error_type}});
}

double PerformanceCounters::get_avg_latency(const std::string& operation) const {
    std::shared_lock lock(counters_mutex_);
    
    auto it = latencies_.find(operation);
    if (it == latencies_.end() || it->second.empty()) {
        return 0.0;
    }
    
    double sum = 0;
    for (double latency : it->second) {
        sum += latency;
    }
    
    return sum / it->second.size();
}

uint64_t PerformanceCounters::get_throughput(const std::string& operation) const {
    auto it = throughput_counters_.find(operation);
    return it != throughput_counters_.end() ? it->second.load() : 0;
}

double PerformanceCounters::get_error_rate(const std::string& operation) const {
    auto error_it = error_counters_.find(operation);
    auto total_it = total_counters_.find(operation);
    
    if (error_it == error_counters_.end() || total_it == total_counters_.end()) {
        return 0.0;
    }
    
    uint64_t errors = error_it->second.load();
    uint64_t total = total_it->second.load();
    
    return total > 0 ? static_cast<double>(errors) / total : 0.0;
}

// BusinessMetrics implementation
BusinessMetrics& BusinessMetrics::instance() {
    static BusinessMetrics instance;
    return instance;
}

void BusinessMetrics::record_order_placed(const std::string& symbol, double notional) {
    MetricsCollector::instance().increment_counter("orders_placed", {{"symbol", symbol}});
    MetricsCollector::instance().set_gauge("notional_value", notional, {{"symbol", symbol}});
}

void BusinessMetrics::record_trade_executed(const std::string& symbol, uint64_t quantity, double price) {
    double volume = quantity * price;
    symbol_volumes_[symbol].store(symbol_volumes_[symbol].load() + volume);
    trade_counts_[symbol].store(trade_counts_[symbol].load() + 1);
    
    MetricsCollector::instance().increment_counter("trades_executed", {{"symbol", symbol}});
    MetricsCollector::instance().set_gauge("trade_volume", volume, {{"symbol", symbol}});
}

void BusinessMetrics::record_client_activity(const std::string& client_id, const std::string& activity) {
    MetricsCollector::instance().increment_counter("client_activity", 
                                                  {{"client_id", client_id}, {"activity", activity}});
}

double BusinessMetrics::get_total_volume(const std::string& symbol) const {
    auto it = symbol_volumes_.find(symbol);
    return it != symbol_volumes_.end() ? it->second.load() : 0.0;
}

uint64_t BusinessMetrics::get_trade_count(const std::string& symbol) const {
    auto it = trade_counts_.find(symbol);
    return it != trade_counts_.end() ? it->second.load() : 0;
}

double BusinessMetrics::get_client_pnl(const std::string& client_id) const {
    auto it = client_pnls_.find(client_id);
    return it != client_pnls_.end() ? it->second.load() : 0.0;
}

// SecurityAuditTrail implementation
SecurityAuditTrail& SecurityAuditTrail::instance() {
    static SecurityAuditTrail instance;
    return instance;
}

void SecurityAuditTrail::log_security_event(const std::string& event_type, const std::string& user_id,
                                           const std::string& resource, const std::string& action,
                                           bool success, const std::string& details,
                                           const std::string& source_ip) {
    SecurityEvent event;
    event.timestamp = std::chrono::system_clock::now();
    event.event_type = event_type;
    event.user_id = user_id;
    event.resource = resource;
    event.action = action;
    event.success = success;
    event.details = details;
    event.source_ip = source_ip;
    
    {
        std::unique_lock lock(audit_mutex_);
        events_.push_back(event);
        
        // Cleanup old events periodically
        if (events_.size() > 100000) {
            cleanup_old_events();
        }
    }
    
    // Log to structured logger
    LOG_STRUCTURED(LogLevel::INFO, "SecurityAudit", "Security event",
                  {"event_type", event_type},
                  {"user_id", user_id},
                  {"resource", resource},
                  {"action", action},
                  {"success", success ? "true" : "false"},
                  {"source_ip", source_ip});
    
    // Update metrics
    MetricsCollector::instance().increment_counter("security_events", 
                                                  {{"type", event_type}, {"success", success ? "true" : "false"}});
}

std::vector<SecurityEvent> SecurityAuditTrail::get_events(
    const std::chrono::system_clock::time_point& from,
    const std::chrono::system_clock::time_point& to) const {
    
    std::shared_lock lock(audit_mutex_);
    
    std::vector<SecurityEvent> filtered_events;
    for (const auto& event : events_) {
        if (event.timestamp >= from && event.timestamp <= to) {
            filtered_events.push_back(event);
        }
    }
    
    return filtered_events;
}

void SecurityAuditTrail::detect_suspicious_activity() {
    std::shared_lock lock(audit_mutex_);
    
    auto now = std::chrono::system_clock::now();
    auto one_hour_ago = now - std::chrono::hours(1);
    
    std::unordered_map<std::string, int> failed_logins;
    
    for (const auto& event : events_) {
        if (event.timestamp >= one_hour_ago && 
            event.event_type == "LOGIN" && !event.success) {
            failed_logins[event.user_id]++;
        }
    }
    
    // Alert on multiple failed logins
    for (const auto& [user_id, count] : failed_logins) {
        if (count >= 5) {
            AlertManager::instance().fire_alert({
                "suspicious_login_" + user_id,
                AlertSeverity::WARNING,
                "Multiple failed login attempts for user: " + user_id,
                now,
                {{"user_id", user_id}, {"failed_count", std::to_string(count)}}
            });
        }
    }
}

void SecurityAuditTrail::cleanup_old_events() {
    auto cutoff = std::chrono::system_clock::now() - std::chrono::hours(24 * 30); // 30 days
    
    events_.erase(
        std::remove_if(events_.begin(), events_.end(),
                      [cutoff](const SecurityEvent& event) {
                          return event.timestamp < cutoff;
                      }),
        events_.end()
    );
}

// ObservabilityStack implementation
ObservabilityStack& ObservabilityStack::instance() {
    static ObservabilityStack instance;
    return instance;
}

void ObservabilityStack::initialize() {
    // Initialize global fields
    auto& logger = StructuredLogger::instance();
    logger.add_global_field("service", "rtes");
    logger.add_global_field("version", "1.0.0");
    
    // Configure anomaly detection
    auto& detector = AnomalyDetector::instance();
    detector.configure_metric("latency", {0.0, 1.0, 3.0, 1000});
    detector.configure_metric("throughput", {1000.0, 100.0, 2.0, 1000});
    
    LOG_STRUCTURED(LogLevel::INFO, "ObservabilityStack", "Initialized observability stack");
}

void ObservabilityStack::start_monitoring() {
    if (monitoring_active_.exchange(true)) {
        return; // Already running
    }
    
    monitoring_thread_ = std::thread(&ObservabilityStack::monitoring_loop, this);
    LOG_STRUCTURED(LogLevel::INFO, "ObservabilityStack", "Started monitoring");
}

void ObservabilityStack::stop_monitoring() {
    monitoring_active_.store(false);
    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }
    
    LOG_STRUCTURED(LogLevel::INFO, "ObservabilityStack", "Stopped monitoring");
}

void ObservabilityStack::set_trace_context(const std::string& trace_id) {
    StructuredLogger::instance().set_trace_id(trace_id);
}

std::shared_ptr<Span> ObservabilityStack::start_operation(const std::string& operation_name) {
    return DistributedTracer::instance().start_span(operation_name);
}

template<typename T>
void ObservabilityStack::log_with_metrics(LogLevel level, const std::string& component,
                                         const std::string& message, const T& metric_value) {
    LOG_STRUCTURED(level, component, message);
    RECORD_METRIC(component + "_value", static_cast<double>(metric_value));
}

void ObservabilityStack::monitoring_loop() {
    while (monitoring_active_.load()) {
        // Check alerts
        AlertManager::instance().check_alerts();
        
        // Detect suspicious security activity
        SecurityAuditTrail::instance().detect_suspicious_activity();
        
        // Update performance metrics
        auto& perf = PerformanceCounters::instance();
        MetricsCollector::instance().set_gauge("system_health", 1.0);
        
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}

// Explicit template instantiation
template void ObservabilityStack::log_with_metrics<int>(LogLevel, const std::string&, const std::string&, const int&);
template void ObservabilityStack::log_with_metrics<double>(LogLevel, const std::string&, const std::string&, const double&);

} // namespace rtes