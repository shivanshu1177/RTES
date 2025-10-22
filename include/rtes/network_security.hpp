#pragma once

#include "rtes/error_handling.hpp"
#include "rtes/memory_safety.hpp"
#include <openssl/ssl.h>
#include <openssl/hmac.h>
#include <string>
#include <chrono>
#include <unordered_map>
#include <atomic>
#include <mutex>

namespace rtes {

// Security configuration
struct SecurityConfig {
    std::string tls_cert_file;
    std::string tls_key_file;
    std::string ca_cert_file;
    BoundedString<64> hmac_key;
    uint32_t max_connections_per_ip = 10;
    uint32_t rate_limit_per_second = 1000;
    std::chrono::seconds session_timeout{3600};
    bool require_client_certs = true;
};

// Rate limiter with token bucket algorithm
class RateLimiter {
public:
    RateLimiter(uint32_t max_tokens, std::chrono::milliseconds refill_interval);
    
    bool try_consume(const std::string& client_id, uint32_t tokens = 1);
    void reset_client(const std::string& client_id);
    size_t active_clients() const;
    
private:
    struct TokenBucket {
        uint32_t tokens;
        std::chrono::steady_clock::time_point last_refill;
    };
    
    uint32_t max_tokens_;
    std::chrono::milliseconds refill_interval_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, TokenBucket> buckets_;
    
    void refill_bucket(TokenBucket& bucket);
};

// TLS/SSL secure TCP channel
class SecureTcpChannel {
public:
    SecureTcpChannel(const SecurityConfig& config);
    ~SecureTcpChannel();
    
    Result<void> initialize();
    Result<int> accept_secure_connection(int listen_fd);
    Result<void> verify_client_certificate(SSL* ssl);
    Result<size_t> secure_read(SSL* ssl, void* buffer, size_t size);
    Result<size_t> secure_write(SSL* ssl, const void* buffer, size_t size);
    void close_connection(SSL* ssl);
    
private:
    SecurityConfig config_;
    SSL_CTX* ssl_ctx_ = nullptr;
    
    Result<void> setup_ssl_context();
    Result<void> load_certificates();
};

// HMAC authenticated UDP broadcast
class AuthenticatedUdpBroadcast {
public:
    AuthenticatedUdpBroadcast(const SecurityConfig& config);
    
    Result<void> initialize();
    Result<void> send_authenticated_message(const void* data, size_t size, const std::string& multicast_group, uint16_t port);
    Result<bool> verify_message_authenticity(const void* data, size_t size);
    
private:
    SecurityConfig config_;
    uint64_t sequence_number_ = 1;
    
    struct AuthenticatedMessage {
        uint64_t sequence;
        uint32_t hmac_size;
        // Followed by: data + HMAC
    };
    
    Result<void> compute_hmac(const void* data, size_t size, uint8_t* hmac, size_t* hmac_len);
};

// Network security monitor for anomaly detection
class NetworkSecurityMonitor {
public:
    NetworkSecurityMonitor();
    
    void record_connection(const std::string& client_ip);
    void record_message(const std::string& client_id, size_t message_size);
    void record_authentication_failure(const std::string& client_ip);
    
    bool detect_anomalous_traffic_patterns(const std::string& client_id);
    bool should_block_connection(const std::string& client_ip);
    void generate_security_alert(const std::string& alert_type, const std::string& details);
    
    // Statistics
    struct SecurityStats {
        std::atomic<uint64_t> total_connections{0};
        std::atomic<uint64_t> blocked_connections{0};
        std::atomic<uint64_t> auth_failures{0};
        std::atomic<uint64_t> anomalies_detected{0};
    };
    
    const SecurityStats& get_stats() const { return stats_; }
    
private:
    struct ClientMetrics {
        uint64_t message_count = 0;
        uint64_t total_bytes = 0;
        std::chrono::steady_clock::time_point first_seen;
        std::chrono::steady_clock::time_point last_activity;
        uint32_t auth_failures = 0;
    };
    
    struct IpMetrics {
        uint32_t connection_count = 0;
        uint32_t auth_failures = 0;
        std::chrono::steady_clock::time_point first_seen;
        std::chrono::steady_clock::time_point last_failure;
    };
    
    mutable std::mutex metrics_mutex_;
    std::unordered_map<std::string, ClientMetrics> client_metrics_;
    std::unordered_map<std::string, IpMetrics> ip_metrics_;
    SecurityStats stats_;
    
    // Anomaly detection thresholds
    static constexpr uint64_t MAX_MESSAGES_PER_MINUTE = 10000;
    static constexpr uint64_t MAX_BYTES_PER_MINUTE = 1048576; // 1MB
    static constexpr uint32_t MAX_AUTH_FAILURES = 5;
    static constexpr uint32_t MAX_CONNECTIONS_PER_IP = 10;
};

// Client authentication and session management
class ClientAuthenticator {
public:
    ClientAuthenticator(const SecurityConfig& config);
    
    Result<std::string> authenticate_certificate(SSL* ssl);
    Result<std::string> validate_api_key(const std::string& api_key);
    Result<std::string> create_session(const std::string& client_id);
    bool validate_session(const std::string& session_token);
    void invalidate_session(const std::string& session_token);
    void cleanup_expired_sessions();
    
private:
    struct Session {
        std::string client_id;
        std::chrono::steady_clock::time_point created;
        std::chrono::steady_clock::time_point last_access;
    };
    
    SecurityConfig config_;
    mutable std::mutex sessions_mutex_;
    std::unordered_map<std::string, Session> active_sessions_;
    std::unordered_map<std::string, std::string> api_keys_; // api_key -> client_id
    
    std::string generate_session_token();
    std::string extract_client_id_from_cert(SSL* ssl);
};

// Secure network layer orchestrator
class SecureNetworkLayer {
public:
    SecureNetworkLayer(const SecurityConfig& config);
    ~SecureNetworkLayer();
    
    Result<void> initialize();
    
    // Secure TCP operations
    Result<int> accept_secure_tcp_connection(int listen_fd, std::string& client_id);
    Result<size_t> read_secure_tcp_message(SSL* ssl, void* buffer, size_t size);
    Result<size_t> write_secure_tcp_message(SSL* ssl, const void* buffer, size_t size);
    
    // Authenticated UDP operations
    Result<void> broadcast_market_data(const void* data, size_t size);
    Result<bool> verify_udp_message(const void* data, size_t size);
    
    // Security monitoring
    bool is_client_rate_limited(const std::string& client_id);
    bool should_block_ip(const std::string& ip_address);
    void record_security_event(const std::string& event_type, const std::string& client_id);
    
private:
    SecurityConfig config_;
    std::unique_ptr<SecureTcpChannel> tcp_channel_;
    std::unique_ptr<AuthenticatedUdpBroadcast> udp_broadcast_;
    std::unique_ptr<RateLimiter> rate_limiter_;
    std::unique_ptr<NetworkSecurityMonitor> security_monitor_;
    std::unique_ptr<ClientAuthenticator> authenticator_;
};

} // namespace rtes