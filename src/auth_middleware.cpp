#include "rtes/auth_middleware.hpp"
#include "rtes/logger.hpp"
#include <chrono>

namespace rtes {

std::unordered_map<std::string, std::chrono::steady_clock::time_point> AuthMiddleware::rate_limits_;
std::chrono::milliseconds AuthMiddleware::rate_limit_window_{1000}; // 1 second

void AuthMiddleware::initialize() {
    LOG_INFO("Authentication middleware initialized");
}

bool AuthMiddleware::authenticate_and_authorize(
    const std::string& token,
    const std::string& operation,
    const std::string& resource,
    RequestHandler handler) {
    
    // Validate token format
    if (!SecurityUtils::is_safe_string(token)) {
        LOG_WARN_SAFE("Invalid token format for operation: {}", operation);
        return false;
    }
    
    // Authenticate user
    AuthContext ctx = SecurityUtils::authenticate_user(token);
    if (!ctx.authenticated) {
        LOG_WARN_SAFE("Authentication failed for operation: {}", operation);
        return false;
    }
    
    // Check rate limiting
    if (!check_rate_limit(ctx.user_id)) {
        LOG_WARN_SAFE("Rate limit exceeded for user: {}", ctx.user_id);
        return false;
    }
    
    // Check authorization
    if (!SecurityUtils::is_authorized_for_operation(ctx, operation)) {
        LOG_WARN_SAFE("Authorization failed for user {} operation: {}", ctx.user_id, operation);
        return false;
    }
    
    // Execute handler with authenticated context
    try {
        return handler(ctx, resource);
    } catch (const std::exception& e) {
        LOG_ERROR_SAFE("Handler execution failed: {}", e.what());
        return false;
    }
}

AuthContext AuthMiddleware::validate_session(const std::string& token) {
    if (!SecurityUtils::is_safe_string(token)) {
        return {}; // Invalid token format
    }
    
    return SecurityUtils::authenticate_user(token);
}

bool AuthMiddleware::is_operation_allowed(const AuthContext& ctx, const std::string& operation) {
    return SecurityUtils::is_authorized_for_operation(ctx, operation);
}

bool AuthMiddleware::check_rate_limit(const std::string& user_id) {
    auto now = std::chrono::steady_clock::now();
    auto it = rate_limits_.find(user_id);
    
    if (it == rate_limits_.end()) {
        rate_limits_[user_id] = now;
        return true;
    }
    
    if (now - it->second < rate_limit_window_) {
        return false; // Rate limited
    }
    
    it->second = now;
    return true;
}

AuthGuard::AuthGuard(const std::string& token, const std::string& operation)
    : authorized_(false) {
    
    context_ = AuthMiddleware::validate_session(token);
    if (context_.authenticated) {
        authorized_ = AuthMiddleware::is_operation_allowed(context_, operation);
    }
    
    if (!authorized_) {
        LOG_WARN_SAFE("Authorization failed for operation: {}", operation);
    }
}

} // namespace rtes