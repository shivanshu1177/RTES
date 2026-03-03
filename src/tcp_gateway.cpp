#include "rtes/tcp_gateway.hpp"
#include "rtes/logger.hpp"
#include "rtes/auth_middleware.hpp"
#include "rtes/security_utils.hpp"
#include "rtes/input_validation.hpp"
#include "rtes/network_security.hpp"
#include "rtes/thread_safety.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>

namespace rtes {

ClientConnection::ClientConnection(int fd) : fd_(fd) {
    setup_socket();
}

ClientConnection::~ClientConnection() {
    disconnect();
}

bool ClientConnection::setup_socket() {
    if (!fd_.valid()) return false;
    
    // Set TCP_NODELAY
    int flag = 1;
    if (setsockopt(fd_.get(), IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
        LOG_ERROR("Failed to set TCP_NODELAY");
        return false;
    }
    
    // Set non-blocking
    int flags = fcntl(fd_.get(), F_GETFL, 0);
    if (flags < 0 || fcntl(fd_.get(), F_SETFL, flags | O_NONBLOCK) < 0) {
        LOG_ERROR("Failed to set non-blocking");
        return false;
    }
    
    return true;
}

ssize_t ClientConnection::read_data_safe(void* buffer, size_t size) {
    if (!connected_.load() || !fd_.valid()) return -1;
    
    // Validate buffer size
    if (size > MAX_MESSAGE_SIZE) {
        LOG_ERROR_SAFE("Read size {} exceeds maximum {}", size, MAX_MESSAGE_SIZE);
        return -1;
    }
    
    ssize_t bytes_read = recv(fd_.get(), buffer, size, 0);
    if (bytes_read <= 0) {
        if (bytes_read == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
            disconnect();
        }
    }
    
    return bytes_read;
}

ssize_t ClientConnection::write_data_safe(const void* buffer, size_t size) {
    if (!connected_.load() || !fd_.valid()) return -1;
    
    // Validate buffer size
    if (size > MAX_MESSAGE_SIZE) {
        LOG_ERROR_SAFE("Write size {} exceeds maximum {}", size, MAX_MESSAGE_SIZE);
        return -1;
    }
    
    ssize_t bytes_sent = send(fd_.get(), buffer, size, MSG_NOSIGNAL);
    if (bytes_sent < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            disconnect();
        }
    }
    
    return bytes_sent;
}

bool ClientConnection::has_complete_message() const {
    if (read_buffer_.size() < sizeof(MessageHeader)) return false;
    
    const MessageHeader* header = reinterpret_cast<const MessageHeader*>(read_buffer_.data());
    
    // Validate message length
    if (!MessageValidator::validate_message_size(header->length, sizeof(MessageHeader), MAX_MESSAGE_SIZE)) {
        return false;
    }
    
    return read_buffer_.size() >= header->length;
}

bool ClientConnection::read_message_safe(FixedSizeBuffer<8192>& buffer) {
    // Try to read more data
    FixedSizeBuffer<4096> temp_buffer;
    ssize_t bytes_read = read_data_safe(temp_buffer.data(), temp_buffer.capacity());
    
    if (bytes_read > 0) {
        // Validate and sanitize input
        if (!MessageValidator::sanitize_network_input(temp_buffer.data(), bytes_read)) {
            LOG_WARN("Invalid network input received");
            return false;
        }
        
        try {
            read_buffer_.append(temp_buffer.data(), bytes_read);
        } catch (const BufferOverflowError& e) {
            LOG_ERROR_SAFE("Read buffer overflow: {}", e.what());
            disconnect();
            return false;
        }
    }
    
    // Check if we have a complete message
    if (!has_complete_message()) return false;
    
    const MessageHeader* header = reinterpret_cast<const MessageHeader*>(read_buffer_.data());
    
    // Validate message header
    if (!validate_message_header(read_buffer_.data(), read_buffer_.size())) {
        LOG_WARN("Invalid message header");
        return false;
    }
    
    // Extract complete message safely
    try {
        buffer.write(read_buffer_.data(), header->length);
        
        // Remove processed message from buffer
        size_t remaining = read_buffer_.size() - header->length;
        if (remaining > 0) {
            std::memmove(const_cast<void*>(read_buffer_.data()), 
                        static_cast<const char*>(read_buffer_.data()) + header->length, 
                        remaining);
        }
        read_buffer_.clear();
        if (remaining > 0) {
            read_buffer_.append(static_cast<const char*>(read_buffer_.data()), remaining);
        }
    } catch (const BufferOverflowError& e) {
        LOG_ERROR_SAFE("Message buffer overflow: {}", e.what());
        return false;
    }
    
    return true;
}

bool ClientConnection::write_message_safe(const void* message, size_t size) {
    if (!message || size == 0) return false;
    
    // Validate message size
    if (!MessageValidator::validate_message_size(size, sizeof(MessageHeader), MAX_MESSAGE_SIZE)) {
        LOG_ERROR_SAFE("Invalid message size: {}", size);
        return false;
    }
    
    try {
        // Add to write buffer
        write_buffer_.append(message, size);
        
        // Try to send buffered data
        while (write_offset_ < write_buffer_.size()) {
            ssize_t bytes_sent = write_data_safe(
                static_cast<const char*>(write_buffer_.data()) + write_offset_, 
                write_buffer_.size() - write_offset_);
            
            if (bytes_sent <= 0) {
                return errno == EAGAIN || errno == EWOULDBLOCK;
            }
            
            write_offset_ += bytes_sent;
        }
        
        // Clear sent data
        write_buffer_.clear();
        write_offset_ = 0;
        
    } catch (const BufferOverflowError& e) {
        LOG_ERROR_SAFE("Write buffer overflow: {}", e.what());
        return false;
    }
    
    return true;
}

void ClientConnection::disconnect() {
    if (connected_.exchange(false)) {
        fd_.close();
    }
}

bool ClientConnection::validate_message_header(const void* data, size_t size) const {
    if (size < sizeof(MessageHeader)) return false;
    
    const MessageHeader* header = static_cast<const MessageHeader*>(data);
    
    // Validate message length
    if (header->length < sizeof(MessageHeader) || header->length > MAX_MESSAGE_SIZE) {
        return false;
    }
    
    // Validate message type
    if (header->type > 255) { // Assuming reasonable message type range
        return false;
    }
    
    return true;
}

TcpGateway::TcpGateway(uint16_t port, RiskManager* risk_manager, OrderPool* order_pool)
    : port_(port), risk_manager_(risk_manager), order_pool_(order_pool), 
      running_(false), connections_accepted_(0), messages_received_(0), messages_sent_(0), next_sequence_(1) {
    
    // Register with shutdown manager
    ShutdownManager::instance().register_component(
        "TcpGateway", 
        [this]() { stop(); }
    );
    SecurityConfig security_config;
    security_config.tls_cert_file = "certs/server.crt";
    security_config.tls_key_file = "certs/server.key";
    security_config.ca_cert_file = "certs/ca.crt";
    security_config.hmac_key.assign("secure_hmac_key_12345");
    security_config.rate_limit_per_second = 1000;
    
    secure_network_ = std::make_unique<SecureNetworkLayer>(security_config);
}

TcpGateway::~TcpGateway() {
    stop();
}

void TcpGateway::start() {
    if (running_.load()) return;
    
    if (ShutdownManager::instance().is_shutdown_requested()) {
        LOG_WARN("Cannot start TCP gateway during shutdown");
        return;
    }
    
    if (!setup_listen_socket() || !setup_epoll()) {
        LOG_ERROR("Failed to setup TCP gateway");
        return;
    }
    
    running_.store(true);
    acceptor_thread_ = std::thread(&TcpGateway::acceptor_loop, this);
    worker_thread_ = std::thread(&TcpGateway::worker_loop, this);
    
    LOG_INFO("TCP gateway started on port " + std::to_string(port_));
}

void TcpGateway::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    
    // Drain work before shutdown
    work_drainer_.drain_work();
    
