/**
 * @file protocol.cpp
 * @brief Binary protocol utilities for message integrity
 * 
 * Provides:
 * - CRC32 checksum calculation and validation
 * - Message header validation
 * - High-precision timestamps
 * 
 * Protocol features:
 * - Fixed-size headers (cache-friendly)
 * - Checksum for integrity
 * - Sequence numbers for gap detection
 * - Nanosecond timestamps
 */

#include "rtes/protocol.hpp"
#include <chrono>

namespace rtes {

/**
 * @brief Calculate CRC32 checksum for data
 * @param data Pointer to data buffer
 * @param length Length of data in bytes
 * @return 32-bit CRC checksum
 * 
 * Uses CRC32 polynomial 0xEDB88320 (reversed).
 * Detects single-bit errors and burst errors.
 */
uint32_t ProtocolUtils::calculate_checksum(const void* data, size_t length) {
    // CRC32 algorithm (IEEE 802.3 polynomial)
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFF;
    
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return ~crc;
}

/**
 * @brief Validate message checksum
 * @param header Message header containing checksum
 * @param payload Message payload (after header)
 * @return true if checksum valid
 * 
 * Security checks:
 * - Message length validation (prevent buffer overflow)
 * - Null pointer check
 * - Checksum comparison
 */
bool ProtocolUtils::validate_checksum(const MessageHeader& header, const void* payload) {
    // Security: Validate message length (prevent buffer overflow)
    if (header.length < sizeof(MessageHeader) || header.length > 8192) {
        return false;
    }
    
    // Security: Null pointer check
    if (!payload) {
        return false;
    }
    
    // Calculate checksum for payload
    size_t payload_length = header.length - sizeof(MessageHeader);
    uint32_t calculated = calculate_checksum(payload, payload_length);
    
    // Compare with header checksum
    return calculated == header.checksum;
}

/**
 * @brief Set checksum in message header
 * @param header Message header to update
 * @param payload Message payload
 * 
 * Calculates checksum for payload and stores in header.
 * Sets checksum to 0 if validation fails.
 */
void ProtocolUtils::set_checksum(MessageHeader& header, const void* payload) {
    // Validate inputs
    if (header.length < sizeof(MessageHeader) || header.length > 8192 || !payload) {
        header.checksum = 0;
        return;
    }
    
    // Calculate and set checksum
    size_t payload_length = header.length - sizeof(MessageHeader);
    header.checksum = calculate_checksum(payload, payload_length);
}

/**
 * @brief Get current timestamp in nanoseconds
 * @return Nanoseconds since epoch
 * 
 * Uses high_resolution_clock for precision.
 * Suitable for latency measurements and message timestamps.
 */
uint64_t ProtocolUtils::get_timestamp_ns() {
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
}

} // namespace rtes