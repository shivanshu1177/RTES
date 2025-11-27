/**
 * @file security_utils.cpp
 * @brief Security utilities for input validation and authorization
 * 
 * Provides:
 * - Log injection prevention (CWE-117)
 * - Path traversal protection (CWE-22/23/24)
 * - Input sanitization
 * - Authentication and authorization
 * - Symbol and order ID validation
 * 
 * Security principles:
 * - Fail-safe defaults (deny by default)
 * - Input validation (whitelist approach)
 * - Output encoding (escape special chars)
 * - Least privilege (role-based access)
 */

#include "rtes/security_utils.hpp"
#include "rtes/logger.hpp"
#include <algorithm>
#include <cctype>
#include <regex>

namespace rtes {

// Allowed configuration directories (whitelist)
const std::unordered_set<std::string> SecurityUtils::ALLOWED_CONFIG_DIRS = {
    "/etc/rtes",
    "./configs",
    "../configs"
};

// Control characters to filter (security: prevent injection)
const std::unordered_set<char> SecurityUtils::CONTROL_CHARS = {
    '\x00', '\x01', '\x02', '\x03', '\x04', '\x05', '\x06', '\x07',
    '\x08', '\x0B', '\x0C', '\x0E', '\x0F', '\x10', '\x11', '\x12',
    '\x13', '\x14', '\x15', '\x16', '\x17', '\x18', '\x19', '\x1A',
    '\x1B', '\x1C', '\x1D', '\x1E', '\x1F', '\x7F'
};

/**
 * @brief Sanitize input for logging (prevent log injection)
 * @param input Raw input string
 * @return Sanitized string safe for logging
 * 
 * Security (CWE-117 - Log Injection):
 * - Removes control characters
 * - Escapes newlines, carriage returns, tabs
 * - Escapes backslashes and quotes
 * 
 * Prevents attackers from:
 * - Injecting fake log entries
 * - Breaking log parsers
 * - Hiding malicious activity
 */
std::string SecurityUtils::sanitize_log_input(std::string_view input) {
    std::string sanitized;
    sanitized.reserve(input.size() * 2);  // Reserve space for escaping
    
    for (char c : input) {
        // Remove control characters (security)
        if (CONTROL_CHARS.contains(c)) {
            continue;
        }
        
        // Escape special characters
        switch (c) {
            case '\n': sanitized += "\\n"; break;   // Escape newline
            case '\r': sanitized += "\\r"; break;   // Escape carriage return
            case '\t': sanitized += "\\t"; break;   // Escape tab
            case '\\': sanitized += "\\\\"; break; // Escape backslash
            case '"':  sanitized += "\\\""; break; // Escape quote
            default:   sanitized += c; break;
        }
    }
    
    return sanitized;
}

/**
 * @brief Validate file path (prevent path traversal)
 * @param path File path to validate
 * @param base_dir Base directory (must be within this)
 * @return true if path is safe
 * 
 * Security (CWE-22/23/24 - Path Traversal):
 * - Resolves to canonical paths (removes "..", symlinks)
 * - Checks if file within base directory
 * - Validates against whitelist of allowed directories
 * 
 * Prevents attackers from:
 * - Reading arbitrary files (../../etc/passwd)
 * - Writing to system directories
 * - Following symlinks outside allowed paths
 */
bool SecurityUtils::validate_file_path(std::string_view path, std::string_view base_dir) {
    try {
        std::filesystem::path file_path(path);
        std::filesystem::path base_path(base_dir);
        
        // Security: Resolve to canonical paths (removes "..", symlinks)
        auto canonical_file = std::filesystem::weakly_canonical(file_path);
        auto canonical_base = std::filesystem::canonical(base_path);
        
        // Security: Check if file is within base directory
        auto relative = std::filesystem::relative(canonical_file, canonical_base);
        if (relative.empty() || relative.string().starts_with("..")) {
            return false;  // Path escapes base directory
        }
        
        // Security: Check against whitelist of allowed directories
        std::string path_str = canonical_file.string();
        for (const auto& allowed : ALLOWED_CONFIG_DIRS) {
            if (path_str.starts_with(allowed)) {
                return true;  // Path in allowed directory
            }
        }
        
        return false;  // Path not in whitelist
    } catch (const std::filesystem::filesystem_error&) {
        return false;  // Fail-safe: deny on error
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

/**
 * @brief Authenticate user from token
 * @param token Authentication token
 * @return AuthContext with user info and permissions
 * 
 * SECURITY WARNING: This is a MOCK implementation for development only!
 * 
 * For production, implement:
 * 1. JWT signature validation using public key
 * 2. Token expiration checking
 * 3. Issuer and audience claim verification
 * 4. User database lookup for permissions
 * 5. Token revocation checking
 * 6. Rate limiting on authentication attempts
 * 
 * Current implementation:
 * - Validates token format (length, characters)
 * - Requires RTES_AUTH_MODE=development
 * - Uses prefix-based mock authentication
 * - NOT SECURE - DO NOT USE IN PRODUCTION
 */
AuthContext SecurityUtils::authenticate_user(std::string_view token) {
    AuthContext ctx;
    
    // Validate token format and length (basic security)
    if (token.empty() || token.size() < 32 || token.size() > 512) {
        LOG_INFO("Invalid token length");
        return ctx; // Not authenticated
    }
    
    // Validate token contains only safe characters (prevent injection)
    if (!is_safe_string(token)) {
        LOG_INFO("Token contains invalid characters");
        return ctx;
    }
    
    // TODO: Implement proper JWT/OAuth validation
    // This is a placeholder - MUST be replaced with secure authentication
    // For production:
    // 1. Validate JWT signature using public key
    // 2. Check token expiration
    // 3. Verify issuer and audience claims
    // 4. Query user database for permissions
    
    LOG_INFO("Using mock authentication - NOT FOR PRODUCTION USE");
    
    // Mock implementation for development only
    const char* auth_mode = std::getenv("RTES_AUTH_MODE");
    if (!auth_mode || std::string(auth_mode) != "development") {
        LOG_ERROR("Mock authentication disabled in non-development mode");
        return ctx;
    }
    
    // Development-only mock authentication
    if (token.starts_with("dev_admin_")) {
        ctx.user_id = std::string(token.substr(10, 16));
        ctx.role = UserRole::ADMIN;
        ctx.authenticated = true;
        ctx.permissions = {"all"};
    } else if (token.starts_with("dev_trader_")) {
        ctx.user_id = std::string(token.substr(11, 16));
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