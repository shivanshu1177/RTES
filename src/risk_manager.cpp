#include "rtes/risk_manager.hpp"
#include "rtes/logger.hpp"
#include "rtes/security_utils.hpp"

namespace rtes {

RiskManager::RiskManager(const RiskConfig& config, const std::vector<SymbolConfig>& symbols)
    : config_(config) {
    
    // Build symbol lookup map
    for (const auto& symbol : symbols) {
        symbol_configs_[symbol.symbol] = symbol;
    }
    
    input_queue_ = std::make_unique<SPSCQueue<RiskRequest>>(65536);
}

RiskManager::~RiskManager() {
    stop();
}

void RiskManager::start() {
    if (running_.load()) return;
    
    running_.store(true);
    worker_thread_ = std::thread(&RiskManager::run, this);
    
    LOG_INFO("Risk manager started");
}

void RiskManager::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    LOG_INFO("Risk manager stopped");
}

bool RiskManager::submit_order(Order* order) {
    if (!order) return false;
    
    RiskRequest request;
    request.type = RiskRequest::NEW_ORDER;
    request.order = order;
    
    return input_queue_->push(request);
}

bool RiskManager::submit_cancel(OrderID order_id, ClientID client_id) {
    RiskRequest request;
    request.type = RiskRequest::CANCEL_ORDER;
    request.order_id = order_id;
    request.client_id = client_id;
    
    return input_queue_->push(request);
}

void RiskManager::add_matching_engine(const std::string& symbol, MatchingEngine* engine) {
    matching_engines_[symbol] = engine;
}

void RiskManager::run() {
    RiskRequest request;
    
    while (running_.load()) {
        if (input_queue_->pop(request)) {
            switch (request.type) {
                case RiskRequest::NEW_ORDER: {
                    auto result = validate_new_order(request.order);
                    if (result == RiskResult::APPROVED) {
                        // Forward to matching engine
                        std::string symbol(request.order->symbol);
                        auto it = matching_engines_.find(symbol);
                        if (it != matching_engines_.end()) {
                            it->second->submit_order(request.order);
                        }
                    } else {
                        // Reject order
                        request.order->status = OrderStatus::REJECTED;
                        orders_rejected_.fetch_add(1);
                        LOG_WARN_SAFE("Order rejected: {} for client: {}", 
                                     static_cast<int>(result), 
                                     SecurityUtils::sanitize_log_input(request.order->client_id));
                    }
                    orders_processed_.fetch_add(1);
                    break;
                }
                case RiskRequest::CANCEL_ORDER: {
                    auto result = validate_cancel_order(request.order_id, request.client_id);
                    if (result == RiskResult::APPROVED) {
                        // Forward cancel to all matching engines (symbol unknown)
                        for (auto& [symbol, engine] : matching_engines_) {
                            engine->cancel_order(request.order_id, request.client_id);
                        }
                        remove_from_client_state(request.order_id, request.client_id);
                    }
                    break;
                }
            }
        } else {
            std::this_thread::yield();
        }
    }
}

RiskResult RiskManager::validate_new_order(Order* order) {
    if (!order) return RiskResult::REJECTED_SIZE;
    
    // Validate input data
    if (!SecurityUtils::is_valid_symbol(order->symbol)) {
        LOG_WARN_SAFE("Invalid symbol format: {}", SecurityUtils::sanitize_log_input(order->symbol));
        return RiskResult::REJECTED_SYMBOL;
    }
    
    if (!SecurityUtils::is_safe_string(order->client_id)) {
        LOG_WARN_SAFE("Invalid client ID format: {}", SecurityUtils::sanitize_log_input(order->client_id));
        return RiskResult::REJECTED_SIZE;
    }
    
    // Check symbol allowed
    if (!check_symbol_allowed(order)) {
        return RiskResult::REJECTED_SYMBOL;
    }
    
    // Check order size
    if (!check_order_size(order)) {
        return RiskResult::REJECTED_SIZE;
    }
    
    // Check price collar
    if (!check_price_collar(order)) {
        return RiskResult::REJECTED_PRICE;
    }
    
    // Get or create client state
    auto& client_state = client_states_[order->client_id];
    
    // Check rate limit
    if (!check_rate_limit(client_state)) {
        return RiskResult::REJECTED_RATE_LIMIT;
    }
    
    // Check duplicate order
    if (!check_duplicate_order(order, client_state)) {
        return RiskResult::REJECTED_DUPLICATE;
    }
    
    // Check credit limit
    if (!check_credit_limit(order, client_state)) {
        return RiskResult::REJECTED_CREDIT;
    }
    
    // Update client state
    update_client_state(order, client_state);
    
    return RiskResult::APPROVED;
}

