#include "rtes/http_server.hpp"
#include "rtes/logger.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sstream>
#include <cstring>

namespace rtes {

HttpServer::HttpServer(uint16_t port) : port_(port) {
}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::start() {
    if (running_.load()) return;
    
    if (!setup_listen_socket()) {
        LOG_ERROR("Failed to setup HTTP server socket");
        return;
    }
    
    running_.store(true);
    server_thread_ = std::thread(&HttpServer::server_loop, this);
    
    LOG_INFO("HTTP server started on port " + std::to_string(port_));
}

void HttpServer::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
    
    LOG_INFO("HTTP server stopped");
}

void HttpServer::add_handler(const std::string& path, HttpHandler handler) {
    handlers_[path] = handler;
}

bool HttpServer::setup_listen_socket() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        return false;
    }
    
    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);
    
    if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        return false;
    }
    
    if (listen(listen_fd_, 10) < 0) {
        return false;
    }
    
    return true;
}

void HttpServer::server_loop() {
    while (running_.load()) {
        struct sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        
        int client_fd = accept(listen_fd_, reinterpret_cast<struct sockaddr*>(&client_addr), &addr_len);
        if (client_fd >= 0) {
            handle_client(client_fd);
            close(client_fd);
        }
    }
}

void HttpServer::handle_client(int client_fd) {
    char buffer[4096];
    ssize_t received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    
    if (received <= 0) return;
    if (received >= static_cast<ssize_t>(sizeof(buffer))) {
        LOG_WARN("HTTP request too large, truncating");
        received = sizeof(buffer) - 1;
    }
    
    buffer[received] = '\0';
    std::string request(buffer, received);
    
    // Parse request line
    size_t line_end = request.find("\r\n");
    if (line_end == std::string::npos || line_end > 8192) {
        LOG_WARN("Invalid HTTP request format");
        return;
    }
    
    std::string request_line = request.substr(0, line_end);
    std::string path = parse_request_path(request_line);
    
    // Validate path to prevent directory traversal
    if (path.find("..") != std::string::npos || path.find("//") != std::string::npos) {
        LOG_WARN_SAFE("Potential path traversal attempt: {}", path);
        std::string response = create_response(400, "text/plain", "Bad Request");
        send(client_fd, response.c_str(), response.length(), 0);
        return;
    }
    
    std::string response;
    
    // Find handler
    auto it = handlers_.find(path);
    if (it != handlers_.end()) {
        try {
            std::string content = it->second(path, "");
            response = create_response(200, "text/plain; charset=utf-8", content);
        } catch (const std::exception& e) {
            response = create_response(500, "text/plain", "Internal Server Error");
        }
    } else {
        response = create_response(404, "text/plain", "Not Found");
    }
    
    send(client_fd, response.c_str(), response.length(), 0);
    requests_served_.fetch_add(1);
}

std::string HttpServer::parse_request_path(const std::string& request_line) {
    std::istringstream iss(request_line);
    std::string method, path, version;
    
    if (iss >> method >> path >> version) {
        // Remove query parameters
        size_t query_pos = path.find('?');
        if (query_pos != std::string::npos) {
            path = path.substr(0, query_pos);
        }
        return path;
    }
    
    return "/";
}

std::string HttpServer::create_response(int status_code, const std::string& content_type, const std::string& body) {
    std::ostringstream response;
    
    std::string status_text;
    switch (status_code) {
        case 200: status_text = "OK"; break;
        case 404: status_text = "Not Found"; break;
        case 500: status_text = "Internal Server Error"; break;
        default: status_text = "Unknown"; break;
    }
    
    response << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << body.length() << "\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    response << body;
    
    return response.str();
}

} // namespace rtes