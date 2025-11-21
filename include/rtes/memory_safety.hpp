#pragma once

/**
 * @file memory_safety.hpp
 * @brief Bounds-checked buffers, fixed-size strings, and RAII wrappers
 *
 * This file provides safety primitives for the RTES exchange:
 *
 *   BoundedString<N> — Bounds-checked string (COLD PATH ONLY)
 *     - Has non-trivial copy assignment for safety checks
 *     - NOT trivially copyable — CANNOT be used in lock-free queues
 *     - Use FixedString<N> (from types.hpp) for queue/union contexts
 *
 *   FixedSizeBuffer<N> — Stack-allocated I/O buffer
 *     - Used in TCP gateway for zero-copy message framing
 *
 *   BoundsCheckedBuffer — Heap buffer with guard pages
 *     - Debug/development only — compiled out in release
 *
 *   FileDescriptor — RAII wrapper for POSIX file descriptors
 *
 *   AlignedBuffer<T> — Cache-aligned heap buffer with RAII
 *
 * IMPORTANT: Types used in lock-free queues (SPSC/MPMC) and unions
 * (MarketDataEvent) MUST be trivially copyable. Use FixedString<N>
 * from types.hpp, NOT BoundedString<N> from this file.
 */

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cinttypes>
#include <stdexcept>
#include <array>
#include <string>
#include <string_view>
#include <new>
#include <type_traits>

// Guard pages only in debug builds
#ifndef NDEBUG
#define RTES_ENABLE_GUARD_PAGES 1
#else
#define RTES_ENABLE_GUARD_PAGES 0
#endif

namespace rtes {

// ═══════════════════════════════════════════════════════════════
//  Exceptions
// ═══════════════════════════════════════════════════════════════

class BufferOverflowError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// ═══════════════════════════════════════════════════════════════
//  BoundsCheckedBuffer — Heap buffer with optional guard pages
// ═══════════════════════════════════════════════════════════════

/**
 * Dynamically allocated buffer with bounds-checked access.
 *
 * In debug builds: Uses mmap + mprotect guard pages before and after
 * the buffer. Any out-of-bounds access triggers SIGSEGV immediately
 * at the point of access — much easier to debug than silent corruption.
 *
 * In release builds: Uses aligned_alloc without guard pages.
 * Bounds are still software-checked on read/write calls.
 */
class BoundsCheckedBuffer {
public:
    explicit BoundsCheckedBuffer(size_t size);
    ~BoundsCheckedBuffer();

    // Non-copyable, movable
    BoundsCheckedBuffer(const BoundsCheckedBuffer&) = delete;
    BoundsCheckedBuffer& operator=(const BoundsCheckedBuffer&) = delete;
    BoundsCheckedBuffer(BoundsCheckedBuffer&& other) noexcept;
    BoundsCheckedBuffer& operator=(BoundsCheckedBuffer&& other) noexcept;

    /**
     * Bounds-checked read.
     * @throws BufferOverflowError if [offset, offset+length) exceeds buffer
     */
    void checked_read(void* dest, size_t offset, size_t length) const;

    /**
     * Bounds-checked write.
     * @throws BufferOverflowError if [offset, offset+length) exceeds buffer
     */
    void checked_write(const void* src, size_t offset, size_t length);

    /** Check if range is valid without throwing */
    [[nodiscard]] bool validate_range(size_t offset, size_t length) const noexcept {
        return offset <= size_ && length <= size_ - offset;
    }

    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] const void* data() const noexcept { return buffer_; }
    [[nodiscard]] void* data() noexcept { return buffer_; }

private:
    void* buffer_{nullptr};
    size_t size_{0};

#if RTES_ENABLE_GUARD_PAGES
    static constexpr size_t GUARD_SIZE = 4096;
    void* allocation_base_{nullptr};  // Base of mmap region
    size_t allocation_size_{0};       // Total mmap size
    void setup_guard_pages();
    void cleanup_guard_pages();
#endif
};

// ═══════════════════════════════════════════════════════════════
//  FixedSizeBuffer — Stack-allocated I/O buffer
// ═══════════════════════════════════════════════════════════════

/**
 * Fixed-capacity stack buffer for network I/O.
 * Used in TCP gateway for zero-copy message framing.
 *
 * Not trivially copyable (has used_size_ tracking).
 * Not intended for lock-free queue use.
 */
