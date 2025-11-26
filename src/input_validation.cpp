#include "rtes/input_validation.hpp"
#include "rtes/logger.hpp"
#include "rtes/thread_safety.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace rtes {

// MessageValidator implementation
const std::unordered_set<uint32_t> MessageValidator::VALID_MESSAGE_TYPES = {
    NEW_ORDER, CANCEL_ORDER, ORDER_ACK, TRADE_REPORT, HEARTBEAT
};

Result<void> MessageValidator::validate_message_header(const MessageHeader& header) {
    // Validate message type
    if (!is_valid_message_type(header.type)) {
        return make_error_code(ValidationError::INVALID_MESSAGE_TYPE);
    }
    
    // Validate message size
    if (!is_valid_message_size(header.length, static_cast<MessageType>(header.type))) {
        return make_error_code(ValidationError::INVALID_MESSAGE_SIZE);
    }
    
    // Validate sequence number (should be non-zero)
    if (header.sequence == 0) {
        return make_error_code(ValidationError::INVALID_FIELD_VALUE);
    }
    
    return Result<void>();
}

Result<void> MessageValidator::validate_message_payload(const void* payload, size_t length, MessageType type) {
    if (!payload && length > 0) {
        return make_error_code(ValidationError::INVALID_FIELD_VALUE);
    }
    
    size_t expected_size = 0;
    switch (type) {
        case NEW_ORDER: expected_size = sizeof(NewOrderMessage) - sizeof(MessageHeader); break;
        case CANCEL_ORDER: expected_size = sizeof(CancelOrderMessage) - sizeof(MessageHeader); break;
        case ORDER_ACK: expected_size = sizeof(OrderAckMessage) - sizeof(MessageHeader); break;
        case TRADE_REPORT: expected_size = sizeof(TradeMessage) - sizeof(MessageHeader); break;
        case HEARTBEAT: expected_size = sizeof(HeartbeatMessage) - sizeof(MessageHeader); break;
        default: 
            LOG_ERROR("Invalid message type: {}", static_cast<uint32_t>(type));
            return make_error_code(ValidationError::INVALID_MESSAGE_TYPE);
    }
    
    if (length != expected_size) {
        return make_error_code(ValidationError::INVALID_MESSAGE_SIZE);
    }
    
    return Result<void>();
}

Result<void> MessageValidator::sanitize_message_fields(NewOrderMessage& message) {
    // Validate order ID (non-zero)
    if (message.order_id == 0) {
        return make_error_code(ValidationError::INVALID_FIELD_VALUE);
    }
    
    // Sanitize and validate symbol
    std::string symbol_str = message.symbol.c_str();
    symbol_str = FieldValidators::sanitize_symbol(symbol_str);
    if (symbol_str.empty() || symbol_str.length() > 7) {
        return make_error_code(ValidationError::INVALID_FIELD_FORMAT);
    }
    message.symbol.assign(symbol_str);
    
    // Validate side
    if (message.side != static_cast<uint8_t>(Side::BUY) && 
        message.side != static_cast<uint8_t>(Side::SELL)) {
        return make_error_code(ValidationError::INVALID_FIELD_VALUE);
    }
    
    // Validate quantity (positive)
    if (message.quantity == 0 || message.quantity > 1000000) {
        return make_error_code(ValidationError::INVALID_FIELD_RANGE);
    }
    
    // Validate price (positive for limit orders)
    if (message.order_type == static_cast<uint8_t>(OrderType::LIMIT) && message.price == 0) {
        return make_error_code(ValidationError::INVALID_FIELD_VALUE);
    }
    
    // Validate order type
    if (message.order_type != static_cast<uint8_t>(OrderType::MARKET) &&
        message.order_type != static_cast<uint8_t>(OrderType::LIMIT)) {
        return make_error_code(ValidationError::INVALID_FIELD_VALUE);
    }
    
    return Result<void>();
}

