#pragma once

/**
 * @file spsc_queue.hpp
 * @brief Lock-free Single-Producer Single-Consumer bounded queue
 *
 * This is the critical data path between RiskManager and MatchingEngine.
 * Every order flows through this queue. Every nanosecond matters.
 *
 * Optimizations over naive ring buffer:
 *
 *   1. POWER-OF-2 CAPACITY + BITMASK
 *      `index & mask_` replaces `index % capacity_`.
 *      Cost: 1 cycle vs 20-40 cycles (eliminates integer division).
 *
 *   2. CACHED HEAD/TAIL
 *      Producer caches consumer's tail; consumer caches producer's head.
 *      Cross-core atomic load only when cached value says full/empty.
 *      Reduces cache line transfers from every-op to rare-event.
 *      This is the LMAX Disruptor / Folly pattern.
 *
 *   3. MONOTONIC INDICES
 *      head_ and tail_ increment forever (never wrap to 0).
 *      Buffer index = value & mask_.
 *      Unsigned overflow is well-defined in C++ — arithmetic works
 *      even when indices wrap past SIZE_MAX.
 *      Eliminates the "waste one slot" trick and simplifies full/empty.
 *
 *   4. CACHE LINE ISOLATION
 *      Producer data (head_ + cached_tail_) on one cache line.
 *      Consumer data (tail_ + cached_head_) on another cache line.
 *      Shared read-only data (capacity, mask, buffer ptr) on a third.
 *      Zero false sharing.
 *
 *   5. ALIGNED BUFFER ALLOCATION
 *      Buffer is cache-line aligned via std::aligned_alloc.
 *      Ensures buffer[0] starts on a cache line boundary.
 *
 *   6. PREFETCH SUPPORT
 *      consumer_prefetch_next() prefetches the next slot to be read.
 *      Called during batch drain to hide memory latency.
 *
 * Memory ordering (minimal — only what's required):
 *   Producer: relaxed read of own head_, acquire read of tail_ (rare),
 *             release store of head_ (makes buffer write visible)
 *   Consumer: relaxed read of own tail_, acquire read of head_ (rare),
 *             release store of tail_ (makes buffer read visible)
 *
 * Requirements:
 *   - T must be trivially copyable (for memcpy-safe slot assignment)
 *   - Single producer thread, single consumer thread (undefined otherwise)
 *
 * @tparam T Element type (must be trivially copyable)
 */

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>

#ifdef __x86_64__
#include <immintrin.h>
#endif

namespace rtes {

namespace detail {
#ifndef RTES_DETAIL_QUEUE_CONSTANTS_
#define RTES_DETAIL_QUEUE_CONSTANTS_

inline constexpr size_t CACHE_LINE = 64;

/**
 * Round up to next power of 2.
 * Used to ensure capacity allows bitmask indexing.
 * Compile-time evaluation for constexpr inputs.
 */
[[nodiscard]] constexpr size_t round_up_pow2(size_t v) {
    if (v == 0) return 1;
    --v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    return v + 1;
}

#endif // RTES_DETAIL_QUEUE

} // namespace detail

template<typename T>
class SPSCQueue {
    static_assert(std::is_trivially_copyable_v<T>,
        "SPSCQueue requires trivially copyable T for lock-free safety. "
        "If T has std::string or other non-trivial members, redesign T.");

public:
    /**
     * @param min_capacity Minimum number of slots. Rounded UP to power of 2.
     *                     E.g., 65536 → 65536. 50000 → 65536.
     * @throws std::bad_alloc if buffer allocation fails
     */
    explicit SPSCQueue(size_t min_capacity)
        : capacity_(detail::round_up_pow2(min_capacity))
        , mask_(capacity_ - 1)
        , buffer_(allocate_buffer(capacity_))
    {
        assert(capacity_ >= min_capacity);
        assert((capacity_ & mask_) == 0);  // Power of 2 invariant
    }

    ~SPSCQueue() {
        std::free(buffer_);
    }

    // Non-copyable, non-movable (shared between producer/consumer threads)
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&) = delete;
    SPSCQueue& operator=(SPSCQueue&&) = delete;

    // ═══════════════════════════════════════════════════════
    //  Producer API (call from SINGLE producer thread only)
    // ═══════════════════════════════════════════════════════

