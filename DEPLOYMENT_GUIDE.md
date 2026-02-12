# RTES Deployment Guide

## Prerequisites

### System Requirements

**Minimum**:
- CPU: 4 cores, 2.5 GHz
- RAM: 8 GB
- Disk: 20 GB SSD
- Network: 1 Gbps
- OS: Linux (Ubuntu 20.04+, RHEL 8+)

**Recommended**:
- CPU: 8+ cores, 3.0+ GHz (Intel Xeon or AMD EPYC)
- RAM: 32 GB
- Disk: 100 GB NVMe SSD
- Network: 10 Gbps
- OS: Linux with real-time kernel

### Software Dependencies

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    g++-11 \
    libssl-dev \
    pkg-config \
    git

# RHEL/CentOS
sudo yum install -y \
    gcc-toolset-11 \
    cmake \
    openssl-devel \
    pkgconfig \
    git
```

### Compiler Requirements

- GCC 11+ or Clang 13+
- C++20 support required
- CMake 3.20+

## Building from Source

### 1. Clone Repository

```bash
git clone https://github.com/your-org/rtes.git
cd rtes
```

### 2. Build Release Version

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 3. Run Tests

```bash
ctest --output-on-failure
```

### 4. Install

```bash
sudo make install
# Installs to /usr/local/bin by default
```

## Configuration

### 1. Create Configuration File

```bash
sudo mkdir -p /etc/rtes
sudo cp configs/config.json /etc/rtes/config.json
```

### 2. Edit Configuration

```json
{
  "exchange": {
    "name": "RTES-PROD",
    "tcp_port": 8888,
    "udp_multicast_group": "239.0.0.1",
    "udp_port": 9999,
    "metrics_port": 8080
  },
  "symbols": [
    {
      "symbol": "AAPL",
      "tick_size": 0.01,
      "lot_size": 1,
      "price_collar_pct": 10.0
    }
  ],
  "risk": {
    "max_order_size": 10000,
    "max_notional_per_client": 1000000.0,
    "max_orders_per_second": 1000,
    "price_collar_enabled": true
  },
  "performance": {
    "order_pool_size": 1000000,
    "queue_capacity": 65536,
    "enable_cpu_pinning": true,
    "tcp_nodelay": true,
    "udp_buffer_size": 262144
  }
}
```

### 3. Set Environment Variables

```bash
# Required security variables
export RTES_HMAC_KEY=$(openssl rand -hex 32)
export RTES_TLS_CERT=/etc/rtes/certs/server.crt
export RTES_TLS_KEY=/etc/rtes/certs/server.key
export RTES_CA_CERT=/etc/rtes/certs/ca.crt

# Optional
export RTES_API_KEYS_FILE=/etc/rtes/api_keys.conf
```

### 4. Generate TLS Certificates

```bash
# Create certificate directory
sudo mkdir -p /etc/rtes/certs
cd /etc/rtes/certs

# Generate CA key and certificate
openssl genrsa -out ca.key 4096
openssl req -new -x509 -days 3650 -key ca.key -out ca.crt \
    -subj "/C=US/ST=State/L=City/O=Organization/CN=RTES-CA"

# Generate server key and CSR
openssl genrsa -out server.key 4096
openssl req -new -key server.key -out server.csr \
    -subj "/C=US/ST=State/L=City/O=Organization/CN=rtes.example.com"

# Sign server certificate
openssl x509 -req -days 365 -in server.csr \
    -CA ca.crt -CAkey ca.key -CAcreateserial \
    -out server.crt

# Set permissions
sudo chmod 600 server.key ca.key
sudo chmod 644 server.crt ca.crt
```

## Deployment Methods

### Method 1: Systemd Service (Recommended)

#### 1. Create Service File

```bash
sudo nano /etc/systemd/system/rtes.service
```

```ini
[Unit]
Description=RTES Trading Exchange
After=network.target

[Service]
Type=simple
User=rtes
Group=rtes
WorkingDirectory=/opt/rtes
Environment="RTES_HMAC_KEY=<your-key>"
Environment="RTES_TLS_CERT=/etc/rtes/certs/server.crt"
Environment="RTES_TLS_KEY=/etc/rtes/certs/server.key"
Environment="RTES_CA_CERT=/etc/rtes/certs/ca.crt"
ExecStart=/usr/local/bin/trading_exchange /etc/rtes/config.json
Restart=on-failure
RestartSec=5
LimitNOFILE=65536
LimitMEMLOCK=infinity

