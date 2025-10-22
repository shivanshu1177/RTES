#include <gtest/gtest.h>
#include "rtes/security_utils.hpp"
#include "rtes/auth_middleware.hpp"
#include "rtes/logger.hpp"

using namespace rtes;

class SecurityTest : public ::testing::Test {
protected:
    void SetUp() override {
        AuthMiddleware::initialize();
    }
};

TEST_F(SecurityTest, LogInputSanitization) {
    // Test control character removal
    std::string malicious = "user\x00\x01\x02input";
    std::string sanitized = SecurityUtils::sanitize_log_input(malicious);
    EXPECT_EQ(sanitized, "userinput");
    
    // Test escape sequences
    std::string with_newlines = "line1\nline2\r\nline3";
    std::string escaped = SecurityUtils::sanitize_log_input(with_newlines);
    EXPECT_EQ(escaped, "line1\\nline2\\r\\nline3");
    
    // Test quote escaping
    std::string with_quotes = "test \"quoted\" string";
    std::string quote_escaped = SecurityUtils::sanitize_log_input(with_quotes);
    EXPECT_EQ(quote_escaped, "test \\\"quoted\\\" string");
}

TEST_F(SecurityTest, PathTraversalPrevention) {
    // Test valid paths
    EXPECT_TRUE(SecurityUtils::validate_file_path("./configs/config.json", "./configs"));
    EXPECT_TRUE(SecurityUtils::validate_file_path("../configs/test.json", "../configs"));
    
    // Test path traversal attempts
    EXPECT_FALSE(SecurityUtils::validate_file_path("../../etc/passwd", "./configs"));
    EXPECT_FALSE(SecurityUtils::validate_file_path("../../../root/.ssh/id_rsa", "./configs"));
    EXPECT_FALSE(SecurityUtils::validate_file_path("/etc/shadow", "./configs"));
    
    // Test path normalization
    std::string normalized = SecurityUtils::normalize_path("./configs/../configs/config.json");
    EXPECT_FALSE(normalized.empty());
}

TEST_F(SecurityTest, InputValidation) {
    // Test valid symbols
    EXPECT_TRUE(SecurityUtils::is_valid_symbol("AAPL"));
    EXPECT_TRUE(SecurityUtils::is_valid_symbol("MSFT"));
    EXPECT_TRUE(SecurityUtils::is_valid_symbol("GOOGL"));
    
    // Test invalid symbols
    EXPECT_FALSE(SecurityUtils::is_valid_symbol(""));
    EXPECT_FALSE(SecurityUtils::is_valid_symbol("aapl")); // lowercase
    EXPECT_FALSE(SecurityUtils::is_valid_symbol("AAPL;")); // special chars
    EXPECT_FALSE(SecurityUtils::is_valid_symbol("VERYLONGSYMBOLNAME")); // too long
    
    // Test order ID validation
    EXPECT_TRUE(SecurityUtils::is_valid_order_id("ORDER123"));
    EXPECT_TRUE(SecurityUtils::is_valid_order_id("order-456"));
    EXPECT_TRUE(SecurityUtils::is_valid_order_id("order_789"));
    
    // Test invalid order IDs
    EXPECT_FALSE(SecurityUtils::is_valid_order_id(""));
    EXPECT_FALSE(SecurityUtils::is_valid_order_id("order;123")); // semicolon
    EXPECT_FALSE(SecurityUtils::is_valid_order_id("order\x00123")); // null byte
}

TEST_F(SecurityTest, SafeFormatting) {
    // Test normal formatting
    std::string result = SecurityUtils::safe_format("Order {} for client {}", 12345, "client1");
    EXPECT_EQ(result, "Order 12345 for client client1");
    
    // Test format error handling
    std::string error_result = SecurityUtils::safe_format("Invalid format {}", "too", "many", "args");
    EXPECT_TRUE(error_result.find("[FORMAT_ERROR") != std::string::npos);
}

