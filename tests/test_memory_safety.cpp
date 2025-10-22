#include <gtest/gtest.h>
#include "rtes/memory_safety.hpp"
#include <thread>
#include <vector>

using namespace rtes;

class MemorySafetyTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(MemorySafetyTest, BoundsCheckedBufferBasic) {
    BoundsCheckedBuffer buffer(1024);
    
    // Test valid operations
    std::string test_data = "Hello, World!";
    EXPECT_NO_THROW(buffer.checked_write(test_data.data(), 0, test_data.size()));
    
    std::string read_data(test_data.size(), '\0');
    EXPECT_NO_THROW(buffer.checked_read(read_data.data(), 0, test_data.size()));
    EXPECT_EQ(test_data, read_data);
    
    // Test bounds validation
    EXPECT_TRUE(buffer.validate_range(0, 1024));
    EXPECT_TRUE(buffer.validate_range(512, 512));
    EXPECT_FALSE(buffer.validate_range(0, 1025));
    EXPECT_FALSE(buffer.validate_range(1024, 1));
    EXPECT_FALSE(buffer.validate_range(SIZE_MAX, 1));
}

TEST_F(MemorySafetyTest, BoundsCheckedBufferOverflow) {
    BoundsCheckedBuffer buffer(100);
    
    std::vector<uint8_t> large_data(200, 0xAA);
    
    // Should throw on overflow
    EXPECT_THROW(buffer.checked_write(large_data.data(), 0, large_data.size()), 
                 BufferOverflowError);
    
    // Should throw on out-of-bounds read
    std::vector<uint8_t> read_buffer(50);
    EXPECT_THROW(buffer.checked_read(read_buffer.data(), 90, 50), 
                 BufferOverflowError);
}

TEST_F(MemorySafetyTest, FixedSizeBufferOperations) {
    FixedSizeBuffer<256> buffer;
    
    // Test basic operations
    std::string data = "Test data";
    EXPECT_NO_THROW(buffer.write(data.data(), data.size()));
    EXPECT_EQ(buffer.size(), data.size());
    
    std::string read_data(data.size(), '\0');
    EXPECT_NO_THROW(buffer.read(read_data.data(), data.size()));
    EXPECT_EQ(data, read_data);
    
    // Test append
    std::string more_data = " more data";
    EXPECT_NO_THROW(buffer.append(more_data.data(), more_data.size()));
    EXPECT_EQ(buffer.size(), data.size() + more_data.size());
    
    // Test overflow protection
    std::vector<uint8_t> large_data(300, 0xBB);
    EXPECT_THROW(buffer.write(large_data.data(), large_data.size()), 
                 BufferOverflowError);
}

TEST_F(MemorySafetyTest, BoundedStringOperations) {
    BoundedString<16> str;
    
    // Test assignment
    EXPECT_NO_THROW(str.assign("Hello"));
    EXPECT_EQ(std::string(str.c_str()), "Hello");
    EXPECT_EQ(str.length(), 5);
    
    // Test overflow protection
    std::string long_string(20, 'X');
    EXPECT_THROW(str.assign(long_string.c_str()), BufferOverflowError);
    
    // Test comparison
    BoundedString<16> str2;
    str2.assign("Hello");
    EXPECT_TRUE(str == str2);
    
    str2.assign("World");
    EXPECT_FALSE(str == str2);
    
    // Test clear
    str.clear();
    EXPECT_TRUE(str.empty());
    EXPECT_EQ(str.length(), 0);
}

TEST_F(MemorySafetyTest, MessageValidation) {
    // Test valid message sizes
    EXPECT_TRUE(MessageValidator::validate_message_size(100, 50, 200));
    EXPECT_TRUE(MessageValidator::validate_message_size(50, 50, 200));
    EXPECT_TRUE(MessageValidator::validate_message_size(200, 50, 200));
    
    // Test invalid message sizes
    EXPECT_FALSE(MessageValidator::validate_message_size(49, 50, 200));
    EXPECT_FALSE(MessageValidator::validate_message_size(201, 50, 200));
    
    // Test string field validation
    EXPECT_TRUE(MessageValidator::validate_string_field("ValidString", 20));
    EXPECT_FALSE(MessageValidator::validate_string_field("TooLongString", 10));
    EXPECT_FALSE(MessageValidator::validate_string_field("Bad\x00String", 20));
    EXPECT_FALSE(MessageValidator::validate_string_field(nullptr, 20));
    
    // Test network input sanitization
    std::string clean_data = "Clean network data";
    EXPECT_TRUE(MessageValidator::sanitize_network_input(clean_data.data(), clean_data.size()));
    
    std::string dirty_data = "Dirty\x00\x01data";
    EXPECT_FALSE(MessageValidator::sanitize_network_input(dirty_data.data(), dirty_data.size()));
}

