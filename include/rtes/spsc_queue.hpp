#pragma once

#include <atomic>
#include <memory>

namespace rtes {

template<typename T>
class SPSCQueue {
public:
    explicit SPSCQueue(size_t capacity) 
        : capacity_(capacity + 1), buffer_(std::make_unique<T[]>(capacity_)) {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }
    
    // Producer side - single writer
    bool push(const T& item) {
        auto head = head_.load(std::memory_order_relaxed);
        auto next_head = (head + 1) % capacity_;
        
        if (next_head == tail_.load(std::memory_order_acquire)) {
            return false; // Queue full
        }
        
        buffer_[head] = item;
        head_.store(next_head, std::memory_order_release);
        return true;
    }
    
    // Consumer side - single reader  
    bool pop(T& item) {
        auto tail = tail_.load(std::memory_order_relaxed);
        
        if (tail == head_.load(std::memory_order_acquire)) {
            return false; // Queue empty
        }
        
        item = buffer_[tail];
        tail_.store((tail + 1) % capacity_, std::memory_order_release);
        return true;
    }
    
    bool empty() const {
        return tail_.load(std::memory_order_acquire) == 
               head_.load(std::memory_order_acquire);
    }
    
    size_t size() const {
        auto head = head_.load(std::memory_order_acquire);
        auto tail = tail_.load(std::memory_order_acquire);
        return (head >= tail) ? (head - tail) : (capacity_ - tail + head);
    }

private:
    const size_t capacity_;
    std::unique_ptr<T[]> buffer_;
    
    // Cache line alignment to avoid false sharing
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};

} // namespace rtes