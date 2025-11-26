#include "rtes/network_security.hpp"
#include "rtes/logger.hpp"
#include <openssl/err.h>
#include <openssl/x509.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <random>
#include <iomanip>
#include <sstream>

namespace rtes {

// RateLimiter implementation
RateLimiter::RateLimiter(uint32_t max_tokens, std::chrono::milliseconds refill_interval)
    : max_tokens_(max_tokens), refill_interval_(refill_interval) {}

bool RateLimiter::try_consume(const std::string& client_id, uint32_t tokens) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& bucket = buckets_[client_id];
    refill_bucket(bucket);
    
    if (bucket.tokens >= tokens) {
        bucket.tokens -= tokens;
        return true;
    }
    
    return false;
}

void RateLimiter::refill_bucket(TokenBucket& bucket) {
    auto now = std::chrono::steady_clock::now();
    
    if (bucket.last_refill == std::chrono::steady_clock::time_point{}) {
        bucket.tokens = max_tokens_;
        bucket.last_refill = now;
        return;
    }
    
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - bucket.last_refill);
    if (elapsed >= refill_interval_) {
        uint32_t tokens_to_add = static_cast<uint32_t>(elapsed.count() / refill_interval_.count());
        bucket.tokens = std::min(max_tokens_, bucket.tokens + tokens_to_add);
        bucket.last_refill = now;
    }
}

size_t RateLimiter::active_clients() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buckets_.size();
}

// SecureTcpChannel implementation
SecureTcpChannel::SecureTcpChannel(const SecurityConfig& config) : config_(config) {}

SecureTcpChannel::~SecureTcpChannel() {
    if (ssl_ctx_) {
        SSL_CTX_free(ssl_ctx_);
    }
}

Result<void> SecureTcpChannel::initialize() {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    
    return setup_ssl_context();
}

Result<void> SecureTcpChannel::setup_ssl_context() {
    ssl_ctx_ = SSL_CTX_new(TLS_server_method());
    if (!ssl_ctx_) {
        return ErrorCode::NETWORK_CONNECTION_FAILED;
    }
    
    // Set security options
    SSL_CTX_set_options(ssl_ctx_, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);
    SSL_CTX_set_min_proto_version(ssl_ctx_, TLS1_2_VERSION);
    
    // Load certificates
    auto cert_result = load_certificates();
    if (cert_result.has_error()) {
        return cert_result;
    }
    
    // Configure client certificate verification
    if (config_.require_client_certs) {
        SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
        
        if (!config_.ca_cert_file.empty()) {
            if (SSL_CTX_load_verify_locations(ssl_ctx_, config_.ca_cert_file.c_str(), nullptr) != 1) {
                return ErrorCode::FILE_NOT_FOUND;
            }
        }
    }
    
    return Result<void>();
}

Result<void> SecureTcpChannel::load_certificates() {
    if (SSL_CTX_use_certificate_file(ssl_ctx_, config_.tls_cert_file.c_str(), SSL_FILETYPE_PEM) != 1) {
        LOG_ERROR_SAFE("Failed to load certificate: {}", config_.tls_cert_file);
        return ErrorCode::FILE_NOT_FOUND;
    }
    
    if (SSL_CTX_use_PrivateKey_file(ssl_ctx_, config_.tls_key_file.c_str(), SSL_FILETYPE_PEM) != 1) {
        LOG_ERROR_SAFE("Failed to load private key: {}", config_.tls_key_file);
        return ErrorCode::FILE_NOT_FOUND;
    }
    
    if (SSL_CTX_check_private_key(ssl_ctx_) != 1) {
        LOG_ERROR("Private key does not match certificate");
        return ErrorCode::FILE_CORRUPTED;
    }
    
    return Result<void>();
}