Result<void> MessageValidator::sanitize_message_fields(CancelOrderMessage& message) {
    if (message.order_id == 0) {
        return make_error_code(ValidationError::INVALID_FIELD_VALUE);
    }
    
    std::string symbol_str = message.symbol.c_str();
    symbol_str = FieldValidators::sanitize_symbol(symbol_str);
    if (symbol_str.empty()) {
        return make_error_code(ValidationError::INVALID_FIELD_FORMAT);
    }
    message.symbol.assign(symbol_str);
    
    return Result<void>();
}

Result<void> MessageValidator::sanitize_message_fields(OrderAckMessage& message) {
    if (message.order_id == 0) {
        return make_error_code(ValidationError::INVALID_FIELD_VALUE);
    }
    
    if (message.status == 0 || message.status > 2) {
        return make_error_code(ValidationError::INVALID_FIELD_VALUE);
    }
    
    std::string reason_str = message.reason.c_str();
    reason_str = InputSanitizer::sanitize_network_input(reason_str);
    message.reason.assign(reason_str);
    
    return Result<void>();
}

bool MessageValidator::is_valid_message_type(uint32_t type) {
    return VALID_MESSAGE_TYPES.contains(type);
}

bool MessageValidator::is_valid_message_size(uint32_t size, MessageType type) {
    if (size < MIN_MESSAGE_SIZE || size > MAX_MESSAGE_SIZE) {
        return false;
    }
    
    size_t expected_size = 0;
    switch (type) {
        case NEW_ORDER: expected_size = sizeof(NewOrderMessage); break;
        case CANCEL_ORDER: expected_size = sizeof(CancelOrderMessage); break;
        case ORDER_ACK: expected_size = sizeof(OrderAckMessage); break;
        case TRADE_REPORT: expected_size = sizeof(TradeMessage); break;
        case HEARTBEAT: expected_size = sizeof(HeartbeatMessage); break;
        default: 
            LOG_ERROR("Unknown message type in size validation: {}", static_cast<uint32_t>(type));
            return false;
    }
    
    return size == expected_size;
}

// ConfigurationValidator implementation
Result<void> ConfigurationValidator::validate_full_config(const Config& config) {
    auto schema_result = validate_config_schema(config);
    if (schema_result.has_error()) return schema_result;
    
    auto values_result = validate_config_values(config);
    if (values_result.has_error()) return values_result;
    
    return validate_config_dependencies(config);
}

Result<void> ConfigurationValidator::validate_config_schema(const Config& config) {
    // Validate required sections exist
    if (config.exchange.name.empty()) {
        return make_error_code(ValidationError::MISSING_REQUIRED_FIELD);
    }
    
    if (config.symbols.empty()) {
        return make_error_code(ValidationError::MISSING_REQUIRED_FIELD);
    }
    
    return Result<void>();
}

Result<void> ConfigurationValidator::validate_config_values(const Config& config) {
    auto exchange_result = validate_exchange_config(config.exchange);
    if (exchange_result.has_error()) return exchange_result;
    
    auto risk_result = validate_risk_config(config.risk);
    if (risk_result.has_error()) return risk_result;
    
    auto perf_result = validate_performance_config(config.performance);
    if (perf_result.has_error()) return perf_result;
    
    return validate_symbol_configs(config.symbols);
}

Result<void> ConfigurationValidator::validate_config_dependencies(const Config& config) {
    // Validate port conflicts
    if (config.exchange.tcp_port == config.exchange.udp_port ||
        config.exchange.tcp_port == config.exchange.metrics_port ||
        config.exchange.udp_port == config.exchange.metrics_port) {
        return make_error_code(ValidationError::INVALID_CONFIGURATION);
    }
    
    // Validate performance vs risk settings
    if (config.performance.order_pool_size < config.risk.max_orders_per_second * 10) {
        LOG_WARN("Order pool size may be too small for configured order rate");
    }
    
    return Result<void>();
}

Result<void> ConfigurationValidator::validate_exchange_config(const ExchangeConfig& config) {
    if (!FieldValidators::validate_port_range(config.tcp_port) ||
        !FieldValidators::validate_port_range(config.udp_port) ||
        !FieldValidators::validate_port_range(config.metrics_port)) {
        return make_error_code(ValidationError::INVALID_FIELD_RANGE);
    }
    
    // Validate multicast group format (basic check)
    if (config.udp_multicast_group.empty() || 
        config.udp_multicast_group.find('.') == std::string::npos) {
        return make_error_code(ValidationError::INVALID_FIELD_FORMAT);
    }
    
    return Result<void>();
}

