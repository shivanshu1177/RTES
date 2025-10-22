#pragma once

#include "rtes/observability.hpp"
#include "rtes/secure_config.hpp"
#include "rtes/thread_safety.hpp"
#include <string>
#include <vector>
#include <functional>
#include <chrono>

namespace rtes {

enum class ReadinessStatus {
    PASS,
    WARN,
    FAIL
};

struct ReadinessCheck {
    std::string name;
    std::string category;
    std::function<ReadinessStatus()> check_fn;
    std::string description;
    bool critical{true};
};

struct ReadinessResult {
    ReadinessStatus status;
    std::string message;
    std::vector<std::string> details;
    std::chrono::milliseconds duration;
};

class ProductionReadinessValidator {
public:
    static ProductionReadinessValidator& instance();
    
    void register_check(const ReadinessCheck& check);
    std::unordered_map<std::string, ReadinessResult> run_all_checks();
    ReadinessResult run_health_checks();
    ReadinessResult validate_security_configuration();
    ReadinessResult verify_backup_and_recovery();
    
    bool is_production_ready() const;
    std::string generate_readiness_report() const;

private:
    mutable std::shared_mutex checks_mutex_;
    std::vector<ReadinessCheck> checks_ GUARDED_BY(checks_mutex_);
    
    void setup_default_checks();
    ReadinessResult run_performance_validation();
    ReadinessResult run_security_validation();
    ReadinessResult run_infrastructure_validation();
};

// Chaos Engineering
enum class ChaosScenario {
    NETWORK_PARTITION,
    HIGH_LATENCY,
    MEMORY_PRESSURE,
    CPU_SPIKE,
    DISK_FULL,
    CONNECTION_DROPS,
    RANDOM_FAILURES
};

struct ChaosExperiment {
    std::string name;
    ChaosScenario scenario;
    std::chrono::seconds duration;
    double intensity{0.1}; // 0.0 to 1.0
    std::function<void()> setup_fn;
    std::function<void()> cleanup_fn;
    std::function<bool()> success_criteria;
};

class ChaosEngineer {
public:
    static ChaosEngineer& instance();
    
    void register_experiment(const ChaosExperiment& experiment);
    bool run_experiment(const std::string& name);
    void run_all_experiments();
    
    void inject_network_latency(std::chrono::milliseconds latency);
    void inject_memory_pressure(double percentage);
    void inject_random_failures(double failure_rate);
    void stop_all_chaos();

private:
    mutable std::shared_mutex experiments_mutex_;
    std::vector<ChaosExperiment> experiments_ GUARDED_BY(experiments_mutex_);
    atomic_wrapper<bool> chaos_active_{false};
    
    void setup_default_experiments();
};

// Load Testing
struct LoadTestConfig {
    uint32_t concurrent_clients{100};
    uint32_t orders_per_second{10000};
    std::chrono::seconds duration{60};
    std::vector<std::string> symbols{"AAPL", "GOOGL", "MSFT"};
    bool enable_chaos{false};
};

struct LoadTestResult {
    uint64_t total_orders;
    uint64_t successful_orders;
    uint64_t failed_orders;
    double avg_latency_us;
    double p99_latency_us;
    double throughput_ops;
    bool passed_sla;
};

class LoadTester {
public:
    static LoadTester& instance();
    
    LoadTestResult run_load_test(const LoadTestConfig& config);
    bool validate_performance_under_load();
    void generate_load_test_report(const LoadTestResult& result);

private:
    void simulate_client_load(const LoadTestConfig& config, LoadTestResult& result);
    void inject_chaos_during_test();
};

// Blue-Green Deployment
enum class DeploymentSlot {
    BLUE,
    GREEN
};

class BlueGreenDeployment {
public:
    static BlueGreenDeployment& instance();
    
    Result<void> prepare_deployment(DeploymentSlot target_slot, const std::string& version);
    Result<void> switch_traffic(DeploymentSlot target_slot);
    Result<void> rollback_deployment();
    
    DeploymentSlot get_active_slot() const;
    DeploymentSlot get_standby_slot() const;
    
    bool validate_slot_health(DeploymentSlot slot);

private:
    atomic_wrapper<DeploymentSlot> active_slot_{DeploymentSlot::BLUE};
    std::string blue_version_;
    std::string green_version_;
    
    bool start_instance_in_slot(DeploymentSlot slot, const std::string& version);
    void stop_instance_in_slot(DeploymentSlot slot);
};

// Incident Response
enum class IncidentSeverity {
    P1_CRITICAL,    // System down, trading halted
    P2_HIGH,        // Degraded performance, some impact
    P3_MEDIUM,      // Minor issues, monitoring required
    P4_LOW          // Informational, no immediate action
};

struct Incident {
    std::string id;
    std::string title;
    std::string description;
    IncidentSeverity severity;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point resolved_at;
    std::vector<std::string> actions_taken;
    bool resolved{false};
};

class IncidentManager {
public:
    static IncidentManager& instance();
    
    std::string create_incident(const std::string& title, const std::string& description, 
                               IncidentSeverity severity);
    void add_action(const std::string& incident_id, const std::string& action);
    void resolve_incident(const std::string& incident_id);
    
    std::vector<Incident> get_active_incidents() const;
    void execute_runbook(const std::string& runbook_name);

private:
    mutable std::shared_mutex incidents_mutex_;
    std::vector<Incident> incidents_ GUARDED_BY(incidents_mutex_);
    std::unordered_map<std::string, std::function<void()>> runbooks_;
    
    void setup_default_runbooks();
    std::string generate_incident_id();
};

// Disaster Recovery
struct BackupConfig {
    std::string backup_location;
    std::chrono::hours retention_period{24 * 30}; // 30 days
    bool enable_encryption{true};
    std::string encryption_key_id;
};

class DisasterRecovery {
public:
    static DisasterRecovery& instance();
    
    Result<void> create_backup(const BackupConfig& config);
    Result<void> restore_from_backup(const std::string& backup_id);
    Result<void> validate_backup_integrity(const std::string& backup_id);
    
    std::vector<std::string> list_available_backups() const;
    bool test_disaster_recovery_procedure();

private:
    std::string backup_system_state(const BackupConfig& config);
    bool verify_backup_completeness(const std::string& backup_id);
};

} // namespace rtes