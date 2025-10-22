#include "rtes/deployment_manager.hpp"
#include "rtes/logger.hpp"
#include <sstream>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

namespace rtes {

// RollingUpdateManager implementation
RollingUpdateManager& RollingUpdateManager::instance() {
    static RollingUpdateManager instance;
    return instance;
}

Result<void> RollingUpdateManager::start_deployment(const DeploymentConfig& config) {
    std::lock_guard lock(deployment_mutex_);
    
    if (deployment_active_.load()) {
        return ErrorCode::DEPLOYMENT_ALREADY_ACTIVE;
    }
    
    current_config_ = config;
    deployment_active_.store(true);
    current_phase_.store(DeploymentPhase::INITIALIZING);
    traffic_percentage_.store(0.0);
    
    deployment_thread_ = std::thread(&RollingUpdateManager::deployment_loop, this);
    
    LOG_INFO("Started deployment for version {} to {}", 
             config.version, static_cast<int>(config.target_env));
    
    return Result<void>();
}

void RollingUpdateManager::abort_deployment(const std::string& reason) {
    LOG_WARN("Aborting deployment: {}", reason);
    
    current_phase_.store(DeploymentPhase::ROLLBACK);
    execute_rollback();
    
    deployment_active_.store(false);
    if (deployment_thread_.joinable()) {
        deployment_thread_.join();
    }
}

DeploymentPhase RollingUpdateManager::get_current_phase() const {
    return current_phase_.load();
}

double RollingUpdateManager::get_traffic_percentage() const {
    return traffic_percentage_.load();
}

bool RollingUpdateManager::is_deployment_active() const {
    return deployment_active_.load();
}

void RollingUpdateManager::register_traffic_controller(std::function<void(double)> controller) {
    std::lock_guard lock(deployment_mutex_);
    traffic_controller_ = std::move(controller);
}

void RollingUpdateManager::register_rollback_handler(std::function<void()> handler) {
    std::lock_guard lock(deployment_mutex_);
    rollback_handler_ = std::move(handler);
}

void RollingUpdateManager::deployment_loop() {
    try {
        // Phase 1: Health Check
        current_phase_.store(DeploymentPhase::HEALTH_CHECK);
        LOG_INFO("Deployment phase: Health Check");
        
        auto health_start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - health_start < current_config_.health_check_timeout) {
            if (validate_health_checks()) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        if (!validate_health_checks()) {
            LOG_ERROR("Health checks failed during deployment");
            current_phase_.store(DeploymentPhase::FAILED);
            abort_deployment("Health checks failed");
            return;
        }
        
        // Phase 2: Traffic Ramp
        if (current_config_.enable_canary) {
            current_phase_.store(DeploymentPhase::TRAFFIC_RAMP);
            LOG_INFO("Deployment phase: Traffic Ramp");
            ramp_traffic();
        }
        
        // Phase 3: Full Deployment
        current_phase_.store(DeploymentPhase::FULL_DEPLOYMENT);
        LOG_INFO("Deployment phase: Full Deployment");
        
        if (traffic_controller_) {
            traffic_controller_(1.0);
        }
        traffic_percentage_.store(1.0);
        
        // Final health check
        std::this_thread::sleep_for(std::chrono::seconds(10));
        if (!validate_health_checks()) {
            LOG_ERROR("Final health check failed");
            abort_deployment("Final health check failed");
            return;
        }
        
        deployment_active_.store(false);
        LOG_INFO("Deployment completed successfully");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Deployment failed with exception: {}", e.what());
        abort_deployment("Exception during deployment");
    }
}

bool RollingUpdateManager::validate_health_checks() {
    auto& health_manager = HealthCheckManager::instance();
    
    if (!current_config_.required_health_checks.empty()) {
        auto detailed_status = health_manager.get_detailed_status();
        
        for (const auto& check_name : current_config_.required_health_checks) {
            auto it = detailed_status.find(check_name);
            if (it == detailed_status.end() || 
                it->second == HealthCheckManager::HealthStatus::UNHEALTHY) {
                return false;
            }
        }
    }
    
    return health_manager.get_overall_status() != HealthCheckManager::HealthStatus::UNHEALTHY;
}

void RollingUpdateManager::ramp_traffic() {
    double current_traffic = 0.0;
    auto ramp_start = std::chrono::steady_clock::now();
    
    while (current_traffic < 1.0 && 
           std::chrono::steady_clock::now() - ramp_start < current_config_.traffic_ramp_duration) {
        
        current_traffic = std::min(1.0, current_traffic + current_config_.traffic_ramp_step);
        
        if (traffic_controller_) {
            traffic_controller_(current_traffic);
        }
        traffic_percentage_.store(current_traffic);
        
        LOG_INFO("Traffic ramped to {}%", current_traffic * 100);
        
        // Health check during ramp
        if (!validate_health_checks()) {
            LOG_ERROR("Health check failed during traffic ramp");
            abort_deployment("Health check failed during ramp");
            return;
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}

void RollingUpdateManager::execute_rollback() {
    LOG_INFO("Executing rollback");
    
    if (rollback_handler_) {
        try {
            rollback_handler_();
        } catch (const std::exception& e) {
            LOG_ERROR("Rollback handler failed: {}", e.what());
        }
    }
    
    if (traffic_controller_) {
        traffic_controller_(0.0);
    }
    traffic_percentage_.store(0.0);
}

// ReadinessProbe implementation
ReadinessProbe& ReadinessProbe::instance() {
    static ReadinessProbe instance;
    return instance;
}

void ReadinessProbe::register_readiness_check(const std::string& name, std::function<bool()> check) {
    std::unique_lock lock(checks_mutex_);
    readiness_checks_[name] = std::move(check);
}

bool ReadinessProbe::is_ready() const {
    std::shared_lock lock(checks_mutex_);
    
    for (const auto& [name, check] : readiness_checks_) {
        try {
            if (!check()) {
                return false;
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Readiness check '{}' failed: {}", name, e.what());
            return false;
        }
    }
    
    return true;
}

std::vector<std::string> ReadinessProbe::get_failed_checks() const {
    std::shared_lock lock(checks_mutex_);
    std::vector<std::string> failed;
    
    for (const auto& [name, check] : readiness_checks_) {
        try {
            if (!check()) {
                failed.push_back(name);
            }
        } catch (const std::exception&) {
            failed.push_back(name);
        }
    }
    
    return failed;
}

// LivenessProbe implementation
LivenessProbe& LivenessProbe::instance() {
    static LivenessProbe instance;
    return instance;
}

void LivenessProbe::register_liveness_check(const std::string& name, std::function<bool()> check) {
    std::unique_lock lock(checks_mutex_);
    liveness_checks_[name] = std::move(check);
}

bool LivenessProbe::is_alive() const {
    std::shared_lock lock(checks_mutex_);
    
    // Check if heartbeat is recent (within last 30 seconds)
    auto now = std::chrono::system_clock::now();
    auto last_hb = last_heartbeat_.load();
    if (now - last_hb > std::chrono::seconds(30)) {
        return false;
    }
    
    for (const auto& [name, check] : liveness_checks_) {
        try {
            if (!check()) {
                return false;
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Liveness check '{}' failed: {}", name, e.what());
            return false;
        }
    }
    
    return true;
}

void LivenessProbe::update_last_heartbeat() {
    last_heartbeat_.store(std::chrono::system_clock::now());
}

std::chrono::system_clock::time_point LivenessProbe::get_last_heartbeat() const {
    return last_heartbeat_.load();
}

// DeploymentHealthEndpoints implementation
DeploymentHealthEndpoints& DeploymentHealthEndpoints::instance() {
    static DeploymentHealthEndpoints instance;
    return instance;
}

void DeploymentHealthEndpoints::start_server(uint16_t port) {
    if (server_running_.exchange(true)) {
        return; // Already running
    }
    
    port_ = port;
    server_thread_ = std::thread(&DeploymentHealthEndpoints::server_loop, this);
    
    LOG_INFO("Health endpoints server started on port {}", port);
}

void DeploymentHealthEndpoints::stop_server() {
    server_running_.store(false);
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

std::string DeploymentHealthEndpoints::handle_health_request() const {
    auto& health_manager = HealthCheckManager::instance();
    auto status = health_manager.get_overall_status();
    auto detailed = health_manager.get_detailed_status();
    
    std::unordered_map<std::string, std::string> details;
    for (const auto& [name, check_status] : detailed) {
        std::string status_str;
        switch (check_status) {
            case HealthCheckManager::HealthStatus::HEALTHY: status_str = "healthy"; break;
            case HealthCheckManager::HealthStatus::DEGRADED: status_str = "degraded"; break;
            case HealthCheckManager::HealthStatus::UNHEALTHY: status_str = "unhealthy"; break;
            default: status_str = "unknown"; break;
        }
        details[name] = status_str;
    }
    
    std::string overall_status;
    switch (status) {
        case HealthCheckManager::HealthStatus::HEALTHY: overall_status = "healthy"; break;
        case HealthCheckManager::HealthStatus::DEGRADED: overall_status = "degraded"; break;
        case HealthCheckManager::HealthStatus::UNHEALTHY: overall_status = "unhealthy"; break;
        default: overall_status = "unknown"; break;
    }
    
    return create_json_response(overall_status, details);
}

std::string DeploymentHealthEndpoints::handle_ready_request() const {
    auto& readiness = ReadinessProbe::instance();
    bool ready = readiness.is_ready();
    auto failed_checks = readiness.get_failed_checks();
    
    std::unordered_map<std::string, std::string> details;
    details["ready"] = ready ? "true" : "false";
    
    if (!failed_checks.empty()) {
        std::ostringstream oss;
        for (size_t i = 0; i < failed_checks.size(); ++i) {
            if (i > 0) oss << ",";
            oss << failed_checks[i];
        }
        details["failed_checks"] = oss.str();
    }
    
    return create_json_response(ready ? "ready" : "not_ready", details);
}

std::string DeploymentHealthEndpoints::handle_live_request() const {
    auto& liveness = LivenessProbe::instance();
    bool alive = liveness.is_alive();
    
    std::unordered_map<std::string, std::string> details;
    details["alive"] = alive ? "true" : "false";
    
    auto last_hb = liveness.get_last_heartbeat();
    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_hb);
    details["last_heartbeat_seconds_ago"] = std::to_string(duration.count());
    
    return create_json_response(alive ? "alive" : "dead", details);
}

std::string DeploymentHealthEndpoints::handle_metrics_request() const {
    auto& deployment = RollingUpdateManager::instance();
    
    std::unordered_map<std::string, std::string> metrics;
    metrics["deployment_active"] = deployment.is_deployment_active() ? "1" : "0";
    metrics["traffic_percentage"] = std::to_string(deployment.get_traffic_percentage());
    
    std::string phase_str;
    switch (deployment.get_current_phase()) {
        case DeploymentPhase::INITIALIZING: phase_str = "initializing"; break;
        case DeploymentPhase::HEALTH_CHECK: phase_str = "health_check"; break;
        case DeploymentPhase::TRAFFIC_RAMP: phase_str = "traffic_ramp"; break;
        case DeploymentPhase::FULL_DEPLOYMENT: phase_str = "full_deployment"; break;
        case DeploymentPhase::ROLLBACK: phase_str = "rollback"; break;
        case DeploymentPhase::FAILED: phase_str = "failed"; break;
        default: phase_str = "unknown"; break;
    }
    metrics["deployment_phase"] = phase_str;
    
    return create_json_response("ok", metrics);
}

void DeploymentHealthEndpoints::server_loop() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        LOG_ERROR("Failed to create health server socket");
        return;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);
    
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        LOG_ERROR("Failed to bind health server to port {}", port_);
        close(server_fd);
        return;
    }
    
    if (listen(server_fd, 10) < 0) {
        LOG_ERROR("Failed to listen on health server");
        close(server_fd);
        return;
    }
    
    while (server_running_.load()) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (server_running_.load()) {
                LOG_ERROR("Failed to accept health server connection");
            }
            continue;
        }
        
        // Simple HTTP request handling
        char buffer[1024] = {0};
        read(client_fd, buffer, sizeof(buffer) - 1);
        
        std::string response;
        std::string http_response;
        
        if (strstr(buffer, "GET /health")) {
            response = handle_health_request();
        } else if (strstr(buffer, "GET /ready")) {
            response = handle_ready_request();
        } else if (strstr(buffer, "GET /live")) {
            response = handle_live_request();
        } else if (strstr(buffer, "GET /metrics")) {
            response = handle_metrics_request();
        } else {
            response = R"({"status": "not_found"})";
        }
        
        http_response = "HTTP/1.1 200 OK\r\n"
                       "Content-Type: application/json\r\n"
                       "Content-Length: " + std::to_string(response.length()) + "\r\n"
                       "\r\n" + response;
        
        write(client_fd, http_response.c_str(), http_response.length());
        close(client_fd);
    }
    
    close(server_fd);
}

std::string DeploymentHealthEndpoints::create_json_response(
    const std::string& status, 
    const std::unordered_map<std::string, std::string>& details) const {
    
    std::ostringstream json;
    json << "{\"status\":\"" << status << "\"";
    
    if (!details.empty()) {
        json << ",\"details\":{";
        bool first = true;
        for (const auto& [key, value] : details) {
            if (!first) json << ",";
            json << "\"" << key << "\":\"" << value << "\"";
            first = false;
        }
        json << "}";
    }
    
    json << "}";
    return json.str();
}

} // namespace rtes