    /**
     * Push an element to the queue.
     *
     * Fast path (queue not full): no cross-core atomic reads.
     * Slow path (cached tail says full): one acquire load of tail_.
     *
     * @param item Element to copy into queue
     * @return true if pushed, false if queue is full
     */
    [[nodiscard]] bool push(const T& item) {
        const size_t head = head_.load(std::memory_order_relaxed);

        // Fast path: check cached tail (no cross-core read)
        if (head - cached_tail_ >= capacity_) [[unlikely]] {
            // Slow path: reload tail from consumer's cache line
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (head - cached_tail_ >= capacity_) {
                return false;  // Truly full
            }
        }

        // Write element to slot (producer owns this slot until head_ advances)
        buffer_[head & mask_] = item;

        // Publish: make buffer write visible, then advance head
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    /**
     * Push with move semantics.
     */
    [[nodiscard]] bool push(T&& item) {
        const size_t head = head_.load(std::memory_order_relaxed);

        if (head - cached_tail_ >= capacity_) [[unlikely]] {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (head - cached_tail_ >= capacity_) {
                return false;
            }
        }

        buffer_[head & mask_] = std::move(item);
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // ═══════════════════════════════════════════════════════
    //  Consumer API (call from SINGLE consumer thread only)
    // ═══════════════════════════════════════════════════════

    /**
     * Pop an element from the queue.
     *
     * Fast path (queue not empty): no cross-core atomic reads.
     * Slow path (cached head = tail): one acquire load of head_.
     *
     * @param[out] item Element copied from queue
     * @return true if popped, false if queue is empty
     */
    [[nodiscard]] bool pop(T& item) {
        const size_t tail = tail_.load(std::memory_order_relaxed);

        // Fast path: check cached head (no cross-core read)
        if (tail == cached_head_) [[unlikely]] {
            // Slow path: reload head from producer's cache line
            cached_head_ = head_.load(std::memory_order_acquire);
            if (tail == cached_head_) {
                return false;  // Truly empty
            }
        }

        // Read element from slot (consumer owns this slot until tail_ advances)
        item = buffer_[tail & mask_];

        // Publish: make buffer read complete, then advance tail
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    /**
     * Prefetch the next element to be popped.
     * Call after a successful pop() to hide memory latency
     * for the next pop in a batch drain loop.
     *
     * Brings the next queue slot into L1 cache (~1-3ns)
     * while the current element is being processed (~100ns+).
     */
    void prefetch_next() const {
#ifdef __x86_64__
        const size_t tail = tail_.load(std::memory_order_relaxed);
        _mm_prefetch(
            reinterpret_cast<const char*>(&buffer_[tail & mask_]),
            _MM_HINT_T0);
#endif
    }

    /**
     * Check if queue is empty from consumer's perspective.
     * Cheaper than empty() — uses cached head first, only
     * falls back to acquire load if cached value says empty.
     *
     * Designed for the spin-wait loop in MatchingEngine.
     *
     * @return true if queue appears empty
     */
    [[nodiscard]] bool consumer_empty() {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail != cached_head_) return false;

        // Cached head says empty — reload from producer
        cached_head_ = head_.load(std::memory_order_acquire);
        return tail == cached_head_;
    }

    // ═══════════════════════════════════════════════════════
    //  Shared API (callable from any thread, approximate)
    // ═══════════════════════════════════════════════════════

    /**
     * Approximate empty check. Two acquire loads.
     * Result may be stale by the time caller uses it.
     * For monitoring/diagnostics only.
     */
    [[nodiscard]] bool empty() const {
        // Load tail first (lower value) to avoid transient negative size
        const size_t tail = tail_.load(std::memory_order_acquire);
        const size_t head = head_.load(std::memory_order_acquire);
        return head == tail;
    }

    /**
     * Approximate size. May be briefly inconsistent.
     * For monitoring/diagnostics only — never use for control flow.
     */
    [[nodiscard]] size_t size() const {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        return head - tail;  // Unsigned arithmetic handles overflow
    }

    /** Total queue capacity (always a power of 2). */
    [[nodiscard]] size_t capacity() const { return capacity_; }

private:
    // ═══════════════════════════════════════════════════════
    //  SHARED READ-ONLY DATA (written once at construction)
    //  Both threads read — never written after ctor.
    //  Safe to share a cache line.
    // ═══════════════════════════════════════════════════════

    const size_t capacity_;   // Power of 2
    const size_t mask_;       // capacity_ - 1 (bitmask for index)
    T* const     buffer_;     // Cache-line-aligned heap allocation

    // ═══════════════════════════════════════════════════════
    //  PRODUCER CACHE LINE
    //  Only the producer thread writes here.
    //  cached_tail_ is the producer's local copy of tail_.
    //  Only reloaded when cached value says "full".
    // ═══════════════════════════════════════════════════════

    alignas(detail::CACHE_LINE)
    std::atomic<size_t> head_{0};      // Monotonically increasing
    size_t              cached_tail_{0}; // Last known tail (avoids acquire)

    // ═══════════════════════════════════════════════════════
    //  CONSUMER CACHE LINE
    //  Only the consumer thread writes here.
    //  cached_head_ is the consumer's local copy of head_.
    //  Only reloaded when cached value says "empty".
    // ═══════════════════════════════════════════════════════

    alignas(detail::CACHE_LINE)
    std::atomic<size_t> tail_{0};      // Monotonically increasing
    size_t              cached_head_{0}; // Last known head (avoids acquire)

    // ═══════════════════════════════════════════════════════
    //  Buffer Allocation
    // ═══════════════════════════════════════════════════════

    /**
     * Allocate cache-line-aligned buffer.
     * std::aligned_alloc requires size to be a multiple of alignment.
     */
    [[nodiscard]] static T* allocate_buffer(size_t cap) {
        const size_t bytes = cap * sizeof(T);

        // Round up to cache line boundary (required by aligned_alloc)
        const size_t aligned_bytes =
            (bytes + detail::CACHE_LINE - 1) & ~(detail::CACHE_LINE - 1);

        void* ptr = std::aligned_alloc(detail::CACHE_LINE, aligned_bytes);
        if (!ptr) throw std::bad_alloc();

        // Zero-initialize (safe for trivially copyable types)
        std::memset(ptr, 0, aligned_bytes);

        return static_cast<T*>(ptr);
    }
};

} // namespace rtes