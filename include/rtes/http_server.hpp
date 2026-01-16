#pragma once

#include <thread>
#include <atomic>
#include <string>
#include <functional>
#include <unordered_map>

namespace rtes {

using HttpHandler = std::function<std::string(const std::string& path, const std::string& query)>;

class HttpServer {
public:
    explicit HttpServer(uint16_t port);
    ~HttpServer();
    
    void start();
    void stop();
    
    void add_handler(const std::string& path, HttpHandler handler);
    
    // Statistics
    uint64_t requests_served() const { return requests_served_.load(); }

private:
    uint16_t port_;
    std::atomic<bool> running_{false};
    std::thread server_thread_;
    
    int listen_fd_{-1};
    std::unordered_map<std::string, HttpHandler> handlers_;
    std::atomic<uint64_t> requests_served_{0};
    
    bool setup_listen_socket();
    void server_loop();
    void handle_client(int client_fd);
    
    std::string parse_request_path(const std::string& request_line);
    std::string create_response(int status_code, const std::string& content_type, const std::string& body);
};

} // namespace rtes