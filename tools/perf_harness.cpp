#include "rtes/strategies.hpp"
#include "rtes/market_data.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace rtes;

struct LatencyStats {
    std::vector<double> latencies;
    double min_latency = std::numeric_limits<double>::max();
    double max_latency = 0.0;
    double sum_latency = 0.0;
    
    void add_sample(double latency_us) {
        latencies.push_back(latency_us);
        min_latency = std::min(min_latency, latency_us);
        max_latency = std::max(max_latency, latency_us);
        sum_latency += latency_us;
    }
    
    void calculate_percentiles() {
        if (latencies.empty()) return;
        std::sort(latencies.begin(), latencies.end());
    }
    
    double get_percentile(double p) const {
        if (latencies.empty()) return 0.0;
        size_t index = static_cast<size_t>(latencies.size() * p / 100.0);
        index = std::min(index, latencies.size() - 1);
        return latencies[index];
    }
    
    double get_average() const {
        return latencies.empty() ? 0.0 : sum_latency / latencies.size();
    }
};

class PerformanceHarness {
public:
    PerformanceHarness(const std::string& host, uint16_t port) 
        : host_(host), port_(port) {}
    
    void run_comprehensive_test() {
        std::cout << "=== RTES Performance Harness ===" << std::endl;
        std::cout << "Target: " << host_ << ":" << port_ << std::endl;
        std::cout << "SLOs: ≥100K orders/sec, avg ≤10μs, p99 ≤100μs, p999 ≤500μs" << std::endl;
        std::cout << std::endl;
        
        // Verify exchange is running
        if (!verify_exchange_health()) {
            std::cerr << "Exchange health check failed" << std::endl;
            return;
        }
        
        // Run test suite
        test_single_client_latency();
        test_throughput_scaling();
        test_sustained_load();
        test_burst_handling();
        test_market_data_latency();
        
        // Generate report
        generate_performance_report();
    }

private:
    std::string host_;
    uint16_t port_;
    std::vector<std::string> test_results_;
    
    bool verify_exchange_health() {
        std::cout << "Verifying exchange health..." << std::endl;
        
        // Check HTTP health endpoint
        std::string cmd = "curl -f -s http://" + host_ + ":8080/health > /dev/null 2>&1";
        if (system(cmd.c_str()) != 0) {
            std::cout << "✗ HTTP health check failed" << std::endl;
            return false;
        }
        
        // Test TCP connectivity
        LiquidityTakerStrategy test_client(host_, port_, 99999, "AAPL");
        if (!test_client.connect()) {
            std::cout << "✗ TCP connectivity failed" << std::endl;
            return false;
        }
        test_client.disconnect();
        
        std::cout << "✓ Exchange is healthy" << std::endl;
        return true;
    }
    
    void test_single_client_latency() {
        std::cout << "\n=== Single Client Latency Test ===" << std::endl;
        
        LatencyStats stats;
        LiquidityTakerStrategy client(host_, port_, 10001, "AAPL");
        
        if (!client.connect()) {
            test_results_.push_back("Single Client Latency: FAILED (connection)");
            return;
        }
        
        // Warm up
        std::cout << "Warming up..." << std::endl;
        client.run(std::chrono::seconds(5));
        
        // Measure latency for individual orders
        std::cout << "Measuring order latency..." << std::endl;
        
        for (int i = 0; i < 1000; ++i) {
            auto start = std::chrono::high_resolution_clock::now();
            
            client.send_new_order("AAPL", Side::BUY, 100, 150000 + i);
            
            // Wait for ack (simplified - in real test would track specific order)
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            
            auto end = std::chrono::high_resolution_clock::now();
            auto latency_us = std::chrono::duration<double, std::micro>(end - start).count();
            stats.add_sample(latency_us);
        }
        
        client.disconnect();
        
        stats.calculate_percentiles();
        
        std::cout << "Results:" << std::endl;
        std::cout << "  Samples: " << stats.latencies.size() << std::endl;
        std::cout << "  Average: " << std::fixed << std::setprecision(2) << stats.get_average() << "μs" << std::endl;
        std::cout << "  Min: " << stats.min_latency << "μs" << std::endl;
        std::cout << "  Max: " << stats.max_latency << "μs" << std::endl;
        std::cout << "  P50: " << stats.get_percentile(50) << "μs" << std::endl;
        std::cout << "  P90: " << stats.get_percentile(90) << "μs" << std::endl;
        std::cout << "  P99: " << stats.get_percentile(99) << "μs" << std::endl;
        std::cout << "  P999: " << stats.get_percentile(99.9) << "μs" << std::endl;
        
        // Check SLOs
        bool avg_slo = stats.get_average() <= 10.0;
        bool p99_slo = stats.get_percentile(99) <= 100.0;
        bool p999_slo = stats.get_percentile(99.9) <= 500.0;
        
        std::cout << "  SLO Check: Avg " << (avg_slo ? "✓" : "✗") 
                  << " P99 " << (p99_slo ? "✓" : "✗")
                  << " P999 " << (p999_slo ? "✓" : "✗") << std::endl;
        
        std::string result = std::string("Single Client Latency: ") + 
                           (avg_slo && p99_slo && p999_slo ? "PASSED" : "FAILED");
        test_results_.push_back(result);
    }
    