TEST_F(SecurityTest, Authentication) {
    // Test valid admin token
    AuthContext admin_ctx = SecurityUtils::authenticate_user("admin_token_12345");
    EXPECT_TRUE(admin_ctx.authenticated);
    EXPECT_EQ(admin_ctx.role, UserRole::ADMIN);
    EXPECT_EQ(admin_ctx.user_id, "admin");
    
    // Test valid trader token
    AuthContext trader_ctx = SecurityUtils::authenticate_user("trader_user123");
    EXPECT_TRUE(trader_ctx.authenticated);
    EXPECT_EQ(trader_ctx.role, UserRole::TRADER);
    EXPECT_EQ(trader_ctx.user_id, "user123");
    
    // Test invalid token
    AuthContext invalid_ctx = SecurityUtils::authenticate_user("invalid");
    EXPECT_FALSE(invalid_ctx.authenticated);
    
    // Test empty token
    AuthContext empty_ctx = SecurityUtils::authenticate_user("");
    EXPECT_FALSE(empty_ctx.authenticated);
}

TEST_F(SecurityTest, Authorization) {
    AuthContext admin_ctx;
    admin_ctx.authenticated = true;
    admin_ctx.role = UserRole::ADMIN;
    admin_ctx.user_id = "admin";
    
    AuthContext trader_ctx;
    trader_ctx.authenticated = true;
    trader_ctx.role = UserRole::TRADER;
    trader_ctx.user_id = "trader1";
    
    AuthContext viewer_ctx;
    viewer_ctx.authenticated = true;
    viewer_ctx.role = UserRole::VIEWER;
    viewer_ctx.user_id = "viewer1";
    
    // Test admin permissions
    EXPECT_TRUE(SecurityUtils::is_authorized_for_operation(admin_ctx, "shutdown"));
    EXPECT_TRUE(SecurityUtils::is_authorized_for_operation(admin_ctx, "place_order"));
    EXPECT_TRUE(SecurityUtils::is_authorized_for_operation(admin_ctx, "cancel_order"));
    
    // Test trader permissions
    EXPECT_FALSE(SecurityUtils::is_authorized_for_operation(trader_ctx, "shutdown"));
    EXPECT_TRUE(SecurityUtils::is_authorized_for_operation(trader_ctx, "place_order"));
    EXPECT_TRUE(SecurityUtils::is_authorized_for_operation(trader_ctx, "cancel_order"));
    
    // Test viewer permissions
    EXPECT_FALSE(SecurityUtils::is_authorized_for_operation(viewer_ctx, "shutdown"));
    EXPECT_FALSE(SecurityUtils::is_authorized_for_operation(viewer_ctx, "place_order"));
    EXPECT_FALSE(SecurityUtils::is_authorized_for_operation(viewer_ctx, "cancel_order"));
    
    // Test unauthenticated user
    AuthContext unauth_ctx;
    unauth_ctx.authenticated = false;
    EXPECT_FALSE(SecurityUtils::is_authorized_for_operation(unauth_ctx, "place_order"));
}

TEST_F(SecurityTest, SecureLogging) {
    // Test that secure logging methods work without crashing
    Logger& logger = Logger::instance();
    logger.set_level(LogLevel::DEBUG);
    
    // These should not crash and should sanitize input
    logger.debug_safe("Test debug message: {}", "safe_input");
    logger.info_safe("Order {} processed for client {}", 12345, "client1");
    logger.warn_safe("Warning: invalid input {}", "malicious\x00\x01input");
    logger.error_safe("Error processing order {}", 67890);
    
    // Test macro versions
    LOG_DEBUG_SAFE("Debug: {}", "test");
    LOG_INFO_SAFE("Info: {} {}", "param1", "param2");
    LOG_WARN_SAFE("Warning: {}", "issue");
    LOG_ERROR_SAFE("Error: {}", "problem");
}