Result<int> SecureTcpChannel::accept_secure_connection(int listen_fd) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    int client_fd = accept(listen_fd, reinterpret_cast<struct sockaddr*>(&client_addr), &addr_len);
    if (client_fd < 0) {
        return ErrorCode::NETWORK_CONNECTION_FAILED;
    }
    
    SSL* ssl = SSL_new(ssl_ctx_);
    if (!ssl) {
        close(client_fd);
        return ErrorCode::MEMORY_ALLOCATION_FAILED;
    }
    
    SSL_set_fd(ssl, client_fd);
    
    if (SSL_accept(ssl) <= 0) {
        SSL_free(ssl);
        close(client_fd);
        return ErrorCode::NETWORK_CONNECTION_FAILED;
    }
    
    // Verify client certificate if required
    if (config_.require_client_certs) {
        auto verify_result = verify_client_certificate(ssl);
        if (verify_result.has_error()) {
            SSL_free(ssl);
            close(client_fd);
            return verify_result.error();
        }
    }
    
    return reinterpret_cast<int>(ssl); // Return SSL pointer as int (unsafe but minimal)
}

Result<void> SecureTcpChannel::verify_client_certificate(SSL* ssl) {
    X509* cert = SSL_get_peer_certificate(ssl);
    if (!cert) {
        return ErrorCode::NETWORK_CONNECTION_FAILED;
    }
    
    long verify_result = SSL_get_verify_result(ssl);
    X509_free(cert);
    
    if (verify_result != X509_V_OK) {
        LOG_WARN_SAFE("Client certificate verification failed: {}", verify_result);
        return ErrorCode::NETWORK_CONNECTION_FAILED;
    }
    
    return Result<void>();
}

Result<size_t> SecureTcpChannel::secure_read(SSL* ssl, void* buffer, size_t size) {
    int bytes_read = SSL_read(ssl, buffer, static_cast<int>(size));
    if (bytes_read <= 0) {
        int error = SSL_get_error(ssl, bytes_read);
        if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
            return ErrorCode::NETWORK_DISCONNECTED;
        }
        return 0;
    }
    
    return static_cast<size_t>(bytes_read);
}

Result<size_t> SecureTcpChannel::secure_write(SSL* ssl, const void* buffer, size_t size) {
    int bytes_written = SSL_write(ssl, buffer, static_cast<int>(size));
    if (bytes_written <= 0) {
        int error = SSL_get_error(ssl, bytes_written);
        if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
            return ErrorCode::NETWORK_DISCONNECTED;
        }
        return 0;
    }
    
    return static_cast<size_t>(bytes_written);
}

// AuthenticatedUdpBroadcast implementation
AuthenticatedUdpBroadcast::AuthenticatedUdpBroadcast(const SecurityConfig& config) : config_(config) {}

Result<void> AuthenticatedUdpBroadcast::initialize() {
    if (config_.hmac_key.empty()) {
        return ErrorCode::INVALID_CONFIGURATION;
    }
    return Result<void>();
}

Result<void> AuthenticatedUdpBroadcast::send_authenticated_message(const void* data, size_t size, const std::string& multicast_group, uint16_t port) {
    // Compute HMAC
    uint8_t hmac[EVP_MAX_MD_SIZE];
    size_t hmac_len = 0;
    
    auto hmac_result = compute_hmac(data, size, hmac, &hmac_len);
    if (hmac_result.has_error()) {
        return hmac_result;
    }
    
    // Create authenticated message
    size_t total_size = sizeof(AuthenticatedMessage) + size + hmac_len;
    FixedSizeBuffer<8192> message_buffer;
    
    if (total_size > message_buffer.capacity()) {
        return ErrorCode::MEMORY_BUFFER_OVERFLOW;
    }
    
    AuthenticatedMessage auth_msg;
    auth_msg.sequence = sequence_number_++;
    auth_msg.hmac_size = static_cast<uint32_t>(hmac_len);
    
    message_buffer.write(&auth_msg, sizeof(auth_msg));
    message_buffer.append(data, size);
    message_buffer.append(hmac, hmac_len);
    
    // Send UDP message
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return ErrorCode::NETWORK_CONNECTION_FAILED;
    }
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, multicast_group.c_str(), &addr.sin_addr);
    
    ssize_t sent = sendto(sock, message_buffer.data(), message_buffer.size(), 0,
                         reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    
    close(sock);
    
    if (sent < 0) {
        return ErrorCode::NETWORK_CONNECTION_FAILED;
    }
    
    return Result<void>();
}

