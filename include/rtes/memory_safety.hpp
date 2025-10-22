#pragma once

#include <cstddef>
#include <stdexcept>
#include <array>
#include <string>
#include <memory>
#include <cstring>

namespace rtes {

class BufferOverflowError : public std::runtime_error {
public:
    explicit BufferOverflowError(const std::string& msg) : std::runtime_error(msg) {}
};

// Bounds-checked buffer with overflow protection
class BoundsCheckedBuffer {
public:
    explicit BoundsCheckedBuffer(size_t size);
    ~BoundsCheckedBuffer();
    
    void checked_read(void* dest, size_t offset, size_t length) const;
    void checked_write(const void* src, size_t offset, size_t length);
    bool validate_range(size_t offset, size_t length) const noexcept;
    
    size_t size() const noexcept { return size_; }
    const void* data() const noexcept { return buffer_; }
    void* data() noexcept { return buffer_; }
    
private:
    void* buffer_;
    size_t size_;
    static constexpr size_t GUARD_SIZE = 4096;
    
    void setup_guard_pages();
    void cleanup_guard_pages();
};

// Fixed-size buffer template with compile-time bounds checking
template<size_t MAX_SIZE>
class FixedSizeBuffer {
public:
    FixedSizeBuffer() : used_size_(0) {
        static_assert(MAX_SIZE > 0, "Buffer size must be positive");
    }
    
    void write(const void* src, size_t length) {
        if (length > MAX_SIZE) {
            throw BufferOverflowError("Write exceeds buffer capacity");
        }
        std::memcpy(data_.data(), src, length);
        used_size_ = length;
    }
    
    void read(void* dest, size_t length) const {
        if (length > used_size_) {
            throw BufferOverflowError("Read exceeds used buffer size");
        }
        std::memcpy(dest, data_.data(), length);
    }
    
    void append(const void* src, size_t length) {
        if (used_size_ + length > MAX_SIZE) {
            throw BufferOverflowError("Append exceeds buffer capacity");
        }
        std::memcpy(data_.data() + used_size_, src, length);
        used_size_ += length;
    }
    
    size_t size() const noexcept { return used_size_; }
    size_t capacity() const noexcept { return MAX_SIZE; }
    const void* data() const noexcept { return data_.data(); }
    void* data() noexcept { return data_.data(); }
    void clear() noexcept { used_size_ = 0; }
    
private:
    std::array<uint8_t, MAX_SIZE> data_;
    size_t used_size_;
};

// Bounded string with length validation
template<size_t MAX_LEN>
class BoundedString {
public:
    BoundedString() = default;
    
    explicit BoundedString(const char* str) {
        assign(str);
    }
    
    explicit BoundedString(const std::string& str) {
        assign(str.c_str(), str.length());
    }
    
    void assign(const char* str) {
        if (!str) {
            clear();
            return;
        }
        size_t len = std::strlen(str);
        assign(str, len);
    }
    
    void assign(const char* str, size_t length) {
        if (length >= MAX_LEN) {
            throw BufferOverflowError("String exceeds maximum length");
        }
        std::memcpy(data_.data(), str, length);
        data_[length] = '\0';
        length_ = length;
    }
    
    const char* c_str() const noexcept { return data_.data(); }
    size_t length() const noexcept { return length_; }
    size_t max_length() const noexcept { return MAX_LEN - 1; }
    bool empty() const noexcept { return length_ == 0; }
    void clear() noexcept { length_ = 0; data_[0] = '\0'; }
    
    bool operator==(const BoundedString& other) const noexcept {
        return length_ == other.length_ && 
               std::memcmp(data_.data(), other.data_.data(), length_) == 0;
    }
    
private:
    std::array<char, MAX_LEN> data_{};
    size_t length_ = 0;
};

// Message validation utilities
class MessageValidator {
public:
    static bool validate_message_size(size_t received, size_t expected_min, size_t expected_max) noexcept;
    static bool sanitize_network_input(const void* data, size_t length) noexcept;
    static bool validate_string_field(const char* str, size_t max_length) noexcept;
};

// RAII wrapper for file descriptors
class FileDescriptor {
public:
    explicit FileDescriptor(int fd = -1) noexcept : fd_(fd) {}
    ~FileDescriptor() { close(); }
    
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    
    FileDescriptor(FileDescriptor&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }
    
    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }
    
    int get() const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ >= 0; }
    void reset(int new_fd = -1) noexcept;
    int release() noexcept;
    void close() noexcept;
    
private:
    int fd_;
};

// RAII wrapper for memory allocations
template<typename T>
class UniqueBuffer {
public:
    explicit UniqueBuffer(size_t count) 
        : ptr_(static_cast<T*>(std::aligned_alloc(alignof(T), sizeof(T) * count)))
        , count_(count) {
        if (!ptr_) {
            throw std::bad_alloc();
        }
    }
    
    ~UniqueBuffer() {
        std::free(ptr_);
    }
    
    UniqueBuffer(const UniqueBuffer&) = delete;
    UniqueBuffer& operator=(const UniqueBuffer&) = delete;
    
    UniqueBuffer(UniqueBuffer&& other) noexcept 
        : ptr_(other.ptr_), count_(other.count_) {
        other.ptr_ = nullptr;
        other.count_ = 0;
    }
    
    UniqueBuffer& operator=(UniqueBuffer&& other) noexcept {
        if (this != &other) {
            std::free(ptr_);
            ptr_ = other.ptr_;
            count_ = other.count_;
            other.ptr_ = nullptr;
            other.count_ = 0;
        }
        return *this;
    }
    
    T* get() const noexcept { return ptr_; }
    size_t size() const noexcept { return count_; }
    T& operator[](size_t index) const {
        if (index >= count_) {
            throw std::out_of_range("Buffer index out of range");
        }
        return ptr_[index];
    }
    
private:
    T* ptr_;
    size_t count_;
};

} // namespace rtes