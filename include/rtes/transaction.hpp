#pragma once

#include "rtes/error_handling.hpp"
#include "rtes/types.hpp"
#include <functional>
#include <vector>
#include <memory>
#include <atomic>

namespace rtes {

// Transaction action interface
class TransactionAction {
public:
    virtual ~TransactionAction() = default;
    virtual Result<void> execute() = 0;
    virtual Result<void> rollback() = 0;
    virtual std::string description() const = 0;
};

// RAII transaction manager
class Transaction {
public:
    Transaction(std::string name = "");
    ~Transaction();
    
    // Non-copyable, movable
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction(Transaction&&) = default;
    Transaction& operator=(Transaction&&) = default;
    
    // Add action to transaction
    void add_action(std::unique_ptr<TransactionAction> action);
    
    // Execute all actions
    Result<void> commit();
    
    // Rollback all executed actions
    Result<void> rollback();
    
    // Check if transaction is active
    bool is_active() const { return state_ == State::ACTIVE; }
    bool is_committed() const { return state_ == State::COMMITTED; }
    bool is_rolled_back() const { return state_ == State::ROLLED_BACK; }
    
    const std::string& name() const { return name_; }
    
private:
    enum class State { ACTIVE, COMMITTED, ROLLED_BACK };
    
    std::string name_;
    std::vector<std::unique_ptr<TransactionAction>> actions_;
    std::vector<std::unique_ptr<TransactionAction>> executed_actions_;
    State state_ = State::ACTIVE;
    bool auto_rollback_ = true;
};

// Concrete transaction actions
class OrderBookAction : public TransactionAction {
public:
    enum class Type { ADD_ORDER, CANCEL_ORDER, EXECUTE_TRADE };
    
    OrderBookAction(Type type, OrderID order_id, std::string symbol);
    
    Result<void> execute() override;
    Result<void> rollback() override;
    std::string description() const override;
    
private:
    Type type_;
    OrderID order_id_;
    std::string symbol_;
    // Saved state for rollback
    struct SavedState {
        // Order book state snapshot
    } saved_state_;
};

class MemoryPoolAction : public TransactionAction {
public:
    enum class Type { ALLOCATE, DEALLOCATE };
    
    MemoryPoolAction(Type type, void* ptr, size_t size);
    
    Result<void> execute() override;
    Result<void> rollback() override;
    std::string description() const override;
    
private:
    Type type_;
    void* ptr_;
    size_t size_;
};

class NetworkAction : public TransactionAction {
public:
    enum class Type { SEND_MESSAGE, CLOSE_CONNECTION };
    
    NetworkAction(Type type, int connection_id, std::vector<uint8_t> data = {});
    
    Result<void> execute() override;
    Result<void> rollback() override;
    std::string description() const override;
    
private:
    Type type_;
    int connection_id_;
    std::vector<uint8_t> data_;
    bool executed_ = false;
};

// Transaction manager for coordinating multiple transactions
class TransactionManager {
public:
    static TransactionManager& instance();
    
    // Create new transaction
    std::unique_ptr<Transaction> begin_transaction(const std::string& name = "");
    
    // Global rollback for system recovery
    Result<void> rollback_all_active_transactions();
    
    // Statistics
    size_t active_transaction_count() const;
    size_t total_transactions() const { return total_transactions_.load(); }
    size_t failed_transactions() const { return failed_transactions_.load(); }
    
private:
    TransactionManager() = default;
    
    std::vector<std::weak_ptr<Transaction>> active_transactions_;
    std::atomic<size_t> total_transactions_{0};
    std::atomic<size_t> failed_transactions_{0};
    mutable std::mutex mutex_;
};

// RAII transaction scope
class TransactionScope {
public:
    explicit TransactionScope(std::string name = "");
    ~TransactionScope();
    
    Transaction& transaction() { return *transaction_; }
    
    // Commit transaction (prevents auto-rollback)
    Result<void> commit();
    
private:
    std::unique_ptr<Transaction> transaction_;
    bool committed_ = false;
};

} // namespace rtes