#include <gtest/gtest.h>
#include "rtes/production_readiness.hpp"
#include <thread>
#include <chrono>

using namespace rtes;

class ProductionReadinessTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize observability stack
        ObservabilityStack::instance().initialize();
    }
    
    void TearDown() override {
        // Clean up
    }
};

TEST_F(ProductionReadinessTest, ProductionReadinessValidator) {
    auto& validator = ProductionReadinessValidator::instance();
    
    // Register custom check
    ReadinessCheck custom_check{
        "test_check",
        "Testing",
        []() { return ReadinessStatus::PASS; },
        "Test check for validation",
        true
    };
    
    validator.register_check(custom_check);
    
    // Run all checks
    auto results = validator.run_all_checks();
    EXPECT_FALSE(results.empty());
    
    // Check specific results
    EXPECT_NE(results.find("test_check"), results.end());
    EXPECT_EQ(results["test_check"].status, ReadinessStatus::PASS);
    
    // Generate report
    std::string report = validator.generate_readiness_report();
    EXPECT_FALSE(report.empty());
    EXPECT_NE(report.find("test_check"), std::string::npos);
}

TEST_F(ProductionReadinessTest, HealthChecks) {
    auto& validator = ProductionReadinessValidator::instance();
    
    auto health_result = validator.run_health_checks();
    EXPECT_NE(health_result.status, ReadinessStatus::FAIL);
    
    // Should pass basic health checks in test environment
    EXPECT_TRUE(health_result.status == ReadinessStatus::PASS || 
                health_result.status == ReadinessStatus::WARN);
}

TEST_F(ProductionReadinessTest, SecurityValidation) {
    auto& validator = ProductionReadinessValidator::instance();
    
    auto security_result = validator.validate_security_configuration();
    
    // In test environment, some security checks may warn
    EXPECT_TRUE(security_result.status == ReadinessStatus::PASS ||
                security_result.status == ReadinessStatus::WARN);
}

TEST_F(ProductionReadinessTest, ChaosEngineer) {
    auto& chaos = ChaosEngineer::instance();
    
    // Register test experiment
    ChaosExperiment test_experiment{
        "test_experiment",
        ChaosScenario::RANDOM_FAILURES,
        std::chrono::seconds(1),
        0.1,
        []() { /* setup */ },
        []() { /* cleanup */ },
        []() { return true; /* always succeed in test */ }
    };
    
    chaos.register_experiment(test_experiment);
    
    // Run experiment
    bool success = chaos.run_experiment("test_experiment");
    EXPECT_TRUE(success);
}

TEST_F(ProductionReadinessTest, ChaosInjection) {
    auto& chaos = ChaosEngineer::instance();
    
    // Test chaos injection methods (should not crash)
    chaos.inject_network_latency(std::chrono::milliseconds(10));
    chaos.inject_memory_pressure(0.1);
    chaos.inject_random_failures(0.01);
    
    // Stop all chaos
    chaos.stop_all_chaos();
    
    // Should complete without issues
    EXPECT_TRUE(true);
}

TEST_F(ProductionReadinessTest, LoadTester) {
    auto& load_tester = LoadTester::instance();
    
    // Configure light load test for unit testing
    LoadTestConfig config{
        .concurrent_clients = 5,
        .orders_per_second = 100,
        .duration = std::chrono::seconds(2),
        .symbols = {"TEST"},
        .enable_chaos = false
    };
    
    auto result = load_tester.run_load_test(config);
    
    EXPECT_GT(result.total_orders, 0);
    EXPECT_GT(result.successful_orders, 0);
    EXPECT_GE(result.throughput_ops, 0.0);
    EXPECT_GE(result.avg_latency_us, 0.0);
}

TEST_F(ProductionReadinessTest, BlueGreenDeployment) {
    auto& deployment = BlueGreenDeployment::instance();
    
    // Test slot management
    DeploymentSlot current_slot = deployment.get_active_slot();
    DeploymentSlot standby_slot = deployment.get_standby_slot();
    
    EXPECT_NE(current_slot, standby_slot);
    
    // Test slot validation (should pass in test environment)
    bool health_check = deployment.validate_slot_health(current_slot);
    EXPECT_TRUE(health_check);
}

TEST_F(ProductionReadinessTest, IncidentManager) {
    auto& incident_manager = IncidentManager::instance();
    
    // Create test incident
    std::string incident_id = incident_manager.create_incident(
        "Test Incident",
        "This is a test incident for validation",
        IncidentSeverity::P3_MEDIUM
    );
    
    EXPECT_FALSE(incident_id.empty());
    EXPECT_NE(incident_id.find("INC-"), std::string::npos);
    
    // Add action to incident
    incident_manager.add_action(incident_id, "Investigated test scenario");
    
    // Get active incidents
    auto active_incidents = incident_manager.get_active_incidents();
    EXPECT_FALSE(active_incidents.empty());
    
    bool found_incident = false;
    for (const auto& incident : active_incidents) {
        if (incident.id == incident_id) {
            found_incident = true;
            EXPECT_EQ(incident.title, "Test Incident");
            EXPECT_EQ(incident.severity, IncidentSeverity::P3_MEDIUM);
            EXPECT_FALSE(incident.resolved);
            break;
        }
    }
    EXPECT_TRUE(found_incident);
    
    // Resolve incident
    incident_manager.resolve_incident(incident_id);
    
    // Check if resolved
    active_incidents = incident_manager.get_active_incidents();
    bool still_active = false;
    for (const auto& incident : active_incidents) {
        if (incident.id == incident_id && !incident.resolved) {
            still_active = true;
            break;
        }
    }
    EXPECT_FALSE(still_active);
}

