#pragma once

#include "rtes/secure_config.hpp"
#include "rtes/thread_safety.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <functional>

namespace rtes {

enum class DeploymentPhase {
    INITIALIZING,
    HEALTH_CHECK,
    TRAFFIC_RAMP,
    FULL_DEPLOYMENT,
    ROLLBACK,
    FAILED
};

struct DeploymentConfig {
    std::string version;
    Environment target_env;
    std::chrono::seconds health_check_timeout{30};
    std::chrono::seconds traffic_ramp_duration{300};
    double traffic_ramp_step{0.1};
    bool enable_canary{true};
    std::vector<std::string> required_health_checks;
};

class RollingUpdateManager {
public:
    static RollingUpdateManager& instance();
    
    Result<void> start_deployment(const DeploymentConfig& config);
    void abort_deployment(const std::string& reason);
    
    DeploymentPhase get_current_phase() const;
    double get_traffic_percentage() const;
    bool is_deployment_active() const;
    
    void register_traffic_controller(std::function<void(double)> controller);
    void register_rollback_handler(std::function<void()> handler);

private:
    mutable std::mutex deployment_mutex_;
    atomic_wrapper<DeploymentPhase> current_phase_{DeploymentPhase::INITIALIZING};
    atomic_wrapper<double> traffic_percentage_{0.0};
    atomic_wrapper<bool> deployment_active_{false};
    
    DeploymentConfig current_config_;
    std::function<void(double)> traffic_controller_;
    std::function<void()> rollback_handler_;
    std::thread deployment_thread_;
    
    void deployment_loop();
    bool validate_health_checks();
    void ramp_traffic();
    void execute_rollback();
};

class ReadinessProbe {
public:
    static ReadinessProbe& instance();
    
    void register_readiness_check(const std::string& name, std::function<bool()> check);
    bool is_ready() const;
    std::vector<std::string> get_failed_checks() const;

private:
    mutable std::shared_mutex checks_mutex_;
    std::unordered_map<std::string, std::function<bool()>> readiness_checks_ GUARDED_BY(checks_mutex_);
};

class LivenessProbe {
public:
    static LivenessProbe& instance();
    
    void register_liveness_check(const std::string& name, std::function<bool()> check);
    bool is_alive() const;
    void update_last_heartbeat();
    
    std::chrono::system_clock::time_point get_last_heartbeat() const;

private:
    mutable std::shared_mutex checks_mutex_;
    std::unordered_map<std::string, std::function<bool()>> liveness_checks_ GUARDED_BY(checks_mutex_);
    atomic_wrapper<std::chrono::system_clock::time_point> last_heartbeat_{std::chrono::system_clock::now()};
};

class DeploymentHealthEndpoints {
public:
    static DeploymentHealthEndpoints& instance();
    
    void start_server(uint16_t port = 8080);
    void stop_server();
    
    std::string handle_health_request() const;
    std::string handle_ready_request() const;
    std::string handle_live_request() const;
    std::string handle_metrics_request() const;

private:
    atomic_wrapper<bool> server_running_{false};
    std::thread server_thread_;
    uint16_t port_{8080};
    
    void server_loop();
    std::string create_json_response(const std::string& status, const std::unordered_map<std::string, std::string>& details) const;
};

} // namespace rtes