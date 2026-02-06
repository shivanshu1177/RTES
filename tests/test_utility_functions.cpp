#include <gtest/gtest.h>
#include "rtes/security_utils.hpp"
#include "rtes/input_validation.hpp"
#include "rtes/protocol.hpp"
#include <cstring>
#include <thread>
#include <chrono>

using namespace rtes;

// SecurityUtils Tests
TEST(SecurityUtilsTest, SanitizeLogInput) {
    EXPECT_EQ(SecurityUtils::sanitize_log_input("normal text"), "normal text");
    EXPECT_EQ(SecurityUtils::sanitize_log_input("line1\nline2"), "line1\\nline2");
    EXPECT_EQ(SecurityUtils::sanitize_log_input("tab\there"), "tab\\there");
    EXPECT_EQ(SecurityUtils::sanitize_log_input("return\rchar"), "return\\rchar");
}

TEST(SecurityUtilsTest, ValidateFilePath) {
    // Use ./configs which is in the ALLOWED_CONFIG_DIRS whitelist
    EXPECT_TRUE(SecurityUtils::validate_file_path("./configs/config.json", "./configs"));
    EXPECT_FALSE(SecurityUtils::validate_file_path("./configs/../passwd", "./configs"));
    EXPECT_FALSE(SecurityUtils::validate_file_path("../../etc/passwd", "./configs"));
    EXPECT_TRUE(SecurityUtils::validate_file_path("./configs/subdir/file.txt", "./configs"));
}

TEST(SecurityUtilsTest, NormalizePath) {
    // normalize_path resolves symlinks; on macOS /etc -> /private/etc
    std::string norm1 = SecurityUtils::normalize_path("/etc/rtes/./config.json");
    EXPECT_NE(norm1.find("config.json"), std::string::npos);
    std::string norm2 = SecurityUtils::normalize_path("/etc/rtes//config.json");
    EXPECT_NE(norm2.find("config.json"), std::string::npos);
}

TEST(SecurityUtilsTest, IsValidSymbol) {
    EXPECT_TRUE(SecurityUtils::is_valid_symbol("AAPL"));
    EXPECT_TRUE(SecurityUtils::is_valid_symbol("MSFT"));
    EXPECT_FALSE(SecurityUtils::is_valid_symbol("BRK.A")); // dot not allowed
    EXPECT_FALSE(SecurityUtils::is_valid_symbol(""));
    EXPECT_FALSE(SecurityUtils::is_valid_symbol("TOOLONGSYMBOL"));
    EXPECT_FALSE(SecurityUtils::is_valid_symbol("SYM@BOL"));
}

TEST(SecurityUtilsTest, IsValidOrderId) {
    EXPECT_TRUE(SecurityUtils::is_valid_order_id("12345"));
    EXPECT_TRUE(SecurityUtils::is_valid_order_id("ORDER123"));
    EXPECT_FALSE(SecurityUtils::is_valid_order_id(""));
    EXPECT_FALSE(SecurityUtils::is_valid_order_id("ID\nINJECT"));
}

TEST(SecurityUtilsTest, IsSafeString) {
    EXPECT_TRUE(SecurityUtils::is_safe_string("SafeString123"));
    EXPECT_FALSE(SecurityUtils::is_safe_string("Unsafe\nString"));
}

// ProtocolUtils Tests
TEST(ProtocolUtilsTest, CalculateChecksum) {
    const char data[] = "test data";
    uint32_t checksum1 = ProtocolUtils::calculate_checksum(data, strlen(data));
    uint32_t checksum2 = ProtocolUtils::calculate_checksum(data, strlen(data));
    EXPECT_EQ(checksum1, checksum2);
    
    const char different[] = "different";
    uint32_t checksum3 = ProtocolUtils::calculate_checksum(different, strlen(different));
    EXPECT_NE(checksum1, checksum3);
}