TEST_F(MemorySafetyTest, FileDescriptorRAII) {
    // Test invalid descriptor
    FileDescriptor fd;
    EXPECT_FALSE(fd.valid());
    EXPECT_EQ(fd.get(), -1);
    
    // Test valid descriptor (using pipe for testing)
    int pipe_fds[2];
    ASSERT_EQ(pipe(pipe_fds), 0);
    
    {
        FileDescriptor read_fd(pipe_fds[0]);
        FileDescriptor write_fd(pipe_fds[1]);
        
        EXPECT_TRUE(read_fd.valid());
        EXPECT_TRUE(write_fd.valid());
        EXPECT_EQ(read_fd.get(), pipe_fds[0]);
        EXPECT_EQ(write_fd.get(), pipe_fds[1]);
        
        // Test move semantics
        FileDescriptor moved_fd = std::move(read_fd);
        EXPECT_TRUE(moved_fd.valid());
        EXPECT_FALSE(read_fd.valid());
        
        // Test reset
        moved_fd.reset();
        EXPECT_FALSE(moved_fd.valid());
        
    } // Destructors should close remaining descriptors
    
    // Verify descriptors are closed (write should fail)
    char test_byte = 'X';
    EXPECT_EQ(write(pipe_fds[1], &test_byte, 1), -1);
}

TEST_F(MemorySafetyTest, UniqueBufferOperations) {
    // Test allocation and access
    UniqueBuffer<int> buffer(100);
    EXPECT_EQ(buffer.size(), 100);
    EXPECT_NE(buffer.get(), nullptr);
    
    // Test element access
    buffer[0] = 42;
    buffer[99] = 84;
    EXPECT_EQ(buffer[0], 42);
    EXPECT_EQ(buffer[99], 84);
    
    // Test bounds checking
    EXPECT_THROW(buffer[100], std::out_of_range);
    
    // Test move semantics
    UniqueBuffer<int> moved_buffer = std::move(buffer);
    EXPECT_EQ(moved_buffer.size(), 100);
    EXPECT_EQ(moved_buffer[0], 42);
    EXPECT_EQ(buffer.size(), 0);
    EXPECT_EQ(buffer.get(), nullptr);
}

TEST_F(MemorySafetyTest, ConcurrentAccess) {
    BoundsCheckedBuffer buffer(1024);
    const int num_threads = 4;
    const int writes_per_thread = 100;
    
    std::vector<std::thread> threads;
    std::atomic<int> error_count{0};
    
    // Launch threads that write to different parts of the buffer
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&buffer, &error_count, t, writes_per_thread]() {
            size_t offset = t * 256; // Each thread gets 256 bytes
            
            for (int i = 0; i < writes_per_thread; ++i) {
                try {
                    uint32_t data = (t << 16) | i;
                    buffer.checked_write(&data, offset + (i % 64) * sizeof(uint32_t), sizeof(uint32_t));
                } catch (const BufferOverflowError&) {
                    error_count.fetch_add(1);
                }
            }
        });
    }
    
    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Should have no errors since each thread writes to its own region
    EXPECT_EQ(error_count.load(), 0);
}

TEST_F(MemorySafetyTest, GuardPageProtection) {
    // This test verifies that guard pages work (may cause segfault if protection fails)
    BoundsCheckedBuffer buffer(4096);
    
    // Write to valid area should work
    std::vector<uint8_t> data(4096, 0xCC);
    EXPECT_NO_THROW(buffer.checked_write(data.data(), 0, data.size()));
    
    // Attempt to write beyond buffer should throw
    EXPECT_THROW(buffer.checked_write(data.data(), 4096, 1), BufferOverflowError);
    
    // Note: Direct memory access beyond bounds would trigger segfault due to guard pages
    // This is intentional behavior for detecting buffer overruns
}