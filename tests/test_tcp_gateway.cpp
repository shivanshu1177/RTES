#include <gtest/gtest.h>
#include "rtes/tcp_gateway.hpp"
#include "rtes/risk_manager.hpp"
#include "rtes/memory_pool.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>
#include <chrono>

namespace rtes {

class TcpGatewayTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup risk manager
        RiskConfig risk_config;
        risk_config.max_order_size = 10000;
        risk_config.max_notional_per_client = 1000000.0;
        risk_config.max_orders_per_second = 1000;
        risk_config.price_collar_enabled = false;
        
        std::vector<SymbolConfig> symbols = {{"AAPL", 0.01, 1, 10.0}};
        
        pool = std::make_unique<OrderPool>(1000);
        risk_manager = std::make_unique<RiskManager>(risk_config, symbols);
        gateway = std::make_unique<TcpGateway>(18888, risk_manager.get(), pool.get());
        
        risk_manager->start();
        gateway->start();
        
        // Give gateway time to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    void TearDown() override {
        gateway->stop();
        risk_manager->stop();
    }
    
    int create_client_socket() {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return -1;
        
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(18888);
        
        if (connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(sock);
            return -1;
        }
        
        return sock;
    }
    
    bool send_message(int sock, const void* message, size_t size) {
        ssize_t sent = send(sock, message, size, 0);
        return sent == static_cast<ssize_t>(size);
    }
    
    bool recv_message(int sock, void* buffer, size_t size) {
        size_t total_received = 0;
        while (total_received < size) {
            ssize_t received = recv(sock, static_cast<char*>(buffer) + total_received, 
                                  size - total_received, 0);
            if (received <= 0) return false;
            total_received += received;
        }
        return true;
    }
    
    std::unique_ptr<OrderPool> pool;
    std::unique_ptr<RiskManager> risk_manager;
    std::unique_ptr<TcpGateway> gateway;
};

TEST_F(TcpGatewayTest, BasicConnection) {
    int client_sock = create_client_socket();
    ASSERT_GE(client_sock, 0);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    EXPECT_EQ(gateway->connections_accepted(), 1);
    
    close(client_sock);
}

TEST_F(TcpGatewayTest, NewOrderMessage) {
    int client_sock = create_client_socket();
    ASSERT_GE(client_sock, 0);
    
    // Create new order message
    NewOrderMessage order_msg;
    order_msg.header = MessageHeader(NEW_ORDER, sizeof(NewOrderMessage), 1, 
                                   ProtocolUtils::get_timestamp_ns());
    order_msg.order_id = 12345;
    order_msg.client_id = 100;
    std::strncpy(order_msg.symbol, "AAPL", sizeof(order_msg.symbol));
    order_msg.side = static_cast<uint8_t>(Side::BUY);
    order_msg.quantity = 1000;
    order_msg.price = 15000;
    order_msg.order_type = static_cast<uint8_t>(OrderType::LIMIT);
    
    ProtocolUtils::set_checksum(order_msg.header, &order_msg.order_id);
    
    // Send order
    EXPECT_TRUE(send_message(client_sock, &order_msg, sizeof(order_msg)));
    
    // Receive acknowledgment
    OrderAckMessage ack;
    EXPECT_TRUE(recv_message(client_sock, &ack, sizeof(ack)));
    
    EXPECT_EQ(ack.header.type, ORDER_ACK);
    EXPECT_EQ(ack.order_id, 12345);
    EXPECT_EQ(ack.status, 1);  // Accepted
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    EXPECT_EQ(gateway->messages_received(), 1);
    EXPECT_EQ(gateway->messages_sent(), 1);
    
    close(client_sock);
}

