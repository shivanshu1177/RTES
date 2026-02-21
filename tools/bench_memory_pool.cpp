#include "rtes/memory_pool.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace rtes;

int main() {
    // Pool sized to exactly cover all iterations — no heap fallback on hot path.
    const size_t N = 10'000;
    OrderPool pool(N);

    std::vector<Order*> ptrs;
    ptrs.reserve(N);

    // Warm up
    for (size_t i = 0; i < 100; ++i) {
        auto* p = pool.acquire();
        if (p) ptrs.push_back(p);
    }
    for (auto* p : ptrs) pool.release(p);
    ptrs.clear();

    auto t0 = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < N; ++i) {
        auto* p = pool.acquire();
        if (p) ptrs.push_back(p);
    }
    for (auto* p : ptrs) pool.release(p);

    auto t1 = std::chrono::high_resolution_clock::now();
    auto total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

    size_t ops = ptrs.size() * 2;  // acquire + release pairs
    double ns_per_op = static_cast<double>(total_ns) / ops;

    std::cout << "\n=== RTES Memory Pool Benchmark ===\n"
              << "  Pool capacity : " << N << " Order objects (pre-allocated, no heap fallback)\n"
              << "  Operations    : " << ops << " (" << ptrs.size() << " acquire + "
                                     << ptrs.size() << " release)\n"
              << "  Total time    : " << total_ns / 1000 << " μs\n"
              << "  Avg per op    : " << std::fixed << std::setprecision(1)
                                      << ns_per_op << " ns\n\n"
              << "  [" << (ptrs.size() == N ? "PASS" : "FAIL")
              << "] Zero heap fallback (all " << N << " acquires returned from pool)\n\n";

    return (ptrs.size() == N) ? 0 : 1;
}
