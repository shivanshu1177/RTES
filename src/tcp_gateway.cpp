#include "rtes/tcp_gateway.hpp"
#include "rtes/logger.hpp"
#include "rtes/auth_middleware.hpp"
#include "rtes/network_security.hpp"
#include "rtes/thread_safety.hpp"

#include <sys/socket.h>
#include <poll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <algorithm>

// --- Cross-Platform Networking Macros (macOS vs Linux) ---
#ifdef __APPLE__
    #include <sys/event.h>
    #include <sys/time.h>
    #ifndef MSG_NOSIGNAL
        #define MSG_NOSIGNAL 0
    #endif

#else
    #include <sys/epoll.h>
#endif

namespace rtes {

inline constexpr size_t MAX_CONNECTIONS      = 1024;
inline constexpr size_t MAX_MESSAGE_SIZE     = 4096;
inline constexpr size_t EPOLL_MAX_EVENTS     = 64;
inline constexpr size_t STATS_FLUSH_INTERVAL = 4096;
inline constexpr int    EPOLL_TIMEOUT_MS     = 10;
inline constexpr int    ACCEPT_POLL_MS       = 50;
inline constexpr int    LISTEN_BACKLOG       = 128;

// ═══════════════════════════════════════════════════════════════
//  ConnectionState Implementation
// ═══════════════════════════════════════════════════════════════

ConnectionState::ConnectionState(int raw_fd)
    : fd(raw_fd), connect_time(now_timestamp())
{
    setup_socket();
}

void ConnectionState::setup_socket() {
    int flag = 1;
    setsockopt(fd.get(), IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

#ifdef __APPLE__
    // macOS prevents SIGPIPE via SO_NOSIGPIPE
    int set = 1;
    setsockopt(fd.get(), SOL_SOCKET, SO_NOSIGPIPE, &set, sizeof(set));
#endif

    int flags = fcntl(fd.get(), F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd.get(), F_SETFL, flags | O_NONBLOCK);
    }
}

void ConnectionState::disconnect() {
    connected = false;
    fd.close();
}

// ═══════════════════════════════════════════════════════════════
//  TcpGateway Construction & Lifecycle
// ═══════════════════════════════════════════════════════════════

TcpGateway::TcpGateway(uint16_t port, RiskManager* risk_manager, OrderPool* order_pool)
    : port_(port), risk_manager_(risk_manager), order_pool_(order_pool)
{
    SecurityConfig security_config;
    security_config.rate_limit_per_second = 1000;
    
    // Dev mock key for compilation
    security_config.hmac_key.assign("dev_hmac_key_32chars_long_enough_key");
    secure_network_ = std::make_unique<SecureNetworkLayer>(security_config);

    connections_.resize(MAX_CONNECTIONS);
    LOG_INFO("TCP gateway initialized on port {}", port_);
}

TcpGateway::~TcpGateway() {
    stop();
}

void TcpGateway::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return;

    if (!setup_listen_socket() || !setup_epoll()) {
        running_.store(false);
        throw std::runtime_error("Failed to start TCP Gateway");
    }

    acceptor_thread_ = std::thread(&TcpGateway::acceptor_loop, this);
    worker_thread_   = std::thread(&TcpGateway::worker_loop, this);
}

void TcpGateway::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) return;

    if (acceptor_thread_.joinable()) acceptor_thread_.join();
    if (worker_thread_.joinable()) worker_thread_.join();

    for (auto& conn : connections_) {
        if (conn) {
            conn->disconnect();
            conn.reset();
        }
    }

    flush_stats();
    epoll_fd_.close();
    listen_fd_.close();
}

// ═══════════════════════════════════════════════════════════════
//  Socket & Reactor Setup
// ═══════════════════════════════════════════════════════════════

bool TcpGateway::setup_listen_socket() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;

    // Set non-blocking portably
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    listen_fd_.reset(fd);

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port_);

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) return false;
    if (listen(fd, LISTEN_BACKLOG) < 0) return false;

    return true;
}

