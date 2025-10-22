#include "rtes/transaction.hpp"
#include "rtes/logger.hpp"
#include <algorithm>

namespace rtes {

// Transaction implementation
Transaction::Transaction(std::string name) : name_(std::move(name)) {
    if (name_.empty()) {
        name_ = "Transaction_" + std::to_string(reinterpret_cast<uintptr_t>(this));
    }
    LOG_DEBUG_SAFE("Transaction {} started", name_);
}

Transaction::~Transaction() {
    if (state_ == State::ACTIVE && auto_rollback_) {
        LOG_WARN_SAFE("Auto-rolling back transaction {}", name_);
        rollback();
    }
}

void Transaction::add_action(std::unique_ptr<TransactionAction> action) {
    if (state_ != State::ACTIVE) {
        LOG_ERROR_SAFE("Cannot add action to inactive transaction {}", name_);
        return;
    }
    
    LOG_DEBUG_SAFE("Added action '{}' to transaction {}", action->description(), name_);
    actions_.push_back(std::move(action));
}

Result<void> Transaction::commit() {
    if (state_ != State::ACTIVE) {
        return ErrorCode::SYSTEM_CORRUPTED_STATE;
    }
    
    LOG_INFO_SAFE("Committing transaction {} with {} actions", name_, actions_.size());
    
    // Execute all actions
    for (auto& action : actions_) {
        auto result = action->execute();
        if (result.has_error()) {
            LOG_ERROR_SAFE("Action '{}' failed in transaction {}: {}", 
                          action->description(), name_, result.error().message());
            
            // Rollback executed actions
            rollback();
            return result.error();
        }
        
        executed_actions_.push_back(std::move(action));
    }
    
    actions_.clear();
    state_ = State::COMMITTED;
    auto_rollback_ = false;
    
    LOG_INFO_SAFE("Transaction {} committed successfully", name_);
    return Result<void>();
}

Result<void> Transaction::rollback() {
    if (state_ == State::ROLLED_BACK) {
        return Result<void>();
    }
    
    LOG_WARN_SAFE("Rolling back transaction {} with {} executed actions", 
                  name_, executed_actions_.size());
    
    // Rollback in reverse order
    std::error_code last_error;
    for (auto it = executed_actions_.rbegin(); it != executed_actions_.rend(); ++it) {
        auto result = (*it)->rollback();
        if (result.has_error()) {
            LOG_ERROR_SAFE("Rollback failed for action '{}': {}", 
                          (*it)->description(), result.error().message());
            last_error = result.error();
        }
    }
    
    executed_actions_.clear();
    actions_.clear();
    state_ = State::ROLLED_BACK;
    
    if (last_error) {
        return last_error;
    }
    
    LOG_INFO_SAFE("Transaction {} rolled back successfully", name_);
    return Result<void>();
}

// OrderBookAction implementation
OrderBookAction::OrderBookAction(Type type, OrderID order_id, std::string symbol)
    : type_(type), order_id_(order_id), symbol_(std::move(symbol)) {}

Result<void> OrderBookAction::execute() {
    switch (type_) {
        case Type::ADD_ORDER:
            LOG_DEBUG_SAFE("Executing add order {} for symbol {}", order_id_, symbol_);
            // Save current state for rollback
            // Execute order addition
            break;
            
        case Type::CANCEL_ORDER:
            LOG_DEBUG_SAFE("Executing cancel order {} for symbol {}", order_id_, symbol_);
            // Save current state for rollback
            // Execute order cancellation
            break;
            
        case Type::EXECUTE_TRADE:
            LOG_DEBUG_SAFE("Executing trade for order {} in symbol {}", order_id_, symbol_);
            // Save current state for rollback
            // Execute trade
            break;
    }
    
    return Result<void>();
}

Result<void> OrderBookAction::rollback() {
    switch (type_) {
        case Type::ADD_ORDER:
            LOG_DEBUG_SAFE("Rolling back add order {} for symbol {}", order_id_, symbol_);
            // Restore previous state
            break;
            
        case Type::CANCEL_ORDER:
            LOG_DEBUG_SAFE("Rolling back cancel order {} for symbol {}", order_id_, symbol_);
            // Restore previous state
            break;
            
        case Type::EXECUTE_TRADE:
            LOG_DEBUG_SAFE("Rolling back trade for order {} in symbol {}", order_id_, symbol_);
            // Restore previous state
            break;
    }
    
    return Result<void>();
}

std::string OrderBookAction::description() const {
    switch (type_) {
        case Type::ADD_ORDER: return "AddOrder(" + std::to_string(order_id_) + ")";
        case Type::CANCEL_ORDER: return "CancelOrder(" + std::to_string(order_id_) + ")";
        case Type::EXECUTE_TRADE: return "ExecuteTrade(" + std::to_string(order_id_) + ")";
    }
    return "UnknownOrderBookAction";
}

// MemoryPoolAction implementation
MemoryPoolAction::MemoryPoolAction(Type type, void* ptr, size_t size)
    : type_(type), ptr_(ptr), size_(size) {}

Result<void> MemoryPoolAction::execute() {
    switch (type_) {
        case Type::ALLOCATE:
            LOG_DEBUG_SAFE("Executing memory allocation of {} bytes", size_);
            // Perform allocation
            break;
            
        case Type::DEALLOCATE:
            LOG_DEBUG_SAFE("Executing memory deallocation of {} bytes", size_);
            // Perform deallocation
            break;
    }
    
    return Result<void>();
}

Result<void> MemoryPoolAction::rollback() {
    switch (type_) {
        case Type::ALLOCATE:
            LOG_DEBUG_SAFE("Rolling back memory allocation of {} bytes", size_);
            // Free allocated memory
            break;
            
        case Type::DEALLOCATE:
            LOG_DEBUG_SAFE("Rolling back memory deallocation of {} bytes", size_);
            // Cannot restore deallocated memory - this is a limitation
            LOG_WARN("Cannot rollback memory deallocation - memory lost");
            break;
    }
    
    return Result<void>();
}

std::string MemoryPoolAction::description() const {
    switch (type_) {
        case Type::ALLOCATE: return "MemoryAllocate(" + std::to_string(size_) + ")";
        case Type::DEALLOCATE: return "MemoryDeallocate(" + std::to_string(size_) + ")";
    }
    return "UnknownMemoryAction";
}

// NetworkAction implementation
NetworkAction::NetworkAction(Type type, int connection_id, std::vector<uint8_t> data)
    : type_(type), connection_id_(connection_id), data_(std::move(data)) {}

Result<void> NetworkAction::execute() {
    switch (type_) {
        case Type::SEND_MESSAGE:
            LOG_DEBUG_SAFE("Executing send message to connection {}", connection_id_);
            executed_ = true;
            // Send message
            break;
            
        case Type::CLOSE_CONNECTION:
            LOG_DEBUG_SAFE("Executing close connection {}", connection_id_);
            executed_ = true;
            // Close connection
            break;
    }
    
    return Result<void>();
}

Result<void> NetworkAction::rollback() {
    if (!executed_) return Result<void>();
    
    switch (type_) {
        case Type::SEND_MESSAGE:
            LOG_DEBUG_SAFE("Rolling back send message to connection {}", connection_id_);
            // Cannot unsend message - log for compensation
            LOG_WARN_SAFE("Cannot rollback sent message to connection {}", connection_id_);
            break;
            
        case Type::CLOSE_CONNECTION:
            LOG_DEBUG_SAFE("Rolling back close connection {}", connection_id_);
            // Cannot reopen closed connection
            LOG_WARN_SAFE("Cannot rollback closed connection {}", connection_id_);
            break;
    }
    
    executed_ = false;
    return Result<void>();
}

std::string NetworkAction::description() const {
    switch (type_) {
        case Type::SEND_MESSAGE: return "SendMessage(conn=" + std::to_string(connection_id_) + ")";
        case Type::CLOSE_CONNECTION: return "CloseConnection(" + std::to_string(connection_id_) + ")";
    }
    return "UnknownNetworkAction";
}

// TransactionManager implementation
TransactionManager& TransactionManager::instance() {
    static TransactionManager instance;
    return instance;
}

std::unique_ptr<Transaction> TransactionManager::begin_transaction(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto transaction = std::make_unique<Transaction>(name);
    total_transactions_.fetch_add(1);
    
    LOG_DEBUG_SAFE("Started transaction: {}", transaction->name());
    return transaction;
}

Result<void> TransactionManager::rollback_all_active_transactions() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    LOG_WARN("Rolling back all active transactions");
    