    void test_throughput_scaling() {
        std::cout << "\n=== Throughput Scaling Test ===" << std::endl;
        
        std::vector<int> client_counts = {1, 5, 10, 20, 50};
        bool throughput_slo_met = false;
        
        for (int num_clients : client_counts) {
            std::cout << "Testing with " << num_clients << " clients..." << std::endl;
            
            std::vector<std::unique_ptr<LiquidityTakerStrategy>> clients;
            std::vector<std::thread> threads;
            
            // Create clients
            for (int i = 0; i < num_clients; ++i) {
                auto client = std::make_unique<LiquidityTakerStrategy>(host_, port_, 20000 + i, "AAPL");
                if (client->connect()) {
                    clients.push_back(std::move(client));
                }
            }
            
            if (clients.size() != static_cast<size_t>(num_clients)) {
                std::cout << "  ✗ Failed to connect all clients" << std::endl;
                continue;
            }
            
            // Run test
            auto start_time = std::chrono::high_resolution_clock::now();
            
            for (auto& client : clients) {
                threads.emplace_back([&client]() {
                    client->run(std::chrono::seconds(10));
                });
            }
            
            for (auto& thread : threads) {
                thread.join();
            }
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
            
            // Calculate total throughput
            uint64_t total_orders = 0;
            for (const auto& client : clients) {
                total_orders += client->orders_sent();
            }
            
            double throughput = static_cast<double>(total_orders) / duration.count();
            
            std::cout << "  Orders: " << total_orders << ", Throughput: " 
                      << std::fixed << std::setprecision(0) << throughput << " orders/sec" << std::endl;
            
            if (throughput >= 100000) {
                throughput_slo_met = true;
                std::cout << "  ✓ Throughput SLO met" << std::endl;
            }
            
            // Cleanup
            for (auto& client : clients) {
                client->disconnect();
            }
        }
        
        std::string result = std::string("Throughput Scaling: ") + (throughput_slo_met ? "PASSED" : "FAILED");
        test_results_.push_back(result);
    }
    
