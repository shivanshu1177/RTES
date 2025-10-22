#include "rtes/security_utils.hpp"
#include <algorithm>
#include <cctype>
#include <regex>

namespace rtes {

const std::unordered_set<std::string> SecurityUtils::ALLOWED_CONFIG_DIRS = {
    "/etc/rtes",
    "./configs",
    "../configs"
};

const std::unordered_set<char> SecurityUtils::CONTROL_CHARS = {
    '\x00', '\x01', '\x02', '\x03', '\x04', '\x05', '\x06', '\x07',
    '\x08', '\x0B', '\x0C', '\x0E', '\x0F', '\x10', '\x11', '\x12',
    '\x13', '\x14', '\x15', '\x16', '\x17', '\x18', '\x19', '\x1A',
    '\x1B', '\x1C', '\x1D', '\x1E', '\x1F', '\x7F'
};

std::string SecurityUtils::sanitize_log_input(std::string_view input) {
    std::string sanitized;
    sanitized.reserve(input.size() * 2);
    
    for (char c : input) {
        if (CONTROL_CHARS.contains(c)) {
            continue; // Remove control characters
        }
        
        switch (c) {
            case '\n': sanitized += "\\n"; break;
            case '\r': sanitized += "\\r"; break;
            case '\t': sanitized += "\\t"; break;
            case '\\': sanitized += "\\\\"; break;
            case '"':  sanitized += "\\\""; break;
            default:   sanitized += c; break;
        }
    }
    
    return sanitized;
}

bool SecurityUtils::validate_file_path(std::string_view path, std::string_view base_dir) {
    try {
        std::filesystem::path file_path(path);
        std::filesystem::path base_path(base_dir);
        
        // Resolve to canonical paths
        auto canonical_file = std::filesystem::weakly_canonical(file_path);
        auto canonical_base = std::filesystem::canonical(base_path);
        
        // Check if file is within base directory
        auto relative = std::filesystem::relative(canonical_file, canonical_base);
        if (relative.empty() || relative.string().starts_with("..")) {
            return false;
        }
        
        // Check against allowed directories
        std::string path_str = canonical_file.string();
        for (const auto& allowed : ALLOWED_CONFIG_DIRS) {
            if (path_str.starts_with(allowed)) {
                return true;
            }
        }
        
        return false;
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

std::string SecurityUtils::normalize_path(std::string_view path) {
    try {
        return std::filesystem::weakly_canonical(path).string();
    } catch (const std::filesystem::filesystem_error&) {
        return "";
    }
}

bool SecurityUtils::check_permission(const AuthContext& ctx, const std::string& permission) {
    if (!ctx.authenticated) return false;
    
    // Admin has all permissions
    if (ctx.role == UserRole::ADMIN) return true;
    
    return ctx.permissions.contains(permission);
}

bool SecurityUtils::is_authorized_for_operation(const AuthContext& ctx, const std::string& operation) {
    if (!ctx.authenticated) return false;
    
    static const std::unordered_set<std::string> ADMIN_OPS = {
        "shutdown", "config_reload", "user_management"
    };
    
    static const std::unordered_set<std::string> TRADER_OPS = {
        "place_order", "cancel_order", "view_positions"
    };
    
    if (ADMIN_OPS.contains(operation)) {
        return ctx.role == UserRole::ADMIN;
    }
    
    if (TRADER_OPS.contains(operation)) {
        return ctx.role == UserRole::ADMIN || ctx.role == UserRole::TRADER;
    }
    
    return ctx.role != UserRole::VIEWER; // Viewers can only read
}

AuthContext SecurityUtils::authenticate_user(std::string_view token) {
    AuthContext ctx;
    
    // Basic token validation (implement proper JWT/OAuth in production)
    if (token.empty() || token.size() < 10) {
        return ctx; // Not authenticated
    }
    
    // Mock authentication - replace with real implementation
    if (token == "admin_token_12345") {
        ctx.user_id = "admin";
        ctx.role = UserRole::ADMIN;
        ctx.authenticated = true;
        ctx.permissions = {"all"};
    } else if (token.starts_with("trader_")) {
        ctx.user_id = std::string(token.substr(7));
        ctx.role = UserRole::TRADER;
        ctx.authenticated = true;
        ctx.permissions = {"place_order", "cancel_order", "view_positions"};
    }
    
    return ctx;
}

bool SecurityUtils::is_valid_symbol(std::string_view symbol) {
    if (symbol.empty() || symbol.size() > 10) return false;
    
    static const std::regex symbol_pattern("^[A-Z][A-Z0-9]*$");
    return std::regex_match(symbol.begin(), symbol.end(), symbol_pattern);
}

bool SecurityUtils::is_valid_order_id(std::string_view order_id) {
    if (order_id.empty() || order_id.size() > 20) return false;
    
    return std::all_of(order_id.begin(), order_id.end(), 
                      [](char c) { return std::isalnum(c) || c == '-' || c == '_'; });
}

bool SecurityUtils::is_safe_string(std::string_view input) {
    return std::none_of(input.begin(), input.end(), 
                       [](char c) { return CONTROL_CHARS.contains(c); });
}

} // namespace rtes