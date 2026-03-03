# RTES Operations Runbook

## Deployment

### Prerequisites
```bash
# Install dependencies (Ubuntu/Debian)
sudo apt update
sudo apt install -y build-essential cmake libgtest-dev pkg-config

# Install dependencies (RHEL/CentOS)
sudo yum groupinstall -y "Development Tools"
sudo yum install -y cmake3 gtest-devel pkgconfig
```

### Build and Install
```bash
git clone <repository>
cd RTES
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
```

### Configuration
```bash
# Copy config template
sudo cp configs/config.json /etc/rtes/
sudo chown rtes:rtes /etc/rtes/config.json

# Create log directory
sudo mkdir -p /var/log/rtes
sudo chown rtes:rtes /var/log/rtes
```

## Service Management

### Systemd Service
```ini
# /etc/systemd/system/rtes.service
[Unit]
Description=Real-Time Trading Exchange Simulator
After=network.target

[Service]
Type=simple
User=rtes
Group=rtes
ExecStart=/usr/local/bin/trading_exchange /etc/rtes/config.json
Restart=always
RestartSec=5
LimitNOFILE=65536
LimitMEMLOCK=infinity

[Install]
WantedBy=multi-user.target
```

### Service Commands
```bash
# Enable and start
sudo systemctl enable rtes
sudo systemctl start rtes

# Status and logs
sudo systemctl status rtes
sudo journalctl -u rtes -f

# Stop and restart
sudo systemctl stop rtes
sudo systemctl restart rtes
```

## Monitoring

### Health Checks
```bash
# Service health
curl -f http://localhost:8080/health || echo "Service unhealthy"

# Readiness check
curl -f http://localhost:8080/ready || echo "Service not ready"

# Metrics scrape
curl -s http://localhost:8080/metrics | grep rtes_
```

### Log Analysis
```bash
# Error patterns
sudo journalctl -u rtes | grep -E "(ERROR|FATAL|CRITICAL)"

# Performance metrics
sudo journalctl -u rtes | grep "latency_us"

# Connection issues
sudo journalctl -u rtes | grep -E "(connection|socket)"
```

## Troubleshooting

### Common Issues

#### Service Won't Start
```bash
# Check config syntax
python -m json.tool /etc/rtes/config.json

# Verify permissions
ls -la /etc/rtes/config.json
sudo -u rtes cat /etc/rtes/config.json

# Check port availability
sudo netstat -tlnp | grep -E "(8888|8080|9999)"
```

#### High CPU Usage
```bash
# Check thread affinity
ps -eLo pid,tid,comm,psr | grep trading_exchange

# Profile with perf
sudo perf record -g ./trading_exchange config.json
sudo perf report
```

#### Memory Leaks
```bash
# Run with AddressSanitizer
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
./trading_exchange config.json

# Monitor memory usage
watch -n 1 'ps -p $(pgrep trading_exchange) -o pid,vsz,rss,pmem'
```

#### Network Issues
```bash
# Check multicast routing
ip route show | grep 239.0.0.1

# Test UDP multicast
python tools/md_recv.py --group 239.0.0.1 --port 9999

# TCP connection test
telnet localhost 8888
```

## Maintenance

### Log Rotation
```bash
# /etc/logrotate.d/rtes
/var/log/rtes/*.log {
    daily
    rotate 30
    compress
    delaycompress
    missingok
    notifempty
    postrotate
        systemctl reload rtes
    endscript
}
```

### Backup Procedures
```bash
# Config backup
sudo cp /etc/rtes/config.json /backup/rtes-config-$(date +%Y%m%d).json

# Event log backup (if enabled)
sudo tar -czf /backup/rtes-logs-$(date +%Y%m%d).tar.gz /var/log/rtes/
```

### Updates
```bash
# Graceful shutdown
sudo systemctl stop rtes

# Update binary
sudo cp new_trading_exchange /usr/local/bin/
sudo chmod +x /usr/local/bin/trading_exchange

# Restart service
sudo systemctl start rtes
```

## Security

### Firewall Rules
```bash
# Allow required ports
sudo ufw allow 8888/tcp comment "RTES Order Entry"
sudo ufw allow 8080/tcp comment "RTES Metrics"
sudo ufw allow 9999/udp comment "RTES Market Data"
```

### User Management
```bash
# Create service user
sudo useradd -r -s /bin/false rtes
sudo usermod -aG rtes rtes
```

## Performance Tuning

### Real-time Configuration
```bash
# Set process priority
sudo chrt -f 99 $(pgrep trading_exchange)

# Lock memory pages
sudo prlimit --pid $(pgrep trading_exchange) --memlock=unlimited
```

### Network Optimization
```bash
# Increase buffer sizes
echo 'net.core.rmem_max = 268435456' | sudo tee -a /etc/sysctl.conf
echo 'net.core.wmem_max = 268435456' | sudo tee -a /etc/sysctl.conf
sudo sysctl -p
```