    if (acceptor_thread_.joinable()) acceptor_thread_.join();
    if (worker_thread_.joinable()) worker_thread_.join();
    
    // Close all connections with proper synchronization
    {
        scoped_lock lock(connections_mutex_);
        for (auto& [fd, conn] : connections_) {
            conn->disconnect();
        }
        connections_.clear();
    }
    
    // RAII handles cleanup automatically
    epoll_fd_.close();
    listen_fd_.close();
    
    LOG_INFO("TCP gateway stopped");
}

bool TcpGateway::setup_listen_socket() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_ERROR("Failed to create listen socket");
        return false;
    }
    
    listen_fd_.reset(fd);
    
    // Set socket options
    int opt = 1;
    setsockopt(listen_fd_.get(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Bind to port
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);
    
    if (bind(listen_fd_.get(), reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG_ERROR_SAFE("Failed to bind to port {}", port_);
        return false;
    }
    
    if (listen(listen_fd_.get(), 128) < 0) {
        LOG_ERROR("Failed to listen on socket");
        return false;
    }
    
    return true;
}

bool TcpGateway::setup_epoll() {
    int fd = epoll_create1(EPOLL_CLOEXEC);
    if (fd < 0) {
        LOG_ERROR("Failed to create epoll");
        return false;
    }
    
    epoll_fd_.reset(fd);
    
    struct epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd_.get();
    
    if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, listen_fd_.get(), &ev) < 0) {
        LOG_ERROR("Failed to add listen socket to epoll");
        return false;
    }
    
    return true;
}

