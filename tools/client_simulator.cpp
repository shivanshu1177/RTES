#include "rtes/strategies.hpp"
#include <iostream>
#include <string>
#include <chrono>

static void usage(const char* prog) {
    std::cerr << "Usage: " << prog
              << " --strategy <market_maker|liquidity_taker>"
              << " [--host <host>] [--port <port>]"
              << " [--symbol <SYMBOL>] [--duration <seconds>]"
              << " [--spread <ticks>] [--size <qty>]\n";
}

int main(int argc, char* argv[]) {
    std::string strategy = "liquidity_taker";
    std::string host     = "localhost";
    uint16_t    port     = 8888;
    std::string symbol   = "AAPL";
    int         duration = 10;
    uint64_t    spread   = 100;    // 100 fixed-point ticks = $0.01
    uint32_t    size     = 100;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--strategy" && i + 1 < argc) strategy = argv[++i];
        else if (arg == "--host"     && i + 1 < argc) host     = argv[++i];
        else if (arg == "--port"     && i + 1 < argc) port     = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--symbol"   && i + 1 < argc) symbol   = argv[++i];
        else if (arg == "--duration" && i + 1 < argc) duration = std::stoi(argv[++i]);
        else if (arg == "--spread"   && i + 1 < argc) spread   = static_cast<uint64_t>(std::stoul(argv[++i]));
        else if (arg == "--size"     && i + 1 < argc) size     = static_cast<uint32_t>(std::stoul(argv[++i]));
        else if (arg == "--help" || arg == "-h") { usage(argv[0]); return 0; }
    }

    std::cout << "=== RTES Client Simulator ===\n"
              << "  strategy : " << strategy << "\n"
              << "  host     : " << host << ":" << port << "\n"
              << "  symbol   : " << symbol << "\n"
              << "  duration : " << duration << "s\n";

    if (strategy == "liquidity_taker") {
        rtes::LiquidityTakerStrategy strat(host, port, symbol);
        if (!strat.connect()) {
            std::cerr << "Failed to connect to " << host << ":" << port << "\n";
            return 1;
        }
        std::cout << "Running liquidity taker for " << duration << "s...\n";
        strat.run(std::chrono::seconds(duration));
        strat.disconnect();

        std::cout << "\n=== Results ===\n"
                  << "  orders sent     : " << strat.orders_sent()     << "\n"
                  << "  orders acked    : " << strat.orders_acked()    << "\n"
                  << "  orders rejected : " << strat.orders_rejected() << "\n"
                  << "  trades received : " << strat.trades_received() << "\n"
                  << "  avg latency     : " << strat.avg_latency_us()  << " μs\n";

    } else if (strategy == "market_maker") {
        std::cout << "  spread   : " << spread << " ticks\n"
                  << "  size     : " << size   << " lots\n";
        rtes::MarketMakerStrategy strat(host, port, symbol, spread, size);
        if (!strat.connect()) {
            std::cerr << "Failed to connect to " << host << ":" << port << "\n";
            return 1;
        }
        std::cout << "Running market maker for " << duration << "s...\n";
        strat.run(std::chrono::seconds(duration));
        strat.disconnect();

        std::cout << "\n=== Results ===\n"
                  << "  orders sent     : " << strat.orders_sent()     << "\n"
                  << "  orders acked    : " << strat.orders_acked()    << "\n"
                  << "  orders rejected : " << strat.orders_rejected() << "\n"
                  << "  trades received : " << strat.trades_received() << "\n";

    } else {
        std::cerr << "Unknown strategy: " << strategy << "\n";
        usage(argv[0]);
        return 1;
    }

    return 0;
}
