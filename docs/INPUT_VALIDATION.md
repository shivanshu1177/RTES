# RTES Input Validation Framework

## Overview
This document describes the comprehensive input validation framework implemented in RTES to ensure all external inputs are properly validated, sanitized, and secured against malicious data and injection attacks.

## Validation Architecture

### 1. Multi-Layer Validation Strategy
**Layer 1: Format Validation**
- Message structure and size validation
- Protocol compliance checking
- Basic type and range validation

**Layer 2: Business Logic Validation**
- Field value constraints
- Cross-field relationship validation
- Business rule enforcement

**Layer 3: Security Validation**
- Input sanitization and escaping
- Injection attack prevention
- Malicious pattern detection

**Layer 4: Authorization Validation**
- Permission-based access control
- Resource ownership verification
- Operation authorization checks

### 2. Validation Components

#### MessageValidator
**Purpose**: Validates all network protocol messages for structure, content, and security.

**Key Functions**:
```cpp
// Header validation
Result<void> validate_message_header(const MessageHeader& header);

// Payload validation by message type
Result<void> validate_message_payload(const void* payload, size_t length, MessageType type);

// Field sanitization
Result<void> sanitize_message_fields(NewOrderMessage& message);
Result<void> sanitize_message_fields(CancelOrderMessage& message);
```

**Validation Rules**:
- Message type must be in valid set (NEW_ORDER, CANCEL_ORDER, etc.)
- Message size must match expected size for type
- Sequence numbers must be non-zero
- All string fields sanitized for control characters
- Numeric fields validated for range and type

#### ConfigurationValidator
**Purpose**: Ensures configuration files are valid, consistent, and secure.

**Key Functions**:
```cpp
// Schema validation
Result<void> validate_config_schema(const Config& config);

// Value validation
Result<void> validate_config_values(const Config& config);

// Dependency validation
Result<void> validate_config_dependencies(const Config& config);
```

**Validation Rules**:
- Required fields must be present
- Port numbers in valid range (1024-65535)
- No port conflicts between services
- Numeric values within business constraints
- File paths validated for security
- Cross-component consistency checks

#### FieldValidators
**Purpose**: Provides reusable validation rules for common field types.

**Available Validators**:
```cpp
// Numeric validators
ValidationRule range_validator(double min, double max);
ValidationRule positive_validator();
ValidationRule non_zero_validator();

// String validators
ValidationRule length_validator(size_t min_len, size_t max_len);
ValidationRule regex_validator(const std::string& pattern);
ValidationRule symbol_validator();

// Cross-field validators
bool validate_price_quantity_relationship(Price price, Quantity quantity);
bool validate_port_range(uint16_t port);
```

## Validation Patterns

### 1. Early Rejection Pattern
**Purpose**: Fail fast on invalid inputs to prevent resource waste.

```cpp
void process_message_safe(ClientConnection* conn, const FixedSizeBuffer<8192>& buffer) {
    // Step 1: Basic structure validation
    if (buffer.size() < sizeof(MessageHeader)) {
        LOG_WARN("Message too small for header");
        return; // Early rejection
    }
    
    // Step 2: Header validation
    auto header_validation = MessageValidator::validate_message_header(*header);
    if (header_validation.has_error()) {
        LOG_WARN_SAFE("Invalid message header: {}", header_validation.error().message());
        return; // Early rejection
    }
    
    // Step 3: Payload validation
    auto payload_validation = MessageValidator::validate_message_payload(payload, payload_size, type);
    if (payload_validation.has_error()) {
        LOG_WARN_SAFE("Invalid message payload: {}", payload_validation.error().message());
        return; // Early rejection
    }
    
    // Continue with processing only if all validations pass
}
```

### 2. Validation Chain Pattern
**Purpose**: Compose multiple validation rules for comprehensive checking.

```cpp
ValidationChain create_order_validator() {
    ValidationChain chain;
    
    // Add individual field validators
    chain.add_rule("symbol", FieldValidators::symbol_validator())
         .add_rule("quantity", FieldValidators::range_validator(1, 1000000))
         .add_rule("price", FieldValidators::positive_validator());
    
    // Add cross-field validation
    chain.add_custom_validator([](const auto& fields) -> Result<void> {
        double price = std::stod(fields.at("price"));
        uint64_t quantity = std::stoull(fields.at("quantity"));
        
        if (!FieldValidators::validate_price_quantity_relationship(price, quantity)) {
            return make_error_code(ValidationError::INVALID_FIELD_VALUE);
        }
        return Result<void>();
    });
    
    return chain;
}
```