void TcpGateway::acceptor_loop() {
    while (running_.load()) {
        accept_connection();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void TcpGateway::worker_loop() {
    struct epoll_event events[64];
    
    while (running_.load()) {
        int nfds = epoll_wait(epoll_fd_.get(), events, 64, 10);
        
        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            
            if (fd == listen_fd_.get()) {
                accept_connection();
            } else {
                handle_client_data(fd);
            }
        }
    }
}

void TcpGateway::accept_connection() {
    if (ShutdownManager::instance().is_shutdown_requested()) return;
    
    struct sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);
    
    int client_fd = accept(listen_fd_.get(), reinterpret_cast<struct sockaddr*>(&client_addr), &addr_len);
    if (client_fd < 0) return;
    
    auto conn = std::make_unique<ClientConnection>(client_fd);
    
    // Add to epoll
    struct epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = client_fd;
    
    if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, client_fd, &ev) == 0) {
        {
            scoped_lock lock(connections_mutex_);
            connections_[client_fd] = std::move(conn);
        }
        connections_accepted_.fetch_add(1);
        LOG_INFO_SAFE("Client connected: fd={}", client_fd);
    } else {
        ::close(client_fd);
    }
}

void TcpGateway::handle_client_data(int client_fd) {
    std::shared_ptr<ClientConnection> conn;
    {
        scoped_lock lock(connections_mutex_);
        auto it = connections_.find(client_fd);
        if (it == connections_.end()) return;
        conn = it->second;
    }
    
    FixedSizeBuffer<8192> buffer;
    
    while (conn->read_message_safe(buffer)) {
        process_message_safe(conn.get(), buffer);
        messages_received_.fetch_add(1);
        buffer.clear();
    }
    
    if (!conn->is_connected()) {
        remove_connection(client_fd);
    }
}

void TcpGateway::remove_connection(int client_fd) {
    epoll_ctl(epoll_fd_.get(), EPOLL_CTL_DEL, client_fd, nullptr);
    {
        scoped_lock lock(connections_mutex_);
        connections_.erase(client_fd);
    }
    LOG_INFO_SAFE("Client disconnected: fd={}", client_fd);
}

