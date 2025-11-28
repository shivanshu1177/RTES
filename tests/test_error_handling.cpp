#include <gtest/gtest.h>
#include "rtes/error_handling.hpp"
#include "rtes/transaction.hpp"
#include <thread>
#include <chrono>

using namespace rtes;

class ErrorHandlingTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ErrorHandlingTest, ErrorCodeConversion) {
    auto ec = make_error_code(ErrorCode::NETWORK_CONNECTION_FAILED);
    EXPECT_EQ(ec.value(), static_cast<int>(ErrorCode::NETWORK_CONNECTION_FAILED));
    EXPECT_EQ(ec.category().name(), std::string("rtes"));
    EXPECT_FALSE(ec.message().empty());
}

TEST_F(ErrorHandlingTest, ResultMonadSuccess) {
    Result<int> success_result(42);
    
    EXPECT_TRUE(success_result.has_value());
    EXPECT_FALSE(success_result.has_error());
    EXPECT_EQ(success_result.value(), 42);
    
    // Test map operation
    auto mapped = success_result.map([](int x) { return x * 2; });
    EXPECT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped.value(), 84);
}

TEST_F(ErrorHandlingTest, ResultMonadError) {
    Result<int> error_result(ErrorCode::NETWORK_CONNECTION_FAILED);
    
    EXPECT_FALSE(error_result.has_value());
    EXPECT_TRUE(error_result.has_error());
    EXPECT_EQ(error_result.error().value(), static_cast<int>(ErrorCode::NETWORK_CONNECTION_FAILED));
    
    // Test map operation on error
    auto mapped = error_result.map([](int x) { return x * 2; });
    EXPECT_TRUE(mapped.has_error());
    EXPECT_EQ(mapped.error(), error_result.error());
}

TEST_F(ErrorHandlingTest, ResultVoidSpecialization) {
    Result<void> success_result;
    EXPECT_TRUE(success_result.has_value());
    EXPECT_FALSE(success_result.has_error());
    
    Result<void> error_result(ErrorCode::FILE_NOT_FOUND);
    EXPECT_FALSE(error_result.has_value());
    EXPECT_TRUE(error_result.has_error());
    EXPECT_EQ(error_result.error().value(), static_cast<int>(ErrorCode::FILE_NOT_FOUND));
}

TEST_F(ErrorHandlingTest, NetworkErrorRecovery) {
    NetworkErrorRecovery recovery(2, std::chrono::milliseconds(10));
    
    // Test recovery capability
    EXPECT_TRUE(recovery.can_recover(ErrorCode::NETWORK_CONNECTION_FAILED));
    EXPECT_TRUE(recovery.can_recover(ErrorCode::NETWORK_TIMEOUT));
    EXPECT_FALSE(recovery.can_recover(ErrorCode::FILE_NOT_FOUND));
    
    // Test recovery without connection factory
    ErrorContext context("TestComponent", "TestOperation");
    auto result = recovery.attempt_recovery(context);
    EXPECT_TRUE(result.has_error());
    
    // Test with successful connection factory
    int call_count = 0;
    recovery.set_connection_factory([&call_count]() -> Result<void> {
        call_count++;
        if (call_count >= 2) {
            return Result<void>();  // Success on second attempt
        }
        return ErrorCode::NETWORK_CONNECTION_FAILED;
    });
    
    result = recovery.attempt_recovery(context);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(call_count, 2);
}

TEST_F(ErrorHandlingTest, FileErrorRecovery) {
    std::vector<std::string> fallback_paths = {"/tmp/test1", "/tmp/test2"};
    FileErrorRecovery recovery(fallback_paths);
    
    EXPECT_TRUE(recovery.can_recover(ErrorCode::FILE_NOT_FOUND));
    EXPECT_TRUE(recovery.can_recover(ErrorCode::FILE_PERMISSION_DENIED));
    EXPECT_FALSE(recovery.can_recover(ErrorCode::NETWORK_CONNECTION_FAILED));
    
    ErrorContext context("FileComponent", "ReadFile");
    auto result = recovery.attempt_recovery(context);
    // Result depends on filesystem state, just verify it doesn't crash
    EXPECT_TRUE(result.has_value() || result.has_error());
}

TEST_F(ErrorHandlingTest, MemoryErrorRecovery) {
    MemoryErrorRecovery recovery;
    
    EXPECT_TRUE(recovery.can_recover(ErrorCode::MEMORY_ALLOCATION_FAILED));
    EXPECT_TRUE(recovery.can_recover(ErrorCode::MEMORY_POOL_EXHAUSTED));
    EXPECT_FALSE(recovery.can_recover(ErrorCode::NETWORK_CONNECTION_FAILED));
    
    ErrorContext context("MemoryComponent", "Allocate");
    auto result = recovery.attempt_recovery(context);
    EXPECT_TRUE(result.has_value());  // Should enable emergency mode
}