TEST_F(TcpGatewayTest, CancelOrderMessage) {
    int client_sock = create_client_socket();
    ASSERT_GE(client_sock, 0);
    
    // First submit an order
    NewOrderMessage order_msg;
    order_msg.header = MessageHeader(NEW_ORDER, sizeof(NewOrderMessage), 1,
                                   ProtocolUtils::get_timestamp_ns());
    order_msg.order_id = 12345;
    order_msg.client_id = 100;
    std::strncpy(order_msg.symbol, "AAPL", sizeof(order_msg.symbol));
    order_msg.side = static_cast<uint8_t>(Side::BUY);
    order_msg.quantity = 1000;
    order_msg.price = 15000;
    order_msg.order_type = static_cast<uint8_t>(OrderType::LIMIT);
    
    ProtocolUtils::set_checksum(order_msg.header, &order_msg.order_id);
    EXPECT_TRUE(send_message(client_sock, &order_msg, sizeof(order_msg)));
    
    OrderAckMessage ack1;
    EXPECT_TRUE(recv_message(client_sock, &ack1, sizeof(ack1)));
    
    // Now cancel the order
    CancelOrderMessage cancel_msg;
    cancel_msg.header = MessageHeader(CANCEL_ORDER, sizeof(CancelOrderMessage), 2,
                                    ProtocolUtils::get_timestamp_ns());
    cancel_msg.order_id = 12345;
    cancel_msg.client_id = 100;
    std::strncpy(cancel_msg.symbol, "AAPL", sizeof(cancel_msg.symbol));
    
    ProtocolUtils::set_checksum(cancel_msg.header, &cancel_msg.order_id);
    EXPECT_TRUE(send_message(client_sock, &cancel_msg, sizeof(cancel_msg)));
    
    OrderAckMessage ack2;
    EXPECT_TRUE(recv_message(client_sock, &ack2, sizeof(ack2)));
    
    EXPECT_EQ(ack2.header.type, ORDER_ACK);
    EXPECT_EQ(ack2.order_id, 12345);
    
    close(client_sock);
}

TEST_F(TcpGatewayTest, InvalidChecksum) {
    int client_sock = create_client_socket();
    ASSERT_GE(client_sock, 0);
    
    NewOrderMessage order_msg;
    order_msg.header = MessageHeader(NEW_ORDER, sizeof(NewOrderMessage), 1,
                                   ProtocolUtils::get_timestamp_ns());
    order_msg.order_id = 12345;
    order_msg.client_id = 100;
    std::strncpy(order_msg.symbol, "AAPL", sizeof(order_msg.symbol));
    order_msg.side = static_cast<uint8_t>(Side::BUY);
    order_msg.quantity = 1000;
    order_msg.price = 15000;
    order_msg.order_type = static_cast<uint8_t>(OrderType::LIMIT);
    
    // Set invalid checksum
    order_msg.header.checksum = 0xDEADBEEF;
    
    EXPECT_TRUE(send_message(client_sock, &order_msg, sizeof(order_msg)));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Should not receive acknowledgment due to invalid checksum
    EXPECT_EQ(gateway->messages_sent(), 0);
    
    close(client_sock);
}

TEST_F(TcpGatewayTest, MultipleClients) {
    constexpr int num_clients = 5;
    std::vector<int> client_socks;
    
    // Connect multiple clients
    for (int i = 0; i < num_clients; ++i) {
        int sock = create_client_socket();
        ASSERT_GE(sock, 0);
        client_socks.push_back(sock);
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_EQ(gateway->connections_accepted(), num_clients);
    
    // Send orders from each client
    for (int i = 0; i < num_clients; ++i) {
        NewOrderMessage order_msg;
        order_msg.header = MessageHeader(NEW_ORDER, sizeof(NewOrderMessage), i + 1,
                                       ProtocolUtils::get_timestamp_ns());
        order_msg.order_id = i + 1;
        order_msg.client_id = 100 + i;
        std::strncpy(order_msg.symbol, "AAPL", sizeof(order_msg.symbol));
        order_msg.side = static_cast<uint8_t>(Side::BUY);
        order_msg.quantity = 100;
        order_msg.price = 15000;
        order_msg.order_type = static_cast<uint8_t>(OrderType::LIMIT);
        
        ProtocolUtils::set_checksum(order_msg.header, &order_msg.order_id);
        EXPECT_TRUE(send_message(client_socks[i], &order_msg, sizeof(order_msg)));
    }
    
    // Receive acknowledgments
    for (int i = 0; i < num_clients; ++i) {
        OrderAckMessage ack;
        EXPECT_TRUE(recv_message(client_socks[i], &ack, sizeof(ack)));
        EXPECT_EQ(ack.order_id, i + 1);
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    EXPECT_EQ(gateway->messages_received(), num_clients);
    EXPECT_EQ(gateway->messages_sent(), num_clients);
    
    // Close all connections
    for (int sock : client_socks) {
        close(sock);
    }
}

} // namespace rtes