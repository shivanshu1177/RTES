#pragma once

#include <array>
#include <cstring>
#include <unistd.h>

namespace rtes {

// RAII wrapper for POSIX file descriptors — move-only, closes on destroy
class FileDescriptor {
public:
    explicit FileDescriptor(int fd = -1) noexcept : fd_(fd) {}

    ~FileDescriptor() {
        if (fd_ >= 0) ::close(fd_);
    }

    // Move-only
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    FileDescriptor(FileDescriptor&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) ::close(fd_);
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    int get() const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ >= 0; }

    // Release ownership without closing
    int release() noexcept {
        int fd = fd_;
        fd_ = -1;
        return fd;
    }

    void reset(int fd = -1) noexcept {
        if (fd_ >= 0) ::close(fd_);
        fd_ = fd;
    }

private:
    int fd_;
};

// Fixed-size byte buffer with bounds tracking
template<size_t N>
struct FixedSizeBuffer {
    std::array<uint8_t, N> data{};
    size_t size{0};

    void clear() noexcept { size = 0; }
    size_t remaining() const noexcept { return N - size; }
    size_t capacity() const noexcept { return N; }
    bool full() const noexcept { return size >= N; }
    bool empty() const noexcept { return size == 0; }

    // Returns false if there's not enough room
    bool append(const void* src, size_t len) noexcept {
        if (len > remaining()) return false;
        std::memcpy(data.data() + size, src, len);
        size += len;
        return true;
    }

    const uint8_t* begin() const noexcept { return data.data(); }
    uint8_t* begin() noexcept { return data.data(); }
};

} // namespace rtes
