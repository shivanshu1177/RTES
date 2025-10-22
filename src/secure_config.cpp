#include "rtes/secure_config.hpp"
#include "rtes/logger.hpp"
#include <fstream>
#include <sstream>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <cstdlib>
#include <algorithm>

namespace rtes {

// ConfigEncryption implementation
Result<EncryptedConfig> ConfigEncryption::encrypt(const std::string& data, const std::string& key_id) {
    EncryptedConfig result;
    result.key_id = key_id;
    
    // Generate random IV
    result.iv.resize(16);
    if (RAND_bytes(result.iv.data(), 16) != 1) {
        return ErrorCode::ENCRYPTION_FAILED;
    }
    
    // Get encryption key from environment
    std::string key_env = "RTES_ENCRYPTION_KEY_" + key_id;
    const char* key_ptr = std::getenv(key_env.c_str());
    if (!key_ptr) {
        return ErrorCode::ENCRYPTION_KEY_NOT_FOUND;
    }
    
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return ErrorCode::ENCRYPTION_FAILED;
    
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, 
                          reinterpret_cast<const unsigned char*>(key_ptr), 
                          result.iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return ErrorCode::ENCRYPTION_FAILED;
    }
    
    result.encrypted_data.resize(data.length() + 16);
    int len, ciphertext_len;
    
    if (EVP_EncryptUpdate(ctx, result.encrypted_data.data(), &len,
                         reinterpret_cast<const unsigned char*>(data.c_str()), 
                         data.length()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return ErrorCode::ENCRYPTION_FAILED;
    }
    ciphertext_len = len;
    
    if (EVP_EncryptFinal_ex(ctx, result.encrypted_data.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return ErrorCode::ENCRYPTION_FAILED;
    }
    ciphertext_len += len;
    
    result.encrypted_data.resize(ciphertext_len);
    EVP_CIPHER_CTX_free(ctx);
    
    return result;
}

Result<std::string> ConfigEncryption::decrypt(const EncryptedConfig& config, const std::string& key) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return ErrorCode::DECRYPTION_FAILED;
    
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
                          reinterpret_cast<const unsigned char*>(key.c_str()),
                          config.iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return ErrorCode::DECRYPTION_FAILED;
    }
    
    std::vector<unsigned char> plaintext(config.encrypted_data.size() + 16);
    int len, plaintext_len;
    
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len,
                         config.encrypted_data.data(), 
                         config.encrypted_data.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return ErrorCode::DECRYPTION_FAILED;
    }
    plaintext_len = len;
    
    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return ErrorCode::DECRYPTION_FAILED;
    }
    plaintext_len += len;
    
    EVP_CIPHER_CTX_free(ctx);
    
    return std::string(reinterpret_cast<char*>(plaintext.data()), plaintext_len);
}

std::string ConfigEncryption::generate_key() {
    unsigned char key[32];
    RAND_bytes(key, 32);
    
    std::stringstream ss;
    for (int i = 0; i < 32; ++i) {
        ss << std::hex << static_cast<int>(key[i]);
    }
    return ss.str();
}

bool ConfigEncryption::verify_signature(const std::string& data, const ConfigSignature& sig) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.c_str()), data.length(), hash);
    
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        ss << std::hex << static_cast<int>(hash[i]);
    }
    
    return ss.str() == sig.hash;
}

// SecureConfigManager implementation
SecureConfigManager& SecureConfigManager::instance() {
    static SecureConfigManager instance;
    return instance;
}

Result<void> SecureConfigManager::load_config(const std::string& config_path, Environment env) {
    std::unique_lock lock(config_mutex_);
    
    config_path_ = config_path;
    environment_ = env;
    
    // Load base configuration
    auto load_result = load_encrypted_file(config_path);
    if (load_result.has_error()) return load_result;
    
    // Load environment-specific overrides
    auto env_result = load_environment_overrides();
    if (env_result.has_error()) return env_result;
    
    // Validate final configuration
    auto validate_result = validate_configuration();
    if (validate_result.has_error()) return validate_result;
    
    LOG_INFO("Configuration loaded successfully for environment: {}", 
             static_cast<int>(environment_));
    
    return Result<void>();
}

Result<void> SecureConfigManager::reload_config() {
    return load_config(config_path_, environment_);
}