template<size_t MAX_SIZE>
class FixedSizeBuffer {
    static_assert(MAX_SIZE > 0, "Buffer size must be positive");

public:
    FixedSizeBuffer() = default;

    /**
     * Write data to buffer (overwrite from start).
     * @throws BufferOverflowError if length > capacity
     */
    void write(const void* src, size_t length) {
        if (length > MAX_SIZE) [[unlikely]] {
            throw BufferOverflowError("Write exceeds buffer capacity");
        }
        std::memcpy(data_.data(), src, length);
        used_size_ = length;
    }

    /**
     * Read data from buffer.
     * @throws BufferOverflowError if length > used size
     */
    void read(void* dest, size_t length) const {
        if (length > used_size_) [[unlikely]] {
            throw BufferOverflowError("Read exceeds used buffer size");
        }
        std::memcpy(dest, data_.data(), length);
    }

    /**
     * Append data to buffer.
     * @throws BufferOverflowError if append would exceed capacity
     */
    void append(const void* src, size_t length) {
        if (used_size_ + length > MAX_SIZE) [[unlikely]] {
            throw BufferOverflowError("Append exceeds buffer capacity");
        }
        std::memcpy(data_.data() + used_size_, src, length);
        used_size_ += length;
    }

    /** Remaining writable capacity */
    [[nodiscard]] size_t remaining() const noexcept {
        return MAX_SIZE - used_size_;
    }

    [[nodiscard]] size_t size() const noexcept { return used_size_; }
    [[nodiscard]] constexpr size_t capacity() const noexcept { return MAX_SIZE; }
    [[nodiscard]] const uint8_t* data() const noexcept { return data_.data(); }
    [[nodiscard]] uint8_t* data() noexcept { return data_.data(); }
    [[nodiscard]] bool empty() const noexcept { return used_size_ == 0; }
    [[nodiscard]] bool full() const noexcept { return used_size_ == MAX_SIZE; }
    void clear() noexcept { used_size_ = 0; }

private:
    std::array<uint8_t, MAX_SIZE> data_{};
    size_t used_size_{0};
};

// ═══════════════════════════════════════════════════════════════
//  BoundedString — Safety-checked string (COLD PATH ONLY)
// ═══════════════════════════════════════════════════════════════

/**
 * Bounds-checked fixed-capacity string.
 *
 * ╔══════════════════════════════════════════════════════════╗
 * ║  WARNING: NOT trivially copyable.                       ║
 * ║  Do NOT use in lock-free queues or unions.              ║
 * ║  Use FixedString<N> from types.hpp for those contexts.  ║
 * ╚══════════════════════════════════════════════════════════╝
 *
 * Use BoundedString for:
 *   - Configuration parsing (cold path)
 *   - Logging
 *   - Error messages
 *   - Any context where safety > performance
 *
 * @tparam MAX_LEN Capacity including null terminator.
 */
template<size_t MAX_LEN>
class BoundedString {
    static_assert(MAX_LEN > 1, "BoundedString must hold at least 1 char + null");

public:
    BoundedString() = default;

    explicit BoundedString(const char* str) {
        if (str) assign(str);
    }

    explicit BoundedString(std::string_view sv) {
        assign(sv.data(), sv.size());
    }

    // ── Assignment ──

    void assign(const char* str) {
        if (!str) {
            clear();
            return;
        }
        assign(str, std::strlen(str));
    }

    void assign(const char* str, size_t length) {
        if (length >= MAX_LEN) {
            throw BufferOverflowError(
                "String length " + std::to_string(length) +
                " exceeds max " + std::to_string(MAX_LEN - 1));
        }
        std::memcpy(data_.data(), str, length);
        data_[length] = '\0';
        length_ = length;
    }

    /**
     * Assign with truncation instead of throwing.
     * Use when silent truncation is acceptable (logging, etc.)
     */
    void assign_truncate(const char* str) {
        if (!str) {
            clear();
            return;
        }
        size_t len = std::strlen(str);
        if (len >= MAX_LEN) len = MAX_LEN - 1;
        std::memcpy(data_.data(), str, len);
        data_[len] = '\0';
        length_ = len;
    }

    // ── Accessors ──

