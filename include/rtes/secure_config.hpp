#pragma once

#include "rtes/error_handling.hpp"
#include "rtes/thread_safety.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>

namespace rtes {

enum class Environment {
    DEVELOPMENT,
    STAGING,
    PRODUCTION
};

struct ConfigSignature {
    std::string hash;
    std::string signature;
    std::chrono::system_clock::time_point timestamp;
};

struct EncryptedConfig {
    std::vector<uint8_t> encrypted_data;
    std::vector<uint8_t> iv;
    std::string key_id;
};

class ConfigEncryption {
public:
    static Result<EncryptedConfig> encrypt(const std::string& data, const std::string& key_id);
    static Result<std::string> decrypt(const EncryptedConfig& config, const std::string& key);
    static std::string generate_key();
    static bool verify_signature(const std::string& data, const ConfigSignature& sig);
};

class SecureConfigManager {
public:
    static SecureConfigManager& instance();
    
    Result<void> load_config(const std::string& config_path, Environment env);
    Result<void> reload_config();
    
    template<typename T>
    Result<T> get_value(const std::string& key) const;
    
    Result<void> set_value(const std::string& key, const std::string& value);
    bool has_key(const std::string& key) const;
    
    void register_change_callback(const std::string& key, std::function<void(const std::string&)> callback);
    
    Environment get_environment() const { return environment_; }
    bool is_production() const { return environment_ == Environment::PRODUCTION; }

private:
    mutable std::shared_mutex config_mutex_;
    std::unordered_map<std::string, std::string> config_data_ GUARDED_BY(config_mutex_);
    std::unordered_map<std::string, std::function<void(const std::string&)>> callbacks_ GUARDED_BY(config_mutex_);
    
    Environment environment_{Environment::DEVELOPMENT};
    std::string config_path_;
    std::string encryption_key_;
    ConfigSignature signature_;
    
    Result<void> load_encrypted_file(const std::string& path);
    Result<void> load_environment_overrides();
    Result<void> validate_configuration();
    void notify_callbacks(const std::string& key, const std::string& value);
};

class FeatureFlags {
public:
    static FeatureFlags& instance();
    
    bool is_enabled(const std::string& flag) const;
    void set_flag(const std::string& flag, bool enabled);
    void set_rollout_percentage(const std::string& flag, double percentage);
    
    bool is_enabled_for_user(const std::string& flag, const std::string& user_id) const;

private:
    mutable std::shared_mutex flags_mutex_;
    std::unordered_map<std::string, bool> flags_ GUARDED_BY(flags_mutex_);
    std::unordered_map<std::string, double> rollout_percentages_ GUARDED_BY(flags_mutex_);
    
    uint64_t hash_user_id(const std::string& user_id) const;
};

class HealthCheckManager {
public:
    enum class HealthStatus {
        HEALTHY,
        DEGRADED,
        UNHEALTHY
    };
    
    struct HealthCheck {
        std::string name;
        std::function<HealthStatus()> check_fn;
        std::chrono::milliseconds timeout{1000};
        bool critical{true};
    };
    
    static HealthCheckManager& instance();
    
    void register_check(const HealthCheck& check);
    HealthStatus get_overall_status() const;
    std::unordered_map<std::string, HealthStatus> get_detailed_status() const;
    
    bool is_ready() const;
    bool is_live() const;

private:
    mutable std::shared_mutex checks_mutex_;
    std::vector<HealthCheck> checks_ GUARDED_BY(checks_mutex_);
    
    HealthStatus run_check(const HealthCheck& check) const;
};

class EmergencyShutdown {
public:
    static EmergencyShutdown& instance();
    
    void register_trigger(const std::string& name, std::function<bool()> trigger_fn);
    void register_shutdown_handler(std::function<void()> handler);
    
    void initiate_emergency_shutdown(const std::string& reason);
    bool is_emergency_shutdown_active() const;
    
    void start_monitoring();
    void stop_monitoring();

private:
    mutable std::mutex shutdown_mutex_;
    atomic_wrapper<bool> emergency_active_{false};
    atomic_wrapper<bool> monitoring_active_{false};
    
    std::vector<std::pair<std::string, std::function<bool()>>> triggers_ GUARDED_BY(shutdown_mutex_);
    std::vector<std::function<void()>> handlers_ GUARDED_BY(shutdown_mutex_);
    std::thread monitor_thread_;
    
    void monitor_loop();
};

} // namespace rtes