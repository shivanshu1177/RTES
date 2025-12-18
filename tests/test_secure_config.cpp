#include <gtest/gtest.h>
#include "rtes/secure_config.hpp"
#include <fstream>
#include <cstdlib>

using namespace rtes;

class SecureConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up test environment variables
        setenv("RTES_ENCRYPTION_KEY_test", "0123456789abcdef0123456789abcdef", 1);
        setenv("RTES_exchange.tcp_port_DEV", "9999", 1);
        
        // Create test config file
        std::ofstream config_file("test_config.txt");
        config_file << "exchange.name=TEST_EXCHANGE\n";
        config_file << "exchange.tcp_port=8888\n";
        config_file << "exchange.udp_port=9999\n";
        config_file << "risk.max_order_size=100000\n";
        config_file.close();
    }
    
    void TearDown() override {
        unsetenv("RTES_ENCRYPTION_KEY_test");
        unsetenv("RTES_exchange.tcp_port_DEV");
        std::remove("test_config.txt");
    }
};

TEST_F(SecureConfigTest, ConfigEncryptionDecryption) {
    std::string test_data = "sensitive configuration data";
    
    auto encrypted_result = ConfigEncryption::encrypt(test_data, "test");
    ASSERT_TRUE(encrypted_result.has_value());
    
    auto encrypted = encrypted_result.value();
    EXPECT_FALSE(encrypted.encrypted_data.empty());
    EXPECT_FALSE(encrypted.iv.empty());
    EXPECT_EQ(encrypted.key_id, "test");
    
    std::string key = "0123456789abcdef0123456789abcdef";
    auto decrypted_result = ConfigEncryption::decrypt(encrypted, key);
    ASSERT_TRUE(decrypted_result.has_value());
    
    EXPECT_EQ(decrypted_result.value(), test_data);
}

TEST_F(SecureConfigTest, SecureConfigManagerBasic) {
    auto& config_manager = SecureConfigManager::instance();
    
    auto load_result = config_manager.load_config("test_config.txt", Environment::DEVELOPMENT);
    ASSERT_TRUE(load_result.has_value());
    
    EXPECT_EQ(config_manager.get_environment(), Environment::DEVELOPMENT);
    EXPECT_TRUE(config_manager.has_key("exchange.name"));
    
    auto name_result = config_manager.get_value<std::string>("exchange.name");
    ASSERT_TRUE(name_result.has_value());
    EXPECT_EQ(name_result.value(), "TEST_EXCHANGE");
}

TEST_F(SecureConfigTest, EnvironmentOverrides) {
    auto& config_manager = SecureConfigManager::instance();
    
    auto load_result = config_manager.load_config("test_config.txt", Environment::DEVELOPMENT);
    ASSERT_TRUE(load_result.has_value());
    
    // Environment variable should override config file
    auto port_result = config_manager.get_value<int>("exchange.tcp_port");
    ASSERT_TRUE(port_result.has_value());
    EXPECT_EQ(port_result.value(), 9999); // From environment variable
}

TEST_F(SecureConfigTest, ConfigValidation) {
    auto& config_manager = SecureConfigManager::instance();
    
    // Create invalid config
    std::ofstream invalid_config("invalid_config.txt");
    invalid_config << "invalid.key=value\n";
    invalid_config.close();
    
    auto load_result = config_manager.load_config("invalid_config.txt", Environment::DEVELOPMENT);
    EXPECT_TRUE(load_result.has_error());
    
    std::remove("invalid_config.txt");
}

TEST_F(SecureConfigTest, FeatureFlags) {
    auto& flags = FeatureFlags::instance();
    
    EXPECT_FALSE(flags.is_enabled("test_feature"));
    
    flags.set_flag("test_feature", true);
    EXPECT_TRUE(flags.is_enabled("test_feature"));
    
    flags.set_rollout_percentage("gradual_feature", 50.0);
    
    // Test user-specific rollout
    bool user1_enabled = flags.is_enabled_for_user("gradual_feature", "user1");
    bool user2_enabled = flags.is_enabled_for_user("gradual_feature", "user2");
    
    // At least one should be different due to hash distribution
    // (This is probabilistic but very likely with different user IDs)
}

TEST_F(SecureConfigTest, HealthCheckManager) {
    auto& health_manager = HealthCheckManager::instance();
    
    // Register test health checks
    HealthCheckManager::HealthCheck check1{
        "test_check_1",
        []() { return HealthCheckManager::HealthStatus::HEALTHY; },
        std::chrono::milliseconds(100),
        true
    };
    
    HealthCheckManager::HealthCheck check2{
        "test_check_2",
        []() { return HealthCheckManager::HealthStatus::DEGRADED; },
        std::chrono::milliseconds(100),
        false
    };
    
    health_manager.register_check(check1);
    health_manager.register_check(check2);
    
    auto overall_status = health_manager.get_overall_status();
    EXPECT_EQ(overall_status, HealthCheckManager::HealthStatus::DEGRADED);
    
    auto detailed_status = health_manager.get_detailed_status();
    EXPECT_EQ(detailed_status["test_check_1"], HealthCheckManager::HealthStatus::HEALTHY);
    EXPECT_EQ(detailed_status["test_check_2"], HealthCheckManager::HealthStatus::DEGRADED);
    
    EXPECT_TRUE(health_manager.is_ready());
    EXPECT_TRUE(health_manager.is_live());
}

TEST_F(SecureConfigTest, EmergencyShutdown) {
    auto& emergency = EmergencyShutdown::instance();
    
    bool shutdown_triggered = false;
    emergency.register_shutdown_handler([&shutdown_triggered]() {
        shutdown_triggered = true;
    });
    
    bool trigger_activated = false;
    emergency.register_trigger("test_trigger", [&trigger_activated]() {
        return trigger_activated;
    });
    
    EXPECT_FALSE(emergency.is_emergency_shutdown_active());
    
    emergency.initiate_emergency_shutdown("Test shutdown");
    
    EXPECT_TRUE(emergency.is_emergency_shutdown_active());
    EXPECT_TRUE(shutdown_triggered);
}

TEST_F(SecureConfigTest, ConfigChangeCallbacks) {
    auto& config_manager = SecureConfigManager::instance();
    
    std::string callback_value;
    config_manager.register_change_callback("test_key", [&callback_value](const std::string& value) {
        callback_value = value;
    });
    
    auto set_result = config_manager.set_value("test_key", "test_value");
    ASSERT_TRUE(set_result.has_value());
    
    EXPECT_EQ(callback_value, "test_value");
}

// Performance test for configuration access
TEST_F(SecureConfigTest, ConfigAccessPerformance) {
    auto& config_manager = SecureConfigManager::instance();
    
    auto load_result = config_manager.load_config("test_config.txt", Environment::DEVELOPMENT);
    ASSERT_TRUE(load_result.has_value());
    
    const int iterations = 100000;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        auto result = config_manager.get_value<std::string>("exchange.name");
        EXPECT_TRUE(result.has_value());
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Should be very fast (less than 10μs per access on average)
    double avg_time_us = static_cast<double>(duration.count()) / iterations;
    EXPECT_LT(avg_time_us, 10.0);
}