#include "rtes/strategies.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>

using namespace rtes;

std::atomic<bool> shutdown_requested{false};

void signal_handler(int) {
    shutdown_requested.store(true);
}

class LoadGenerator {
public:
    LoadGenerator(const std::string& host, uint16_t port, const std::vector<std::string>& symbols)
        : host_(host), port_(port), symbols_(symbols) {}
    
    void run(int num_clients, int duration_seconds) {
        std::cout << "Starting load test with " << num_clients << " clients for " 
                  << duration_seconds << " seconds\n";
        
        std::vector<std::thread> client_threads;
        std::vector<std::unique_ptr<ClientBase>> clients;
        
        // Create diverse mix of strategies
        for (int i = 0; i < num_clients; ++i) {
            uint32_t client_id = 1000 + i;
            std::string symbol = symbols_[i % symbols_.size()];
            
            std::unique_ptr<ClientBase> client;
            
            switch (i % 4) {
                case 0:  // Market makers (25%)
                    client = std::make_unique<MarketMakerStrategy>(host_, port_, client_id, symbol);
                    break;
                case 1:  // Liquidity takers (25%)
                    client = std::make_unique<LiquidityTakerStrategy>(host_, port_, client_id, symbol);
                    break;
                case 2:  // Momentum traders (25%)
                    client = std::make_unique<MomentumStrategy>(host_, port_, client_id, symbol);
                    break;
                case 3:  // Arbitrageurs (25%)
                    {
                        std::string symbol2 = symbols_[(i + 1) % symbols_.size()];
                        client = std::make_unique<ArbitrageStrategy>(host_, port_, client_id, symbol, symbol2);
                    }
                    break;
            }
            
            if (client->connect()) {
                clients.push_back(std::move(client));
            } else {
                std::cerr << "Failed to connect client " << client_id << "\n";
            }
        }
        
        std::cout << "Connected " << clients.size() << " clients\n";
        
        // Start all clients
        auto start_time = std::chrono::steady_clock::now();
        
        for (auto& client : clients) {
            client_threads.emplace_back([&client, duration_seconds]() {
                client->run(std::chrono::seconds(duration_seconds));
            });
        }
        
        // Monitor progress
        while (!shutdown_requested.load()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time);
            
            if (elapsed.count() >= duration_seconds) {
                break;
            }
            
            // Print progress every 10 seconds
            if (elapsed.count() % 10 == 0 && elapsed.count() > 0) {
                print_statistics(clients, elapsed.count());
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        // Wait for all clients to finish
        for (auto& thread : client_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        
        auto end_time = std::chrono::steady_clock::now();
        auto total_elapsed = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
        
        print_final_statistics(clients, total_elapsed.count());
    }

private:
    std::string host_;
    uint16_t port_;
    std::vector<std::string> symbols_;
    
    void print_statistics(const std::vector<std::unique_ptr<ClientBase>>& clients, int elapsed_seconds) {
        uint64_t total_orders = 0;
        uint64_t total_acked = 0;
        uint64_t total_rejected = 0;
        uint64_t total_trades = 0;
        
        for (const auto& client : clients) {
            total_orders += client->orders_sent();
            total_acked += client->orders_acked();
            total_rejected += client->orders_rejected();
            total_trades += client->trades_received();
        }
        
        double orders_per_sec = static_cast<double>(total_orders) / elapsed_seconds;
        double reject_rate = total_orders > 0 ? (static_cast<double>(total_rejected) / total_orders * 100) : 0;
        
        std::cout << "[" << elapsed_seconds << "s] Orders: " << total_orders 
                  << " (" << orders_per_sec << "/s), Acked: " << total_acked
                  << ", Rejected: " << total_rejected << " (" << reject_rate << "%)"
                  << ", Trades: " << total_trades << "\n";
    }
    
    void print_final_statistics(const std::vector<std::unique_ptr<ClientBase>>& clients, int total_seconds) {
        uint64_t total_orders = 0;
        uint64_t total_acked = 0;
        uint64_t total_rejected = 0;
        uint64_t total_trades = 0;
        
        for (const auto& client : clients) {
            total_orders += client->orders_sent();
            total_acked += client->orders_acked();
            total_rejected += client->orders_rejected();
            total_trades += client->trades_received();
        }
        
        std::cout << "\n=== Load Test Results ===\n";
        std::cout << "Duration: " << total_seconds << " seconds\n";
        std::cout << "Clients: " << clients.size() << "\n";
        std::cout << "Total orders sent: " << total_orders << "\n";
        std::cout << "Orders acknowledged: " << total_acked << "\n";
        std::cout << "Orders rejected: " << total_rejected << "\n";
        std::cout << "Trades received: " << total_trades << "\n";
        
        if (total_seconds > 0) {
            double avg_orders_per_sec = static_cast<double>(total_orders) / total_seconds;
            double avg_trades_per_sec = static_cast<double>(total_trades) / total_seconds;
            std::cout << "Average order rate: " << avg_orders_per_sec << " orders/sec\n";
            std::cout << "Average trade rate: " << avg_trades_per_sec << " trades/sec\n";
        }
        
        if (total_orders > 0) {
            double reject_rate = static_cast<double>(total_rejected) / total_orders * 100;
            double fill_rate = static_cast<double>(total_trades) / total_orders * 100;
            std::cout << "Reject rate: " << reject_rate << "%\n";
            std::cout << "Fill rate: " << fill_rate << "%\n";
        }
    }
};

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n";
    std::cout << "Options:\n";
    std::cout << "  --host <host>        Exchange host (default: localhost)\n";
    std::cout << "  --port <port>        Exchange port (default: 8888)\n";
    std::cout << "  --clients <num>      Number of clients (default: 10)\n";
    std::cout << "  --duration <sec>     Test duration in seconds (default: 60)\n";
    std::cout << "  --symbols <list>     Comma-separated symbol list (default: AAPL,MSFT,GOOGL)\n";
    std::cout << "  --help               Show this help\n";
}

std::vector<std::string> split_symbols(const std::string& symbols_str) {
    std::vector<std::string> symbols;
    std::stringstream ss(symbols_str);
    std::string symbol;
    
    while (std::getline(ss, symbol, ',')) {
        symbols.push_back(symbol);
    }
    
    return symbols;
}

int main(int argc, char* argv[]) {
    std::string host = "localhost";
    uint16_t port = 8888;
    int num_clients = 10;
    int duration = 60;
    std::string symbols_str = "AAPL,MSFT,GOOGL";
    
    // Parse command line arguments
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc && std::string(argv[i]) != "--help") {
            std::cerr << "Missing value for " << argv[i] << "\n";
            return 1;
        }
        
        std::string arg = argv[i];
        if (arg == "--host") {
            host = argv[i + 1];
        } else if (arg == "--port") {
            port = std::stoi(argv[i + 1]);
        } else if (arg == "--clients") {
            num_clients = std::stoi(argv[i + 1]);
        } else if (arg == "--duration") {
            duration = std::stoi(argv[i + 1]);
        } else if (arg == "--symbols") {
            symbols_str = argv[i + 1];
        } else if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }
    
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    auto symbols = split_symbols(symbols_str);
    if (symbols.empty()) {
        std::cerr << "No symbols specified\n";
        return 1;
    }
    
    LoadGenerator generator(host, port, symbols);
    generator.run(num_clients, duration);
    
    return 0;
}