[Install]
WantedBy=multi-user.target
```

#### 2. Create User and Directories

```bash
sudo useradd -r -s /bin/false rtes
sudo mkdir -p /opt/rtes/logs
sudo chown -R rtes:rtes /opt/rtes
```

#### 3. Enable and Start Service

```bash
sudo systemctl daemon-reload
sudo systemctl enable rtes
sudo systemctl start rtes
sudo systemctl status rtes
```

#### 4. View Logs

```bash
sudo journalctl -u rtes -f
```

### Method 2: Docker Deployment

#### 1. Build Docker Image

```bash
docker build -t rtes:latest .
```

#### 2. Run Container

```bash
docker run -d \
    --name rtes \
    --network host \
    -v /etc/rtes:/etc/rtes:ro \
    -e RTES_HMAC_KEY=${RTES_HMAC_KEY} \
    -e RTES_TLS_CERT=/etc/rtes/certs/server.crt \
    -e RTES_TLS_KEY=/etc/rtes/certs/server.key \
    -e RTES_CA_CERT=/etc/rtes/certs/ca.crt \
    --restart unless-stopped \
    rtes:latest /etc/rtes/config.json
```

#### 3. View Logs

```bash
docker logs -f rtes
```

### Method 3: Docker Compose

#### 1. Create docker-compose.yml

```yaml
version: '3.8'

services:
  rtes:
    image: rtes:latest
    container_name: rtes
    network_mode: host
    restart: unless-stopped
    volumes:
      - /etc/rtes:/etc/rtes:ro
      - ./logs:/opt/rtes/logs
    environment:
      - RTES_HMAC_KEY=${RTES_HMAC_KEY}
      - RTES_TLS_CERT=/etc/rtes/certs/server.crt
      - RTES_TLS_KEY=/etc/rtes/certs/server.key
      - RTES_CA_CERT=/etc/rtes/certs/ca.crt
    command: /etc/rtes/config.json
    
  prometheus:
    image: prom/prometheus:latest
    container_name: prometheus
    ports:
      - "9090:9090"
    volumes:
      - ./docker/prometheus.yml:/etc/prometheus/prometheus.yml
      - prometheus-data:/prometheus
    command:
      - '--config.file=/etc/prometheus/prometheus.yml'
      - '--storage.tsdb.path=/prometheus'
    
  grafana:
    image: grafana/grafana:latest
    container_name: grafana
    ports:
      - "3000:3000"
    volumes:
      - grafana-data:/var/lib/grafana
      - ./docker/grafana-datasources.yml:/etc/grafana/provisioning/datasources/datasources.yml
    environment:
      - GF_SECURITY_ADMIN_PASSWORD=admin

volumes:
  prometheus-data:
  grafana-data:
```

#### 2. Start Stack

```bash
docker-compose up -d
```

### Method 4: Kubernetes Deployment

#### 1. Create Namespace

```bash
kubectl create namespace rtes
```

#### 2. Create ConfigMap

```bash
kubectl create configmap rtes-config \
    --from-file=config.json=/etc/rtes/config.json \
    -n rtes
```

#### 3. Create Secrets

```bash
kubectl create secret generic rtes-certs \
    --from-file=server.crt=/etc/rtes/certs/server.crt \
    --from-file=server.key=/etc/rtes/certs/server.key \
    --from-file=ca.crt=/etc/rtes/certs/ca.crt \
    -n rtes

kubectl create secret generic rtes-secrets \
    --from-literal=hmac-key=${RTES_HMAC_KEY} \
    -n rtes