Result<void> AuthenticatedUdpBroadcast::compute_hmac(const void* data, size_t size, uint8_t* hmac, size_t* hmac_len) {
    unsigned int len = 0;
    
    if (!HMAC(EVP_sha256(), config_.hmac_key.c_str(), config_.hmac_key.length(),
              static_cast<const unsigned char*>(data), size, hmac, &len)) {
        return ErrorCode::SYSTEM_CORRUPTED_STATE;
    }
    
    *hmac_len = len;
    return Result<void>();
}

// NetworkSecurityMonitor implementation
NetworkSecurityMonitor::NetworkSecurityMonitor() {}

void NetworkSecurityMonitor::record_connection(const std::string& client_ip) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    auto& ip_metrics = ip_metrics_[client_ip];
    ip_metrics.connection_count++;
    
    if (ip_metrics.first_seen == std::chrono::steady_clock::time_point{}) {
        ip_metrics.first_seen = std::chrono::steady_clock::now();
    }
    
    stats_.total_connections.fetch_add(1);
}

void NetworkSecurityMonitor::record_message(const std::string& client_id, size_t message_size) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    // Validate message size to prevent integer overflow
    if (message_size > 1048576) { // 1MB max
        LOG_WARN_SAFE("Abnormally large message size: {} from client: {}", message_size, client_id);
        stats_.anomalies_detected.fetch_add(1);
        return;
    }
    
    auto& client_metrics = client_metrics_[client_id];
    
    // Check for overflow before incrementing
    if (client_metrics.message_count == UINT64_MAX) {
        LOG_ERROR_SAFE("Message count overflow for client: {}", client_id);
        return;
    }
    
    if (client_metrics.total_bytes > UINT64_MAX - message_size) {
        LOG_ERROR_SAFE("Total bytes overflow for client: {}", client_id);
        return;
    }
    
    client_metrics.message_count++;
    client_metrics.total_bytes += message_size;
    client_metrics.last_activity = std::chrono::steady_clock::now();
    
    if (client_metrics.first_seen == std::chrono::steady_clock::time_point{}) {
        client_metrics.first_seen = client_metrics.last_activity;
    }
}

bool NetworkSecurityMonitor::detect_anomalous_traffic_patterns(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    auto it = client_metrics_.find(client_id);
    if (it == client_metrics_.end()) {
        return false;
    }
    
    const auto& metrics = it->second;
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::minutes>(now - metrics.first_seen);
    
    if (duration.count() > 0) {
        uint64_t messages_per_minute = metrics.message_count / duration.count();
        uint64_t bytes_per_minute = metrics.total_bytes / duration.count();
        
        if (messages_per_minute > MAX_MESSAGES_PER_MINUTE || bytes_per_minute > MAX_BYTES_PER_MINUTE) {
            stats_.anomalies_detected.fetch_add(1);
            generate_security_alert("TRAFFIC_ANOMALY", client_id);
            return true;
        }
    }
    
    return false;
}

bool NetworkSecurityMonitor::should_block_connection(const std::string& client_ip) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    auto it = ip_metrics_.find(client_ip);
    if (it == ip_metrics_.end()) {
        return false;
    }
    
    const auto& metrics = it->second;
    
    if (metrics.connection_count > MAX_CONNECTIONS_PER_IP || metrics.auth_failures > MAX_AUTH_FAILURES) {
        stats_.blocked_connections.fetch_add(1);
        return true;
    }
    
    return false;
}

void NetworkSecurityMonitor::generate_security_alert(const std::string& alert_type, const std::string& details) {
    LOG_WARN_SAFE("SECURITY ALERT [{}]: {}", alert_type, details);
}

// ClientAuthenticator implementation
ClientAuthenticator::ClientAuthenticator(const SecurityConfig& config) : config_(config) {
    // Load API keys from secure storage (environment variables or key management service)
    const char* api_keys_env = std::getenv("RTES_API_KEYS_FILE");
    if (api_keys_env) {
        load_api_keys_from_file(api_keys_env);
    } else {
        LOG_WARN("No API keys file configured. Set RTES_API_KEYS_FILE environment variable.");
    }
}

