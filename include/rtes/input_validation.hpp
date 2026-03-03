#pragma once

#include "rtes/protocol.hpp"
#include "rtes/config.hpp"
#include "rtes/error_handling.hpp"
#include <string>
#include <regex>
#include <unordered_set>
#include <functional>

namespace rtes {

// Validation error codes
enum class ValidationError {
    INVALID_MESSAGE_TYPE = 6000,
    INVALID_MESSAGE_SIZE,
    INVALID_FIELD_VALUE,
    INVALID_FIELD_FORMAT,
    INVALID_FIELD_RANGE,
    MISSING_REQUIRED_FIELD,
    INVALID_CONFIGURATION,
    SCHEMA_VIOLATION
};

// Field validation rules
struct ValidationRule {
    std::function<bool(const std::string&)> validator;
    std::string error_message;
    
    ValidationRule(std::function<bool(const std::string&)> v, std::string msg)
        : validator(std::move(v)), error_message(std::move(msg)) {}
};

// Message validator for network protocols
class MessageValidator {
public:
    static Result<void> validate_message_header(const MessageHeader& header);
    static Result<void> validate_message_payload(const void* payload, size_t length, MessageType type);
    static Result<void> sanitize_message_fields(NewOrderMessage& message);
    static Result<void> sanitize_message_fields(CancelOrderMessage& message);
    static Result<void> sanitize_message_fields(OrderAckMessage& message);
    
    // Message type validation
    static bool is_valid_message_type(uint32_t type);
    static bool is_valid_message_size(uint32_t size, MessageType type);
    
private:
    static constexpr uint32_t MAX_MESSAGE_SIZE = 8192;
    static constexpr uint32_t MIN_MESSAGE_SIZE = sizeof(MessageHeader);
    static const std::unordered_set<uint32_t> VALID_MESSAGE_TYPES;
};

// Configuration validator
class ConfigurationValidator {
public:
    static Result<void> validate_config_schema(const Config& config);
    static Result<void> validate_config_values(const Config& config);
    static Result<void> validate_config_dependencies(const Config& config);
    static Result<void> validate_full_config(const Config& config);
    
private:
    static Result<void> validate_exchange_config(const ExchangeConfig& config);
    static Result<void> validate_risk_config(const RiskConfig& config);
    static Result<void> validate_performance_config(const PerformanceConfig& config);
    static Result<void> validate_symbol_configs(const std::vector<SymbolConfig>& symbols);
};

// Field validators for specific data types
class FieldValidators {
public:
    // Numeric validators
    static ValidationRule range_validator(double min, double max);
    static ValidationRule positive_validator();
    static ValidationRule non_zero_validator();
    
    // String validators
    static ValidationRule length_validator(size_t min_len, size_t max_len);
    static ValidationRule regex_validator(const std::string& pattern);
    static ValidationRule alphanumeric_validator();
    static ValidationRule symbol_validator();
    
    // Enum validators
    static ValidationRule enum_validator(const std::unordered_set<int>& valid_values);
    
    // Cross-field validators
    static bool validate_price_quantity_relationship(Price price, Quantity quantity);
    static bool validate_port_range(uint16_t port);
    static bool validate_percentage(double value);
    
    // Sanitization functions
    static std::string sanitize_string(const std::string& input, size_t max_length);
    static std::string sanitize_symbol(const std::string& symbol);
    static std::string sanitize_client_id(const std::string& client_id);
};

// Validation chain for early rejection
class ValidationChain {
public:
    ValidationChain& add_rule(const std::string& field_name, const ValidationRule& rule);
    ValidationChain& add_custom_validator(std::function<Result<void>()> validator);
    
    Result<void> validate(const std::unordered_map<std::string, std::string>& fields);
    Result<void> validate_early_reject(); // Fast path validation
    
    void clear() { rules_.clear(); custom_validators_.clear(); }
    
private:
    std::unordered_map<std::string, std::vector<ValidationRule>> rules_;
    std::vector<std::function<Result<void>()>> custom_validators_;
};

// Input sanitizer for all external inputs
class InputSanitizer {
public:
    static std::string sanitize_network_input(const std::string& input);
    static std::string sanitize_config_value(const std::string& value);
    static std::string sanitize_log_input(const std::string& input);
    
    // Remove dangerous characters
    static std::string remove_control_chars(const std::string& input);
    static std::string escape_special_chars(const std::string& input);
    static std::string normalize_whitespace(const std::string& input);
    
private:
    static const std::unordered_set<char> DANGEROUS_CHARS;
    static const std::unordered_set<char> CONTROL_CHARS;
};

} // namespace rtes