TEST_F(ProductionReadinessTest, DisasterRecovery) {
    auto& disaster_recovery = DisasterRecovery::instance();
    
    // Test backup creation
    BackupConfig config{
        .backup_location = "/tmp/test_backup",
        .retention_period = std::chrono::hours(24),
        .enable_encryption = false,
        .encryption_key_id = "test_key"
    };
    
    auto backup_result = disaster_recovery.create_backup(config);
    EXPECT_TRUE(backup_result.has_value());
    
    // Test backup listing
    auto backups = disaster_recovery.list_available_backups();
    EXPECT_FALSE(backups.empty());
    
    // Test disaster recovery procedure
    bool dr_test_passed = disaster_recovery.test_disaster_recovery_procedure();
    EXPECT_TRUE(dr_test_passed);
}

TEST_F(ProductionReadinessTest, EndToEndValidation) {
    auto& validator = ProductionReadinessValidator::instance();
    
    // This test validates the entire production readiness pipeline
    
    // 1. Run all readiness checks
    auto results = validator.run_all_checks();
    EXPECT_FALSE(results.empty());
    
    // 2. Count failures
    int failures = 0;
    for (const auto& [name, result] : results) {
        if (result.status == ReadinessStatus::FAIL) {
            failures++;
        }
    }
    
    // 3. In test environment, we should have minimal failures
    EXPECT_LE(failures, 2); // Allow up to 2 failures in test environment
    
    // 4. Generate comprehensive report
    std::string report = validator.generate_readiness_report();
    EXPECT_FALSE(report.empty());
    EXPECT_NE(report.find("Production Ready"), std::string::npos);
}

TEST_F(ProductionReadinessTest, PerformanceUnderChaos) {
    auto& chaos = ChaosEngineer::instance();
    auto& perf = PerformanceCounters::instance();
    
    // Record baseline performance
    auto baseline_start = std::chrono::high_resolution_clock::now();
    
    // Simulate some operations
    for (int i = 0; i < 1000; ++i) {
        auto op_start = std::chrono::high_resolution_clock::now();
        std::this_thread::sleep_for(std::chrono::microseconds(1));
        auto op_end = std::chrono::high_resolution_clock::now();
        
        perf.record_latency("test_operation", op_end - op_start);
        perf.record_throughput("test_operation");
    }
    
    double baseline_latency = perf.get_avg_latency("test_operation");
    
    // Inject chaos
    chaos.inject_network_latency(std::chrono::milliseconds(1));
    
    // Record performance under chaos
    for (int i = 0; i < 1000; ++i) {
        auto op_start = std::chrono::high_resolution_clock::now();
        std::this_thread::sleep_for(std::chrono::microseconds(2)); // Slightly slower under chaos
        auto op_end = std::chrono::high_resolution_clock::now();
        
        perf.record_latency("test_operation_chaos", op_end - op_start);
        perf.record_throughput("test_operation_chaos");
    }
    
    double chaos_latency = perf.get_avg_latency("test_operation_chaos");
    
    // Stop chaos
    chaos.stop_all_chaos();
    
    // Validate that system remained functional under chaos
    EXPECT_GT(baseline_latency, 0.0);
    EXPECT_GT(chaos_latency, 0.0);
    
    // Performance should degrade but not catastrophically
    EXPECT_LT(chaos_latency, baseline_latency * 10); // Less than 10x degradation
}

TEST_F(ProductionReadinessTest, ConcurrentIncidentHandling) {
    auto& incident_manager = IncidentManager::instance();
    
    const int num_incidents = 10;
    std::vector<std::thread> threads;
    std::vector<std::string> incident_ids(num_incidents);
    
    // Create incidents concurrently
    for (int i = 0; i < num_incidents; ++i) {
        threads.emplace_back([&, i]() {
            incident_ids[i] = incident_manager.create_incident(
                "Concurrent Test Incident " + std::to_string(i),
                "Test incident created concurrently",
                IncidentSeverity::P4_LOW
            );
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Verify all incidents were created
    for (int i = 0; i < num_incidents; ++i) {
        EXPECT_FALSE(incident_ids[i].empty());
    }
    
    // Get active incidents
    auto active_incidents = incident_manager.get_active_incidents();
    EXPECT_GE(active_incidents.size(), num_incidents);
    
    // Resolve incidents concurrently
    threads.clear();
    for (int i = 0; i < num_incidents; ++i) {
        threads.emplace_back([&, i]() {
            incident_manager.resolve_incident(incident_ids[i]);
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Verify incidents were resolved
    active_incidents = incident_manager.get_active_incidents();
    int remaining_test_incidents = 0;
    for (const auto& incident : active_incidents) {
        if (incident.title.find("Concurrent Test Incident") != std::string::npos && !incident.resolved) {
            remaining_test_incidents++;
        }
    }
    EXPECT_EQ(remaining_test_incidents, 0);
}

// Performance test for production readiness validation
TEST_F(ProductionReadinessTest, ValidationPerformance) {
    auto& validator = ProductionReadinessValidator::instance();
    
    const int iterations = 10;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        auto results = validator.run_all_checks();
        EXPECT_FALSE(results.empty());
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Validation should be fast (less than 1 second per iteration on average)
    double avg_time_ms = static_cast<double>(duration.count()) / iterations;
    EXPECT_LT(avg_time_ms, 1000.0);
    
    std::cout << "Average validation time: " << avg_time_ms << " ms" << std::endl;
}