#pragma once

#include <string>
#include <string_view>
#include <filesystem>
#include <format>
#include <unordered_set>

namespace rtes {

enum class UserRole {
    ADMIN,
    TRADER,
    VIEWER,
    SYSTEM
};

struct AuthContext {
    std::string user_id;
    UserRole role;
    std::unordered_set<std::string> permissions;
    bool authenticated = false;
};

class SecurityUtils {
public:
    // Log input sanitization (CWE-117)
    static std::string sanitize_log_input(std::string_view input);
    
    // Path traversal prevention (CWE-22/23/24)
    static bool validate_file_path(std::string_view path, std::string_view base_dir);
    static std::string normalize_path(std::string_view path);
    
    // Type-safe formatting (CWE-134)
    template<typename... Args>
    static std::string safe_format(std::string_view format_str, Args&&... args) {
        try {
            return std::vformat(format_str, std::make_format_args(args...));
        } catch (const std::format_error& e) {
            return std::string("[FORMAT_ERROR: ") + e.what() + "]";
        }
    }
    
    // Authorization checks (CWE-862)
    static bool check_permission(const AuthContext& ctx, const std::string& permission);
    static bool is_authorized_for_operation(const AuthContext& ctx, const std::string& operation);
    static AuthContext authenticate_user(std::string_view token);
    
    // Input validation
    static bool is_valid_symbol(std::string_view symbol);
    static bool is_valid_order_id(std::string_view order_id);
    static bool is_safe_string(std::string_view input);

private:
    static const std::unordered_set<std::string> ALLOWED_CONFIG_DIRS;
    static const std::unordered_set<char> CONTROL_CHARS;
};

} // namespace rtes