TEST(ProtocolUtilsTest, ValidateChecksum) {
    NewOrderMessage msg;
    msg.header.type = NEW_ORDER;
    msg.header.length = sizeof(NewOrderMessage);
    msg.order_id = 12345;
    
    ProtocolUtils::set_checksum(msg.header, &msg.order_id);
    EXPECT_TRUE(ProtocolUtils::validate_checksum(msg.header, &msg.order_id));
    
    msg.order_id = 99999;
    EXPECT_FALSE(ProtocolUtils::validate_checksum(msg.header, &msg.order_id));
}

TEST(ProtocolUtilsTest, GetTimestamp) {
    uint64_t ts1 = ProtocolUtils::get_timestamp_ns();
    std::this_thread::sleep_for(std::chrono::microseconds(1));
    uint64_t ts2 = ProtocolUtils::get_timestamp_ns();
    EXPECT_GE(ts2, ts1);
    EXPECT_LT(ts2 - ts1, 1000000);
}

// MessageValidator Tests
TEST(MessageValidatorTest, ValidateMessageHeader) {
    MessageHeader header(NEW_ORDER, sizeof(NewOrderMessage), 1, ProtocolUtils::get_timestamp_ns());
    auto result = MessageValidator::validate_message_header(header);
    EXPECT_TRUE(result.has_value());
    
    header.type = 9999;
    result = MessageValidator::validate_message_header(header);
    EXPECT_TRUE(result.has_error());
}

TEST(MessageValidatorTest, IsValidMessageType) {
    EXPECT_TRUE(MessageValidator::is_valid_message_type(NEW_ORDER));
    EXPECT_TRUE(MessageValidator::is_valid_message_type(CANCEL_ORDER));
    EXPECT_TRUE(MessageValidator::is_valid_message_type(ORDER_ACK));
    EXPECT_FALSE(MessageValidator::is_valid_message_type(9999));
}

TEST(MessageValidatorTest, IsValidMessageSize) {
    EXPECT_TRUE(MessageValidator::is_valid_message_size(sizeof(NewOrderMessage), NEW_ORDER));
    EXPECT_FALSE(MessageValidator::is_valid_message_size(10, NEW_ORDER));
    EXPECT_FALSE(MessageValidator::is_valid_message_size(10000, NEW_ORDER));
}

// FieldValidators Tests
TEST(FieldValidatorsTest, RangeValidator) {
    auto validator = FieldValidators::range_validator(0.0, 100.0);
    EXPECT_TRUE(validator.validator("50.0"));
    EXPECT_TRUE(validator.validator("0.0"));
    EXPECT_TRUE(validator.validator("100.0"));
    EXPECT_FALSE(validator.validator("-1.0"));
    EXPECT_FALSE(validator.validator("101.0"));
}

TEST(FieldValidatorsTest, PositiveValidator) {
    auto validator = FieldValidators::positive_validator();
    EXPECT_TRUE(validator.validator("1.0"));
    EXPECT_TRUE(validator.validator("100.5"));
    EXPECT_FALSE(validator.validator("0.0"));
    EXPECT_FALSE(validator.validator("-1.0"));
}

TEST(FieldValidatorsTest, LengthValidator) {
    auto validator = FieldValidators::length_validator(3, 10);
    EXPECT_TRUE(validator.validator("test"));
    EXPECT_TRUE(validator.validator("abc"));
    EXPECT_FALSE(validator.validator("ab"));
    EXPECT_FALSE(validator.validator("toolongstring"));
}

TEST(FieldValidatorsTest, AlphanumericValidator) {
    auto validator = FieldValidators::alphanumeric_validator();
    EXPECT_TRUE(validator.validator("ABC123"));
    EXPECT_TRUE(validator.validator("test"));
    EXPECT_FALSE(validator.validator("test@123"));
    EXPECT_FALSE(validator.validator("test space"));
}

TEST(FieldValidatorsTest, SymbolValidator) {
    auto validator = FieldValidators::symbol_validator();
    EXPECT_TRUE(validator.validator("AAPL"));
    EXPECT_TRUE(validator.validator("MSFT"));
    EXPECT_FALSE(validator.validator(""));
    EXPECT_FALSE(validator.validator("TOOLONGSYMBOLNAME"));
}