### 3. Sanitization Pattern
**Purpose**: Clean and normalize inputs while preserving valid data.

```cpp
Result<void> sanitize_new_order(NewOrderMessage& msg) {
    // Sanitize symbol (uppercase, alphanumeric only)
    std::string symbol_str = msg.symbol.c_str();
    symbol_str = FieldValidators::sanitize_symbol(symbol_str);
    if (symbol_str.empty()) {
        return make_error_code(ValidationError::INVALID_FIELD_FORMAT);
    }
    msg.symbol.assign(symbol_str);
    
    // Sanitize client ID (alphanumeric, underscore, hyphen only)
    std::string client_str = msg.client_id.c_str();
    client_str = FieldValidators::sanitize_client_id(client_str);
    msg.client_id.assign(client_str);
    
    return Result<void>();
}
```

## Security Validations

### 1. Input Sanitization
**Control Character Removal**:
```cpp
std::string InputSanitizer::remove_control_chars(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    
    for (char c : input) {
        if (!CONTROL_CHARS.contains(c)) {
            result += c;
        }
    }
    
    return result;
}
```

**Special Character Escaping**:
```cpp
std::string InputSanitizer::escape_special_chars(const std::string& input) {
    std::string result;
    result.reserve(input.size() * 2);
    
    for (char c : input) {
        switch (c) {
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            case '\\': result += "\\\\"; break;
            case '"':  result += "\\\""; break;
            default:   result += c; break;
        }
    }
    
    return result;
}
```

### 2. Injection Prevention
**SQL Injection Prevention**:
- All database queries use parameterized statements
- Input validation prevents malicious SQL patterns
- String length limits prevent buffer overflows

**Log Injection Prevention**:
- All log inputs sanitized before logging
- Control characters removed from log messages
- Special characters escaped in structured logs

**Command Injection Prevention**:
- No system command execution from user input
- File path validation prevents directory traversal
- Input character set restrictions

### 3. Format String Attack Prevention
**Type-Safe Formatting**:
```cpp
// Instead of: printf(user_input)
// Use: LOG_INFO_SAFE("User input: {}", sanitized_input)

template<typename... Args>
void log_safe(LogLevel level, std::string_view format_str, Args&&... args) {
    try {
        auto formatted = std::vformat(format_str, std::make_format_args(args...));
        auto sanitized = SecurityUtils::sanitize_log_input(formatted);
        log(level, sanitized);
    } catch (const std::format_error& e) {
        log(LogLevel::ERROR, "[FORMAT_ERROR: " + std::string(e.what()) + "]");
    }
}
```

## Validation Rules by Component

### 1. Network Message Validation

**NewOrderMessage**:
- `order_id`: Non-zero, unique within session
- `client_id`: Alphanumeric + underscore/hyphen, max 32 chars
- `symbol`: Uppercase alphanumeric, max 8 chars
- `side`: Must be BUY (1) or SELL (2)
- `quantity`: 1 to 1,000,000
- `price`: Positive for limit orders, zero for market orders
- `order_type`: Must be MARKET (1) or LIMIT (2)

**CancelOrderMessage**:
- `order_id`: Non-zero, must exist in system
- `client_id`: Must match original order client
- `symbol`: Valid symbol format

**OrderAckMessage**:
- `order_id`: Non-zero
- `status`: 1 (Accepted) or 2 (Rejected)
- `reason`: Sanitized string, max 32 chars

### 2. Configuration Validation

**ExchangeConfig**:
- `name`: Non-empty string, max 64 chars
- `tcp_port`: 1024-65535, unique
- `udp_port`: 1024-65535, unique
- `metrics_port`: 1024-65535, unique
- `udp_multicast_group`: Valid IP format

**RiskConfig**:
- `max_order_size`: 1 to 10,000,000
- `max_notional_per_client`: Positive value
- `max_orders_per_second`: 1 to 100,000
- `price_collar_enabled`: Boolean

**PerformanceConfig**:
- `order_pool_size`: 1,000 to 10,000,000
- `queue_capacity`: 1,000 to 1,000,000
- `udp_buffer_size`: 1,024 to 1,048,576 bytes

