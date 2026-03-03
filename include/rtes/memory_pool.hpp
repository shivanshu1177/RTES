#pragma once

#include "types.hpp"
#include <vector>
#include <memory>
#include <mutex>

namespace rtes {

template<typename T>
class MemoryPool {
public:
    explicit MemoryPool(size_t initial_size = 1000) {
        pool_.reserve(initial_size);
        for (size_t i = 0; i < initial_size; ++i) {
            pool_.emplace_back(std::make_unique<T>());
        }
    }
    
    T* acquire() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pool_.empty()) {
            return new T{};
        }
        auto ptr = pool_.back().release();
        pool_.pop_back();
        return ptr;
    }
    
    void release(T* ptr) {
        if (!ptr) return;
        std::lock_guard<std::mutex> lock(mutex_);
        pool_.emplace_back(ptr);
    }

private:
    std::vector<std::unique_ptr<T>> pool_;
    std::mutex mutex_;
};

using OrderPool = MemoryPool<Order>;

} // namespace rtes