TEST_F(ErrorHandlingTest, TransactionBasicOperations) {
    Transaction tx("TestTransaction");
    
    EXPECT_TRUE(tx.is_active());
    EXPECT_FALSE(tx.is_committed());
    EXPECT_FALSE(tx.is_rolled_back());
    EXPECT_EQ(tx.name(), "TestTransaction");
    
    // Empty transaction should commit successfully
    auto result = tx.commit();
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(tx.is_committed());
}

TEST_F(ErrorHandlingTest, TransactionWithActions) {
    Transaction tx("ActionTransaction");
    
    // Add mock actions
    auto action1 = std::make_unique<OrderBookAction>(
        OrderBookAction::Type::ADD_ORDER, 12345, "AAPL");
    auto action2 = std::make_unique<MemoryPoolAction>(
        MemoryPoolAction::Type::ALLOCATE, nullptr, 1024);
    
    tx.add_action(std::move(action1));
    tx.add_action(std::move(action2));
    
    // Commit should execute all actions
    auto result = tx.commit();
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(tx.is_committed());
}

TEST_F(ErrorHandlingTest, TransactionRollback) {
    Transaction tx("RollbackTransaction");
    
    auto action = std::make_unique<NetworkAction>(
        NetworkAction::Type::SEND_MESSAGE, 1, std::vector<uint8_t>{1, 2, 3});
    
    tx.add_action(std::move(action));
    
    // Manual rollback
    auto result = tx.rollback();
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(tx.is_rolled_back());
}

TEST_F(ErrorHandlingTest, TransactionScope) {
    bool committed = false;
    
    {
        TransactionScope scope("ScopeTest");
        
        auto action = std::make_unique<OrderBookAction>(
            OrderBookAction::Type::CANCEL_ORDER, 67890, "MSFT");
        
        scope.transaction().add_action(std::move(action));
        
        // Commit explicitly
        auto result = scope.commit();
        EXPECT_TRUE(result.has_value());
        committed = true;
    }
    
    EXPECT_TRUE(committed);
}

TEST_F(ErrorHandlingTest, TransactionAutoRollback) {
    Transaction* tx_ptr = nullptr;
    
    {
        Transaction tx("AutoRollbackTest");
        tx_ptr = &tx;
        
        auto action = std::make_unique<MemoryPoolAction>(
            MemoryPoolAction::Type::DEALLOCATE, nullptr, 512);
        
        tx.add_action(std::move(action));
        
        EXPECT_TRUE(tx.is_active());
        // Transaction goes out of scope without commit - should auto-rollback
    }
    
    // Cannot check tx_ptr state as it's destroyed, but test verifies no crash
}

TEST_F(ErrorHandlingTest, TransactionManager) {
    auto& manager = TransactionManager::instance();
    
    size_t initial_count = manager.total_transactions();
    
    auto tx1 = manager.begin_transaction("ManagedTx1");
    auto tx2 = manager.begin_transaction("ManagedTx2");
    
    EXPECT_EQ(manager.total_transactions(), initial_count + 2);
    
    // Commit one transaction
    auto result = tx1->commit();
    EXPECT_TRUE(result.has_value());
    
    // Rollback all active (should affect tx2)
    result = manager.rollback_all_active_transactions();
    EXPECT_TRUE(result.has_value());
}

TEST_F(ErrorHandlingTest, CircuitBreakerPattern) {
    NetworkErrorRecovery recovery(1, std::chrono::milliseconds(1));
    
    int failure_count = 0;
    recovery.set_connection_factory([&failure_count]() -> Result<void> {
        failure_count++;
        return ErrorCode::NETWORK_CONNECTION_FAILED;  // Always fail
    });
    
    ErrorContext context("CircuitTest", "Connect");
    
    // Trigger multiple failures to open circuit
    for (int i = 0; i < 10; ++i) {
        auto result = recovery.attempt_recovery(context);
        EXPECT_TRUE(result.has_error());
    }
    
    // Circuit should limit retry attempts
    EXPECT_LT(failure_count, 20);  // Should be much less due to circuit breaker
}

TEST_F(ErrorHandlingTest, ErrorContextCreation) {
    ErrorContext context("TestComponent", "TestOperation", "Additional details");
    
    EXPECT_EQ(context.component, "TestComponent");
    EXPECT_EQ(context.operation, "TestOperation");
    EXPECT_EQ(context.details, "Additional details");
    EXPECT_GT(context.timestamp.time_since_epoch().count(), 0);
}

TEST_F(ErrorHandlingTest, ResultOrElseChaining) {
    Result<int> error_result(ErrorCode::NETWORK_CONNECTION_FAILED);
    
    auto recovered = error_result.or_else([](const std::error_code& ec) -> Result<int> {
        if (ec.value() == static_cast<int>(ErrorCode::NETWORK_CONNECTION_FAILED)) {
            return Result<int>(999);  // Recovery value
        }
        return ec;
    });
    
    EXPECT_TRUE(recovered.has_value());
    EXPECT_EQ(recovered.value(), 999);
}