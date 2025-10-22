# RTES Deployment Guide

## Overview

This guide covers secure deployment procedures, configuration management, and operational controls for the Real-Time Trading Exchange Simulator (RTES).

## Configuration Management

### Environment Profiles

RTES supports three deployment environments:

- **Development** (`dev`): Relaxed security, enhanced debugging
- **Staging** (`staging`): Production-like environment for testing
- **Production** (`prod`): Full security, performance optimizations

### Configuration Files

```
configs/
├── config.dev.json      # Development environment
├── config.staging.json  # Staging environment
└── config.prod.json     # Production environment
```

### Secure Configuration Loading

#### Encryption
```bash
# Generate encryption key
export RTES_ENCRYPTION_KEY_prod_key_2024=$(openssl rand -hex 32)

# Encrypt sensitive configuration
./tools/encrypt_config.sh config.prod.json
```

#### Environment Variable Overrides
```bash
# Override specific configuration values
export RTES_exchange.tcp_port_PROD=8888
export RTES_risk.max_order_size_PROD=10000000
export RTES_security.log_level_PROD=INFO
```

#### Configuration Validation
```cpp
// Automatic validation on load
auto& config = SecureConfigManager::instance();
auto result = config.load_config("config.prod.json", Environment::PRODUCTION);

if (result.has_error()) {
    LOG_ERROR("Configuration validation failed: {}", result.error().message());
    return -1;
}
```

## Deployment Procedures

### Prerequisites

1. **Build System**
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

2. **Environment Setup**
```bash
# Production encryption keys
export RTES_ENCRYPTION_KEY_prod_key_2024="your-32-byte-hex-key"

# TLS certificates
cp server.crt /etc/rtes/certs/
cp server.key /etc/rtes/certs/
cp ca.crt /etc/rtes/certs/
```

3. **Health Check Registration**
```cpp
// Register application health checks
auto& health = HealthCheckManager::instance();

health.register_check({
    "order_book_health",
    []() { return order_book.is_healthy() ? 
           HealthStatus::HEALTHY : HealthStatus::UNHEALTHY; },
    std::chrono::milliseconds(1000),
    true  // critical
});
```

### Deployment Script Usage

#### Basic Deployment
```bash
# Development deployment
./scripts/deploy.sh dev v1.2.3

# Production deployment with canary
./scripts/deploy.sh prod v1.2.3

# Staging deployment without canary
./scripts/deploy.sh staging v1.2.3 --no-canary
```

#### Advanced Options
```bash
# Custom configuration and timeouts
./scripts/deploy.sh prod v1.2.3 \
    --config custom_config.json \
    --health-timeout 120 \
    --ramp-duration 600

# Dry run to validate deployment
./scripts/deploy.sh prod v1.2.3 --dry-run
```

### Rolling Update Process

1. **Health Check Phase** (30-60s)
   - Validate new instance health
   - Check all critical components
   - Verify configuration integrity

2. **Traffic Ramp Phase** (5-10 minutes)
   - Gradually increase traffic to new instance
   - Monitor error rates and latency
   - Automatic rollback on failure

3. **Full Deployment**
   - Complete traffic cutover
   - Final health validation
   - Old instance shutdown

```cpp
// Configure rolling update
DeploymentConfig config{
    "v1.2.3",                           // version
    Environment::PRODUCTION,            // environment
    std::chrono::seconds(60),          // health_check_timeout
    std::chrono::seconds(600),         // traffic_ramp_duration
    0.1,                               // traffic_ramp_step (10%)
    true,                              // enable_canary
    {"order_book_health", "tcp_gateway_health"}  // required_checks
};

auto& update_manager = RollingUpdateManager::instance();
update_manager.start_deployment(config);
```

## Health Checks and Monitoring

### Health Check Endpoints

| Endpoint | Purpose | Response |
|----------|---------|----------|
| `/health` | Overall system health | `{"status": "healthy\|degraded\|unhealthy"}` |
| `/ready` | Readiness for traffic | `{"ready": true\|false}` |
| `/live` | Liveness probe | `{"alive": true\|false}` |
| `/metrics` | Deployment metrics | Prometheus format |

### Kubernetes Integration

```yaml
# deployment.yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: rtes-trading-exchange
spec:
  replicas: 3
  template:
    spec:
      containers:
      - name: trading-exchange
        image: rtes:v1.2.3
        ports:
        - containerPort: 8888
        - containerPort: 8080
        readinessProbe:
          httpGet:
            path: /ready
            port: 8080
          initialDelaySeconds: 10
          periodSeconds: 5
        livenessProbe:
          httpGet:
            path: /live
            port: 8080
          initialDelaySeconds: 30
          periodSeconds: 10
        env:
        - name: RTES_ENVIRONMENT
          value: "production"
        - name: RTES_ENCRYPTION_KEY_prod_key_2024
          valueFrom:
            secretKeyRef:
              name: rtes-secrets
              key: encryption-key
```

### Custom Health Checks

