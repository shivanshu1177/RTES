#include <gtest/gtest.h>
#include "rtes/http_server.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <chrono>

namespace rtes {

class HttpServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        server = std::make_unique<HttpServer>(18080);
        
        // Add test handlers
        server->add_handler("/test", [](const std::string&, const std::string&) {
            return "Hello, World!";
        });
        
        server->add_handler("/json", [](const std::string&, const std::string&) {
            return "{\"status\": \"ok\"}";
        });
        
        server->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    void TearDown() override {
        server->stop();
    }
    
    std::string send_http_request(const std::string& request) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return "";
        
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(18080);
        
        if (connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(sock);
            return "";
        }
        
        send(sock, request.c_str(), request.length(), 0);
        
        char buffer[4096];
        ssize_t received = recv(sock, buffer, sizeof(buffer) - 1, 0);
        close(sock);
        
        if (received > 0) {
            buffer[received] = '\0';
            return std::string(buffer);
        }
        
        return "";
    }
    
    std::unique_ptr<HttpServer> server;
};

TEST_F(HttpServerTest, BasicGetRequest) {
    std::string request = "GET /test HTTP/1.1\r\nHost: localhost\r\n\r\n";
    std::string response = send_http_request(request);
    
    EXPECT_NE(response.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(response.find("Hello, World!"), std::string::npos);
    EXPECT_NE(response.find("Content-Length: 13"), std::string::npos);
}

TEST_F(HttpServerTest, JsonResponse) {
    std::string request = "GET /json HTTP/1.1\r\nHost: localhost\r\n\r\n";
    std::string response = send_http_request(request);
    
    EXPECT_NE(response.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(response.find("{\"status\": \"ok\"}"), std::string::npos);
}

TEST_F(HttpServerTest, NotFoundResponse) {
    std::string request = "GET /nonexistent HTTP/1.1\r\nHost: localhost\r\n\r\n";
    std::string response = send_http_request(request);
    
    EXPECT_NE(response.find("HTTP/1.1 404 Not Found"), std::string::npos);
    EXPECT_NE(response.find("Not Found"), std::string::npos);
}

TEST_F(HttpServerTest, MultipleRequests) {
    for (int i = 0; i < 5; ++i) {
        std::string request = "GET /test HTTP/1.1\r\nHost: localhost\r\n\r\n";
        std::string response = send_http_request(request);
        
        EXPECT_NE(response.find("HTTP/1.1 200 OK"), std::string::npos);
        EXPECT_NE(response.find("Hello, World!"), std::string::npos);
    }
    
    EXPECT_EQ(server->requests_served(), 5);
}

TEST_F(HttpServerTest, ConcurrentRequests) {
    constexpr int num_threads = 10;
    std::vector<std::thread> threads;
    std::atomic<int> successful_requests{0};
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, &successful_requests]() {
            std::string request = "GET /test HTTP/1.1\r\nHost: localhost\r\n\r\n";
            std::string response = send_http_request(request);
            
            if (response.find("HTTP/1.1 200 OK") != std::string::npos) {
                successful_requests.fetch_add(1);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(successful_requests.load(), num_threads);
    EXPECT_EQ(server->requests_served(), num_threads);
}

} // namespace rtes