    void test_sustained_load() {
        std::cout << "\n=== Sustained Load Test ===" << std::endl;
        
        constexpr int num_clients = 20;
        constexpr int duration_minutes = 5;
        
        std::cout << "Running " << num_clients << " clients for " << duration_minutes << " minutes..." << std::endl;
        
        std::vector<std::unique_ptr<LiquidityTakerStrategy>> clients;
        std::vector<std::thread> threads;
        
        // Create diverse client mix
        for (int i = 0; i < num_clients; ++i) {
            std::unique_ptr<ClientBase> client;
            std::string symbol = (i % 3 == 0) ? "AAPL" : (i % 3 == 1) ? "MSFT" : "GOOGL";
            
            switch (i % 4) {
                case 0:
                    client = std::make_unique<MarketMakerStrategy>(host_, port_, 30000 + i, symbol);
                    break;
                case 1:
                    client = std::make_unique<LiquidityTakerStrategy>(host_, port_, 30000 + i, symbol);
                    break;
                case 2:
                    client = std::make_unique<MomentumStrategy>(host_, port_, 30000 + i, symbol);
                    break;
                case 3:
                    client = std::make_unique<ArbitrageStrategy>(host_, port_, 30000 + i, "AAPL", "MSFT");
                    break;
            }
            
            if (client->connect()) {
                clients.push_back(std::unique_ptr<LiquidityTakerStrategy>(
                    static_cast<LiquidityTakerStrategy*>(client.release())));
            }
        }
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        for (auto& client : clients) {
            threads.emplace_back([&client, duration_minutes]() {
                client->run(std::chrono::minutes(duration_minutes));
            });
        }
        
        // Monitor progress
        for (int minute = 1; minute <= duration_minutes; ++minute) {
            std::this_thread::sleep_for(std::chrono::minutes(1));
            
            uint64_t total_orders = 0;
            uint64_t total_rejected = 0;
            for (const auto& client : clients) {
                total_orders += client->orders_sent();
                total_rejected += client->orders_rejected();
            }
            
            double reject_rate = total_orders > 0 ? 
                (static_cast<double>(total_rejected) / total_orders * 100) : 0;
            
            std::cout << "  Minute " << minute << ": " << total_orders 
                      << " orders, " << reject_rate << "% rejected" << std::endl;
        }
        
        for (auto& thread : threads) {
            thread.join();
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto actual_duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
        
        uint64_t total_orders = 0;
        uint64_t total_rejected = 0;
        uint64_t total_trades = 0;
        
        for (const auto& client : clients) {
            total_orders += client->orders_sent();
            total_rejected += client->orders_rejected();
            total_trades += client->trades_received();
        }
        
        double reject_rate = total_orders > 0 ? 
            (static_cast<double>(total_rejected) / total_orders * 100) : 0;
        
        std::cout << "Results:" << std::endl;
        std::cout << "  Duration: " << actual_duration.count() << "s" << std::endl;
        std::cout << "  Total orders: " << total_orders << std::endl;
        std::cout << "  Rejected: " << total_rejected << " (" << reject_rate << "%)" << std::endl;
        std::cout << "  Trades: " << total_trades << std::endl;
        
        bool sustained_slo = reject_rate < 5.0;  // Less than 5% reject rate
        std::cout << "  SLO Check: " << (sustained_slo ? "✓" : "✗") << " Reject rate" << std::endl;
        
        std::string result = std::string("Sustained Load: ") + (sustained_slo ? "PASSED" : "FAILED");
        test_results_.push_back(result);
        
        // Cleanup
        for (auto& client : clients) {
            client->disconnect();
        }
    }
    
    void test_burst_handling() {
        std::cout << "\n=== Burst Handling Test ===" << std::endl;
        
        // Test system's ability to handle sudden load spikes
        LiquidityTakerStrategy client(host_, port_, 40001, "AAPL");
        
        if (!client.connect()) {
            test_results_.push_back("Burst Handling: FAILED (connection)");
            return;
        }
        
        std::cout << "Sending burst of orders..." << std::endl;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        // Send rapid burst
        for (int i = 0; i < 10000; ++i) {
            client.send_new_order("AAPL", Side::BUY, 100, 150000 + i);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        // Wait for processing
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        client.disconnect();
        
        double burst_rate = (10000.0 * 1e6) / duration.count();
        
        std::cout << "Results:" << std::endl;
        std::cout << "  Burst duration: " << duration.count() / 1000.0 << "ms" << std::endl;
        std::cout << "  Burst rate: " << std::fixed << std::setprecision(0) << burst_rate << " orders/sec" << std::endl;
        std::cout << "  Orders sent: " << client.orders_sent() << std::endl;
        std::cout << "  Orders acked: " << client.orders_acked() << std::endl;
        
        bool burst_slo = burst_rate >= 500000;  // 500K orders/sec burst capability
        std::cout << "  SLO Check: " << (burst_slo ? "✓" : "✗") << " Burst rate" << std::endl;
        
        std::string result = std::string("Burst Handling: ") + (burst_slo ? "PASSED" : "FAILED");
        test_results_.push_back(result);
    }
    
    void test_market_data_latency() {
        std::cout << "\n=== Market Data Latency Test ===" << std::endl;
        
        // Test UDP market data publishing latency
        int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_socket < 0) {
            test_results_.push_back("Market Data Latency: FAILED (socket)");
            return;
        }
        
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(9999);
        
        if (bind(udp_socket, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(udp_socket);
            test_results_.push_back("Market Data Latency: FAILED (bind)");
            return;
        }
        
        // Join multicast group
        struct ip_mreq mreq{};
        inet_pton(AF_INET, "239.0.0.1", &mreq.imr_multiaddr);
        mreq.imr_interface.s_addr = INADDR_ANY;
        setsockopt(udp_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
        
        // Set timeout
        struct timeval timeout{};
        timeout.tv_sec = 5;
        setsockopt(udp_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        
        std::cout << "Listening for market data..." << std::endl;
        
        // Generate some trades to trigger market data
        LiquidityTakerStrategy client(host_, port_, 50001, "AAPL");
        if (client.connect()) {
            std::thread trade_generator([&client]() {
                for (int i = 0; i < 100; ++i) {
                    client.send_new_order("AAPL", Side::BUY, 100, 150000);
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            });
            
            // Receive market data
            int messages_received = 0;
            uint8_t buffer[1024];
            
            for (int i = 0; i < 50; ++i) {  // Try to receive 50 messages
                ssize_t received = recv(udp_socket, buffer, sizeof(buffer), 0);
                if (received > 0) {
                    messages_received++;
                }
            }
            
            trade_generator.join();
            client.disconnect();
            
            std::cout << "Results:" << std::endl;
            std::cout << "  Messages received: " << messages_received << std::endl;
            
            bool md_slo = messages_received > 10;  // Should receive some market data
            std::cout << "  SLO Check: " << (md_slo ? "✓" : "✗") << " Market data flow" << std::endl;
            
            std::string result = std::string("Market Data Latency: ") + (md_slo ? "PASSED" : "FAILED");
            test_results_.push_back(result);
        } else {
            test_results_.push_back("Market Data Latency: FAILED (connection)");
        }
        
        close(udp_socket);
    }
    
    void generate_performance_report() {
        std::cout << "\n=== Performance Test Summary ===" << std::endl;
        
        int passed = 0;
        int total = test_results_.size();
        
        for (const auto& result : test_results_) {
            std::cout << result << std::endl;
            if (result.find("PASSED") != std::string::npos) {
                passed++;
            }
        }
        
        std::cout << "\nOverall: " << passed << "/" << total << " tests passed" << std::endl;
        
        if (passed == total) {
            std::cout << "🎉 All performance tests PASSED! Exchange meets SLOs." << std::endl;
        } else {
            std::cout << "⚠️  Some performance tests FAILED. Review results above." << std::endl;
        }
        
        // Save detailed report
        std::ofstream report("performance_report.txt");
        report << "RTES Performance Test Report\n";
        report << "============================\n\n";
        report << "Target: " << host_ << ":" << port_ << "\n";
        report << "Timestamp: " << std::chrono::system_clock::now().time_since_epoch().count() << "\n\n";
        
        for (const auto& result : test_results_) {
            report << result << "\n";
        }
        
        report << "\nOverall: " << passed << "/" << total << " tests passed\n";
        report.close();
        
        std::cout << "\nDetailed report saved to: performance_report.txt" << std::endl;
    }
};

int main(int argc, char* argv[]) {
    std::string host = "localhost";
    uint16_t port = 8888;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc && std::string(argv[i]) != "--help") {
            std::cerr << "Missing value for " << argv[i] << std::endl;
            return 1;
        }
        
        std::string arg = argv[i];
        if (arg == "--host") {
            host = argv[i + 1];
        } else if (arg == "--port") {
            port = std::stoi(argv[i + 1]);
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --host <host>    Exchange host (default: localhost)\n";
            std::cout << "  --port <port>    Exchange port (default: 8888)\n";
            std::cout << "  --help           Show this help\n";
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            return 1;
        }
    }
    
    PerformanceHarness harness(host, port);
    harness.run_comprehensive_test();
    
    return 0;
}