```cpp
// Register component-specific health checks
auto& health = HealthCheckManager::instance();

// Order book health
health.register_check({
    "order_book",
    [&order_book]() {
        if (order_book.get_order_count() > 1000000) {
            return HealthStatus::DEGRADED;  // High load
        }
        return order_book.is_operational() ? 
               HealthStatus::HEALTHY : HealthStatus::UNHEALTHY;
    },
    std::chrono::milliseconds(500),
    true
});

// Network connectivity health
health.register_check({
    "network_connectivity",
    []() {
        return test_network_connectivity() ? 
               HealthStatus::HEALTHY : HealthStatus::UNHEALTHY;
    },
    std::chrono::milliseconds(2000),
    true
});
```

## Feature Flags and Gradual Rollout

### Feature Flag Configuration

```cpp
auto& flags = FeatureFlags::instance();

// Enable feature for all users
flags.set_flag("new_matching_engine", true);

// Gradual rollout (50% of users)
flags.set_rollout_percentage("enhanced_risk_checks", 50.0);

// Check feature availability
if (flags.is_enabled_for_user("new_matching_engine", client_id)) {
    // Use new matching engine
    new_matching_engine.process_order(order);
} else {
    // Use legacy matching engine
    legacy_matching_engine.process_order(order);
}
```

### Configuration-Based Feature Flags

```json
{
  "feature_flags": {
    "enable_new_matching_engine": true,
    "enable_enhanced_risk_checks": true,
    "enable_market_data_compression": false,
    "rollout_percentages": {
      "new_ui_features": 25.0,
      "advanced_analytics": 10.0
    }
  }
}
```

## Emergency Procedures

### Emergency Shutdown

```cpp
// Register emergency triggers
auto& emergency = EmergencyShutdown::instance();

// Memory usage trigger
emergency.register_trigger("high_memory", []() {
    return get_memory_usage_percent() > 95.0;
});

// Error rate trigger
emergency.register_trigger("high_error_rate", []() {
    return get_error_rate() > 0.1;  // 10% error rate
});

// Manual emergency shutdown
emergency.initiate_emergency_shutdown("Critical system error detected");
```

### Rollback Procedures

#### Automatic Rollback
```bash
# Automatic rollback on health check failure
./scripts/deploy.sh prod v1.2.3 --health-timeout 60

# If health checks fail, automatic rollback occurs
```

#### Manual Rollback
```bash
# Emergency rollback to previous version
./scripts/rollback.sh

# Rollback to specific version
./scripts/rollback.sh v1.1.9
```

### Circuit Breaker Pattern

```cpp
class TradingCircuitBreaker {
public:
    bool should_process_order() {
        if (error_rate_ > 0.5) {  // 50% error rate
            return false;  // Circuit open
        }
        return true;
    }
    
    void record_success() { /* Update metrics */ }
    void record_failure() { /* Update metrics */ }
};
```

## Security Considerations

### Configuration Encryption

```cpp
// Encrypt sensitive configuration data
auto encrypted_result = ConfigEncryption::encrypt(
    sensitive_config_json, 
    "prod_key_2024"
);

// Decrypt at runtime
auto decrypted_result = ConfigEncryption::decrypt(
    encrypted_config, 
    encryption_key
);
```

### Access Control

```cpp
// Role-based configuration access
if (!SecurityUtils::has_permission(user_context, "config:read")) {
    return ErrorCode::ACCESS_DENIED;
}

// Audit configuration changes
SecurityUtils::audit_log("CONFIG_CHANGE", {
    {"user", user_context.user_id},
    {"key", config_key},
    {"old_value", old_value},
    {"new_value", new_value}
});
```

### Certificate Management

```bash
# Generate TLS certificates
openssl req -x509 -newkey rsa:4096 -keyout server.key -out server.crt -days 365

# Set proper permissions
chmod 600 server.key
chmod 644 server.crt

# Environment variables
export RTES_TLS_CERT_FILE="/etc/rtes/certs/server.crt"
export RTES_TLS_KEY_FILE="/etc/rtes/certs/server.key"
```

## Monitoring and Alerting

### Deployment Metrics

```cpp
// Track deployment progress
auto& metrics = DeploymentMetrics::instance();

metrics.record_deployment_start(version, environment);
metrics.record_health_check_result(check_name, success);
metrics.record_traffic_percentage(percentage);
metrics.record_deployment_completion(success, duration);
```

### Alerting Rules

```yaml
# Prometheus alerting rules
groups:
- name: rtes_deployment
  rules:
  - alert: DeploymentFailed
    expr: rtes_deployment_status != 1
    for: 5m
    labels:
      severity: critical
    annotations:
      summary: "RTES deployment failed"
      
  - alert: HighErrorRate
    expr: rtes_error_rate > 0.05
    for: 2m
    labels:
      severity: warning
    annotations:
      summary: "High error rate during deployment"
```

## Best Practices

### 1. Configuration Management
- Use environment-specific configurations
- Encrypt sensitive data
- Validate configurations before deployment
- Use environment variable overrides for secrets

### 2. Deployment Safety
- Always use health checks
- Implement gradual traffic ramp
- Have rollback procedures ready
- Test deployments in staging first

### 3. Monitoring
- Monitor all critical metrics during deployment
- Set up alerting for deployment failures
- Track deployment success rates
- Monitor application performance post-deployment

### 4. Security
- Rotate encryption keys regularly
- Use TLS for all network communication
- Implement proper access controls
- Audit all configuration changes

This deployment framework ensures safe, secure, and reliable deployments of the RTES system while maintaining the high-performance requirements of trading systems.