Result<void> ConfigurationValidator::validate_risk_config(const RiskConfig& config) {
    if (config.max_order_size == 0 || config.max_order_size > 10000000) {
        return make_error_code(ValidationError::INVALID_FIELD_RANGE);
    }
    
    if (config.max_notional_per_client <= 0) {
        return make_error_code(ValidationError::INVALID_FIELD_VALUE);
    }
    
    if (config.max_orders_per_second == 0 || config.max_orders_per_second > 100000) {
        return make_error_code(ValidationError::INVALID_FIELD_RANGE);
    }
    
    return Result<void>();
}

Result<void> ConfigurationValidator::validate_performance_config(const PerformanceConfig& config) {
    if (config.order_pool_size == 0 || config.order_pool_size > 10000000) {
        return make_error_code(ValidationError::INVALID_FIELD_RANGE);
    }
    
    if (config.queue_capacity == 0 || config.queue_capacity > 1000000) {
        return make_error_code(ValidationError::INVALID_FIELD_RANGE);
    }
    
    return Result<void>();
}

Result<void> ConfigurationValidator::validate_symbol_configs(const std::vector<SymbolConfig>& symbols) {
    for (const auto& symbol : symbols) {
        std::string clean_symbol = FieldValidators::sanitize_symbol(symbol.name);
        if (clean_symbol.empty() || clean_symbol.length() > 8) {
            return make_error_code(ValidationError::INVALID_FIELD_FORMAT);
        }
        
        if (symbol.tick_size <= 0 || symbol.lot_size <= 0) {
            return make_error_code(ValidationError::INVALID_FIELD_VALUE);
        }
    }
    
    return Result<void>();
}

// FieldValidators implementation
bool FieldValidators::validate_port_range(uint16_t port) {
    return port >= 1024 && port <= 65535;
}

std::string FieldValidators::sanitize_symbol(const std::string& symbol) {
    std::string result;
    result.reserve(symbol.length());
    
    for (char c : symbol) {
        if (std::isalnum(c) || c == '.' || c == '-') {
            result += std::toupper(c);
        }
    }
    
    return result;
}

bool FieldValidators::validate_price(double price) {
    return price > 0.0 && price < 1000000.0 && std::isfinite(price);
}

bool FieldValidators::validate_quantity(uint64_t quantity) {
    return quantity > 0 && quantity <= 10000000;
}

// ValidationChain implementation
ValidationChain& ValidationChain::add_validator(std::function<Result<void>()> validator) {
    validators_.push_back(std::move(validator));
    return *this;
}

Result<void> ValidationChain::validate() {
    for (const auto& validator : validators_) {
        auto result = validator();
        if (result.has_error()) {
            return result;
        }
    }
    return Result<void>();capacity > 1000000) {
        return make_error_code(ValidationError::INVALID_FIELD_RANGE);
    }
    
    if (config.udp_buffer_size < 1024 || config.udp_buffer_size > 1048576) {
        return make_error_code(ValidationError::INVALID_FIELD_RANGE);
    }
    
    return Result<void>();
}

Result<void> ConfigurationValidator::validate_symbol_configs(const std::vector<SymbolConfig>& symbols) {
    for (const auto& symbol : symbols) {
        std::string clean_symbol = FieldValidators::sanitize_symbol(symbol.name);
        if (clean_symbol.empty() || clean_symbol.length() > 8) {
            return make_error_code(ValidationError::INVALID_FIELD_FORMAT);
        }
        
        if (symbol.tick_size <= 0 || symbol.lot_size <= 0) {
            return make_error_code(ValidationError::INVALID_FIELD_VALUE);
        }
    }
    
    return Result<void>();
}

// FieldValidators implementation
bool FieldValidators::validate_port_range(uint16_t port) {
    return port >= 1024 && port <= 65535;
}

