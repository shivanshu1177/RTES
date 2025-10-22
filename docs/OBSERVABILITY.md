# RTES Observability and Monitoring

## Overview

The RTES observability stack provides comprehensive monitoring, logging, tracing, and alerting capabilities designed for high-frequency trading systems requiring ultra-low latency and high reliability.

## Architecture

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│ Application     │    │ Observability   │    │ External        │
│ Components      │───▶│ Stack           │───▶│ Systems         │
│                 │    │                 │    │                 │
│ • OrderBook     │    │ • Logging       │    │ • Prometheus    │
│ • TCP Gateway   │    │ • Metrics       │    │ • Grafana       │
│ • Risk Manager  │    │ • Tracing       │    │ • Jaeger        │
│ • Matching      │    │ • Alerting      │    │ • PagerDuty     │
└─────────────────┘    └─────────────────┘    └─────────────────┘
```

## Components

### 1. Structured Logging

#### Features
- **JSON Output**: Machine-readable structured logs
- **Trace Correlation**: Automatic trace ID injection
- **Global Fields**: Service-wide metadata
- **Performance Optimized**: Sub-100μs logging latency

#### Usage
```cpp
// Basic logging
LOG_STRUCTURED(LogLevel::INFO, "OrderBook", "Order processed",
              {"order_id", "12345"}, {"symbol", "AAPL"});

// With trace context
auto span = ObservabilityStack::instance().start_operation("process_order");
LOG_STRUCTURED(LogLevel::DEBUG, "OrderBook", "Matching order");
// Trace ID automatically included

// Global fields
auto& logger = StructuredLogger::instance();
logger.add_global_field("service", "rtes");
logger.add_global_field("version", "1.0.0");
```

#### Log Format
```json
{
  "timestamp": "2024-01-15T10:30:45Z",
  "level": "INFO",
  "component": "OrderBook",
  "message": "Order processed",
  "trace_id": "abc123def456",
  "fields": {
    "order_id": "12345",
    "symbol": "AAPL",
    "service": "rtes",
    "version": "1.0.0"
  }
}
```

### 2. Metrics Collection

#### Metric Types
- **Counters**: Monotonically increasing values
- **Gauges**: Point-in-time values
- **Histograms**: Distribution of values with percentiles

#### Usage
```cpp
auto& metrics = MetricsCollector::instance();

// Counter
metrics.increment_counter("orders_processed", {{"symbol", "AAPL"}});

// Gauge
metrics.set_gauge("active_connections", connection_count);

// Histogram
metrics.record_histogram("order_latency_us", latency_microseconds);

// Prometheus export
std::string prometheus_data = metrics.export_prometheus();
```

#### Key Metrics
| Metric | Type | Description |
|--------|------|-------------|
| `orders_processed_total` | Counter | Total orders processed |
| `order_latency_us` | Histogram | Order processing latency |
| `active_connections` | Gauge | Current TCP connections |
| `memory_usage_bytes` | Gauge | Memory consumption |
| `error_rate` | Gauge | Error rate percentage |

### 3. Distributed Tracing

#### Features
- **Request Correlation**: End-to-end request tracking
- **Span Hierarchy**: Parent-child span relationships
- **Tag Support**: Custom metadata on spans
- **Performance Impact**: <1μs overhead per span

#### Usage
```cpp
// Start root span
auto span = DistributedTracer::instance().start_span("process_order");

// Add tags
DistributedTracer::instance().add_tag(span, "order_id", "12345");
DistributedTracer::instance().add_tag(span, "symbol", "AAPL");

// Child span
auto child_span = DistributedTracer::instance().start_span("risk_check", span->span_id);

// Finish spans
DistributedTracer::instance().finish_span(child_span);
DistributedTracer::instance().finish_span(span);

// Convenience macro
{
    TRACE_OPERATION("match_order");
    // Automatic span creation and cleanup
}
```

#### Trace Visualization
```
process_order (10ms)
├── validate_order (1ms)
├── risk_check (2ms)
├── match_order (5ms)
│   ├── find_matches (2ms)
│   └── execute_trades (3ms)
└── send_ack (1ms)
```

### 4. Performance Monitoring

#### Performance Counters
```cpp
auto& perf = PerformanceCounters::instance();

// Record latency
auto start = std::chrono::high_resolution_clock::now();
process_order(order);
auto end = std::chrono::high_resolution_clock::now();
perf.record_latency("order_processing", end - start);

// Record throughput
perf.record_throughput("orders_per_second", batch_size);

// Record errors
perf.record_error("order_processing", "validation_failed");

