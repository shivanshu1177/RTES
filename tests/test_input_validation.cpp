#include <gtest/gtest.h>
#include "rtes/input_validation.hpp"
#include "rtes/protocol.hpp"

using namespace rtes;

class InputValidationTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(InputValidationTest, MessageHeaderValidation) {
    // Valid header
    MessageHeader valid_header;
    valid_header.type = NEW_ORDER;
    valid_header.length = sizeof(NewOrderMessage);
    valid_header.sequence = 12345;
    valid_header.timestamp = 1234567890;
    valid_header.checksum = 0;
    
    auto result = MessageValidator::validate_message_header(valid_header);
    EXPECT_TRUE(result.has_value());
    
    // Invalid message type
    MessageHeader invalid_type = valid_header;
    invalid_type.type = 999;
    result = MessageValidator::validate_message_header(invalid_type);
    EXPECT_TRUE(result.has_error());
    
    // Invalid sequence (zero)
    MessageHeader invalid_seq = valid_header;
    invalid_seq.sequence = 0;
    result = MessageValidator::validate_message_header(invalid_seq);
    EXPECT_TRUE(result.has_error());
    
    // Invalid size
    MessageHeader invalid_size = valid_header;
    invalid_size.length = 10000;
    result = MessageValidator::validate_message_header(invalid_size);
    EXPECT_TRUE(result.has_error());
}

TEST_F(InputValidationTest, MessagePayloadValidation) {
    // Valid payload for NEW_ORDER
    NewOrderMessage msg;
    size_t payload_size = sizeof(NewOrderMessage) - sizeof(MessageHeader);
    
    auto result = MessageValidator::validate_message_payload(
        &msg.order_id, payload_size, NEW_ORDER);
    EXPECT_TRUE(result.has_value());
    
    // Invalid payload size
    result = MessageValidator::validate_message_payload(
        &msg.order_id, payload_size + 10, NEW_ORDER);
    EXPECT_TRUE(result.has_error());
    
    // Null payload with non-zero size
    result = MessageValidator::validate_message_payload(
        nullptr, payload_size, NEW_ORDER);
    EXPECT_TRUE(result.has_error());
}

TEST_F(InputValidationTest, NewOrderMessageSanitization) {
    NewOrderMessage msg;
    msg.order_id = 12345;
    msg.client_id.assign("CLIENT123");
    msg.symbol.assign("AAPL");
    msg.side = static_cast<uint8_t>(Side::BUY);
    msg.quantity = 100;
    msg.price = 15000; // $150.00
    msg.order_type = static_cast<uint8_t>(OrderType::LIMIT);
    
    auto result = MessageValidator::sanitize_message_fields(msg);
    EXPECT_TRUE(result.has_value());
    
    // Invalid order ID (zero)
    msg.order_id = 0;
    result = MessageValidator::sanitize_message_fields(msg);
    EXPECT_TRUE(result.has_error());
    
    // Invalid side
    msg.order_id = 12345;
    msg.side = 99;
    result = MessageValidator::sanitize_message_fields(msg);
    EXPECT_TRUE(result.has_error());
    
    // Invalid quantity (zero)
    msg.side = static_cast<uint8_t>(Side::BUY);
    msg.quantity = 0;
    result = MessageValidator::sanitize_message_fields(msg);
    EXPECT_TRUE(result.has_error());
    
    // Invalid quantity (too large)
    msg.quantity = 2000000;
    result = MessageValidator::sanitize_message_fields(msg);
    EXPECT_TRUE(result.has_error());
}

TEST_F(InputValidationTest, CancelOrderMessageSanitization) {
    CancelOrderMessage msg;
    msg.order_id = 12345;
    msg.client_id.assign("CLIENT123");
    msg.symbol.assign("AAPL");
    
    auto result = MessageValidator::sanitize_message_fields(msg);
    EXPECT_TRUE(result.has_value());
    
    // Invalid order ID
    msg.order_id = 0;
    result = MessageValidator::sanitize_message_fields(msg);
    EXPECT_TRUE(result.has_error());
    
    // Invalid symbol (empty after sanitization)
    msg.order_id = 12345;
    msg.symbol.assign("!@#$");
    result = MessageValidator::sanitize_message_fields(msg);
    EXPECT_TRUE(result.has_error());
}

