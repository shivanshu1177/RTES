#include "rtes/strategies.hpp"
#include <iostream>
#include <string>
#include <chrono>

using namespace rtes;

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n";
    std::cout << "Options:\n";
    std::cout << "  --strategy <type>    Strategy type: market_maker, momentum, arbitrage, liquidity_taker\n";
    std::cout << "  --symbol <symbol>    Primary symbol (default: AAPL)\n";
    std::cout << "  --symbol2 <symbol>   Secondary symbol for arbitrage\n";
    std::cout << "  --host <host>        Exchange host (default: localhost)\n";
    std::cout << "  --port <port>        Exchange port (default: 8888)\n";
    std::cout << "  --client-id <id>     Client ID (default: random)\n";
    std::cout << "  --duration <sec>     Run duration in seconds (default: 60)\n";
    std::cout << "  --help               Show this help\n";
}

int main(int argc, char* argv[]) {
    std::string strategy = "market_maker";
    std::string symbol = "AAPL";
    std::string symbol2 = "MSFT";
    std::string host = "localhost";
    uint16_t port = 8888;
    uint32_t client_id = std::random_device{}();
    int duration = 60;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc && std::string(argv[i]) != "--help") {
            std::cerr << "Missing value for " << argv[i] << "\n";
            return 1;
        }
        
        std::string arg = argv[i];
        if (arg == "--strategy") {
            strategy = argv[i + 1];
        } else if (arg == "--symbol") {
            symbol = argv[i + 1];
        } else if (arg == "--symbol2") {
            symbol2 = argv[i + 1];
        } else if (arg == "--host") {
            host = argv[i + 1];
        } else if (arg == "--port") {
            port = std::stoi(argv[i + 1]);
        } else if (arg == "--client-id") {
            client_id = std::stoul(argv[i + 1]);
        } else if (arg == "--duration") {
            duration = std::stoi(argv[i + 1]);
        } else if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }
    
    std::cout << "Starting " << strategy << " strategy for " << symbol;
    if (strategy == "arbitrage") {
        std::cout << "/" << symbol2;
    }
    std::cout << " (client " << client_id << ")\n";
    
    std::unique_ptr<ClientBase> client;
    
    if (strategy == "market_maker") {
        client = std::make_unique<MarketMakerStrategy>(host, port, client_id, symbol);
    } else if (strategy == "momentum") {
        client = std::make_unique<MomentumStrategy>(host, port, client_id, symbol);
    } else if (strategy == "arbitrage") {
        client = std::make_unique<ArbitrageStrategy>(host, port, client_id, symbol, symbol2);
    } else if (strategy == "liquidity_taker") {
        client = std::make_unique<LiquidityTakerStrategy>(host, port, client_id, symbol);
    } else {
        std::cerr << "Unknown strategy: " << strategy << "\n";
        return 1;
    }
    
    if (!client->connect()) {
        std::cerr << "Failed to connect to " << host << ":" << port << "\n";
        return 1;
    }
    
    std::cout << "Connected to exchange, running for " << duration << " seconds...\n";
    
    auto start_time = std::chrono::steady_clock::now();
    client->run(std::chrono::seconds(duration));
    auto end_time = std::chrono::steady_clock::now();
    
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
    
    std::cout << "\nStrategy completed after " << elapsed.count() << " seconds\n";
    std::cout << "Statistics:\n";
    std::cout << "  Orders sent: " << client->orders_sent() << "\n";
    std::cout << "  Orders acked: " << client->orders_acked() << "\n";
    std::cout << "  Orders rejected: " << client->orders_rejected() << "\n";
    std::cout << "  Trades received: " << client->trades_received() << "\n";
    
    if (elapsed.count() > 0) {
        double orders_per_sec = static_cast<double>(client->orders_sent()) / elapsed.count();
        std::cout << "  Order rate: " << orders_per_sec << " orders/sec\n";
    }
    
    client->disconnect();
    return 0;
}