// Get statistics
double avg_latency = perf.get_avg_latency("order_processing");
uint64_t throughput = perf.get_throughput("orders_per_second");
double error_rate = perf.get_error_rate("order_processing");
```

#### Business Metrics
```cpp
auto& business = BusinessMetrics::instance();

// Trading metrics
business.record_order_placed("AAPL", notional_value);
business.record_trade_executed("AAPL", quantity, price);
business.record_client_activity("client123", "login");

// Get business insights
double total_volume = business.get_total_volume("AAPL");
uint64_t trade_count = business.get_trade_count("AAPL");
```

### 5. Alerting System

#### Alert Types
- **Threshold Alerts**: Metric exceeds/falls below threshold
- **Anomaly Alerts**: Statistical deviation detection
- **Rate Alerts**: Change rate monitoring

#### Configuration
```cpp
auto& alert_manager = AlertManager::instance();

// Threshold alert
alert_manager.register_threshold_alert(
    "order_latency_us", 
    10000.0,  // 10ms threshold
    AlertSeverity::CRITICAL,
    "Order latency exceeded 10ms"
);

// Anomaly alert
alert_manager.register_anomaly_alert(
    "throughput", 
    AlertSeverity::WARNING
);

// Check alerts (called periodically)
alert_manager.check_alerts();
```

#### Anomaly Detection
```cpp
auto& detector = AnomalyDetector::instance();

// Configure baseline
AnomalyThreshold threshold{
    .mean = 1000.0,           // Expected mean
    .std_dev = 100.0,         // Expected standard deviation
    .z_score_threshold = 3.0, // 3-sigma threshold
    .window_size = 1000       // Rolling window size
};

detector.configure_metric("throughput", threshold);

// Update baseline continuously
detector.update_baseline("throughput", current_value);

// Check for anomalies
if (detector.is_anomaly("throughput", current_value)) {
    // Handle anomaly
}
```

### 6. Security Auditing

#### Security Event Tracking
```cpp
// Log security events
SECURITY_AUDIT("LOGIN", user_id, "system", "authenticate", success);
SECURITY_AUDIT("ORDER", user_id, "orders", "place", success);
SECURITY_AUDIT("CONFIG", user_id, "configuration", "modify", success);

// Detailed logging
SecurityAuditTrail::instance().log_security_event(
    "UNAUTHORIZED_ACCESS",
    user_id,
    "admin_panel",
    "access_attempt",
    false,
    "Invalid credentials",
    source_ip
);
```

#### Suspicious Activity Detection
```cpp
auto& audit = SecurityAuditTrail::instance();

// Automatic detection
audit.detect_suspicious_activity();

// Manual queries
auto now = std::chrono::system_clock::now();
auto events = audit.get_events(now - std::chrono::hours(24), now);

// Filter failed logins
for (const auto& event : events) {
    if (event.event_type == "LOGIN" && !event.success) {
        // Investigate failed login
    }
}
```

## Operational Dashboards

### 1. System Health Dashboard

Access: `http://localhost:3000/dashboard`

#### Panels
- **System Health**: CPU, Memory, Network
- **Performance**: Latency, Throughput, Error Rate
- **Business Metrics**: Volume, Trades, Active Clients
- **Active Alerts**: Current system alerts

#### Features
- **Auto-refresh**: 30-second intervals
- **Real-time metrics**: Live system status
- **Alert integration**: Visual alert indicators
- **Mobile responsive**: Works on all devices

### 2. Alert Management Dashboard

Access: `http://localhost:3000/alerts`

#### Features
- **Alert List**: All active alerts with severity
- **Acknowledgment**: Mark alerts as acknowledged
- **Silencing**: Temporarily silence alerts
- **History**: Historical alert data
- **Escalation**: Automatic escalation policies

### 3. Custom Dashboards

```cpp
// Create custom dashboard panel
DashboardPanel custom_panel;
custom_panel.id = "trading_metrics";
custom_panel.title = "Trading Performance";

custom_panel.widgets = {
    {"avg_latency", "Avg Latency", "metric", "order_latency_us", {}},
    {"throughput", "Orders/sec", "metric", "orders_per_second", {}},
    {"volume_chart", "Volume Trend", "chart", "trading_volume", {}}
};

OperationalDashboard::instance().add_panel(custom_panel);
```

## Integration Examples

### 1. OrderBook Integration

