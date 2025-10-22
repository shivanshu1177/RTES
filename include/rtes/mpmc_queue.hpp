#pragma once

#include <atomic>
#include <memory>

namespace rtes {

template<typename T>
class MPMCQueue {
public:
    explicit MPMCQueue(size_t capacity) 
        : capacity_(capacity), buffer_(std::make_unique<Cell[]>(capacity)) {
        
        for (size_t i = 0; i < capacity; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
        
        enqueue_pos_.store(0, std::memory_order_relaxed);
        dequeue_pos_.store(0, std::memory_order_relaxed);
    }
    
    bool push(const T& item) {
        Cell* cell;
        size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        
        for (;;) {
            cell = &buffer_[pos % capacity_];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            
            if (diff == 0) {
                if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, 
                                                     std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false; // Queue full
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }
        
        cell->data = item;
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }
    
    bool pop(T& item) {
        Cell* cell;
        size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
        
        for (;;) {
            cell = &buffer_[pos % capacity_];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
            
            if (diff == 0) {
                if (dequeue_pos_.compare_exchange_weak(pos, pos + 1,
                                                     std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false; // Queue empty
            } else {
                pos = dequeue_pos_.load(std::memory_order_relaxed);
            }
        }
        
        item = cell->data;
        cell->sequence.store(pos + capacity_, std::memory_order_release);
        return true;
    }

private:
    struct Cell {
        std::atomic<size_t> sequence{0};
        T data;
    };
    
    const size_t capacity_;
    std::unique_ptr<Cell[]> buffer_;
    
    alignas(64) std::atomic<size_t> enqueue_pos_{0};
    alignas(64) std::atomic<size_t> dequeue_pos_{0};
};

} // namespace rtes