TEST_F(InputValidationTest, ConfigurationValidation) {
    Config config;
    
    // Set up valid configuration
    config.exchange.name = "TestExchange";
    config.exchange.tcp_port = 8888;
    config.exchange.udp_port = 9999;
    config.exchange.metrics_port = 8080;
    config.exchange.udp_multicast_group = "239.0.0.1";
    
    config.risk.max_order_size = 1000000;
    config.risk.max_notional_per_client = 10000000.0;
    config.risk.max_orders_per_second = 1000;
    config.risk.price_collar_enabled = true;
    
    config.performance.order_pool_size = 100000;
    config.performance.queue_capacity = 65536;
    config.performance.udp_buffer_size = 8192;
    
    config.symbols.push_back({"AAPL", 0.01, 1, 10.0});
    config.symbols.push_back({"MSFT", 0.01, 1, 10.0});
    
    auto result = ConfigurationValidator::validate_full_config(config);
    EXPECT_TRUE(result.has_value());
    
    // Test invalid port (too low)
    config.exchange.tcp_port = 100;
    result = ConfigurationValidator::validate_full_config(config);
    EXPECT_TRUE(result.has_error());
    
    // Test port conflict
    config.exchange.tcp_port = 8888;
    config.exchange.udp_port = 8888;
    result = ConfigurationValidator::validate_full_config(config);
    EXPECT_TRUE(result.has_error());
}

TEST_F(InputValidationTest, FieldValidators) {
    // Range validator
    auto range_val = FieldValidators::range_validator(1.0, 100.0);
    EXPECT_TRUE(range_val.validator("50.0"));
    EXPECT_FALSE(range_val.validator("150.0"));
    EXPECT_FALSE(range_val.validator("invalid"));
    
    // Positive validator
    auto pos_val = FieldValidators::positive_validator();
    EXPECT_TRUE(pos_val.validator("10.5"));
    EXPECT_FALSE(pos_val.validator("-5.0"));
    EXPECT_FALSE(pos_val.validator("0"));
    
    // Length validator
    auto len_val = FieldValidators::length_validator(3, 8);
    EXPECT_TRUE(len_val.validator("AAPL"));
    EXPECT_FALSE(len_val.validator("AB"));
    EXPECT_FALSE(len_val.validator("VERYLONGSYMBOL"));
    
    // Symbol validator
    auto sym_val = FieldValidators::symbol_validator();
    EXPECT_TRUE(sym_val.validator("AAPL"));
    EXPECT_TRUE(sym_val.validator("MSFT123"));
    EXPECT_FALSE(sym_val.validator("aapl"));
    EXPECT_FALSE(sym_val.validator("AAPL!"));
    EXPECT_FALSE(sym_val.validator("VERYLONGSYMBOLNAME"));
}

TEST_F(InputValidationTest, ValidationChain) {
    ValidationChain chain;
    
    chain.add_rule("price", FieldValidators::positive_validator())
         .add_rule("symbol", FieldValidators::symbol_validator())
         .add_rule("quantity", FieldValidators::range_validator(1, 1000000));
    
    // Valid fields
    std::unordered_map<std::string, std::string> valid_fields = {
        {"price", "150.50"},
        {"symbol", "AAPL"},
        {"quantity", "100"}
    };
    
    auto result = chain.validate(valid_fields);
    EXPECT_TRUE(result.has_value());
    
    // Invalid price
    std::unordered_map<std::string, std::string> invalid_fields = {
        {"price", "-10.0"},
        {"symbol", "AAPL"},
        {"quantity", "100"}
    };
    
    result = chain.validate(invalid_fields);
    EXPECT_TRUE(result.has_error());
    
    // Missing field
    std::unordered_map<std::string, std::string> missing_fields = {
        {"price", "150.50"},
        {"symbol", "AAPL"}
        // quantity missing
    };
    
    result = chain.validate(missing_fields);
    EXPECT_TRUE(result.has_error());
}

