#pragma once

/**
 * @file memory_pool.hpp
 * @brief Lock-free memory pool with ABA-safe Treiber stack free list
 *
 * This pool provides the "allocation-free hot path" guarantee:
 *   - All memory pre-allocated at construction
 *   - allocate() and deallocate() are O(1), lock-free
 *   - No system calls, no heap allocation during operation
 *
 * Thread safety:
 *   - allocate() is safe to call from ANY thread (MPMC)
 *   - deallocate() is safe to call from ANY thread (MPMC)
 *   - Multiple matching engines can deallocate concurrently
 *   - Gateway thread can allocate concurrently with deallocations
 *
 * ABA protection:
 *   Free list head is a 64-bit tagged pointer:
 *     [generation:32][index:32]
 *   Generation increments on every push/pop, preventing ABA.
 *   With 32-bit generation, ABA requires 4 billion operations
 *   between a thread's preemption and resume — effectively impossible.
 *
 * Memory layout:
 *   pool_:  Cache-line-aligned array of T (contiguous)
 *   next_:  Cache-line-aligned array of uint32_t (free list links)
 *   head_:  Atomic tagged pointer (own cache line)
 *   stats_: Atomic counters (own cache line)
 *
 * Debug mode (NDEBUG not defined):
 *   - Tracks ownership per slot (detects double-free)
 *   - Validates pointer range on deallocate
 *   - Asserts on pool exhaustion (configurable)
 *
 * @tparam T Element type. Must be default-constructible.
 */

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>
#include <stdexcept>  
#include <string>     

namespace rtes {

// Forward declaration for OrderPool alias
struct Order;

namespace detail {

inline constexpr size_t POOL_CACHE_LINE = 64;
inline constexpr uint32_t POOL_EMPTY = UINT32_MAX;

/**
 * Tagged index for ABA-safe lock-free stack.
 *
 * Layout (64-bit):
 *   Bits  0-31: Index into pool (or POOL_EMPTY)
 *   Bits 32-63: Generation counter (monotonically increasing)
 *
 * The generation counter prevents ABA:
 *   Even if index wraps back to the same value,
 *   the generation will differ, causing CAS to fail.
 */
struct TaggedIndex {
    uint32_t index;
    uint32_t generation;
};

[[nodiscard]] inline uint64_t pack(uint32_t index, uint32_t gen) {
    return (static_cast<uint64_t>(gen) << 32) | index;
}

[[nodiscard]] inline TaggedIndex unpack(uint64_t packed) {
    return {
        static_cast<uint32_t>(packed),
        static_cast<uint32_t>(packed >> 32)
    };
}

/**
 * Allocate cache-line-aligned memory.
 * @throws std::bad_alloc on failure
 */
template<typename U>
[[nodiscard]] U* alloc_aligned(size_t count) {
    const size_t bytes = sizeof(U) * count;
    const size_t aligned =
        (bytes + POOL_CACHE_LINE - 1) & ~(POOL_CACHE_LINE - 1);

    void* ptr = std::aligned_alloc(POOL_CACHE_LINE, aligned);
    if (!ptr) throw std::bad_alloc();

    std::memset(ptr, 0, aligned);
    return static_cast<U*>(ptr);
}

} // namespace detail

template<typename T>
class MemoryPool {
    static_assert(std::is_default_constructible_v<T>,
        "MemoryPool requires default-constructible T");

public:
    /**
     * @param capacity Maximum number of elements.
     *                 Must be < UINT32_MAX (4 billion).
     * @throws std::bad_alloc if memory allocation fails
     * @throws std::invalid_argument if capacity is 0 or too large
     */
    explicit MemoryPool(size_t capacity)
        : capacity_(validate_capacity(capacity))
        , pool_(detail::alloc_aligned<T>(capacity))
        , next_(detail::alloc_aligned<uint32_t>(capacity))
    {
        // Default-construct all pool elements
        for (size_t i = 0; i < capacity_; ++i) {
            new (&pool_[i]) T();
        }

        // This means allocate() returns elements in order: 0, 1, 2, ...
        // which is cache-friendly for sequential allocation patterns.
        for (size_t i = 0; i < capacity_ - 1; ++i) {
            next_[i] = static_cast<uint32_t>(i + 1);
        }
        next_[capacity_ - 1] = detail::POOL_EMPTY;

        // Head points to first free element (index 0, generation 0)
        head_.store(detail::pack(0, 0), std::memory_order_relaxed);

        // Initialize stats
        allocated_.store(0, std::memory_order_relaxed);
        high_water_.store(0, std::memory_order_relaxed);

#ifndef NDEBUG
        // Debug: track ownership per slot
        owned_ = detail::alloc_aligned<std::atomic<uint32_t>>(capacity_);
        for (size_t i = 0; i < capacity_; ++i) {
            owned_[i].store(0, std::memory_order_relaxed);  // 0 = free
        }
#endif
    }

