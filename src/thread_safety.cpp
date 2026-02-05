#include "rtes/thread_safety.hpp"
#include <algorithm>
#include <stdexcept>

namespace rtes {

// DeadlockDetector implementation
DeadlockDetector& DeadlockDetector::instance() {
    static DeadlockDetector instance;
    return instance;
}

void DeadlockDetector::register_lock_acquisition(std::thread::id tid, void* lock_addr) {
    std::lock_guard lock(detector_mutex_);
    thread_locks_[tid].push_back(lock_addr);
    lock_owners_[lock_addr] = tid;
}

void DeadlockDetector::register_lock_release(std::thread::id tid, void* lock_addr) {
    std::lock_guard lock(detector_mutex_);
    auto& locks = thread_locks_[tid];
    locks.erase(std::remove(locks.begin(), locks.end(), lock_addr), locks.end());
    lock_owners_.erase(lock_addr);
}

bool DeadlockDetector::would_cause_deadlock(std::thread::id tid, void* lock_addr) {
    std::lock_guard lock(detector_mutex_);
    
    // Check if lock is already owned by another thread
    auto owner_it = lock_owners_.find(lock_addr);
    if (owner_it == lock_owners_.end()) {
        return false; // Lock is free
    }
    
    std::thread::id owner = owner_it->second;
    if (owner == tid) {
        return false; // Same thread, no deadlock
    }
    
    // Check for circular dependency
    auto current_locks = thread_locks_[tid];
    for (void* held_lock : current_locks) {
        auto other_owner_it = lock_owners_.find(held_lock);
        if (other_owner_it != lock_owners_.end() && other_owner_it->second == owner) {
            return true; // Circular dependency detected
        }
    }
    
    return false;
}

// ShutdownManager implementation
ShutdownManager& ShutdownManager::instance() {
    static ShutdownManager instance;
    return instance;
}

void ShutdownManager::register_component(const std::string& name, std::function<void()> shutdown_fn) {
    std::unique_lock lock(mutex_);
    components_[name] = std::move(shutdown_fn);
    active_components_.store(active_components_.load() + 1);
}

void ShutdownManager::initiate_shutdown() {
    shutdown_requested_.store(true);
    
    std::shared_lock lock(mutex_);
    for (const auto& [name, shutdown_fn] : components_) {
        try {
            shutdown_fn();
        } catch (...) {
            // Log error but continue shutdown
        }
    }
    
    std::unique_lock shutdown_lock(shutdown_mutex_);
    shutdown_complete_.notify_all();
}

void ShutdownManager::wait_for_shutdown() {
    std::unique_lock lock(shutdown_mutex_);
    shutdown_complete_.wait(lock, [this] { 
        return shutdown_requested_.load() && active_components_.load() == 0; 
    });
}

bool ShutdownManager::is_shutdown_requested() const {
    return shutdown_requested_.load();
}

// LockOrderValidator implementation
LockOrderValidator& LockOrderValidator::instance() {
    static LockOrderValidator instance;
    return instance;
}

void LockOrderValidator::register_lock_order(void* first, void* second) {
    std::lock_guard lock(validator_mutex_);
    lock_graph_[first].push_back(second);
}

bool LockOrderValidator::validate_lock_order(const std::vector<void*>& locks) {
    std::lock_guard lock(validator_mutex_);
    
    for (size_t i = 0; i < locks.size(); ++i) {
        for (size_t j = i + 1; j < locks.size(); ++j) {
            void* first = locks[i];
            void* second = locks[j];
            
            auto it = lock_graph_.find(second);
            if (it != lock_graph_.end()) {
                auto& successors = it->second;
                if (std::find(successors.begin(), successors.end(), first) != successors.end()) {
                    return false; // Ordering violation
                }
            }
        }
    }
    
    return true;
}

// RaceDetector implementation
RaceDetector& RaceDetector::instance() {
    static RaceDetector instance;
    return instance;
}

void RaceDetector::register_memory_access(void* addr, bool is_write) {
    std::unique_lock lock(detector_mutex_);
    
    auto& accesses = memory_accesses_[addr];
    auto now = std::chrono::steady_clock::now();
    auto tid = std::this_thread::get_id();
    
    // Check for race conditions
    for (const auto& access : accesses) {
        if (access.thread_id != tid && 
            (is_write || access.is_write) &&
            (now - access.timestamp) < std::chrono::microseconds(100)) {
            race_detected_.store(true);
            return;
        }
    }
    
    accesses.push_back({tid, is_write, now});
    
    // Keep only recent accesses
    auto cutoff = now - std::chrono::milliseconds(1);
    accesses.erase(
        std::remove_if(accesses.begin(), accesses.end(),
                      [cutoff](const MemoryAccess& access) {
                          return access.timestamp < cutoff;
                      }),
        accesses.end()
    );
}

void RaceDetector::register_synchronization_point() {
    std::unique_lock lock(detector_mutex_);
    // Clear old accesses at synchronization points
    memory_accesses_.clear();
}

bool RaceDetector::has_race_condition() const {
    return race_detected_.load();
}

} // namespace rtes