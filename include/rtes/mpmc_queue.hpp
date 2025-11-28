#pragma once

/**
 * @file mpmc_queue.hpp
 * @brief Lock-free Multi-Producer Multi-Consumer bounded queue
 *
 * Implementation: Dmitry Vyukov's bounded MPMC queue.
 * Reference: https://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue
 *
 * Used in RTES for: MatchingEngine[] → UdpPublisher
 *   Multiple matching engines (one per symbol) push trade/BBO events.
 *   Single UDP publisher thread consumes and broadcasts.
 *
 * Algorithm overview:
 *   Each cell has a sequence number that acts as a ticket.
 *   Producers: CAS enqueue_pos_ to claim a cell, write data,
 *              then publish by advancing the cell's sequence.
 *   Consumers: CAS dequeue_pos_ to claim a cell, read data,
 *              then release by advancing the cell's sequence.
 *
 *   The sequence number serves dual purpose:
 *     - Synchronization: release/acquire between producer and consumer
 *     - State machine: sequence == pos means "writable",
 *                      sequence == pos+1 means "readable",
 *                      sequence == pos+capacity means "recycled"
 *
 * Optimizations over naive implementation:
 *
 *   1. POWER-OF-2 CAPACITY + BITMASK
 *      `pos & mask_` replaces `pos % capacity_` — 1 cycle vs 20-40.
 *
 *   2. CACHE-LINE PADDED CELLS
 *      Each Cell is aligned to 64 bytes. Adjacent cells never share
 *      a cache line, eliminating false sharing between concurrent
 *      producers claiming adjacent positions.
 *
 *   3. ALIGNED BUFFER
 *      Buffer allocated with cache-line alignment via aligned_alloc.
 *
 *   4. PAUSE ON CAS FAILURE
 *      _mm_pause() after failed CAS reduces cache-line contention
 *      and yields resources to hyperthread sibling.
 *
 * Memory ordering (minimal):
 *   - Cell sequence: acquire on read (sync with writer), release on write
 *   - Position CAS: relaxed (only claims position; sync via sequence)
 *   - This matches Vyukov's original specification exactly.
 *
 * @tparam T Element type. MUST be trivially copyable.
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

// Forward declaration (defined in spsc_queue.hpp or shared detail header)
namespace detail {

#ifndef RTES_DETAIL_QUEUE_CONSTANTS_
#define RTES_DETAIL_QUEUE_CONSTANTS_

    inline constexpr size_t CACHE_LINE = 64;

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

#endif // RTES_DETAIL_QUEUE_CONSTANTS_
} // namespace detail

template<typename T>
class MPMCQueue {
    static_assert(std::is_trivially_copyable_v<T>,
        "MPMCQueue requires trivially copyable T. "
        "Non-trivial types risk data races between write and sequence publish.");

public:
    /**
     * @param min_capacity Minimum queue capacity. Rounded UP to power of 2.
     * @throws std::bad_alloc if buffer allocation fails
     */
    explicit MPMCQueue(size_t min_capacity)
        : capacity_(detail::round_up_pow2(min_capacity))
        , mask_(capacity_ - 1)
        , buffer_(allocate_buffer(capacity_))
    {
        assert(capacity_ >= min_capacity);
        assert((capacity_ & mask_) == 0);

        // Initialize cell sequence numbers
        // Cell[i].sequence = i means "cell i is writable at position i"
        for (size_t i = 0; i < capacity_; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }

        enqueue_pos_.store(0, std::memory_order_relaxed);
        dequeue_pos_.store(0, std::memory_order_relaxed);
    }

    ~MPMCQueue() {
        std::free(buffer_);
    }

    // Non-copyable, non-movable (shared between threads)
    MPMCQueue(const MPMCQueue&) = delete;
    MPMCQueue& operator=(const MPMCQueue&) = delete;
    MPMCQueue(MPMCQueue&&) = delete;
    MPMCQueue& operator=(MPMCQueue&&) = delete;

    // ═══════════════════════════════════════════════════════
    //  Producer API (thread-safe, multiple producers allowed)
    // ═══════════════════════════════════════════════════════

    /**
     * Push an element to the queue (copy).
     *
     * Algorithm:
     *   1. Load current enqueue position (relaxed)
     *   2. Load cell sequence at that position (acquire)
     *   3. If sequence == position: cell is writable
     *      - CAS enqueue_pos to claim this cell
     *      - Write data
     *      - Advance sequence (release) to make data visible
     *   4. If sequence < position: queue is full
     *   5. If sequence > position: another producer claimed it, retry
     *
     * @param item Element to copy into queue
     * @return true if pushed, false if queue is full
     */
    [[nodiscard]] bool push(const T& item) {
        Cell* cell;
        size_t pos = enqueue_pos_.load(std::memory_order_relaxed);

        for (;;) {
            cell = &buffer_[pos & mask_];
            const size_t seq = cell->sequence.load(std::memory_order_acquire);
            const auto diff = static_cast<intptr_t>(seq) -
                              static_cast<intptr_t>(pos);

            if (diff == 0) {
                // Cell is writable at this position — try to claim it
                if (enqueue_pos_.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed)) {
                    break;  // Claimed successfully
                }
                // CAS failed — pos updated with current value, retry
                // No pause needed: CAS failure already loaded fresh value
            } else if (diff < 0) {
                // Cell not yet recycled by consumer — queue is full
                return false;
            } else {
                // Another producer already claimed this cell — reload position
                pos = enqueue_pos_.load(std::memory_order_relaxed);
                pause();  // Reduce contention on enqueue_pos_ cache line
            }
        }

        // We own this cell — write data
        cell->data = item;

        // Publish: advance sequence to signal consumer that data is ready
        // Release ensures data write is visible before sequence update
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    /**
     * Push with move semantics.
     */
    [[nodiscard]] bool push(T&& item) {
        Cell* cell;
        size_t pos = enqueue_pos_.load(std::memory_order_relaxed);

        for (;;) {
            cell = &buffer_[pos & mask_];
            const size_t seq = cell->sequence.load(std::memory_order_acquire);
            const auto diff = static_cast<intptr_t>(seq) -
                              static_cast<intptr_t>(pos);

            if (diff == 0) {
                if (enqueue_pos_.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
                pause();
            }
        }

        cell->data = std::move(item);
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    // ═══════════════════════════════════════════════════════
    //  Consumer API (thread-safe, multiple consumers allowed)
    // ═══════════════════════════════════════════════════════

    /**
     * Pop an element from the queue.
     *
     * Algorithm:
     *   1. Load current dequeue position (relaxed)
     *   2. Load cell sequence at that position (acquire)
     *   3. If sequence == position+1: cell has data ready
     *      - CAS dequeue_pos to claim this cell
     *      - Read data
     *      - Advance sequence by capacity (release) to recycle cell
     *   4. If sequence < position+1: queue is empty
     *   5. If sequence > position+1: another consumer claimed it, retry
     *
     * @param[out] item Element read from queue
     * @return true if popped, false if queue is empty
     */
    [[nodiscard]] bool pop(T& item) {
        Cell* cell;
        size_t pos = dequeue_pos_.load(std::memory_order_relaxed);

        for (;;) {
            cell = &buffer_[pos & mask_];
            const size_t seq = cell->sequence.load(std::memory_order_acquire);
            const auto diff = static_cast<intptr_t>(seq) -
                              static_cast<intptr_t>(pos + 1);

            if (diff == 0) {
                // Cell has data at this position — try to claim it
                if (dequeue_pos_.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed)) {
                    break;  // Claimed successfully
                }
            } else if (diff < 0) {
                // Producer hasn't written yet — queue is empty
                return false;
            } else {
                // Another consumer already claimed this cell — reload
                pos = dequeue_pos_.load(std::memory_order_relaxed);
                pause();
            }
        }

        // We own this cell — read data
        item = cell->data;

        // Recycle: advance sequence by capacity so producer can reuse
        // Release ensures read is complete before cell becomes writable
        cell->sequence.store(pos + capacity_, std::memory_order_release);
        return true;
    }

    // ═══════════════════════════════════════════════════════
    //  Monitoring API (approximate, for diagnostics only)
    // ═══════════════════════════════════════════════════════

    /**
     * Approximate check if queue is empty.
     * Result may be stale. For monitoring only — never for control flow.
     */
    [[nodiscard]] bool empty() const {
        const size_t tail = dequeue_pos_.load(std::memory_order_relaxed);
        const size_t head = enqueue_pos_.load(std::memory_order_relaxed);
        return head <= tail;
    }

    /**
     * Approximate queue depth. May be briefly negative under contention
     * (clamped to 0). For Prometheus metrics only.
     */
    [[nodiscard]] size_t size_approx() const {
        const size_t head = enqueue_pos_.load(std::memory_order_relaxed);
        const size_t tail = dequeue_pos_.load(std::memory_order_relaxed);
        const size_t diff = head - tail;
        // Clamp: under contention, head could briefly appear behind tail
        return (diff <= capacity_) ? diff : 0;
    }

    /** Total queue capacity (power of 2). */
    [[nodiscard]] size_t capacity() const { return capacity_; }

private:
    // ═══════════════════════════════════════════════════════
    //  Cell — cache-line padded to eliminate false sharing
    // ═══════════════════════════════════════════════════════

    /**
     * Each cell occupies one or more full cache lines.
     *
     * Layout:
     *   [sequence (8B)] [data (sizeof(T))] [padding to 64B boundary]
     *
     * alignas(64) ensures:
     *   - sizeof(Cell) is a multiple of 64
     *   - Adjacent cells never share a cache line
     *   - Producer writing Cell[N] doesn't invalidate Cell[N±1]
     *
     * Without this alignment, two producers claiming adjacent cells
     * would fight over the same cache line, causing ~40-80ns of
     * cross-core invalidation traffic per operation.
     */
    struct alignas(detail::CACHE_LINE) Cell {
        std::atomic<size_t> sequence;
        T data;
    };

    static_assert(sizeof(Cell) % detail::CACHE_LINE == 0,
        "Cell size must be a multiple of cache line for array alignment");

    // ═══════════════════════════════════════════════════════
    //  Shared read-only data (set once at construction)
    // ═══════════════════════════════════════════════════════

    const size_t capacity_;     // Power of 2
    const size_t mask_;         // capacity_ - 1 (bitmask for indexing)
    Cell* const  buffer_;       // Cache-line-aligned heap allocation

    // ═══════════════════════════════════════════════════════
    //  Producer position — own cache line
    // ═══════════════════════════════════════════════════════

    alignas(detail::CACHE_LINE)
    std::atomic<size_t> enqueue_pos_{0};

    // ═══════════════════════════════════════════════════════
    //  Consumer position — own cache line
    // ═══════════════════════════════════════════════════════

    alignas(detail::CACHE_LINE)
    std::atomic<size_t> dequeue_pos_{0};

    // ═══════════════════════════════════════════════════════
    //  Allocation
    // ═══════════════════════════════════════════════════════

    /**
     * Allocate cache-line-aligned buffer for Cell array.
     */
    [[nodiscard]] static Cell* allocate_buffer(size_t cap) {
        const size_t bytes = cap * sizeof(Cell);

        // std::aligned_alloc requires size to be a multiple of alignment
        // Since sizeof(Cell) is a multiple of CACHE_LINE (due to alignas),
        // bytes is already a multiple of CACHE_LINE.
        static_assert(sizeof(Cell) % detail::CACHE_LINE == 0);

        void* ptr = std::aligned_alloc(detail::CACHE_LINE, bytes);
        if (!ptr) throw std::bad_alloc();

        // Zero-initialize all cells (safe for trivially copyable T)
        std::memset(ptr, 0, bytes);

        return static_cast<Cell*>(ptr);
    }

    // ═══════════════════════════════════════════════════════
    //  Contention reduction
    // ═══════════════════════════════════════════════════════

    /**
     * Reduce contention on CAS retry.
     *
     * _mm_pause:
     *   - Inserts ~5ns delay (Intel: PAUSE, ARM: YIELD)
     *   - Yields pipeline resources to hyperthread sibling
     *   - Reduces memory bus traffic during spin
     *   - Prevents branch misprediction penalty on retry
     */
    static void pause() {
#ifdef __x86_64__
        _mm_pause();
#elif defined(__aarch64__)
        asm volatile("yield" ::: "memory");
#endif
    }
};

} // namespace rtes