    ~MemoryPool() {
        // Destroy all elements (even if pool isn't fully deallocated)
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (size_t i = 0; i < capacity_; ++i) {
                pool_[i].~T();
            }
        }
        std::free(pool_);
        std::free(next_);
#ifndef NDEBUG
        std::free(owned_);
#endif
    }

    // Non-copyable, non-movable (elements hold pointers into pool)
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    MemoryPool(MemoryPool&&) = delete;
    MemoryPool& operator=(MemoryPool&&) = delete;

    // ═══════════════════════════════════════════════════════
    //  Allocation — O(1) lock-free pop from Treiber stack
    // ═══════════════════════════════════════════════════════

    /**
     * Allocate one element from the pool.
     *
     * Lock-free: Uses CAS on tagged head pointer.
     * ABA-safe: Generation counter prevents the ABA problem.
     *
     * Returns a pointer into the pre-allocated pool.
     * The element is default-constructed (from pool init),
     * but may contain stale data from previous use.
     * Caller should initialize all fields before use.
     *
     * @return Pointer to allocated element, or nullptr if pool exhausted
     */
    [[nodiscard]] T* allocate() {
        uint64_t old_head = head_.load(std::memory_order_acquire);

        for (;;) {
            auto [index, gen] = detail::unpack(old_head);

            // Pool exhausted
            if (index == detail::POOL_EMPTY) [[unlikely]] {
                return nullptr;
            }

            // Read next pointer BEFORE CAS
            // This is safe: we haven't claimed the slot yet, but the slot
            // is on the free list so no other thread is writing to next_[index].
            // Other threads may be reading it too (concurrent allocators),
            // but that's fine — next_[index] is immutable while on the free list.
            const uint32_t next_index = next_[index];

            // CAS: try to advance head from (index,gen) to (next_index,gen+1)
            const uint64_t new_head = detail::pack(next_index, gen + 1);

            if (head_.compare_exchange_weak(old_head, new_head,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {

                // Successfully claimed slot 'index'
                update_stats_allocate();

#ifndef NDEBUG
                // Debug: mark as owned (detect double-allocate)
                uint32_t prev = owned_[index].exchange(1, std::memory_order_relaxed);
                assert(prev == 0 && "Double allocation detected — "
                       "slot was already allocated");
#endif

                return &pool_[index];
            }

            // CAS failed — old_head updated with current value, retry
            // _mm_pause not needed: CAS failure gives us fresh data
        }
    }

    // ═══════════════════════════════════════════════════════
    //  Deallocation — O(1) lock-free push to Treiber stack
    // ═══════════════════════════════════════════════════════

    /**
     * Return an element to the pool.
     *
     * Lock-free: Uses CAS on tagged head pointer.
     * ABA-safe: Generation counter incremented on every push.
     *
     * IMPORTANT: The pointer MUST have been returned by allocate()
     * on THIS pool instance. Passing foreign pointers is undefined.
     *
     * @param ptr Pointer to deallocate (must be from this pool, non-null)
     */
    void deallocate(T* ptr) {
        if (!ptr) [[unlikely]] return;

        // Validate pointer is within pool range
        const auto index = static_cast<size_t>(ptr - pool_);

        assert(ptr >= pool_ && ptr < pool_ + capacity_ &&
               "Pointer not from this pool");
        assert(index < capacity_ &&
               "Index out of range — pointer arithmetic error");

        // Runtime check (always, not just debug — security critical)
        if (index >= capacity_) [[unlikely]] {
            // Log and discard — better than corrupting the free list
            return;
        }

#ifndef NDEBUG
        // Debug: verify slot is currently owned (detect double-free)
        uint32_t prev = owned_[index].exchange(0, std::memory_order_relaxed);
        assert(prev == 1 && "Double-free detected — "
               "slot was already free");
        if (prev != 1) return;  // Graceful handling in release
#endif

        // Push slot back onto free list
        uint64_t old_head = head_.load(std::memory_order_acquire);

        for (;;) {
            auto [head_index, gen] = detail::unpack(old_head);

            // Point this slot's next to current head
            next_[index] = head_index;

            // CAS: try to make this slot the new head
            const uint64_t new_head =
                detail::pack(static_cast<uint32_t>(index), gen + 1);

            if (head_.compare_exchange_weak(old_head, new_head,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {

                // Successfully returned slot to free list
                update_stats_deallocate();
                return;
            }

            // CAS failed — old_head updated, retry
            // next_[index] will be overwritten on next iteration
        }
    }

    // ═══════════════════════════════════════════════════════
    //  Statistics (lock-free, eventually consistent)
    // ═══════════════════════════════════════════════════════

    struct Stats {
        size_t capacity;         // Total pool size
        size_t allocated;        // Currently allocated
        size_t available;        // Currently free (approx)
        size_t high_water_mark;  // Peak allocation
        double utilization;      // allocated / capacity
    };

    /**
     * Get pool statistics. All values are eventually consistent.
     * Safe to call from any thread (monitoring).
     */
    [[nodiscard]] Stats get_stats() const {
        const size_t alloc = allocated_.load(std::memory_order_relaxed);
        return Stats{
            .capacity        = capacity_,
            .allocated       = alloc,
            .available       = capacity_ - alloc,
            .high_water_mark = high_water_.load(std::memory_order_relaxed),
            .utilization     = static_cast<double>(alloc) /
                               static_cast<double>(capacity_),
        };
    }

    /** Number of currently allocated elements */
    [[nodiscard]] size_t allocated() const {
        return allocated_.load(std::memory_order_relaxed);
    }

    /** Number of currently free elements (approximate) */
    [[nodiscard]] size_t available() const {
        return capacity_ - allocated_.load(std::memory_order_relaxed);
    }

    /** Total pool capacity */
    [[nodiscard]] size_t capacity() const { return capacity_; }

    /**
     * Check if a pointer belongs to this pool.
     * Useful for debug assertions and error handling.
     */
    [[nodiscard]] bool owns(const T* ptr) const {
        return ptr >= pool_ && ptr < pool_ + capacity_;
    }

    /**
     * Get the index of a pool element.
     * @pre owns(ptr) must be true
     */
    [[nodiscard]] size_t index_of(const T* ptr) const {
        assert(owns(ptr));
        return static_cast<size_t>(ptr - pool_);
    }

private:
    // ═══════════════════════════════════════════════════════
    //  READ-ONLY DATA (set once at construction)
    // ═══════════════════════════════════════════════════════

    const size_t capacity_;
    T* const     pool_;         // Pre-allocated element array (aligned)
    uint32_t*    next_;         // Free list links: next_[i] → next free slot

    // ═══════════════════════════════════════════════════════
    //  HOT DATA — accessed on every allocate/deallocate
    // ═══════════════════════════════════════════════════════

    /**
     * Tagged free list head.
     *   Bits  0-31: Index of first free slot (or POOL_EMPTY)
     *   Bits 32-63: Generation counter (ABA protection)
     *
     * Own cache line to avoid false sharing with stats.
     */
    alignas(detail::POOL_CACHE_LINE)
    std::atomic<uint64_t> head_;

    // ═══════════════════════════════════════════════════════
    //  STATISTICS — own cache line (read by monitoring)
    // ═══════════════════════════════════════════════════════

    alignas(detail::POOL_CACHE_LINE)
    std::atomic<size_t> allocated_{0};
    std::atomic<size_t> high_water_{0};

    // ═══════════════════════════════════════════════════════
    //  DEBUG-ONLY OWNERSHIP TRACKING
    // ═══════════════════════════════════════════════════════

#ifndef NDEBUG
    std::atomic<uint32_t>* owned_{nullptr};  // Per-slot: 0=free, 1=allocated
#endif

    // ═══════════════════════════════════════════════════════
    //  Internal Helpers
    // ═══════════════════════════════════════════════════════

    /**
     * Validate capacity at construction.
     * Must be > 0 and fit in uint32_t (for tagged index).
     */
    [[nodiscard]] static size_t validate_capacity(size_t cap) {
        if (cap == 0) {
            throw std::invalid_argument("MemoryPool capacity must be > 0");
        }
        if (cap >= detail::POOL_EMPTY) {
            throw std::invalid_argument(
                "MemoryPool capacity must be < " +
                std::to_string(detail::POOL_EMPTY));
        }
        return cap;
    }

    /**
     * Update allocation statistics.
     * Using relaxed ordering — stats are approximate and
     * the cost of seq_cst would be unacceptable in the hot path.
     */
    void update_stats_allocate() {
        const size_t current =
            allocated_.fetch_add(1, std::memory_order_relaxed) + 1;

        // Update high water mark (lock-free max)
        size_t hw = high_water_.load(std::memory_order_relaxed);
        while (current > hw) {
            if (high_water_.compare_exchange_weak(
                    hw, current, std::memory_order_relaxed)) {
                break;
            }
        }
    }

    void update_stats_deallocate() {
        allocated_.fetch_sub(1, std::memory_order_relaxed);
    }
};

// ═══════════════════════════════════════════════════════════════
//  Type Alias
// ═══════════════════════════════════════════════════════════════

using OrderPool = MemoryPool<Order>;

} // namespace rtes