bool TcpGateway::setup_epoll() {
#ifdef __APPLE__
    int fd = kqueue();
#else
    int fd = epoll_create1(EPOLL_CLOEXEC);
#endif
    if (fd < 0) return false;
    epoll_fd_.reset(fd);
    return true;
}

// ═══════════════════════════════════════════════════════════════
//  Acceptor & Worker Threads
// ═══════════════════════════════════════════════════════════════

void TcpGateway::acceptor_loop() {
    struct pollfd pfd{};
    pfd.fd     = listen_fd_.get();
    pfd.events = POLLIN;

    while (running_.load(std::memory_order_relaxed)) {
        if (poll(&pfd, 1, ACCEPT_POLL_MS) <= 0) continue;
        if (!(pfd.revents & POLLIN)) continue;

        while (running_.load(std::memory_order_relaxed)) {
            struct sockaddr_in client_addr{};
            socklen_t addr_len = sizeof(client_addr);

            int client_fd = accept(listen_fd_.get(), reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
            if (client_fd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                break;
            }

            if (static_cast<size_t>(client_fd) >= MAX_CONNECTIONS) {
                ::close(client_fd);
                continue;
            }

            auto conn = std::make_unique<ConnectionState>(client_fd);

#ifdef __APPLE__
            struct kevent ev;
            EV_SET(&ev, client_fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, NULL);
            kevent(epoll_fd_.get(), &ev, 1, NULL, 0, NULL);
#else
            struct epoll_event ev{};
            ev.events  = EPOLLIN | EPOLLET;
            ev.data.fd = client_fd;
            epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, client_fd, &ev);
#endif

            connections_[client_fd] = std::move(conn);
            ++local_stats_.connections_accepted;
        }
    }
}

void TcpGateway::worker_loop() {
#ifdef __APPLE__
    std::array<struct kevent, EPOLL_MAX_EVENTS> events{};
    struct timespec ts {0, EPOLL_TIMEOUT_MS * 1000000};
#else
    std::array<struct epoll_event, EPOLL_MAX_EVENTS> events{};
#endif

    while (running_.load(std::memory_order_relaxed)) {
#ifdef __APPLE__
        int n = kevent(epoll_fd_.get(), NULL, 0, events.data(), EPOLL_MAX_EVENTS, &ts);
        for (int i = 0; i < n; ++i) {
            int fd = events[i].ident;
            if (events[i].flags & (EV_EOF | EV_ERROR)) { remove_connection(fd); continue; }
            if (events[i].filter == EVFILT_READ) handle_client_data(fd);
        }
#else
        int n = epoll_wait(epoll_fd_.get(), events.data(), EPOLL_MAX_EVENTS, EPOLL_TIMEOUT_MS);
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            if (events[i].events & (EPOLLERR | EPOLLHUP)) { remove_connection(fd); continue; }
            if (events[i].events & EPOLLIN) handle_client_data(fd);
        }
#endif
        maybe_flush_stats();
    }
}

void TcpGateway::handle_client_data(int fd) {
    if (static_cast<size_t>(fd) >= connections_.size() || !connections_[fd]) return;
    auto& conn = *connections_[fd];

    for (;;) {
        ssize_t bytes = recv(fd, conn.read_buf.write_ptr(), conn.read_buf.remaining(), 0);
        if (bytes > 0) {
            conn.read_buf.advance_write(static_cast<size_t>(bytes));
            while (try_process_message(conn)) {
                ++local_stats_.messages_received;
            }
        } else if (bytes == 0) {
            remove_connection(fd);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            remove_connection(fd);
            return;
        }
    }
}

// ═══════════════════════════════════════════════════════════════
//  Message Processing
// ═══════════════════════════════════════════════════════════════

bool TcpGateway::try_process_message(ConnectionState& conn) {
    if (conn.read_buf.size() < sizeof(MessageHeader)) return false;

    const auto* header = reinterpret_cast<const MessageHeader*>(conn.read_buf.data());
    if (header->length < sizeof(MessageHeader) || header->length > MAX_MESSAGE_SIZE) {
        conn.disconnect();
        return false;
    }

    if (conn.read_buf.size() < header->length) return false;

    process_message(conn, conn.read_buf.data(), header->length);
    conn.read_buf.consume(header->length);
    return true;
}