std::string FieldValidators::sanitize_symbol(const std::string& symbol) {
    std::string result;
    result.reserve(symbol.length());
    
    for (char c : symbol) {
        if (std::isalnum(c) || c == '.' || c == '-') {
            result += std::toupper(c);
        }
    }
    
    return result;
}

bool FieldValidators::validate_price(double price) {
    return price > 0.0 && price < 1000000.0 && std::isfinite(price);
}

bool FieldValidators::validate_quantity(uint64_t quantity) {
    return quantity > 0 && quantity <= 10000000;
}

// ValidationChain implementation
ValidationChain& ValidationChain::add_validator(std::function<Result<void>()> validator) {
    validators_.push_back(std::move(validator));
    return *this;
}

Result<void> ValidationChain::validate() {
    for (const auto& validator : validators_) {
        auto result = validator();
        if (result.has_error()) {
            return result;
        }
    }
    return Result<void>();LD_RANGE);
    }
    
    return Result<void>();
}

Result<void> ConfigurationValidator::validate_symbol_configs(const std::vector<SymbolConfig>& symbols) {
    std::unordered_set<std::string> seen_symbols;
    
    for (const auto& symbol : symbols) {
        if (symbol.symbol.empty() || symbol.symbol.length() > 8) {
            return make_error_code(ValidationError::INVALID_FIELD_FORMAT);
        }
        
        if (seen_symbols.contains(symbol.symbol)) {
            return make_error_code(ValidationError::INVALID_CONFIGURATION);
        }
        seen_symbols.insert(symbol.symbol);
        
        if (symbol.tick_size <= 0 || symbol.lot_size == 0) {
            return make_error_code(ValidationError::INVALID_FIELD_VALUE);
        }
        
        if (!FieldValidators::validate_percentage(symbol.price_collar_pct)) {
            return make_error_code(ValidationError::INVALID_FIELD_RANGE);
        }
    }
    
    return Result<void>();
}

// FieldValidators implementation
ValidationRule FieldValidators::range_validator(double min, double max) {
    return ValidationRule(
        [min, max](const std::string& value) {
            try {
                double val = std::stod(value);
                return val >= min && val <= max;
            } catch (...) {
                return false;
            }
        },
        "Value must be between " + std::to_string(min) + " and " + std::to_string(max)
    );
}

ValidationRule FieldValidators::positive_validator() {
    return ValidationRule(
        [](const std::string& value) {
            try {
                return std::stod(value) > 0;
            } catch (...) {
                return false;
            }
        },
        "Value must be positive"
    );
}

ValidationRule FieldValidators::length_validator(size_t min_len, size_t max_len) {
    return ValidationRule(
        [min_len, max_len](const std::string& value) {
            return value.length() >= min_len && value.length() <= max_len;
        },
        "Length must be between " + std::to_string(min_len) + " and " + std::to_string(max_len)
    );
}

ValidationRule FieldValidators::symbol_validator() {
    return ValidationRule(
        [](const std::string& value) {
            if (value.empty() || value.length() > 8) return false;
            return std::all_of(value.begin(), value.end(), 
                             [](char c) { return std::isalnum(c) && std::isupper(c); });
        },
        "Symbol must be 1-8 uppercase alphanumeric characters"
    );
}

bool FieldValidators::validate_port_range(uint16_t port) {
    return port >= 1024 && port <= 65535;
}

bool FieldValidators::validate_percentage(double value) {
    return value >= 0.0 && value <= 100.0;
}

std::string FieldValidators::sanitize_symbol(const std::string& symbol) {
    std::string result;
    result.reserve(8);
    
    for (char c : symbol) {
        if (std::isalnum(c)) {
            result += std::toupper(c);
        }
        if (result.length() >= 8) break;
    }
    
    return result;
}

std::string FieldValidators::sanitize_client_id(const std::string& client_id) {
    std::string result;
    result.reserve(32);
    
    for (char c : client_id) {
        if (std::isalnum(c) || c == '_' || c == '-') {
            result += c;
        }
        if (result.length() >= 32) break;
    }
    
    return result;
}