```

#### 4. Create Deployment

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: rtes
  namespace: rtes
spec:
  replicas: 1
  selector:
    matchLabels:
      app: rtes
  template:
    metadata:
      labels:
        app: rtes
    spec:
      containers:
      - name: rtes
        image: rtes:latest
        ports:
        - containerPort: 8888
          name: tcp-orders
        - containerPort: 9999
          name: udp-market
          protocol: UDP
        - containerPort: 8080
          name: metrics
        env:
        - name: RTES_HMAC_KEY
          valueFrom:
            secretKeyRef:
              name: rtes-secrets
              key: hmac-key
        - name: RTES_TLS_CERT
          value: /etc/rtes/certs/server.crt
        - name: RTES_TLS_KEY
          value: /etc/rtes/certs/server.key
        - name: RTES_CA_CERT
          value: /etc/rtes/certs/ca.crt
        volumeMounts:
        - name: config
          mountPath: /etc/rtes
        - name: certs
          mountPath: /etc/rtes/certs
        resources:
          requests:
            memory: "8Gi"
            cpu: "4"
          limits:
            memory: "16Gi"
            cpu: "8"
      volumes:
      - name: config
        configMap:
          name: rtes-config
      - name: certs
        secret:
          secretName: rtes-certs
```

#### 5. Create Service

```yaml
apiVersion: v1
kind: Service
metadata:
  name: rtes
  namespace: rtes
spec:
  type: LoadBalancer
  selector:
    app: rtes
  ports:
  - name: tcp-orders
    port: 8888
    targetPort: 8888
    protocol: TCP
  - name: metrics
    port: 8080
    targetPort: 8080
    protocol: TCP
```

#### 6. Deploy

```bash
kubectl apply -f deployment.yaml
kubectl apply -f service.yaml
```

## Performance Tuning

### 1. System Configuration

```bash
# Increase file descriptor limits
echo "* soft nofile 65536" | sudo tee -a /etc/security/limits.conf
echo "* hard nofile 65536" | sudo tee -a /etc/security/limits.conf

# Increase network buffer sizes
sudo sysctl -w net.core.rmem_max=268435456
sudo sysctl -w net.core.wmem_max=268435456
sudo sysctl -w net.ipv4.tcp_rmem="4096 87380 134217728"
sudo sysctl -w net.ipv4.tcp_wmem="4096 65536 134217728"

# Disable transparent huge pages
echo never | sudo tee /sys/kernel/mm/transparent_hugepage/enabled

# Set CPU governor to performance
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
```

### 2. CPU Pinning

```bash
# Pin to specific CPUs (cores 0-7)
taskset -c 0-7 /usr/local/bin/trading_exchange /etc/rtes/config.json
```

### 3. NUMA Configuration

```bash
# Check NUMA topology
numactl --hardware

# Run on specific NUMA node
numactl --cpunodebind=0 --membind=0 /usr/local/bin/trading_exchange /etc/rtes/config.json
```

### 4. Huge Pages

```bash
# Enable huge pages
sudo sysctl -w vm.nr_hugepages=1024

# Mount hugetlbfs
sudo mkdir -p /mnt/huge
sudo mount -t hugetlbfs nodev /mnt/huge
```

## Monitoring Setup

### 1. Prometheus Configuration

```yaml
# docker/prometheus.yml
global:
  scrape_interval: 15s

scrape_configs:
  - job_name: 'rtes'
    static_configs:
      - targets: ['localhost:8080']
```

### 2. Grafana Dashboard

Import dashboard from `docker/grafana-dashboard.json` or create custom:

**Key Metrics**:
- Orders per second
- Latency percentiles (p50, p99, p999)
- Active connections
- Memory pool utilization
- Queue depth
- Error rates

### 3. Alerting Rules

```yaml
# prometheus-alerts.yml
groups:
  - name: rtes
    interval: 30s
    rules:
      - alert: HighLatency
        expr: rtes_order_latency_seconds{quantile="0.99"} > 0.0001
        for: 5m
        annotations:
          summary: "High order latency detected"
      
      - alert: HighErrorRate
        expr: rate(rtes_orders_rejected_total[5m]) > 100
        for: 2m
        annotations:
          summary: "High order rejection rate"
```

## Health Checks

### 1. Liveness Probe

```bash
curl -f http://localhost:8080/health || exit 1
```

### 2. Readiness Probe

```bash
curl -f http://localhost:8080/health | jq -e '.status == "healthy"'
```

### 3. Startup Probe

```bash
# Wait for service to be ready
timeout 60 bash -c 'until curl -f http://localhost:8080/health; do sleep 1; done'
```

