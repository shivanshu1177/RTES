#pragma once

#include "security_utils.hpp"
#include <string>
#include <functional>
#include <unordered_map>

namespace rtes {

class AuthMiddleware {
public:
    using RequestHandler = std::function<bool(const AuthContext&, const std::string&)>;
    
    // Initialize with authentication requirements
    static void initialize();
    
    // Authenticate request and execute handler if authorized
    static bool authenticate_and_authorize(
        const std::string& token,
        const std::string& operation,
        const std::string& resource,
        RequestHandler handler
    );
    
    // Validate session token
    static AuthContext validate_session(const std::string& token);
    
    // Check if operation is allowed for user
    static bool is_operation_allowed(const AuthContext& ctx, const std::string& operation);
    
    // Rate limiting per user
    static bool check_rate_limit(const std::string& user_id);

private:
    static std::unordered_map<std::string, std::chrono::steady_clock::time_point> rate_limits_;
    static std::chrono::milliseconds rate_limit_window_;
};

// RAII authentication guard
class AuthGuard {
public:
    AuthGuard(const std::string& token, const std::string& operation);
    ~AuthGuard() = default;
    
    bool is_authorized() const { return authorized_; }
    const AuthContext& context() const { return context_; }

private:
    AuthContext context_;
    bool authorized_;
};

} // namespace rtes