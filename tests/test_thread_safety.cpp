#include <gtest/gtest.h>
#include "rtes/thread_safety.hpp"
#include <thread>
#include <vector>
#include <chrono>

using namespace rtes;

class ThreadSafetyTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset singletons for clean test state
    }
    
    void TearDown() override {
        // Clean up after tests
    }
};

TEST_F(ThreadSafetyTest, AtomicWrapperBasicOperations) {
    atomic_wrapper<int> counter(0);
    
    EXPECT_EQ(counter.load(), 0);
    
    counter.store(42);
    EXPECT_EQ(counter.load(), 42);
    
    int old_value = counter.exchange(100);
    EXPECT_EQ(old_value, 42);
    EXPECT_EQ(counter.load(), 100);
    
    int expected = 100;
    bool success = counter.compare_exchange_strong(expected, 200);
    EXPECT_TRUE(success);
    EXPECT_EQ(counter.load(), 200);
}

TEST_F(ThreadSafetyTest, AtomicWrapperConcurrency) {
    atomic_wrapper<int> counter(0);
    const int num_threads = 10;
    const int increments_per_thread = 1000;
    
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&counter, increments_per_thread]() {
            for (int j = 0; j < increments_per_thread; ++j) {
                int current = counter.load();
                while (!counter.compare_exchange_weak(current, current + 1)) {
                    // Retry on failure
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(counter.load(), num_threads * increments_per_thread);
}

TEST_F(ThreadSafetyTest, ScopedLockBasic) {
    std::mutex mutex1, mutex2;
    bool critical_section_executed = false;
    
    {
        scoped_lock lock(mutex1, mutex2);
        critical_section_executed = true;
    }
    
    EXPECT_TRUE(critical_section_executed);
}

TEST_F(ThreadSafetyTest, ScopedLockDeadlockDetection) {
    std::mutex mutex1, mutex2;
    
    // This should work fine
    {
        scoped_lock lock(mutex1, mutex2);
    }
    
    // Test potential deadlock scenario
    std::atomic<bool> thread1_started{false};
    std::atomic<bool> thread2_started{false};
    std::atomic<bool> deadlock_detected{false};
    
    std::thread t1([&]() {
        try {
            thread1_started = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            scoped_lock lock(mutex1, mutex2);
        } catch (const std::runtime_error& e) {
            if (std::string(e.what()).find("deadlock") != std::string::npos) {
                deadlock_detected = true;
            }
        }
    });
    
    std::thread t2([&]() {
        try {
            thread2_started = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            scoped_lock lock(mutex2, mutex1);
        } catch (const std::runtime_error& e) {
            if (std::string(e.what()).find("deadlock") != std::string::npos) {
                deadlock_detected = true;
            }
        }
    });
    
    t1.join();
    t2.join();
    
    EXPECT_TRUE(thread1_started.load());
    EXPECT_TRUE(thread2_started.load());
}

TEST_F(ThreadSafetyTest, ConditionVariableSafe) {
    std::mutex mutex;
    condition_variable_safe cv;
    atomic_wrapper<bool> ready(false);
    atomic_wrapper<bool> processed(false);
    
    std::thread worker([&]() {
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return ready.load(); });
        processed.store(true);
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    {
        std::unique_lock lock(mutex);
        ready.store(true);
    }
    cv.notify_one();
    
    worker.join();
    
    EXPECT_TRUE(processed.load());
}

TEST_F(ThreadSafetyTest, ShutdownManagerBasic) {
    auto& manager = ShutdownManager::instance();
    
    atomic_wrapper<bool> component1_shutdown(false);
    atomic_wrapper<bool> component2_shutdown(false);
    
    manager.register_component("test1", [&]() { component1_shutdown.store(true); });
    manager.register_component("test2", [&]() { component2_shutdown.store(true); });
    
    EXPECT_FALSE(manager.is_shutdown_requested());
    
    manager.initiate_shutdown();
    
    EXPECT_TRUE(manager.is_shutdown_requested());
    EXPECT_TRUE(component1_shutdown.load());
    EXPECT_TRUE(component2_shutdown.load());
}

TEST_F(ThreadSafetyTest, WorkDrainerBasic) {
    WorkDrainer drainer("test_drainer");
    atomic_wrapper<int> work_count(0);
    
    // Add some work items
    for (int i = 0; i < 5; ++i) {
        drainer.add_work_item([&work_count]() {
            work_count.store(work_count.load() + 1);
        });
    }
    
    // Process work in separate thread
    std::thread worker([&drainer]() {
        for (int i = 0; i < 5; ++i) {
            drainer.process_work();
        }
    });
    
    worker.join();
    
    EXPECT_EQ(work_count.load(), 5);
}

TEST_F(ThreadSafetyTest, LockOrderValidatorBasic) {
    auto& validator = LockOrderValidator::instance();
    
    std::mutex mutex1, mutex2, mutex3;
    
    // Register valid lock ordering
    validator.register_lock_order(&mutex1, &mutex2);
    validator.register_lock_order(&mutex2, &mutex3);
    
    // Test valid ordering
    std::vector<void*> valid_order = {&mutex1, &mutex2, &mutex3};
    EXPECT_TRUE(validator.validate_lock_order(valid_order));
    
    // Test invalid ordering
    std::vector<void*> invalid_order = {&mutex3, &mutex1};
    EXPECT_FALSE(validator.validate_lock_order(invalid_order));
}

TEST_F(ThreadSafetyTest, RaceDetectorBasic) {
    auto& detector = RaceDetector::instance();
    
    int shared_variable = 0;
    
    // Register concurrent accesses
    std::thread t1([&]() {
        detector.register_memory_access(&shared_variable, true);  // write
        shared_variable = 42;
    });
    
    std::thread t2([&]() {
        std::this_thread::sleep_for(std::chrono::microseconds(50));
        detector.register_memory_access(&shared_variable, false); // read
        volatile int value = shared_variable;
        (void)value;
    });
    
    t1.join();
    t2.join();
    
    // Race should be detected due to concurrent write/read
    EXPECT_TRUE(detector.has_race_condition());
}

TEST_F(ThreadSafetyTest, RaceDetectorSynchronization) {
    auto& detector = RaceDetector::instance();
    
    int shared_variable = 0;
    
    std::thread t1([&]() {
        detector.register_memory_access(&shared_variable, true);
        shared_variable = 42;
        detector.register_synchronization_point();
    });
    
    t1.join();
    
    std::thread t2([&]() {
        detector.register_memory_access(&shared_variable, false);
        volatile int value = shared_variable;
        (void)value;
    });
    
    t2.join();
    
    // No race should be detected due to synchronization
    EXPECT_FALSE(detector.has_race_condition());
}

TEST_F(ThreadSafetyTest, ConcurrentOrderBookOperations) {
    // Test thread safety with actual OrderBook operations
    atomic_wrapper<int> successful_operations(0);
    atomic_wrapper<int> failed_operations(0);
    
    const int num_threads = 4;
    const int operations_per_thread = 100;
    
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < operations_per_thread; ++j) {
                try {
                    // Simulate order book operations
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                    successful_operations.store(successful_operations.load() + 1);
                } catch (...) {
                    failed_operations.store(failed_operations.load() + 1);
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(successful_operations.load(), num_threads * operations_per_thread);
    EXPECT_EQ(failed_operations.load(), 0);
}

TEST_F(ThreadSafetyTest, ShutdownCoordination) {
    auto& manager = ShutdownManager::instance();
    
    atomic_wrapper<int> shutdown_order(0);
    atomic_wrapper<int> component1_order(0);
    atomic_wrapper<int> component2_order(0);
    
    manager.register_component("component1", [&]() {
        component1_order.store(shutdown_order.exchange(shutdown_order.load() + 1) + 1);
    });
    
    manager.register_component("component2", [&]() {
        component2_order.store(shutdown_order.exchange(shutdown_order.load() + 1) + 1);
    });
    
    std::thread shutdown_thread([&]() {
        manager.initiate_shutdown();
    });
    
    shutdown_thread.join();
    
    EXPECT_TRUE(manager.is_shutdown_requested());
    EXPECT_GT(component1_order.load(), 0);
    EXPECT_GT(component2_order.load(), 0);
}

// Performance test for thread safety overhead
TEST_F(ThreadSafetyTest, PerformanceOverhead) {
    const int iterations = 1000000;
    
    // Test atomic wrapper performance
    auto start = std::chrono::high_resolution_clock::now();
    
    atomic_wrapper<int> counter(0);
    for (int i = 0; i < iterations; ++i) {
        counter.store(counter.load() + 1);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Should complete within reasonable time (less than 100ms for 1M operations)
    EXPECT_LT(duration.count(), 100000);
    EXPECT_EQ(counter.load(), iterations);
}