Result<std::string> ClientAuthenticator::authenticate_certificate(SSL* ssl) {
    X509* cert = SSL_get_peer_certificate(ssl);
    if (!cert) {
        return ErrorCode::NETWORK_CONNECTION_FAILED;
    }
    
    std::string client_id = extract_client_id_from_cert(ssl);
    X509_free(cert);
    
    if (client_id.empty()) {
        return ErrorCode::NETWORK_CONNECTION_FAILED;
    }
    
    return client_id;
}

std::string ClientAuthenticator::extract_client_id_from_cert(SSL* ssl) {
    X509* cert = SSL_get_peer_certificate(ssl);
    if (!cert) return "";
    
    X509_NAME* subject = X509_get_subject_name(cert);
    if (!subject) {
        X509_free(cert);
        return "";
    }
    
    char cn[256] = {0};
    X509_NAME_get_text_by_NID(subject, NID_commonName, cn, sizeof(cn));
    
    X509_free(cert);
    return std::string(cn);
}

Result<std::string> ClientAuthenticator::create_session(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    std::string token = generate_session_token();
    
    Session session;
    session.client_id = client_id;
    session.created = std::chrono::steady_clock::now();
    session.last_access = session.created;
    
    active_sessions_[token] = session;
    
    return token;
}

std::string ClientAuthenticator::generate_session_token() {
    // Use cryptographically secure random number generation
    unsigned char random_bytes[32];
    if (RAND_bytes(random_bytes, sizeof(random_bytes)) != 1) {
        LOG_ERROR("Failed to generate secure random bytes");
        throw std::runtime_error("Cryptographic random generation failed");
    }
    
    std::stringstream ss;
    for (size_t i = 0; i < sizeof(random_bytes); ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(random_bytes[i]);
    }
    
    return ss.str();
}

bool ClientAuthenticator::validate_session(const std::string& session_token) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    auto it = active_sessions_.find(session_token);
    if (it == active_sessions_.end()) {
        return false;
    }
    
    auto now = std::chrono::steady_clock::now();
    if (now - it->second.created > config_.session_timeout) {
        active_sessions_.erase(it);
        return false;
    }
    
    it->second.last_access = now;
    return true;
}

// SecureNetworkLayer implementation
SecureNetworkLayer::SecureNetworkLayer(const SecurityConfig& config) : config_(config) {
    tcp_channel_ = std::make_unique<SecureTcpChannel>(config);
    udp_broadcast_ = std::make_unique<AuthenticatedUdpBroadcast>(config);
    rate_limiter_ = std::make_unique<RateLimiter>(config.rate_limit_per_second, std::chrono::seconds(1));
    security_monitor_ = std::make_unique<NetworkSecurityMonitor>();
    authenticator_ = std::make_unique<ClientAuthenticator>(config);
}

SecureNetworkLayer::~SecureNetworkLayer() = default;

Result<void> SecureNetworkLayer::initialize() {
    auto tcp_result = tcp_channel_->initialize();
    if (tcp_result.has_error()) return tcp_result;
    
    auto udp_result = udp_broadcast_->initialize();
    if (udp_result.has_error()) return udp_result;
    
    LOG_INFO("Secure network layer initialized");
    return Result<void>();
}

Result<int> SecureNetworkLayer::accept_secure_tcp_connection(int listen_fd, std::string& client_id) {
    auto ssl_result = tcp_channel_->accept_secure_connection(listen_fd);
    if (ssl_result.has_error()) {
        return ssl_result.error();
    }
    
    SSL* ssl = reinterpret_cast<SSL*>(ssl_result.value());
    
    auto auth_result = authenticator_->authenticate_certificate(ssl);
    if (auth_result.has_error()) {
        tcp_channel_->close_connection(ssl);
        return auth_result.error();
    }
    
    client_id = auth_result.value();
    return ssl_result.value();
}

