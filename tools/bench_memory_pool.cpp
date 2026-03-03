#include "rtes/memory_pool.hpp"
#include "rtes/logger.hpp"
#include <chrono>
#include <vector>

using namespace rtes;

int main() {
    LOG_INFO("Starting memory pool benchmark");
    
    const size_t iterations = 100000;
    OrderPool pool(1000);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<Order*> orders;
    orders.reserve(iterations);
    
    // Allocation benchmark
    for (size_t i = 0; i < iterations; ++i) {
        orders.push_back(pool.acquire());
    }
    
    // Deallocation benchmark
    for (auto* order : orders) {
        pool.release(order);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    LOG_INFO("Memory pool benchmark completed");
    LOG_INFO("Iterations: " + std::to_string(iterations));
    LOG_INFO("Total time: " + std::to_string(duration.count()) + " μs");
    LOG_INFO("Average time per operation: " + std::to_string(duration.count() / (iterations * 2)) + " μs");
    
    return 0;
}