## Backup and Recovery

### 1. Configuration Backup

```bash
# Backup configuration
sudo tar -czf rtes-config-$(date +%Y%m%d).tar.gz /etc/rtes

# Restore configuration
sudo tar -xzf rtes-config-20240115.tar.gz -C /
```

### 2. Log Rotation

```bash
# /etc/logrotate.d/rtes
/opt/rtes/logs/*.log {
    daily
    rotate 7
    compress
    delaycompress
    missingok
    notifempty
    create 0640 rtes rtes
    sharedscripts
    postrotate
        systemctl reload rtes
    endscript
}
```

## Troubleshooting

### Common Issues

#### 1. Port Already in Use

```bash
# Check what's using the port
sudo lsof -i :8888

# Kill process
sudo kill -9 <PID>
```

#### 2. Permission Denied

```bash
# Check file permissions
ls -la /etc/rtes/certs/

# Fix permissions
sudo chown rtes:rtes /etc/rtes/certs/*
sudo chmod 600 /etc/rtes/certs/*.key
```

#### 3. Out of Memory

```bash
# Check memory usage
free -h

# Increase swap
sudo fallocate -l 8G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile
```

#### 4. High Latency

```bash
# Check CPU usage
top -H -p $(pgrep trading_exchange)

# Check network latency
ping -c 10 <client-ip>

# Check disk I/O
iostat -x 1
```

### Debug Mode

```bash
# Run with debug logging
RTES_LOG_LEVEL=DEBUG /usr/local/bin/trading_exchange /etc/rtes/config.json

# Enable core dumps
ulimit -c unlimited
echo "/tmp/core.%e.%p" | sudo tee /proc/sys/kernel/core_pattern
```

### Performance Profiling

```bash
# CPU profiling with perf
sudo perf record -g -p $(pgrep trading_exchange)
sudo perf report

# Memory profiling with valgrind
valgrind --tool=massif /usr/local/bin/trading_exchange /etc/rtes/config.json
```

## Security Hardening

### 1. Firewall Configuration

```bash
# Allow only necessary ports
sudo ufw allow 8888/tcp  # Orders
sudo ufw allow 9999/udp  # Market data
sudo ufw allow 8080/tcp  # Metrics (internal only)
sudo ufw enable
```

### 2. SELinux/AppArmor

```bash
# SELinux context
sudo semanage fcontext -a -t bin_t "/usr/local/bin/trading_exchange"
sudo restorecon -v /usr/local/bin/trading_exchange
```

### 3. Secure Environment Variables

```bash
# Use systemd credentials
sudo systemd-creds encrypt --name=hmac-key - /etc/rtes/hmac-key.cred
```

## Upgrade Procedure

### 1. Backup Current Version

```bash
sudo systemctl stop rtes
sudo cp /usr/local/bin/trading_exchange /usr/local/bin/trading_exchange.backup
```

### 2. Deploy New Version

```bash
sudo cp build/trading_exchange /usr/local/bin/
sudo systemctl start rtes
```

### 3. Verify

```bash
curl http://localhost:8080/health
sudo journalctl -u rtes -n 100
```

### 4. Rollback if Needed

```bash
sudo systemctl stop rtes
sudo cp /usr/local/bin/trading_exchange.backup /usr/local/bin/trading_exchange
sudo systemctl start rtes
```

## Production Checklist

- [ ] System requirements met
- [ ] Dependencies installed
- [ ] Configuration file created
- [ ] Environment variables set
- [ ] TLS certificates generated
- [ ] Firewall configured
- [ ] Monitoring setup (Prometheus/Grafana)
- [ ] Alerting configured
- [ ] Log rotation configured
- [ ] Backup procedures tested
- [ ] Health checks working
- [ ] Performance tuning applied
- [ ] Security hardening complete
- [ ] Documentation reviewed
- [ ] Runbook created
- [ ] On-call rotation established
- [ ] Load testing completed
- [ ] Disaster recovery plan documented

## Support

For issues and questions:
- GitHub Issues: https://github.com/your-org/rtes/issues
- Documentation: https://docs.rtes.example.com
- Email: support@rtes.example.com