void TcpGateway::process_message_safe(ClientConnection* conn, const FixedSizeBuffer<8192>& buffer) {
    if (buffer.size() < sizeof(MessageHeader)) {
        LOG_WARN("Message too small for header");
        return;
    }
    
    const MessageHeader* header = reinterpret_cast<const MessageHeader*>(buffer.data());
    
    // Validate message header first
    auto header_validation = MessageValidator::validate_message_header(*header);
    if (header_validation.has_error()) {
        LOG_WARN_SAFE("Invalid message header: {}", header_validation.error().message());
        return;
    }
    
    const void* payload = static_cast<const char*>(buffer.data()) + sizeof(MessageHeader);
    size_t payload_size = buffer.size() - sizeof(MessageHeader);
    
    // Validate message payload
    auto payload_validation = MessageValidator::validate_message_payload(
        payload, payload_size, static_cast<MessageType>(header->type));
    if (payload_validation.has_error()) {
        LOG_WARN_SAFE("Invalid message payload: {}", payload_validation.error().message());
        return;
    }
    
    // Validate checksum
    if (!ProtocolUtils::validate_checksum(*header, payload)) {
        LOG_WARN("Invalid checksum in message");
        return;
    }
    
    // Extract authentication token (first 32 bytes of payload)
    std::string auth_token;
    if (payload_size >= 32) {
        const char* token_ptr = static_cast<const char*>(payload);
        auth_token = InputSanitizer::sanitize_network_input(std::string(token_ptr, 32));
        payload = static_cast<const char*>(payload) + 32;
        payload_size -= 32;
    } else {
        LOG_WARN("Message missing authentication token");
        return;
    }
    
    switch (header->type) {
        case NEW_ORDER: {
            if (payload_size >= sizeof(NewOrderMessage) - sizeof(MessageHeader)) {
                // Create a copy for validation and sanitization
                NewOrderMessage msg = *reinterpret_cast<const NewOrderMessage*>(
                    static_cast<const char*>(buffer.data()) + 32);
                
                // Validate and sanitize message fields
                auto sanitize_result = MessageValidator::sanitize_message_fields(msg);
                if (sanitize_result.has_error()) {
                    LOG_WARN_SAFE("Invalid new order message: {}", sanitize_result.error().message());
                    return;
                }
                
                AuthMiddleware::authenticate_and_authorize(
                    auth_token, "place_order", "orders",
                    [this, conn, msg](const AuthContext& ctx, const std::string&) {
                        handle_new_order_secure(conn, msg, ctx);
                        return true;
                    }
                );
            }
            break;
        }
            
        case CANCEL_ORDER: {
            if (payload_size >= sizeof(CancelOrderMessage) - sizeof(MessageHeader)) {
                // Create a copy for validation and sanitization
                CancelOrderMessage msg = *reinterpret_cast<const CancelOrderMessage*>(
                    static_cast<const char*>(buffer.data()) + 32);
                
                // Validate and sanitize message fields
                auto sanitize_result = MessageValidator::sanitize_message_fields(msg);
                if (sanitize_result.has_error()) {
                    LOG_WARN_SAFE("Invalid cancel order message: {}", sanitize_result.error().message());
                    return;
                }
                
                AuthMiddleware::authenticate_and_authorize(
                    auth_token, "cancel_order", "orders",
                    [this, conn, msg](const AuthContext& ctx, const std::string&) {
                        handle_cancel_order_secure(conn, msg, ctx);
                        return true;
                    }
                );
            }
            break;
        }
            
        case HEARTBEAT:
            // Handle heartbeat message
            LOG_DEBUG("Heartbeat received");
            break;
            
        default:
            LOG_WARN_SAFE("Unknown message type: {}", header->type);
            break;
    }
}

void TcpGateway::handle_new_order_secure(ClientConnection* conn, const NewOrderMessage& msg, const AuthContext& ctx) {
    // Check rate limiting
    if (secure_network_->is_client_rate_limited(ctx.user_id)) {
        LOG_WARN_SAFE("Rate limit exceeded for client: {}", ctx.user_id);
        send_order_ack(conn, msg.order_id, 2, "Rate limit exceeded");
        return;
    }
    
    secure_network_->record_security_event("ORDER_RECEIVED", ctx.user_id);
    
    // Create validation chain for comprehensive input validation
    ValidationChain validator;
    
    // Add field validators
    validator.add_rule("symbol", FieldValidators::symbol_validator())
             .add_rule("quantity", FieldValidators::range_validator(1, 1000000))
             .add_rule("price", FieldValidators::positive_validator());
    
    // Prepare fields for validation
    std::unordered_map<std::string, std::string> fields = {
        {"symbol", msg.symbol.c_str()},
        {"quantity", std::to_string(msg.quantity)},
        {"price", std::to_string(msg.price)}
    };
    
    // Add cross-field validation
    validator.add_custom_validator([&msg]() -> Result<void> {
        if (!FieldValidators::validate_price_quantity_relationship(msg.price, msg.quantity)) {
            return make_error_code(ValidationError::INVALID_FIELD_VALUE);
        }
        return Result<void>();
    });
    
    // Validate all fields
    auto validation_result = validator.validate(fields);
    if (validation_result.has_error()) {
        send_order_ack(conn, msg.order_id, 2, "Invalid order parameters");
        return;
    }
    
    // Verify client ownership
    if (msg.client_id.c_str() != ctx.user_id) {
        LOG_WARN_SAFE("Client ID mismatch: {} vs {}", msg.client_id.c_str(), ctx.user_id);
        send_order_ack(conn, msg.order_id, 2, "Unauthorized client");
        return;
    }
    
    // Allocate order from pool
    auto* order = order_pool_->allocate();
    if (!order) {
        send_order_ack(conn, msg.order_id, 2, "Order pool exhausted");
        return;
    }
    
    // Construct order
    new (order) Order(msg.order_id, msg.client_id, msg.symbol, 
                     static_cast<Side>(msg.side), static_cast<OrderType>(msg.order_type),
                     msg.quantity, msg.price);
    
    // Submit to risk manager
    if (risk_manager_->submit_order(order)) {
        send_order_ack(conn, msg.order_id, 1, "Accepted");
        LOG_INFO_SAFE("Order accepted: {} for client {}", msg.order_id, ctx.user_id);
    } else {
        order_pool_->deallocate(order);
        send_order_ack(conn, msg.order_id, 2, "Risk queue full");
    }
}