Result<void> SecureNetworkLayer::broadcast_market_data(const void* data, size_t size) {
    return udp_broadcast_->send_authenticated_message(data, size, "239.0.0.1", 9999);
}

bool SecureNetworkLayer::is_client_rate_limited(const std::string& client_id) {
    return !rate_limiter_->try_consume(client_id);
}

} // namespace rtes

void SecureTcpChannel::close_connection(SSL* ssl) {
    if (ssl) {
        SSL_shutdown(ssl);
        int fd = SSL_get_fd(ssl);
        SSL_free(ssl);
        if (fd >= 0) {
            close(fd);
        }
    }
}

void SecureNetworkLayer::record_security_event(const std::string& event_type, const std::string& client_id) {
    security_monitor_->record_message(client_id, 0);
    LOG_INFO_SAFE("Security event [{}] for client: {}", event_type, client_id);
}

bool SecureNetworkLayer::should_block_ip(const std::string& ip_address) {
    return security_monitor_->should_block_connection(ip_address);
}

Result<size_t> SecureNetworkLayer::read_secure_tcp_message(SSL* ssl, void* buffer, size_t size) {
    return tcp_channel_->secure_read(ssl, buffer, size);
}

Result<size_t> SecureNetworkLayer::write_secure_tcp_message(SSL* ssl, const void* buffer, size_t size) {
    return tcp_channel_->secure_write(ssl, buffer, size);
}

Result<bool> SecureNetworkLayer::verify_udp_message(const void* data, size_t size) {
    return udp_broadcast_->verify_message_authenticity(data, size);
}

void RateLimiter::reset_client(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    buckets_.erase(client_id);
}

void ClientAuthenticator::invalidate_session(const std::string& session_token) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    active_sessions_.erase(session_token);
}

void ClientAuthenticator::cleanup_expired_sessions() {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto now = std::chrono::steady_clock::now();
    
    for (auto it = active_sessions_.begin(); it != active_sessions_.end();) {
        if (now - it->second.created > config_.session_timeout) {
            it = active_sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

Result<std::string> ClientAuthenticator::validate_api_key(const std::string& api_key) {
    auto it = api_keys_.find(api_key);
    if (it == api_keys_.end()) {
        return ErrorCode::NETWORK_CONNECTION_FAILED;
    }
    return it->second;
}

void NetworkSecurityMonitor::record_authentication_failure(const std::string& client_ip) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    auto& ip_metrics = ip_metrics_[client_ip];
    ip_metrics.auth_failures++;
    ip_metrics.last_failure = std::chrono::steady_clock::now();
    
    stats_.auth_failures.fetch_add(1);
    
    if (ip_metrics.auth_failures >= MAX_AUTH_FAILURES) {
        generate_security_alert("AUTH_FAILURE_THRESHOLD", client_ip);
    }
}

Result<bool> AuthenticatedUdpBroadcast::verify_message_authenticity(const void* data, size_t size) {
    if (!data || size < sizeof(AuthenticatedMessage)) {
        return ErrorCode::INVALID_ARGUMENT;
    }
    
    const AuthenticatedMessage* auth_msg = static_cast<const AuthenticatedMessage*>(data);
    
    if (size < sizeof(AuthenticatedMessage) + auth_msg->hmac_size) {
        return ErrorCode::INVALID_ARGUMENT;
    }
    
    const uint8_t* payload = static_cast<const uint8_t*>(data) + sizeof(AuthenticatedMessage);
    size_t payload_size = size - sizeof(AuthenticatedMessage) - auth_msg->hmac_size;
    const uint8_t* received_hmac = payload + payload_size;
    
    uint8_t computed_hmac[EVP_MAX_MD_SIZE];
    size_t hmac_len = 0;
    
    auto result = compute_hmac(payload, payload_size, computed_hmac, &hmac_len);
    if (result.has_error()) {
        return result.error();
    }
    
    if (hmac_len != auth_msg->hmac_size) {
        return false;
    }
    
    return CRYPTO_memcmp(computed_hmac, received_hmac, hmac_len) == 0;
}