// ValidationChain implementation
ValidationChain& ValidationChain::add_rule(const std::string& field_name, const ValidationRule& rule) {
    rules_[field_name].push_back(rule);
    return *this;
}

ValidationChain& ValidationChain::add_custom_validator(std::function<Result<void>()> validator) {
    custom_validators_.push_back(std::move(validator));
    return *this;
}

Result<void> ValidationChain::validate(const std::unordered_map<std::string, std::string>& fields) {
    // Validate individual fields
    for (const auto& [field_name, field_rules] : rules_) {
        auto field_it = fields.find(field_name);
        if (field_it == fields.end()) {
            return make_error_code(ValidationError::MISSING_REQUIRED_FIELD);
        }
        
        for (const auto& rule : field_rules) {
            if (!rule.validator(field_it->second)) {
                LOG_ERROR_SAFE("Validation failed for field {}: {}", field_name, rule.error_message);
                return make_error_code(ValidationError::INVALID_FIELD_VALUE);
            }
        }
    }
    
    // Run custom validators
    for (const auto& validator : custom_validators_) {
        auto result = validator();
        if (result.has_error()) {
            return result;
        }
    }
    
    return Result<void>();
}

// InputSanitizer implementation
const std::unordered_set<char> InputSanitizer::DANGEROUS_CHARS = {
    ';', '|', '&', '$', '`', '(', ')', '{', '}', '[', ']', '<', '>', '"', '\''
};

const std::unordered_set<char> InputSanitizer::CONTROL_CHARS = {
    '\x00', '\x01', '\x02', '\x03', '\x04', '\x05', '\x06', '\x07',
    '\x08', '\x0B', '\x0C', '\x0E', '\x0F', '\x10', '\x11', '\x12',
    '\x13', '\x14', '\x15', '\x16', '\x17', '\x18', '\x19', '\x1A',
    '\x1B', '\x1C', '\x1D', '\x1E', '\x1F', '\x7F'
};

std::string InputSanitizer::sanitize_network_input(const std::string& input) {
    if (input.empty() || input.size() > 8192) {
        return "";
    }
    std::string result = remove_control_chars(input);
    result = escape_special_chars(result);
    return normalize_whitespace(result);
}

std::string InputSanitizer::remove_control_chars(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    
    for (char c : input) {
        if (!CONTROL_CHARS.contains(c) && (c >= 0x20 || c == '\t' || c == '\n' || c == '\r')) {
            result += c;
        }
    }
    
    return result;
}

std::string InputSanitizer::normalize_whitespace(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    
    bool prev_space = false;
    for (char c : input) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!prev_space) {
                result += ' ';
                prev_space = true;
            }
        } else {
            result += c;
            prev_space = false;
        }
    }
    
    // Trim leading space
    if (!result.empty() && result.front() == ' ') {
        result.erase(0, 1);
    }
    
    // Trim trailing space
    if (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    
    return result;
}

} // namespace rtes

// Make ValidationError compatible with std::error_code
namespace std {
template<>
struct is_error_code_enum<rtes::ValidationError> : true_type {};
}

namespace rtes {
std::error_code make_error_code(ValidationError ve) {
    static class : public std::error_category {
    public:
        const char* name() const noexcept override { return "validation"; }
        std::string message(int ev) const override {
            switch (static_cast<ValidationError>(ev)) {
                case ValidationError::INVALID_MESSAGE_TYPE: return "Invalid message type";
                case ValidationError::INVALID_MESSAGE_SIZE: return "Invalid message size";
                case ValidationError::INVALID_FIELD_VALUE: return "Invalid field value";
                case ValidationError::INVALID_FIELD_FORMAT: return "Invalid field format";
                case ValidationError::INVALID_FIELD_RANGE: return "Field value out of range";
                case ValidationError::MISSING_REQUIRED_FIELD: return "Missing required field";
                case ValidationError::INVALID_CONFIGURATION: return "Invalid configuration";
                case ValidationError::SCHEMA_VIOLATION: return "Schema validation failed";
                default: return "Unknown validation error";
            }
        }
    } category;
    
    return {static_cast<int>(ve), category};
}
}