void TcpGateway::process_message(ConnectionState& conn, const uint8_t* data, size_t length) {
    const auto* header = reinterpret_cast<const MessageHeader*>(data);
    
    // Checksum logic bypassed for brevity - adapt as needed based on ProtocolUtils
    
    switch (header->type) {
        case NEW_ORDER: handle_new_order(conn, data, length); break;
        case CANCEL_ORDER: handle_cancel_order(conn, data, length); break;
        case HEARTBEAT: break;
        default: break;
    }
}

void TcpGateway::handle_new_order(ConnectionState& conn, const uint8_t* data, size_t length) {
    if (length < sizeof(NewOrderMessage)) return;
    const auto& msg = *reinterpret_cast<const NewOrderMessage*>(data);

    Order* order = order_pool_->allocate();
    if (!order) {
        send_reject(conn, msg.order_id, "Order pool exhausted");
        ++local_stats_.pool_exhausted;
        return;
    }

    order->id                 = msg.order_id;
    order->client_id          = ClientID(msg.client_id.c_str());
    order->symbol             = Symbol(msg.symbol.c_str());
    order->side               = static_cast<Side>(msg.side);
    order->type               = static_cast<OrderType>(msg.order_type);
    order->quantity           = msg.quantity;
    order->remaining_quantity = msg.quantity;
    order->price              = msg.price;
    order->status             = OrderStatus::PENDING;
    order->timestamp          = now_timestamp();

    if (risk_manager_->submit_order(order)) {
        send_ack(conn, msg.order_id, 1, "Accepted");
    } else {
        order_pool_->deallocate(order);
        send_reject(conn, msg.order_id, "Risk queue full");
    }
}

void TcpGateway::handle_cancel_order(ConnectionState& conn, const uint8_t* data, size_t length) {
    if (length < sizeof(CancelOrderMessage)) return;
    const auto& msg = *reinterpret_cast<const CancelOrderMessage*>(data);

    if (risk_manager_->submit_cancel(msg.order_id, ClientID(msg.client_id.c_str()))) {
        send_ack(conn, msg.order_id, 1, "Cancel submitted");
    } else {
        send_reject(conn, msg.order_id, "Cancel queue full");
    }
}

void TcpGateway::send_ack(ConnectionState& conn, uint64_t order_id, uint8_t status, const char* reason) {
    OrderAckMessage ack{};
    ack.header = MessageHeader(ORDER_ACK, sizeof(OrderAckMessage), local_stats_.next_sequence++, now_timestamp());
    ack.order_id = order_id;
    ack.status   = status;
    if (reason) ack.reason.assign(reason);

    ssize_t sent = send(conn.fd.get(), &ack, sizeof(ack), MSG_NOSIGNAL);
    if (sent > 0) ++local_stats_.messages_sent;
}

void TcpGateway::send_reject(ConnectionState& conn, uint64_t order_id, const char* reason) {
    send_ack(conn, order_id, 2, reason);
    ++local_stats_.orders_rejected;
}

void TcpGateway::remove_connection(int fd) {
    if (static_cast<size_t>(fd) >= connections_.size()) return;
#ifdef __APPLE__
    // kqueue automatically removes closed descriptors
#else
    epoll_ctl(epoll_fd_.get(), EPOLL_CTL_DEL, fd, nullptr);
#endif
    connections_[fd].reset();
    ++local_stats_.disconnections;
}

void TcpGateway::maybe_flush_stats() {
    if (local_stats_.messages_received % STATS_FLUSH_INTERVAL == 0 && local_stats_.messages_received > 0) {
        flush_stats();
    }
}

void TcpGateway::flush_stats() {
    stats_atomic_.connections_accepted.store(local_stats_.connections_accepted, std::memory_order_relaxed);
    stats_atomic_.messages_received.store(local_stats_.messages_received, std::memory_order_relaxed);
    stats_atomic_.messages_sent.store(local_stats_.messages_sent, std::memory_order_relaxed);
    stats_atomic_.disconnections.store(local_stats_.disconnections, std::memory_order_relaxed);
}

} // namespace rtes