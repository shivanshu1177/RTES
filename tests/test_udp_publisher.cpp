#include <gtest/gtest.h>
#include "rtes/udp_publisher.hpp"
#include "rtes/market_data.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <chrono>

namespace rtes {

class UdpPublisherTest : public ::testing::Test {
protected:
    void SetUp() override {
        market_data_queue = std::make_unique<MPMCQueue<MarketDataEvent>>(1000);
        publisher = std::make_unique<UdpPublisher>("239.0.0.1", 19999, market_data_queue.get());
        
        // Setup receiver socket
        setup_receiver();
        
        publisher->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    void TearDown() override {
        publisher->stop();
        if (receiver_fd_ >= 0) {
            close(receiver_fd_);
        }
    }
    
    void setup_receiver() {
        receiver_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        ASSERT_GE(receiver_fd_, 0);
        
        int reuse = 1;
        setsockopt(receiver_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(19999);
        
        ASSERT_EQ(bind(receiver_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)), 0);
        
        struct ip_mreq mreq{};
        inet_pton(AF_INET, "239.0.0.1", &mreq.imr_multiaddr);
        mreq.imr_interface.s_addr = INADDR_ANY;
        
        ASSERT_EQ(setsockopt(receiver_fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)), 0);
        
        // Set receive timeout
        struct timeval timeout{};
        timeout.tv_sec = 1;
        setsockopt(receiver_fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    }
    
    bool receive_message(void* buffer, size_t size) {
        ssize_t received = recv(receiver_fd_, buffer, size, 0);
        return received == static_cast<ssize_t>(size);
    }
    
    std::unique_ptr<MPMCQueue<MarketDataEvent>> market_data_queue;
    std::unique_ptr<UdpPublisher> publisher;
    int receiver_fd_{-1};
};

TEST_F(UdpPublisherTest, BBOUpdate) {
    // Create BBO update event
    MarketDataEvent event;
    event.type = MarketDataEvent::BBO_UPDATE;
    std::strncpy(event.symbol, "AAPL", sizeof(event.symbol));
    event.bbo.bid_price = 14950;
    event.bbo.bid_quantity = 1000;
    event.bbo.ask_price = 15050;
    event.bbo.ask_quantity = 500;
    
    // Send event
    EXPECT_TRUE(market_data_queue->push(event));
    
    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Receive UDP message
    BBOUpdateMessage msg;
    EXPECT_TRUE(receive_message(&msg, sizeof(msg)));
    
    EXPECT_EQ(msg.header.type, BBO_UPDATE);
    EXPECT_STREQ(msg.symbol, "AAPL");
    EXPECT_EQ(msg.bid_price, 14950);
    EXPECT_EQ(msg.bid_quantity, 1000);
    EXPECT_EQ(msg.ask_price, 15050);
    EXPECT_EQ(msg.ask_quantity, 500);
    EXPECT_EQ(msg.header.sequence, 1);
    
    EXPECT_EQ(publisher->messages_sent(), 1);
}

TEST_F(UdpPublisherTest, TradeUpdate) {
    // Create trade event
    MarketDataEvent event;
    event.type = MarketDataEvent::TRADE;
    event.trade = Trade(12345, 1001, 1002, "MSFT", 200, 30000);
    
    // Send event
    EXPECT_TRUE(market_data_queue->push(event));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Receive UDP message
    TradeUpdateMessage msg;
    EXPECT_TRUE(receive_message(&msg, sizeof(msg)));
    
    EXPECT_EQ(msg.header.type, TRADE_UPDATE);
    EXPECT_EQ(msg.trade_id, 12345);
    EXPECT_STREQ(msg.symbol, "MSFT");
    EXPECT_EQ(msg.quantity, 200);
    EXPECT_EQ(msg.price, 30000);
    EXPECT_EQ(msg.aggressor_side, 1);  // BUY
    EXPECT_EQ(msg.header.sequence, 1);
    
    EXPECT_EQ(publisher->messages_sent(), 1);
}

TEST_F(UdpPublisherTest, SequenceNumbers) {
    // Send multiple events
    for (int i = 0; i < 5; ++i) {
        MarketDataEvent event;
        event.type = MarketDataEvent::BBO_UPDATE;
        std::strncpy(event.symbol, "TEST", sizeof(event.symbol));
        event.bbo.bid_price = 10000 + i;
        event.bbo.bid_quantity = 100;
        event.bbo.ask_price = 10100 + i;
        event.bbo.ask_quantity = 100;
        
        EXPECT_TRUE(market_data_queue->push(event));
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Receive messages and check sequence numbers
    for (int i = 0; i < 5; ++i) {
        BBOUpdateMessage msg;
        EXPECT_TRUE(receive_message(&msg, sizeof(msg)));
        EXPECT_EQ(msg.header.sequence, i + 1);
        EXPECT_EQ(msg.bid_price, 10000 + i);
    }
    
    EXPECT_EQ(publisher->messages_sent(), 5);
}

TEST_F(UdpPublisherTest, HighThroughput) {
    constexpr int num_events = 1000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Send many events
    for (int i = 0; i < num_events; ++i) {
        MarketDataEvent event;
        event.type = (i % 2 == 0) ? MarketDataEvent::BBO_UPDATE : MarketDataEvent::TRADE;
        
        if (event.type == MarketDataEvent::BBO_UPDATE) {
            std::strncpy(event.symbol, "AAPL", sizeof(event.symbol));
            event.bbo.bid_price = 15000;
            event.bbo.bid_quantity = 100;
            event.bbo.ask_price = 15100;
            event.bbo.ask_quantity = 100;
        } else {
            event.trade = Trade(i, 1001, 1002, "AAPL", 100, 15050);
        }
        
        while (!market_data_queue->push(event)) {
            std::this_thread::yield();
        }
    }
    
    // Wait for processing
    while (publisher->messages_sent() < num_events) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "UDP publisher sent " << num_events << " messages in " 
              << duration.count() << " μs\n";
    std::cout << "Throughput: " << (num_events * 1e6) / duration.count() 
              << " messages/sec\n";
    
    EXPECT_EQ(publisher->messages_sent(), num_events);
    EXPECT_GT(publisher->bytes_sent(), 0);
}

} // namespace rtes