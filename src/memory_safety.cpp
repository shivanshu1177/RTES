#include "rtes/memory_safety.hpp"
#include "rtes/input_validation.hpp"
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cctype>

namespace rtes {

BoundsCheckedBuffer::BoundsCheckedBuffer(size_t size) : size_(size) {
    size_t total_size = size_ + 2 * GUARD_SIZE;
    buffer_ = mmap(nullptr, total_size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (buffer_ == MAP_FAILED) {
        throw std::bad_alloc();
    }

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

// validate_range is defined inline in memory_safety.hpp — NOT duplicated here

void BoundsCheckedBuffer::setup_guard_pages() {
    char* base = static_cast<char*>(buffer_) - GUARD_SIZE;

    if (mprotect(base, GUARD_SIZE, PROT_NONE) != 0) {
        munmap(base, size_ + 2 * GUARD_SIZE);
        throw std::runtime_error("Failed to setup guard pages");
    }

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

// FileDescriptor::reset and FileDescriptor::release are defined
// inline in memory_safety.hpp — NOT duplicated here

void FileDescriptor::close() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

} // namespace rtes