RiskResult RiskManager::validate_cancel_order(OrderID order_id, ClientID client_id) {
    // Validate input
    if (!SecurityUtils::is_safe_string(client_id)) {
        LOG_WARN_SAFE("Invalid client ID in cancel: {}", SecurityUtils::sanitize_log_input(client_id));
        return RiskResult::REJECTED_OWNERSHIP;
    }
    
    if (!check_cancel_ownership(order_id, client_id)) {
        LOG_WARN_SAFE("Cancel ownership check failed for order {} client {}", 
                     order_id, SecurityUtils::sanitize_log_input(client_id));
        return RiskResult::REJECTED_OWNERSHIP;
    }
    
    return RiskResult::APPROVED;
}

bool RiskManager::check_order_size(const Order* order) const {
    return order->quantity > 0 && order->quantity <= config_.max_order_size;
}

bool RiskManager::check_price_collar(const Order* order) const {
    if (!config_.price_collar_enabled) return true;
    
    const auto* symbol_config = get_symbol_config(order->symbol);
    if (!symbol_config) return false;
    
    // For simplicity, assume reference price is current order price
    // In production, this would use market data
    double reference_price = order->price / 10000.0;
    double collar_pct = symbol_config->price_collar_pct / 100.0;
    double min_price = reference_price * (1.0 - collar_pct);
    double max_price = reference_price * (1.0 + collar_pct);
    
    double order_price = order->price / 10000.0;
    return order_price >= min_price && order_price <= max_price;
}

bool RiskManager::check_credit_limit(const Order* order, ClientRiskState& state) const {
    double order_notional = calculate_notional(order);
    return (state.notional_exposure + order_notional) <= config_.max_notional_per_client;
}

bool RiskManager::check_rate_limit(ClientRiskState& state) {
    auto now = std::chrono::steady_clock::now();
    
    // Reset counter if more than 1 second has passed
    if (now - state.last_reset_time >= std::chrono::seconds(1)) {
        state.order_count_last_second = 0;
        state.last_reset_time = now;
    }
    
    if (state.order_count_last_second >= config_.max_orders_per_second) {
        return false;
    }
    
    state.order_count_last_second++;
    return true;
}

bool RiskManager::check_duplicate_order(const Order* order, const ClientRiskState& state) const {
    return state.active_orders.find(order->id) == state.active_orders.end();
}

bool RiskManager::check_symbol_allowed(const Order* order) const {
    return symbol_configs_.find(order->symbol) != symbol_configs_.end();
}

bool RiskManager::check_cancel_ownership(OrderID order_id, ClientID client_id) const {
    auto it = client_states_.find(client_id);
    if (it == client_states_.end()) return false;
    
    return it->second.active_orders.find(order_id) != it->second.active_orders.end();
}

void RiskManager::update_client_state(Order* order, ClientRiskState& state) {
    // Add to active orders
    state.active_orders.insert(order->id);
    
    // Update notional exposure
    state.notional_exposure += calculate_notional(order);
}

void RiskManager::remove_from_client_state(OrderID order_id, ClientID client_id) {
    auto it = client_states_.find(client_id);
    if (it != client_states_.end()) {
        it->second.active_orders.erase(order_id);
        // Note: notional exposure updated on trade/cancel notifications
    }
}

double RiskManager::calculate_notional(const Order* order) const {
    return (order->price / 10000.0) * order->quantity;
}

const SymbolConfig* RiskManager::get_symbol_config(const std::string& symbol) const {
    auto it = symbol_configs_.find(symbol);
    return (it != symbol_configs_.end()) ? &it->second : nullptr;
}

} // namespace rtes