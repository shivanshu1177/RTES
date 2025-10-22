#include "rtes/memory_safety.hpp"
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cctype>

namespace rtes {

BoundsCheckedBuffer::BoundsCheckedBuffer(size_t size) : size_(size) {
    // Allocate buffer with guard pages
    size_t total_size = size_ + 2 * GUARD_SIZE;
    buffer_ = mmap(nullptr, total_size, PROT_READ | PROT_WRITE, 
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if (buffer_ == MAP_FAILED) {
        throw std::bad_alloc();
    }
    
    // Adjust buffer pointer to skip first guard page
    buffer_ = static_cast<char*>(buffer_) + GUARD_SIZE;
    setup_guard_pages();
}

BoundsCheckedBuffer::~BoundsCheckedBuffer() {
    cleanup_guard_pages();
}

void BoundsCheckedBuffer::checked_read(void* dest, size_t offset, size_t length) const {
    if (!validate_range(offset, length)) {
        throw BufferOverflowError("Read operation out of bounds");
    }
    std::memcpy(dest, static_cast<const char*>(buffer_) + offset, length);
}

void BoundsCheckedBuffer::checked_write(const void* src, size_t offset, size_t length) {
    if (!validate_range(offset, length)) {
        throw BufferOverflowError("Write operation out of bounds");
    }
    std::memcpy(static_cast<char*>(buffer_) + offset, src, length);
}

bool BoundsCheckedBuffer::validate_range(size_t offset, size_t length) const noexcept {
    // Check for overflow in addition
    if (offset > size_ || length > size_ - offset) {
        return false;
    }
    return true;
}

void BoundsCheckedBuffer::setup_guard_pages() {
    // Make guard pages inaccessible
    char* base = static_cast<char*>(buffer_) - GUARD_SIZE;
    
    // Before guard page
    if (mprotect(base, GUARD_SIZE, PROT_NONE) != 0) {
        munmap(base, size_ + 2 * GUARD_SIZE);
        throw std::runtime_error("Failed to setup guard pages");
    }
    
    // After guard page
    if (mprotect(static_cast<char*>(buffer_) + size_, GUARD_SIZE, PROT_NONE) != 0) {
        munmap(base, size_ + 2 * GUARD_SIZE);
        throw std::runtime_error("Failed to setup guard pages");
    }
}

void BoundsCheckedBuffer::cleanup_guard_pages() {
    if (buffer_) {
        char* base = static_cast<char*>(buffer_) - GUARD_SIZE;
        munmap(base, size_ + 2 * GUARD_SIZE);
        buffer_ = nullptr;
    }
}

bool MessageValidator::validate_message_size(size_t received, size_t expected_min, size_t expected_max) noexcept {
    return received >= expected_min && received <= expected_max;
}

bool MessageValidator::sanitize_network_input(const void* data, size_t length) noexcept {
    if (!data || length == 0) return false;
    
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    
    // Check for null bytes and control characters in first part (assuming text)
    for (size_t i = 0; i < std::min(length, size_t(64)); ++i) {
        if (bytes[i] == 0 || (bytes[i] < 32 && bytes[i] != '\t' && bytes[i] != '\n' && bytes[i] != '\r')) {
            return false;
        }
    }
    
    return true;
}

bool MessageValidator::validate_string_field(const char* str, size_t max_length) noexcept {
    if (!str) return false;
    
    size_t len = 0;
    while (len < max_length && str[len] != '\0') {
        if (!std::isprint(str[len]) && str[len] != '\t') {
            return false;
        }
        ++len;
    }
    
    return len < max_length; // Must be null-terminated within bounds
}

void FileDescriptor::reset(int new_fd) noexcept {
    close();
    fd_ = new_fd;
}

int FileDescriptor::release() noexcept {
    int fd = fd_;
    fd_ = -1;
    return fd;
}

void FileDescriptor::close() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

} // namespace rtes