TEST_F(InputValidationTest, CustomValidators) {
    ValidationChain chain;
    
    // Add custom validator
    chain.add_custom_validator([]() -> Result<void> {
        // Custom business logic validation
        return Result<void>();
    });
    
    chain.add_custom_validator([]() -> Result<void> {
        // Failing custom validator
        return make_error_code(ValidationError::INVALID_CONFIGURATION);
    });
    
    std::unordered_map<std::string, std::string> fields;
    auto result = chain.validate(fields);
    EXPECT_TRUE(result.has_error());
}

TEST_F(InputValidationTest, InputSanitization) {
    // Test control character removal
    std::string input_with_control = "test\x00\x01string";
    std::string sanitized = InputSanitizer::sanitize_network_input(input_with_control);
    EXPECT_EQ(sanitized, "test string");
    
    // Test whitespace normalization
    std::string input_with_spaces = "test   multiple    spaces";
    sanitized = InputSanitizer::sanitize_network_input(input_with_spaces);
    EXPECT_EQ(sanitized, "test multiple spaces");
    
    // Test symbol sanitization
    std::string dirty_symbol = "aa!pl@123";
    std::string clean_symbol = FieldValidators::sanitize_symbol(dirty_symbol);
    EXPECT_EQ(clean_symbol, "AAPL123");
    
    // Test client ID sanitization
    std::string dirty_client = "client!@#123_test";
    std::string clean_client = FieldValidators::sanitize_client_id(dirty_client);
    EXPECT_EQ(clean_client, "client123_test");
}

TEST_F(InputValidationTest, CrossFieldValidation) {
    // Test price-quantity relationship
    EXPECT_TRUE(FieldValidators::validate_price_quantity_relationship(15000, 100)); // Valid
    EXPECT_TRUE(FieldValidators::validate_price_quantity_relationship(0, 100));     // Market order
    
    // Test port range validation
    EXPECT_TRUE(FieldValidators::validate_port_range(8080));
    EXPECT_FALSE(FieldValidators::validate_port_range(80));    // Too low
    EXPECT_FALSE(FieldValidators::validate_port_range(70000)); // Too high
    
    // Test percentage validation
    EXPECT_TRUE(FieldValidators::validate_percentage(50.0));
    EXPECT_TRUE(FieldValidators::validate_percentage(0.0));
    EXPECT_TRUE(FieldValidators::validate_percentage(100.0));
    EXPECT_FALSE(FieldValidators::validate_percentage(-10.0));
    EXPECT_FALSE(FieldValidators::validate_percentage(150.0));
}

TEST_F(InputValidationTest, MessageTypeValidation) {
    EXPECT_TRUE(MessageValidator::is_valid_message_type(NEW_ORDER));
    EXPECT_TRUE(MessageValidator::is_valid_message_type(CANCEL_ORDER));
    EXPECT_TRUE(MessageValidator::is_valid_message_type(ORDER_ACK));
    EXPECT_TRUE(MessageValidator::is_valid_message_type(TRADE_REPORT));
    EXPECT_TRUE(MessageValidator::is_valid_message_type(HEARTBEAT));
    
    EXPECT_FALSE(MessageValidator::is_valid_message_type(999));
    EXPECT_FALSE(MessageValidator::is_valid_message_type(0));
}

TEST_F(InputValidationTest, MessageSizeValidation) {
    EXPECT_TRUE(MessageValidator::is_valid_message_size(
        sizeof(NewOrderMessage), NEW_ORDER));
    EXPECT_TRUE(MessageValidator::is_valid_message_size(
        sizeof(CancelOrderMessage), CANCEL_ORDER));
    
    EXPECT_FALSE(MessageValidator::is_valid_message_size(
        sizeof(NewOrderMessage) + 100, NEW_ORDER));
    EXPECT_FALSE(MessageValidator::is_valid_message_size(
        10, NEW_ORDER));
}

TEST_F(InputValidationTest, ValidationErrorCodes) {
    auto ec = make_error_code(ValidationError::INVALID_MESSAGE_TYPE);
    EXPECT_EQ(ec.category().name(), std::string("validation"));
    EXPECT_FALSE(ec.message().empty());
    
    ec = make_error_code(ValidationError::INVALID_FIELD_RANGE);
    EXPECT_FALSE(ec.message().empty());
}