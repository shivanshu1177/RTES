#include <gtest/gtest.h>
#include "rtes/spsc_queue.hpp"
#include "rtes/mpmc_queue.hpp"
#include <thread>
#include <vector>
#include <atomic>

namespace rtes {

class SPSCQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        queue = std::make_unique<SPSCQueue<int>>(1000);
    }
    
    std::unique_ptr<SPSCQueue<int>> queue;
};

TEST_F(SPSCQueueTest, BasicOperations) {
    EXPECT_TRUE(queue->empty());
    EXPECT_EQ(queue->size(), 0);
    
    EXPECT_TRUE(queue->push(42));
    EXPECT_FALSE(queue->empty());
    EXPECT_EQ(queue->size(), 1);
    
    int value;
    EXPECT_TRUE(queue->pop(value));
    EXPECT_EQ(value, 42);
    EXPECT_TRUE(queue->empty());
}

TEST_F(SPSCQueueTest, FillAndEmpty) {
    // Fill queue to capacity
    for (int i = 0; i < 1000; ++i) {
        EXPECT_TRUE(queue->push(i));
    }
    
    // Should be full now
    EXPECT_FALSE(queue->push(1000));
    
    // Empty queue
    for (int i = 0; i < 1000; ++i) {
        int value;
        EXPECT_TRUE(queue->pop(value));
        EXPECT_EQ(value, i);
    }
    
    // Should be empty now
    int value;
    EXPECT_FALSE(queue->pop(value));
}

TEST_F(SPSCQueueTest, ProducerConsumer) {
    constexpr int num_items = 10000;
    std::atomic<bool> done{false};
    std::atomic<int> consumed{0};
    
    // Producer thread
    std::thread producer([&]() {
        for (int i = 0; i < num_items; ++i) {
            while (!queue->push(i)) {
                std::this_thread::yield();
            }
        }
        done.store(true);
    });
    
    // Consumer thread
    std::thread consumer([&]() {
        int value;
        while (!done.load() || !queue->empty()) {
            if (queue->pop(value)) {
                consumed.fetch_add(1);
            } else {
                std::this_thread::yield();
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    EXPECT_EQ(consumed.load(), num_items);
}

class MPMCQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        queue = std::make_unique<MPMCQueue<int>>(1000);
    }
    
    std::unique_ptr<MPMCQueue<int>> queue;
};

TEST_F(MPMCQueueTest, BasicOperations) {
    EXPECT_TRUE(queue->push(42));
    
    int value;
    EXPECT_TRUE(queue->pop(value));
    EXPECT_EQ(value, 42);
}

TEST_F(MPMCQueueTest, MultipleProducersConsumers) {
    constexpr int num_producers = 4;
    constexpr int num_consumers = 2;
    constexpr int items_per_producer = 1000;
    
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::atomic<bool> done{false};
    
    // Start producers
    for (int p = 0; p < num_producers; ++p) {
        producers.emplace_back([&, p]() {
            for (int i = 0; i < items_per_producer; ++i) {
                int value = p * items_per_producer + i;
                while (!queue->push(value)) {
                    std::this_thread::yield();
                }
                produced.fetch_add(1);
            }
        });
    }
    
    // Start consumers
    for (int c = 0; c < num_consumers; ++c) {
        consumers.emplace_back([&]() {
            int value;
            while (!done.load() || consumed.load() < produced.load()) {
                if (queue->pop(value)) {
                    consumed.fetch_add(1);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    // Wait for producers
    for (auto& p : producers) {
        p.join();
    }
    
    done.store(true);
    
    // Wait for consumers
    for (auto& c : consumers) {
        c.join();
    }
    
    EXPECT_EQ(produced.load(), num_producers * items_per_producer);
    EXPECT_EQ(consumed.load(), produced.load());
}

} // namespace rtes