    [[nodiscard]] const char* c_str() const noexcept { return data_.data(); }
    [[nodiscard]] size_t length() const noexcept { return length_; }
    [[nodiscard]] constexpr size_t max_length() const noexcept { return MAX_LEN - 1; }
    [[nodiscard]] bool empty() const noexcept { return length_ == 0; }

    void clear() noexcept {
        length_ = 0;
        data_[0] = '\0';
    }

    // ── Explicit conversions (no implicit!) ──

    [[nodiscard]] std::string_view view() const noexcept {
        return std::string_view(data_.data(), length_);
    }

    [[nodiscard]] std::string to_string() const {
        return std::string(data_.data(), length_);
    }

    // ── Comparison ──

    [[nodiscard]] bool operator==(const BoundedString& other) const noexcept {
        return length_ == other.length_ &&
               std::memcmp(data_.data(), other.data_.data(), length_) == 0;
    }

    [[nodiscard]] bool operator!=(const BoundedString& other) const noexcept {
        return !(*this == other);
    }

    [[nodiscard]] bool operator==(const char* str) const noexcept {
        if (!str) return length_ == 0;
        return std::strncmp(data_.data(), str, MAX_LEN) == 0;
    }

    [[nodiscard]] bool operator!=(const char* str) const noexcept {
        return !(*this == str);
    }

    // ── Assignment operators ──

    BoundedString& operator=(const char* str) {
        assign(str);
        return *this;
    }

    BoundedString& operator=(std::string_view sv) {
        assign(sv.data(), sv.size());
        return *this;
    }

    // NOTE: Default copy/move are intentionally NOT used.
    // We define copy assignment for self-assignment safety.
    BoundedString(const BoundedString& other)
        : length_(other.length_) {
        std::memcpy(data_.data(), other.data_.data(), length_ + 1);
    }

    BoundedString& operator=(const BoundedString& other) {
        if (this != &other) {
            length_ = other.length_;
            std::memcpy(data_.data(), other.data_.data(), length_ + 1);
        }
        return *this;
    }

    BoundedString(BoundedString&& other) noexcept
        : length_(other.length_) {
        std::memcpy(data_.data(), other.data_.data(), length_ + 1);
        other.clear();
    }

    BoundedString& operator=(BoundedString&& other) noexcept {
        if (this != &other) {
            length_ = other.length_;
            std::memcpy(data_.data(), other.data_.data(), length_ + 1);
            other.clear();
        }
        return *this;
    }

    ~BoundedString() = default;

private:
    std::array<char, MAX_LEN> data_{};
    size_t length_{0};
};

// Explicitly document that BoundedString is NOT trivially copyable
static_assert(!std::is_trivially_copyable_v<BoundedString<8>>,
    "BoundedString is intentionally non-trivially-copyable. "
    "Use FixedString<N> from types.hpp for lock-free queue contexts.");

// ═══════════════════════════════════════════════════════════════
//  Numeric Formatting (replaces integer operator= on BoundedString)
// ═══════════════════════════════════════════════════════════════

/**
 * Format integer into a BoundedString.
 * Free function — clearer intent than operator= overload.
 *
 * @tparam N BoundedString capacity
 * @param value Integer to format
 * @return BoundedString containing the decimal representation
 */
template<size_t N>
[[nodiscard]] BoundedString<N> to_bounded_string(uint64_t value) {
    BoundedString<N> result;
    char buf[21];  // Max digits in uint64_t + null
    int len = std::snprintf(buf, sizeof(buf), "%" PRIu64, value);
    if (len > 0 && static_cast<size_t>(len) < N) {
        result.assign(buf, static_cast<size_t>(len));
    }
    return result;
}

