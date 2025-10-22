#include "rtes/market_data.hpp"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <atomic>
#include <csignal>

using namespace rtes;

std::atomic<bool> running{true};

void signal_handler(int) {
    running.store(false);
}

class UdpReceiver {
public:
    UdpReceiver(const std::string& group, uint16_t port) 
        : multicast_group_(group), port_(port) {}
    
    bool start() {
        socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd_ < 0) {
            std::cerr << "Failed to create socket\n";
            return false;
        }
        
        // Allow multiple receivers on same port
        int reuse = 1;
        setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        
        // Bind to any address on the specified port
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port_);
        
        if (bind(socket_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            std::cerr << "Failed to bind socket\n";
            return false;
        }
        
        // Join multicast group
        struct ip_mreq mreq{};
        inet_pton(AF_INET, multicast_group_.c_str(), &mreq.imr_multiaddr);
        mreq.imr_interface.s_addr = INADDR_ANY;
        
        if (setsockopt(socket_fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
            std::cerr << "Failed to join multicast group\n";
            return false;
        }
        
        std::cout << "Listening on " << multicast_group_ << ":" << port_ << "\n";
        std::cout << "Press Ctrl+C to stop\n\n";
        
        return true;
    }
    
    void run() {
        uint8_t buffer[1024];
        uint64_t expected_sequence = 1;
        uint64_t messages_received = 0;
        uint64_t gaps_detected = 0;
        
        while (running.load()) {
            ssize_t received = recv(socket_fd_, buffer, sizeof(buffer), 0);
            if (received < static_cast<ssize_t>(sizeof(UdpMessageHeader))) {
                continue;
            }
            
            const UdpMessageHeader* header = reinterpret_cast<const UdpMessageHeader*>(buffer);
            
            // Check for gaps
            if (header->sequence != expected_sequence) {
                if (messages_received > 0) {  // Skip first message gap check
                    gaps_detected++;
                    std::cout << "GAP DETECTED: Expected " << expected_sequence 
                              << ", got " << header->sequence << "\n";
                }
                expected_sequence = header->sequence;
            }
            expected_sequence++;
            messages_received++;
            
            // Process message based on type
            switch (header->type) {
                case BBO_UPDATE:
                    if (received >= static_cast<ssize_t>(sizeof(BBOUpdateMessage))) {
                        process_bbo_update(*reinterpret_cast<const BBOUpdateMessage*>(buffer));
                    }
                    break;
                    
                case TRADE_UPDATE:
                    if (received >= static_cast<ssize_t>(sizeof(TradeUpdateMessage))) {
                        process_trade_update(*reinterpret_cast<const TradeUpdateMessage*>(buffer));
                    }
                    break;
                    
                case DEPTH_UPDATE:
                    if (received >= static_cast<ssize_t>(sizeof(DepthUpdateMessage))) {
                        process_depth_update(*reinterpret_cast<const DepthUpdateMessage*>(buffer));
                    }
                    break;
                    
                default:
                    std::cout << "Unknown message type: " << header->type << "\n";
                    break;
            }
        }
        
        std::cout << "\nStatistics:\n";
        std::cout << "Messages received: " << messages_received << "\n";
        std::cout << "Gaps detected: " << gaps_detected << "\n";
    }
    
    void stop() {
        if (socket_fd_ >= 0) {
            close(socket_fd_);
            socket_fd_ = -1;
        }
    }

private:
    std::string multicast_group_;
    uint16_t port_;
    int socket_fd_{-1};
    
    void process_bbo_update(const BBOUpdateMessage& msg) {
        std::cout << "BBO " << msg.symbol 
                  << " Bid:" << (msg.bid_price / 10000.0) << "x" << msg.bid_quantity
                  << " Ask:" << (msg.ask_price / 10000.0) << "x" << msg.ask_quantity
                  << " Seq:" << msg.header.sequence << "\n";
    }
    
    void process_trade_update(const TradeUpdateMessage& msg) {
        std::cout << "TRADE " << msg.symbol 
                  << " ID:" << msg.trade_id
                  << " " << msg.quantity << "@" << (msg.price / 10000.0)
                  << " Side:" << (msg.aggressor_side == 1 ? "BUY" : "SELL")
                  << " Seq:" << msg.header.sequence << "\n";
    }
    
    void process_depth_update(const DepthUpdateMessage& msg) {
        std::cout << "DEPTH " << msg.symbol 
                  << " Bids:" << static_cast<int>(msg.num_bid_levels)
                  << " Asks:" << static_cast<int>(msg.num_ask_levels)
                  << " Seq:" << msg.header.sequence << "\n";
    }
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "Usage: " << argv[0] << " <multicast_group> <port>\n";
        std::cout << "Example: " << argv[0] << " 239.0.0.1 9999\n";
        return 1;
    }
    
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    UdpReceiver receiver(argv[1], std::stoi(argv[2]));
    
    if (!receiver.start()) {
        return 1;
    }
    
    receiver.run();
    receiver.stop();
    
    return 0;
}