void TcpGateway::handle_cancel_order_secure(ClientConnection* conn, const CancelOrderMessage& msg, const AuthContext& ctx) {
    // Check rate limiting
    if (secure_network_->is_client_rate_limited(ctx.user_id)) {
        LOG_WARN_SAFE("Rate limit exceeded for client: {}", ctx.user_id);
        send_order_ack(conn, msg.order_id, 2, "Rate limit exceeded");
        return;
    }
    
    secure_network_->record_security_event("CANCEL_RECEIVED", ctx.user_id);
    
    // Validate order ID
    if (msg.order_id == 0) {
        send_order_ack(conn, msg.order_id, 2, "Invalid order ID");
        return;
    }
    
    // Validate symbol format
    if (!SecurityUtils::is_valid_symbol(msg.symbol.c_str())) {
        send_order_ack(conn, msg.order_id, 2, "Invalid symbol");
        return;
    }
    
    // Verify client ownership
    if (msg.client_id.c_str() != ctx.user_id) {
        LOG_WARN_SAFE("Cancel unauthorized: client {} vs user {}", msg.client_id.c_str(), ctx.user_id);
        send_order_ack(conn, msg.order_id, 2, "Unauthorized cancel");
        return;
    }
    
    if (risk_manager_->submit_cancel(msg.order_id, msg.client_id)) {
        send_order_ack(conn, msg.order_id, 1, "Cancel submitted");
        LOG_INFO_SAFE("Cancel submitted: {} for client {}", msg.order_id, ctx.user_id);
    } else {
        send_order_ack(conn, msg.order_id, 2, "Cancel failed");
    }
}

// Keep original methods for backward compatibility
void TcpGateway::handle_new_order(ClientConnection* conn, const NewOrderMessage& msg) {
    AuthContext ctx;
    ctx.authenticated = false;
    handle_new_order_secure(conn, msg, ctx);
}

void TcpGateway::handle_cancel_order(ClientConnection* conn, const CancelOrderMessage& msg) {
    AuthContext ctx;
    ctx.authenticated = false;
    handle_cancel_order_secure(conn, msg, ctx);
}

void TcpGateway::send_order_ack(ClientConnection* conn, uint64_t order_id, uint8_t status, const char* reason) {
    OrderAckMessage ack;
    ack.header = MessageHeader(ORDER_ACK, sizeof(OrderAckMessage), 
                              next_sequence_.fetch_add(1), ProtocolUtils::get_timestamp_ns());
    ack.order_id = order_id;
    ack.status = status;
    std::strncpy(ack.reason, reason, sizeof(ack.reason) - 1);
    
    ProtocolUtils::set_checksum(ack.header, &ack.order_id);
    
    if (conn->write_message_safe(&ack, sizeof(ack))) {
        messages_sent_.fetch_add(1);
    }
}

void TcpGateway::send_trade_report(ClientConnection* conn, const Trade& trade) {
    TradeMessage msg;
    msg.header = MessageHeader(TRADE_REPORT, sizeof(TradeMessage),
                              next_sequence_.fetch_add(1), ProtocolUtils::get_timestamp_ns());
    msg.trade_id = trade.id;
    msg.buy_order_id = trade.buy_order_id;
    msg.sell_order_id = trade.sell_order_id;
    std::strncpy(msg.symbol, trade.symbol, sizeof(msg.symbol));
    msg.quantity = trade.quantity;
    msg.price = trade.price;
    msg.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        trade.timestamp.time_since_epoch()).count();
    
    ProtocolUtils::set_checksum(msg.header, &msg.trade_id);
    
    if (conn->write_message_safe(&msg, sizeof(msg))) {
        messages_sent_.fetch_add(1);
    }
}

} // namespace rtes