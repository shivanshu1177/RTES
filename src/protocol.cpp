#include "rtes/protocol.hpp"
#include <chrono>

namespace rtes {

uint32_t ProtocolUtils::calculate_checksum(const void* data, size_t length) {
    // Simple CRC32-like checksum
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

bool ProtocolUtils::validate_checksum(const MessageHeader& header, const void* payload) {
    if (header.length < sizeof(MessageHeader)) return false;
    
    size_t payload_length = header.length - sizeof(MessageHeader);
    uint32_t calculated = calculate_checksum(payload, payload_length);
    
    return calculated == header.checksum;
}

void ProtocolUtils::set_checksum(MessageHeader& header, const void* payload) {
    size_t payload_length = header.length - sizeof(MessageHeader);
    header.checksum = calculate_checksum(payload, payload_length);
}

uint64_t ProtocolUtils::get_timestamp_ns() {
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
}

} // namespace rtes