#include "rtes/memory_pool.hpp"
#include "rtes/types.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace rtes {

void benchmark_single_thread() {
    constexpr size_t pool_size = 1000000;
    constexpr size_t iterations = 10000000;
    
    OrderPool pool(pool_size);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < iterations; ++i) {
        auto* order = pool.allocate();
        if (order) {
            pool.deallocate(order);
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    
    std::cout << "Single-thread benchmark:\n";
    std::cout << "  Iterations: " << iterations << "\n";
    std::cout << "  Total time: " << duration.count() / 1e6 << " ms\n";
    std::cout << "  Avg per op: " << duration.count() / iterations << " ns\n";
    std::cout << "  Ops/sec: " << (iterations * 1e9) / duration.count() << "\n\n";
}

void benchmark_multi_thread() {
    constexpr size_t pool_size = 1000000;
    constexpr size_t iterations_per_thread = 1000000;
    constexpr int num_threads = 4;
    
    OrderPool pool(pool_size);
    std::vector<std::thread> threads;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&]() {
            for (size_t i = 0; i < iterations_per_thread; ++i) {
                auto* order = pool.allocate();
                if (order) {
                    // Simulate minimal work
                    order->id = i;
                    pool.deallocate(order);
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    auto total_ops = num_threads * iterations_per_thread;
    
    std::cout << "Multi-thread benchmark (" << num_threads << " threads):\n";
    std::cout << "  Total ops: " << total_ops << "\n";
    std::cout << "  Total time: " << duration.count() / 1e6 << " ms\n";
    std::cout << "  Avg per op: " << duration.count() / total_ops << " ns\n";
    std::cout << "  Ops/sec: " << (total_ops * 1e9) / duration.count() << "\n\n";
}

} // namespace rtes

int main() {
    std::cout << "Memory Pool Benchmarks\n";
    std::cout << "======================\n\n";
    
    rtes::benchmark_single_thread();
    rtes::benchmark_multi_thread();
    
    return 0;
}