```cpp
class OrderBook {
public:
    Result<void> add_order_safe(Order* order) {
        TRACE_OPERATION("add_order");
        
        auto start = std::chrono::high_resolution_clock::now();
        
        // Process order
        auto result = process_order_internal(order);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto latency = end - start;
        
        // Record metrics
        PerformanceCounters::instance().record_latency("add_order", latency);
        
        if (result.has_error()) {
            PerformanceCounters::instance().record_error("add_order", "processing_failed");
            LOG_STRUCTURED(LogLevel::ERROR, "OrderBook", "Order processing failed",
                          {"order_id", std::to_string(order->id)},
                          {"error", result.error().message()});
        } else {
            BusinessMetrics::instance().record_order_placed(order->symbol, order->notional());
            LOG_STRUCTURED(LogLevel::INFO, "OrderBook", "Order processed successfully",
                          {"order_id", std::to_string(order->id)},
                          {"symbol", order->symbol});
        }
        
        return result;
    }
};
```

### 2. TCP Gateway Integration

```cpp
void TcpGateway::handle_new_order(ClientConnection* conn, const NewOrderMessage& msg) {
    auto span = ObservabilityStack::instance().start_operation("handle_new_order");
    DistributedTracer::instance().add_tag(span, "client_id", msg.client_id.c_str());
    
    // Security audit
    SECURITY_AUDIT("ORDER_RECEIVED", msg.client_id.c_str(), "orders", "place", true);
    
    // Business metrics
    BusinessMetrics::instance().record_client_activity(msg.client_id.c_str(), "place_order");
    
    // Process order with full observability
    auto result = process_order_with_observability(msg);
    
    DistributedTracer::instance().finish_span(span);
}
```

## Prometheus Integration

### Configuration
```yaml
# prometheus.yml
global:
  scrape_interval: 15s

scrape_configs:
  - job_name: 'rtes'
    static_configs:
      - targets: ['localhost:8080']
    metrics_path: '/metrics'
    scrape_interval: 5s
```

### Key Queries
```promql
# Average order latency
rate(order_latency_us_sum[5m]) / rate(order_latency_us_count[5m])

# Orders per second
rate(orders_processed_total[1m])

# Error rate
rate(errors_total[5m]) / rate(requests_total[5m]) * 100

# 99th percentile latency
histogram_quantile(0.99, rate(order_latency_us_bucket[5m]))
```

## Grafana Dashboards

### Trading System Overview
```json
{
  "dashboard": {
    "title": "RTES Trading System",
    "panels": [
      {
        "title": "Order Latency",
        "type": "graph",
        "targets": [
          {
            "expr": "histogram_quantile(0.50, rate(order_latency_us_bucket[5m]))",
            "legendFormat": "p50"
          },
          {
            "expr": "histogram_quantile(0.99, rate(order_latency_us_bucket[5m]))",
            "legendFormat": "p99"
          }
        ]
      },
      {
        "title": "Throughput",
        "type": "singlestat",
        "targets": [
          {
            "expr": "rate(orders_processed_total[1m])",
            "legendFormat": "Orders/sec"
          }
        ]
      }
    ]
  }
}
```

## Jaeger Tracing

### Configuration
```yaml
# jaeger-config.yml
reporter:
  type: jaeger
  jaeger:
    endpoint: http://localhost:14268/api/traces

sampler:
  type: const
  param: 1  # Sample all traces in development
```

### Trace Analysis
- **Service Map**: Visualize service dependencies
- **Trace Timeline**: Request flow analysis
- **Performance Bottlenecks**: Identify slow operations
- **Error Correlation**: Link errors to traces

## Best Practices

### 1. Logging
- Use structured logging for all components
- Include trace IDs for request correlation
- Log at appropriate levels (avoid debug in production)
- Sanitize sensitive data before logging

### 2. Metrics
- Use consistent naming conventions
- Include relevant labels for filtering
- Monitor both technical and business metrics
- Set up proper retention policies

### 3. Tracing
- Trace critical paths end-to-end
- Use meaningful operation names
- Add relevant tags for filtering
- Sample appropriately in production

### 4. Alerting
- Set up alerts for SLI violations
- Use appropriate thresholds and time windows
- Implement escalation policies
- Regularly review and tune alerts

### 5. Performance
- Monitor observability overhead
- Use sampling for high-volume traces
- Batch metrics where possible
- Optimize hot paths

## Troubleshooting

### High Latency Investigation
1. Check latency metrics and percentiles
2. Examine distributed traces for bottlenecks
3. Correlate with system resource usage
4. Review error logs for related issues

### Error Rate Spikes
1. Check error rate metrics by component
2. Examine error logs for patterns
3. Correlate with deployment events
4. Check upstream dependencies

### Performance Degradation
1. Compare current vs baseline metrics
2. Check for anomalies in key metrics
3. Examine resource utilization
4. Review recent configuration changes

This observability framework provides comprehensive monitoring capabilities while maintaining the ultra-low latency requirements of high-frequency trading systems.