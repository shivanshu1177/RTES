#pragma once

#include "rtes/types.hpp"
#include <memory>
#include <atomic>
#include <vector>

namespace rtes {

template<typename T>
class MemoryPool {
public:
    explicit MemoryPool(size_t capacity) 
        : capacity_(capacity), pool_(capacity), free_list_(capacity) {
        
        // Initialize free list with all indices
        for (size_t i = 0; i < capacity; ++i) {
            free_list_[i] = capacity - 1 - i;
        }
        free_count_.store(capacity);
    }
    
    // O(1) allocation
    T* allocate() {
        auto count = free_count_.load(std::memory_order_acquire);
        while (count > 0) {
            if (free_count_.compare_exchange_weak(count, count - 1, 
                                                std::memory_order_acq_rel)) {
                auto index = free_list_[count - 1];
                return &pool_[index];
            }
        }
        return nullptr; // Pool exhausted
    }
    
    // O(1) deallocation
    void deallocate(T* ptr) {
        if (!ptr) return;
        
        auto index = ptr - pool_.data();
        if (index < 0 || index >= static_cast<ptrdiff_t>(capacity_)) return;
        
        auto count = free_count_.load(std::memory_order_acquire);
        while (count < capacity_) {
            free_list_[count] = static_cast<size_t>(index);
            if (free_count_.compare_exchange_weak(count, count + 1,
                                                std::memory_order_acq_rel)) {
                break;
            }
        }
    }
    
    size_t available() const {
        return free_count_.load(std::memory_order_acquire);
    }
    
    size_t capacity() const { return capacity_; }

private:
    const size_t capacity_;
    std::vector<T> pool_;
    std::vector<size_t> free_list_;
    std::atomic<size_t> free_count_{0};
};

using OrderPool = MemoryPool<Order>;

} // namespace rtes