    // Clean up expired weak_ptrs and rollback active ones
    active_transactions_.erase(
        std::remove_if(active_transactions_.begin(), active_transactions_.end(),
                      [](const std::weak_ptr<Transaction>& weak_tx) {
                          return weak_tx.expired();
                      }),
        active_transactions_.end());
    
    std::error_code last_error;
    for (auto& weak_tx : active_transactions_) {
        if (auto tx = weak_tx.lock()) {
            if (tx->is_active()) {
                auto result = tx->rollback();
                if (result.has_error()) {
                    last_error = result.error();
                    failed_transactions_.fetch_add(1);
                }
            }
        }
    }
    
    active_transactions_.clear();
    
    if (last_error) {
        return last_error;
    }
    
    return Result<void>();
}

size_t TransactionManager::active_transaction_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Count non-expired transactions
    return std::count_if(active_transactions_.begin(), active_transactions_.end(),
                        [](const std::weak_ptr<Transaction>& weak_tx) {
                            return !weak_tx.expired();
                        });
}

// TransactionScope implementation
TransactionScope::TransactionScope(std::string name) {
    transaction_ = TransactionManager::instance().begin_transaction(name);
}

TransactionScope::~TransactionScope() {
    if (!committed_ && transaction_) {
        LOG_WARN_SAFE("Auto-rolling back transaction scope: {}", transaction_->name());
        transaction_->rollback();
    }
}

Result<void> TransactionScope::commit() {
    if (!transaction_) {
        return ErrorCode::SYSTEM_CORRUPTED_STATE;
    }
    
    auto result = transaction_->commit();
    if (result.has_value()) {
        committed_ = true;
    }
    
    return result;
}

} // namespace rtes