### 3. Business Logic Validation

**Order Validation**:
- Symbol must be in configured symbol list
- Price must be within collar limits (if enabled)
- Quantity must respect lot size requirements
- Client must have sufficient credit limit
- Order rate must not exceed limits

**Risk Validation**:
- Position limits not exceeded
- Notional exposure within limits
- Price collar validation for limit orders
- Duplicate order detection

## Performance Considerations

### 1. Validation Optimization
**Fast Path Validation**:
- Most common validations first
- Early rejection on obvious failures
- Cached validation results where appropriate
- Minimal string operations in hot paths

**Validation Caching**:
```cpp
class CachedValidator {
    std::unordered_map<std::string, bool> symbol_cache_;
    
public:
    bool is_valid_symbol(const std::string& symbol) {
        auto it = symbol_cache_.find(symbol);
        if (it != symbol_cache_.end()) {
            return it->second;
        }
        
        bool valid = perform_symbol_validation(symbol);
        symbol_cache_[symbol] = valid;
        return valid;
    }
};
```

### 2. Memory Efficiency
**String Handling**:
- Use string views for read-only validation
- Minimize string copying during validation
- Pre-allocate buffers for sanitization
- Use bounded strings to prevent overflows

**Validation State**:
- Reuse validation chain objects
- Pool validation result objects
- Minimize dynamic allocations
- Use stack-based validation where possible

## Error Handling and Reporting

### 1. Validation Error Categories
```cpp
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
```

### 2. Error Context and Logging
```cpp
void log_validation_error(const ValidationError& error, 
                         const std::string& field_name,
                         const std::string& field_value) {
    LOG_ERROR_SAFE("Validation failed for field '{}' with value '{}': {}", 
                   field_name, 
                   SecurityUtils::sanitize_log_input(field_value),
                   make_error_code(error).message());
}
```

### 3. Graceful Degradation
**Validation Failure Handling**:
- Log validation failures with context
- Return appropriate error codes to clients
- Maintain system stability on invalid inputs
- Provide helpful error messages for debugging

## Testing Strategy

### 1. Validation Test Categories
**Positive Tests**:
- Valid inputs pass validation
- Boundary values accepted
- Normal use cases work correctly

**Negative Tests**:
- Invalid inputs rejected
- Boundary violations caught
- Malicious inputs blocked

**Edge Case Tests**:
- Empty inputs handled
- Maximum size inputs processed
- Unicode and special characters

**Security Tests**:
- Injection attack attempts blocked
- Control characters removed
- Format string attacks prevented

### 2. Fuzzing and Property Testing
```cpp
TEST_F(ValidationTest, FuzzMessageValidation) {
    for (int i = 0; i < 10000; ++i) {
        auto random_message = generate_random_message();
        
        // Validation should never crash
        EXPECT_NO_THROW({
            auto result = MessageValidator::validate_message_header(random_message.header);
            // Result can be success or failure, but no exceptions
        });
    }
}
```

## Best Practices

### 1. Validation Design Principles
- **Fail Fast**: Validate inputs as early as possible
- **Defense in Depth**: Multiple validation layers
- **Least Privilege**: Validate based on minimum required access
- **Explicit Validation**: Don't assume inputs are valid
- **Consistent Error Handling**: Uniform error responses

### 2. Implementation Guidelines
- Use validation chains for complex validation
- Sanitize inputs before processing
- Log validation failures for monitoring
- Provide clear error messages
- Test validation thoroughly

### 3. Security Guidelines
- Never trust external inputs
- Sanitize all user-provided data
- Use type-safe formatting functions
- Validate file paths and names
- Implement rate limiting for validation failures

## Monitoring and Metrics

### 1. Validation Metrics
```cpp
class ValidationMetrics {
public:
    void record_validation_failure(ValidationError error, const std::string& component) {
        validation_failures_[error]++;
        component_failures_[component]++;
    }
    
    void record_validation_success(const std::string& component) {
        validation_successes_[component]++;
    }
};
```

### 2. Alerting and Monitoring
- Monitor validation failure rates
- Alert on unusual validation patterns
- Track validation performance metrics
- Log validation errors for analysis

This comprehensive input validation framework ensures that RTES is protected against malicious inputs while maintaining high performance and reliability.