template<typename T>
Result<T> SecureConfigManager::get_value(const std::string& key) const {
    std::shared_lock lock(config_mutex_);
    
    auto it = config_data_.find(key);
    if (it == config_data_.end()) {
        return ErrorCode::CONFIG_KEY_NOT_FOUND;
    }
    
    try {
        if constexpr (std::is_same_v<T, std::string>) {
            return it->second;
        } else if constexpr (std::is_same_v<T, int>) {
            return std::stoi(it->second);
        } else if constexpr (std::is_same_v<T, double>) {
            return std::stod(it->second);
        } else if constexpr (std::is_same_v<T, bool>) {
            return it->second == "true" || it->second == "1";
        }
    } catch (const std::exception&) {
        return ErrorCode::CONFIG_PARSE_ERROR;
    }
    
    return ErrorCode::CONFIG_TYPE_MISMATCH;
}

Result<void> SecureConfigManager::set_value(const std::string& key, const std::string& value) {
    std::unique_lock lock(config_mutex_);
    
    config_data_[key] = value;
    notify_callbacks(key, value);
    
    return Result<void>();
}

bool SecureConfigManager::has_key(const std::string& key) const {
    std::shared_lock lock(config_mutex_);
    return config_data_.find(key) != config_data_.end();
}

void SecureConfigManager::register_change_callback(const std::string& key, 
                                                  std::function<void(const std::string&)> callback) {
    std::unique_lock lock(config_mutex_);
    callbacks_[key] = std::move(callback);
}

Result<void> SecureConfigManager::load_encrypted_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return ErrorCode::CONFIG_FILE_NOT_FOUND;
    }
    
    std::string content((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
    
    // Simple JSON-like parsing for demo
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            
            // Trim whitespace
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            config_data_[key] = value;
        }
    }
    
    return Result<void>();
}

Result<void> SecureConfigManager::load_environment_overrides() {
    // Load environment-specific configuration
    std::string env_suffix;
    switch (environment_) {
        case Environment::DEVELOPMENT: env_suffix = "_DEV"; break;
        case Environment::STAGING: env_suffix = "_STAGING"; break;
        case Environment::PRODUCTION: env_suffix = "_PROD"; break;
        default: return ErrorCode::INVALID_ENVIRONMENT;
    }
    
    // Check for environment variable overrides
    for (const auto& [key, value] : config_data_) {
        std::string env_key = "RTES_" + key + env_suffix;
        std::transform(env_key.begin(), env_key.end(), env_key.begin(), ::toupper);
        
        const char* env_value = std::getenv(env_key.c_str());
        if (env_value) {
            config_data_[key] = env_value;
            LOG_INFO("Override from environment: {} = {}", key, env_value);
        }
    }
    
    return Result<void>();
}

Result<void> SecureConfigManager::validate_configuration() {
    // Validate required keys exist
    std::vector<std::string> required_keys = {
        "exchange.name", "exchange.tcp_port", "exchange.udp_port"
    };
    
    for (const auto& key : required_keys) {
        if (config_data_.find(key) == config_data_.end()) {
            LOG_ERROR("Required configuration key missing: {}", key);
            return ErrorCode::CONFIG_VALIDATION_FAILED;
        }
    }
    
    return Result<void>();
}

void SecureConfigManager::notify_callbacks(const std::string& key, const std::string& value) {
    auto it = callbacks_.find(key);
    if (it != callbacks_.end()) {
        try {
            it->second(value);
        } catch (const std::exception& e) {
            LOG_ERROR("Configuration callback failed for key {}: {}", key, e.what());
        }
    }
}

// FeatureFlags implementation
FeatureFlags& FeatureFlags::instance() {
    static FeatureFlags instance;
    return instance;
}

bool FeatureFlags::is_enabled(const std::string& flag) const {
    std::shared_lock lock(flags_mutex_);
    
    auto it = flags_.find(flag);
    return it != flags_.end() && it->second;
}

void FeatureFlags::set_flag(const std::string& flag, bool enabled) {
    std::unique_lock lock(flags_mutex_);
    flags_[flag] = enabled;
}

void FeatureFlags::set_rollout_percentage(const std::string& flag, double percentage) {
    std::unique_lock lock(flags_mutex_);
    rollout_percentages_[flag] = std::clamp(percentage, 0.0, 100.0);
}

bool FeatureFlags::is_enabled_for_user(const std::string& flag, const std::string& user_id) const {
    std::shared_lock lock(flags_mutex_);
    
    auto flag_it = flags_.find(flag);
    if (flag_it == flags_.end() || !flag_it->second) {
        return false;
    }
    
    auto rollout_it = rollout_percentages_.find(flag);
    if (rollout_it == rollout_percentages_.end()) {
        return true; // No rollout percentage set, flag is fully enabled
    }
    
    uint64_t hash = hash_user_id(user_id);
    double user_percentage = (hash % 10000) / 100.0;
    
    return user_percentage < rollout_it->second;
}