template<size_t N>
[[nodiscard]] BoundedString<N> to_bounded_string(int64_t value) {
    BoundedString<N> result;
    char buf[21];
    int len = std::snprintf(buf, sizeof(buf), "%" PRId64, value);
    if (len > 0 && static_cast<size_t>(len) < N) {
        result.assign(buf, static_cast<size_t>(len));
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════
//  FileDescriptor — RAII POSIX fd wrapper
// ═══════════════════════════════════════════════════════════════

/**
 * Owns a POSIX file descriptor. Closes on destruction.
 * Used for sockets, epoll fds, etc.
 */
class FileDescriptor {
public:
    explicit FileDescriptor(int fd = -1) noexcept : fd_(fd) {}
    ~FileDescriptor() { close(); }

    // Non-copyable
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    // Movable
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

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }

    /**
     * Release ownership of the fd (caller must close).
     * @return The raw fd, or -1 if invalid
     */
    int release() noexcept {
        int fd = fd_;
        fd_ = -1;
        return fd;
    }

    /**
     * Close current fd and take ownership of new one.
     */
    void reset(int new_fd = -1) noexcept {
        close();
        fd_ = new_fd;
    }

    void close() noexcept;  // Implemented in .cpp (calls ::close)

private:
    int fd_;
};

// ═══════════════════════════════════════════════════════════════
//  AlignedBuffer — Cache-aligned heap buffer with RAII
// ═══════════════════════════════════════════════════════════════

/**
 * Cache-line-aligned heap buffer. Replaces UniqueBuffer with
 * correct aligned_alloc usage and const/non-const access.
 *
 * @tparam T Element type
 */
template<typename T>
class AlignedBuffer {
public:
    /**
     * Allocate buffer for count elements.
     * @param count Number of T elements
     * @param alignment Minimum alignment (default: cache line)
     * @throws std::bad_alloc if allocation fails
     */
    explicit AlignedBuffer(size_t count, size_t alignment = 64)
        : count_(count)
    {
        const size_t bytes = sizeof(T) * count;
        // aligned_alloc requires size to be a multiple of alignment
        const size_t aligned_bytes = (bytes + alignment - 1) & ~(alignment - 1);

        ptr_ = static_cast<T*>(std::aligned_alloc(alignment, aligned_bytes));
        if (!ptr_) {
            throw std::bad_alloc();
        }

        // Zero-initialize for trivially copyable types
        if constexpr (std::is_trivially_copyable_v<T>) {
            std::memset(ptr_, 0, aligned_bytes);
        }
    }

    ~AlignedBuffer() {
        std::free(ptr_);
    }

    // Non-copyable
    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;

    // Movable
    AlignedBuffer(AlignedBuffer&& other) noexcept
        : ptr_(other.ptr_), count_(other.count_) {
        other.ptr_ = nullptr;
        other.count_ = 0;
    }

    AlignedBuffer& operator=(AlignedBuffer&& other) noexcept {
        if (this != &other) {
            std::free(ptr_);
            ptr_ = other.ptr_;
            count_ = other.count_;
            other.ptr_ = nullptr;
            other.count_ = 0;
        }
        return *this;
    }

    // ── Unchecked access (hot path) ──

    [[nodiscard]] T* get() noexcept { return ptr_; }
    [[nodiscard]] const T* get() const noexcept { return ptr_; }
    [[nodiscard]] T& operator[](size_t i) noexcept { return ptr_[i]; }
    [[nodiscard]] const T& operator[](size_t i) const noexcept { return ptr_[i]; }

    // ── Checked access (cold path / debug) ──

    [[nodiscard]] T& at(size_t i) {
        if (i >= count_) [[unlikely]] {
            throw std::out_of_range("AlignedBuffer index out of range");
        }
        return ptr_[i];
    }

    [[nodiscard]] const T& at(size_t i) const {
        if (i >= count_) [[unlikely]] {
            throw std::out_of_range("AlignedBuffer index out of range");
        }
        return ptr_[i];
    }

    // ── Metadata ──

    [[nodiscard]] size_t size() const noexcept { return count_; }
    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }

    // ── Iterator support (for range-based for) ──

    [[nodiscard]] T* begin() noexcept { return ptr_; }
    [[nodiscard]] T* end() noexcept { return ptr_ + count_; }
    [[nodiscard]] const T* begin() const noexcept { return ptr_; }
    [[nodiscard]] const T* end() const noexcept { return ptr_ + count_; }

private:
    T* ptr_{nullptr};
    size_t count_{0};
};

} // namespace rtes

// ═══════════════════════════════════════════════════════════════
//  Hash specialization for BoundedString
// ═══════════════════════════════════════════════════════════════

namespace std {
template<size_t N>
struct hash<rtes::BoundedString<N>> {
    size_t operator()(const rtes::BoundedString<N>& bs) const noexcept {
        return hash<string_view>{}(bs.view());
    }
};
} // namespace std