TEST(FieldValidatorsTest, ValidatePriceQuantityRelationship) {
    EXPECT_TRUE(FieldValidators::validate_price_quantity_relationship(100, 10));
    EXPECT_TRUE(FieldValidators::validate_price_quantity_relationship(0, 10));  // market order
    EXPECT_FALSE(FieldValidators::validate_price_quantity_relationship(100, 0));
}

TEST(FieldValidatorsTest, ValidatePortRange) {
    EXPECT_TRUE(FieldValidators::validate_port_range(8080));
    EXPECT_TRUE(FieldValidators::validate_port_range(1024));
    EXPECT_FALSE(FieldValidators::validate_port_range(0));
    EXPECT_FALSE(FieldValidators::validate_port_range(100));
}

TEST(FieldValidatorsTest, ValidatePercentage) {
    EXPECT_TRUE(FieldValidators::validate_percentage(50.0));
    EXPECT_TRUE(FieldValidators::validate_percentage(0.0));
    EXPECT_TRUE(FieldValidators::validate_percentage(100.0));
    EXPECT_FALSE(FieldValidators::validate_percentage(-1.0));
    EXPECT_FALSE(FieldValidators::validate_percentage(101.0));
}

TEST(FieldValidatorsTest, SanitizeString) {
    EXPECT_EQ(FieldValidators::sanitize_string("normal", 10), "normal");
    EXPECT_EQ(FieldValidators::sanitize_string("toolongstring", 5), "toolo");
    EXPECT_EQ(FieldValidators::sanitize_string("test\nline", 10), "test_line");
}

TEST(FieldValidatorsTest, SanitizeSymbol) {
    EXPECT_EQ(FieldValidators::sanitize_symbol("AAPL"), "AAPL");
    EXPECT_EQ(FieldValidators::sanitize_symbol("aapl"), "AAPL");
    EXPECT_EQ(FieldValidators::sanitize_symbol("AA@PL"), "AAPL");
}

TEST(FieldValidatorsTest, SanitizeClientId) {
    EXPECT_EQ(FieldValidators::sanitize_client_id("CLIENT123"), "CLIENT123");
    EXPECT_EQ(FieldValidators::sanitize_client_id("client@123"), "client123"); // @ dropped
}

// InputSanitizer Tests
TEST(InputSanitizerTest, SanitizeNetworkInput) {
    EXPECT_EQ(InputSanitizer::sanitize_network_input("normal"), "normal");
    EXPECT_EQ(InputSanitizer::sanitize_network_input("test\nline"), "test line"); // \n -> space
}

TEST(InputSanitizerTest, RemoveControlChars) {
    EXPECT_EQ(InputSanitizer::remove_control_chars("normal"), "normal");
    EXPECT_EQ(InputSanitizer::remove_control_chars("test\x01\x02"), "test");
}

TEST(InputSanitizerTest, NormalizeWhitespace) {
    EXPECT_EQ(InputSanitizer::normalize_whitespace("test  spaces"), "test spaces");
    EXPECT_EQ(InputSanitizer::normalize_whitespace("  trim  "), "trim");
}

// ValidationChain Tests
TEST(ValidationChainTest, AddRuleAndValidate) {
    ValidationChain chain;
    chain.add_rule("price", FieldValidators::positive_validator());
    chain.add_rule("symbol", FieldValidators::symbol_validator());
    
    std::unordered_map<std::string, std::string> valid_fields = {
        {"price", "100.5"},
        {"symbol", "AAPL"}
    };
    EXPECT_TRUE(chain.validate(valid_fields).has_value());
    
    std::unordered_map<std::string, std::string> invalid_fields = {
        {"price", "-10"},
        {"symbol", "AAPL"}
    };
    EXPECT_TRUE(chain.validate(invalid_fields).has_error());
}

TEST(ValidationChainTest, CustomValidator) {
    ValidationChain chain;
    chain.add_custom_validator([]() -> Result<void> {
        return Result<void>();
    });
    EXPECT_TRUE(chain.validate_early_reject().has_value());
    
    chain.clear();
    chain.add_custom_validator([]() -> Result<void> {
        return Result<void>(ErrorCode::INVALID_ARGUMENT);
    });
    EXPECT_TRUE(chain.validate_early_reject().has_error());
}
