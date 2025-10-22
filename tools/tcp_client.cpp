#include "rtes/protocol.hpp"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

using namespace rtes;

class TcpClient {
public:
    TcpClient(const std::string& host, uint16_t port) : host_(host), port_(port) {}
    
    bool connect() {
        sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_ < 0) {
            std::cerr << "Failed to create socket\n";
            return false;
        }
        
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        
        if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
            std::cerr << "Invalid address\n";
            return false;
        }
        
        if (::connect(sock_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            std::cerr << "Connection failed\n";
            return false;
        }
        
        std::cout << "Connected to " << host_ << ":" << port_ << "\n";
        return true;
    }
    
    void disconnect() {
        if (sock_ >= 0) {
            close(sock_);
            sock_ = -1;
        }
    }
    
    bool send_new_order(uint64_t order_id, uint32_t client_id, const std::string& symbol,
                       Side side, uint64_t quantity, uint64_t price) {
        NewOrderMessage msg;
        msg.header = MessageHeader(NEW_ORDER, sizeof(NewOrderMessage), ++sequence_,
                                 ProtocolUtils::get_timestamp_ns());
        msg.order_id = order_id;
        msg.client_id = client_id;
        std::strncpy(msg.symbol, symbol.c_str(), sizeof(msg.symbol) - 1);
        msg.side = static_cast<uint8_t>(side);
        msg.quantity = quantity;
        msg.price = price;
        msg.order_type = static_cast<uint8_t>(OrderType::LIMIT);
        
        ProtocolUtils::set_checksum(msg.header, &msg.order_id);
        
        ssize_t sent = send(sock_, &msg, sizeof(msg), 0);
        if (sent != sizeof(msg)) {
            std::cerr << "Failed to send order\n";
            return false;
        }
        
        std::cout << "Sent order: ID=" << order_id << " " << symbol 
                  << " " << (side == Side::BUY ? "BUY" : "SELL")
                  << " " << quantity << "@" << (price / 10000.0) << "\n";
        return true;
    }
    
    bool send_cancel_order(uint64_t order_id, uint32_t client_id, const std::string& symbol) {
        CancelOrderMessage msg;
        msg.header = MessageHeader(CANCEL_ORDER, sizeof(CancelOrderMessage), ++sequence_,
                                 ProtocolUtils::get_timestamp_ns());
        msg.order_id = order_id;
        msg.client_id = client_id;
        std::strncpy(msg.symbol, symbol.c_str(), sizeof(msg.symbol) - 1);
        
        ProtocolUtils::set_checksum(msg.header, &msg.order_id);
        
        ssize_t sent = send(sock_, &msg, sizeof(msg), 0);
        if (sent != sizeof(msg)) {
            std::cerr << "Failed to send cancel\n";
            return false;
        }
        
        std::cout << "Sent cancel: ID=" << order_id << "\n";
        return true;
    }
    
    bool receive_response() {
        MessageHeader header;
        ssize_t received = recv(sock_, &header, sizeof(header), 0);
        if (received != sizeof(header)) {
            return false;
        }
        
        if (header.type == ORDER_ACK) {
            OrderAckMessage ack;
            std::memcpy(&ack.header, &header, sizeof(header));
            
            size_t remaining = sizeof(ack) - sizeof(header);
            received = recv(sock_, &ack.order_id, remaining, 0);
            if (received != static_cast<ssize_t>(remaining)) {
                return false;
            }
            
            std::cout << "Received ACK: Order=" << ack.order_id 
                      << " Status=" << (ack.status == 1 ? "ACCEPTED" : "REJECTED")
                      << " Reason=" << ack.reason << "\n";
        } else if (header.type == TRADE_REPORT) {
            TradeMessage trade;
            std::memcpy(&trade.header, &header, sizeof(header));
            
            size_t remaining = sizeof(trade) - sizeof(header);
            received = recv(sock_, &trade.trade_id, remaining, 0);
            if (received != static_cast<ssize_t>(remaining)) {
                return false;
            }
            
            std::cout << "Received TRADE: ID=" << trade.trade_id
                      << " " << trade.symbol << " " << trade.quantity
                      << "@" << (trade.price / 10000.0) << "\n";
        }
        
        return true;
    }

private:
    std::string host_;
    uint16_t port_;
    int sock_{-1};
    uint64_t sequence_{0};
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "Usage: " << argv[0] << " <host> <port>\n";
        return 1;
    }
    
    TcpClient client(argv[1], std::stoi(argv[2]));
    
    if (!client.connect()) {
        return 1;
    }
    
    // Interactive mode
    std::string command;
    std::cout << "\nCommands:\n";
    std::cout << "  order <id> <client_id> <symbol> <side> <qty> <price>\n";
    std::cout << "  cancel <id> <client_id> <symbol>\n";
    std::cout << "  quit\n\n";
    
    while (std::cout << "> " && std::cin >> command) {
        if (command == "quit") {
            break;
        } else if (command == "order") {
            uint64_t id, qty, price;
            uint32_t client_id;
            std::string symbol, side_str;
            
            std::cin >> id >> client_id >> symbol >> side_str >> qty >> price;
            
            Side side = (side_str == "BUY" || side_str == "buy") ? Side::BUY : Side::SELL;
            client.send_new_order(id, client_id, symbol, side, qty, price * 10000);
            
        } else if (command == "cancel") {
            uint64_t id;
            uint32_t client_id;
            std::string symbol;
            
            std::cin >> id >> client_id >> symbol;
            client.send_cancel_order(id, client_id, symbol);
            
        } else {
            std::cout << "Unknown command: " << command << "\n";
        }
        
        // Try to receive response
        client.receive_response();
    }
    
    client.disconnect();
    return 0;
}