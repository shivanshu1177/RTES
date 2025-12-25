#include "rtes/udp_publisher.hpp"
#include "rtes/logger.hpp"

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

#ifdef __linux__
#include <sys/sendfile.h>
#endif

#ifdef __x86_64__
#include <immintrin.h>
#include <sched.h>
#endif

namespace rtes {

inline constexpr size_t MD_SPIN_ITERS = 256;
inline constexpr size_t MD_PAUSE_PER_SPIN = 4;
inline constexpr size_t MD_STATS_FLUSH = 2048;
inline constexpr int SOCKET_SNDBUF_SIZE = 262144;
inline constexpr int MULTICAST_TTL = 1;

UdpPublisher::UdpPublisher(const std::string &multicast_group, uint16_t port, MPMCQueue<MarketDataEvent> *input_queue)
    : multicast_group_(multicast_group), port_(port), input_queue_(input_queue) {
    std::memset(&multicast_addr_, 0, sizeof(multicast_addr_));
    multicast_addr_.sin_family = AF_INET;
    multicast_addr_.sin_port = htons(port_);
    inet_pton(AF_INET, multicast_group.c_str(), &multicast_addr_.sin_addr);
}

UdpPublisher::~UdpPublisher() { stop(); }

void UdpPublisher::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return;
    if (!setup_socket()) { running_.store(false); return; }
    worker_thread_ = std::thread(&UdpPublisher::worker_loop, this);
}

void UdpPublisher::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) return;
    if (worker_thread_.joinable()) worker_thread_.join();
    socket_fd_.close();
    flush_stats();
}

bool UdpPublisher::setup_socket() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    socket_fd_.reset(fd);
    int sndbuf = SOCKET_SNDBUF_SIZE;
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    int ttl = MULTICAST_TTL;
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    int loop = 0;
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
    return true;
}

void UdpPublisher::worker_loop() {
    std::array<MarketDataEvent, MD_BATCH_SIZE> events{};
    std::array<SendBuffer, SENDMMSG_BATCH> send_buffers{};
    while (running_.load(std::memory_order_relaxed)) {
        size_t event_count = drain_events(events);
        if (event_count > 0) {
            size_t msg_count = build_datagrams(events, event_count, send_buffers);
            if (msg_count > 0) batch_send(send_buffers, msg_count);
            maybe_flush_stats();
        } else {
            spin_wait();
        }
    }
}

size_t UdpPublisher::drain_events(std::array<MarketDataEvent, MD_BATCH_SIZE> &events) {
    size_t count = 0;
    while (count < MD_BATCH_SIZE && input_queue_->pop(events[count])) ++count;
    return count;
}

size_t UdpPublisher::build_datagrams(const std::array<MarketDataEvent, MD_BATCH_SIZE> &events, size_t event_count, std::array<SendBuffer, SENDMMSG_BATCH> &buffers) {
    size_t msg_count = 0;
    for (size_t i = 0; i < event_count && msg_count < SENDMMSG_BATCH; ++i) {
        const auto &event = events[i];
        auto &buf = buffers[msg_count];
        if (event.type == MarketDataEvent::BBO_UPDATE) {
            buf.length = build_bbo_datagram(event, buf.data);
        } else if (event.type == MarketDataEvent::TRADE) {
            buf.length = build_trade_datagram(event, buf.data);
        } else {
            continue;
        }
        if (buf.length > 0) ++msg_count;
    }
    return msg_count;
}

size_t UdpPublisher::build_bbo_datagram(const MarketDataEvent &event, uint8_t *out) {
    const Timestamp ts = now_timestamp();
    const uint64_t seq = local_stats_.next_sequence++;
    BBOUpdateMessage msg{};
    msg.header = UdpMessageHeader(rtes::BBO_UPDATE, sizeof(BBOUpdateMessage), seq, ts);
    std::memcpy(msg.symbol, event.symbol, sizeof(msg.symbol));
    msg.bid_price = event.bbo.bid_price;
    msg.bid_quantity = event.bbo.bid_quantity;
    msg.ask_price = event.bbo.ask_price;
    msg.ask_quantity = event.bbo.ask_quantity;
    std::memcpy(out, &msg, sizeof(msg));
    return sizeof(msg);
}

size_t UdpPublisher::build_trade_datagram(const MarketDataEvent &event, uint8_t *out) {
    const Timestamp ts = now_timestamp();
    const uint64_t seq = local_stats_.next_sequence++;
    TradeUpdateMessage msg{};
    msg.header = UdpMessageHeader(rtes::TRADE_UPDATE, sizeof(TradeUpdateMessage), seq, ts);
    msg.trade_id = event.trade.id;
    std::memcpy(msg.symbol, event.symbol, sizeof(msg.symbol));
    msg.quantity = event.trade.quantity;
    msg.price = event.trade.price;
    msg.aggressor_side = 1;
    std::memcpy(out, &msg, sizeof(msg));
    return sizeof(msg);
}

void UdpPublisher::batch_send(const std::array<SendBuffer, SENDMMSG_BATCH> &buffers, size_t count) {
#ifdef __linux__
    std::array<struct iovec, SENDMMSG_BATCH> iovecs{};
    std::array<struct mmsghdr, SENDMMSG_BATCH> msgs{};
    for (size_t i = 0; i < count; ++i) {
        iovecs[i].iov_base = const_cast<uint8_t *>(buffers[i].data);
        iovecs[i].iov_len = buffers[i].length;
        msgs[i].msg_hdr.msg_name = &multicast_addr_;
        msgs[i].msg_hdr.msg_namelen = sizeof(multicast_addr_);
        msgs[i].msg_hdr.msg_iov = &iovecs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
    }
    int sent = sendmmsg(socket_fd_.get(), msgs.data(), static_cast<unsigned int>(count), MSG_DONTWAIT);
    if (sent > 0) {
        local_stats_.messages_sent += static_cast<size_t>(sent);
    }
#else
    for (size_t i = 0; i < count; ++i) {
        ssize_t sent = sendto(socket_fd_.get(), buffers[i].data, buffers[i].length, 0,
                              reinterpret_cast<const sockaddr *>(&multicast_addr_), sizeof(multicast_addr_));
        if (sent > 0) local_stats_.messages_sent++;
    }
#endif
}

void UdpPublisher::spin_wait() {
#ifdef __x86_64__
    sched_yield();
#else
    std::this_thread::yield();
#endif
}

void UdpPublisher::maybe_flush_stats() {
    if (local_stats_.messages_sent % MD_STATS_FLUSH == 0 && local_stats_.messages_sent > 0) flush_stats();
}

void UdpPublisher::flush_stats() {
    stats_atomic_.messages_sent.store(local_stats_.messages_sent, std::memory_order_relaxed);
}

} // namespace rtes