uint64_t FeatureFlags::hash_user_id(const std::string& user_id) const {
    std::hash<std::string> hasher;
    return hasher(user_id);
}

// HealthCheckManager implementation
HealthCheckManager& HealthCheckManager::instance() {
    static HealthCheckManager instance;
    return instance;
}

void HealthCheckManager::register_check(const HealthCheck& check) {
    std::unique_lock lock(checks_mutex_);
    checks_.push_back(check);
}

HealthCheckManager::HealthStatus HealthCheckManager::get_overall_status() const {
    std::shared_lock lock(checks_mutex_);
    
    bool has_critical_failure = false;
    bool has_degraded = false;
    
    for (const auto& check : checks_) {
        HealthStatus status = run_check(check);
        
        if (status == HealthStatus::UNHEALTHY && check.critical) {
            has_critical_failure = true;
        } else if (status == HealthStatus::DEGRADED) {
            has_degraded = true;
        }
    }
    
    if (has_critical_failure) return HealthStatus::UNHEALTHY;
    if (has_degraded) return HealthStatus::DEGRADED;
    return HealthStatus::HEALTHY;
}

std::unordered_map<std::string, HealthCheckManager::HealthStatus> 
HealthCheckManager::get_detailed_status() const {
    std::shared_lock lock(checks_mutex_);
    
    std::unordered_map<std::string, HealthStatus> results;
    for (const auto& check : checks_) {
        results[check.name] = run_check(check);
    }
    
    return results;
}

bool HealthCheckManager::is_ready() const {
    return get_overall_status() != HealthStatus::UNHEALTHY;
}

bool HealthCheckManager::is_live() const {
    std::shared_lock lock(checks_mutex_);
    
    for (const auto& check : checks_) {
        if (check.critical && run_check(check) == HealthStatus::UNHEALTHY) {
            return false;
        }
    }
    
    return true;
}

HealthCheckManager::HealthStatus HealthCheckManager::run_check(const HealthCheck& check) const {
    try {
        return check.check_fn();
    } catch (const std::exception& e) {
        LOG_ERROR("Health check '{}' failed with exception: {}", check.name, e.what());
        return HealthStatus::UNHEALTHY;
    }
}

// EmergencyShutdown implementation
EmergencyShutdown& EmergencyShutdown::instance() {
    static EmergencyShutdown instance;
    return instance;
}

void EmergencyShutdown::register_trigger(const std::string& name, std::function<bool()> trigger_fn) {
    std::lock_guard lock(shutdown_mutex_);
    triggers_.emplace_back(name, std::move(trigger_fn));
}

void EmergencyShutdown::register_shutdown_handler(std::function<void()> handler) {
    std::lock_guard lock(shutdown_mutex_);
    handlers_.push_back(std::move(handler));
}

void EmergencyShutdown::initiate_emergency_shutdown(const std::string& reason) {
    if (emergency_active_.exchange(true)) {
        return; // Already shutting down
    }
    
    LOG_CRITICAL("EMERGENCY SHUTDOWN INITIATED: {}", reason);
    
    std::lock_guard lock(shutdown_mutex_);
    for (const auto& handler : handlers_) {
        try {
            handler();
        } catch (const std::exception& e) {
            LOG_ERROR("Emergency shutdown handler failed: {}", e.what());
        }
    }
}

bool EmergencyShutdown::is_emergency_shutdown_active() const {
    return emergency_active_.load();
}

void EmergencyShutdown::start_monitoring() {
    if (monitoring_active_.exchange(true)) {
        return; // Already monitoring
    }
    
    monitor_thread_ = std::thread(&EmergencyShutdown::monitor_loop, this);
}

void EmergencyShutdown::stop_monitoring() {
    monitoring_active_.store(false);
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
}

void EmergencyShutdown::monitor_loop() {
    while (monitoring_active_.load()) {
        {
            std::lock_guard lock(shutdown_mutex_);
            for (const auto& [name, trigger_fn] : triggers_) {
                try {
                    if (trigger_fn()) {
                        initiate_emergency_shutdown("Trigger activated: " + name);
                        return;
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR("Emergency trigger '{}' failed: {}", name, e.what());
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// Explicit template instantiations
template Result<std::string> SecureConfigManager::get_value<std::string>(const std::string&) const;
template Result<int> SecureConfigManager::get_value<int>(const std::string&) const;
template Result<double> SecureConfigManager::get_value<double>(const std::string&) const;
template Result<bool> SecureConfigManager::get_value<bool>(const std::string&) const;

} // namespace rtes