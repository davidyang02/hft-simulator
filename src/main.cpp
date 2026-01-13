#include "simulation.hpp"
#include <iostream>
#include <iomanip>

using namespace hft;

void printBook(const OrderBook& book, size_t levels = 5) {
    std::cout << "\n=== Order Book (" << book.symbol() << ") ===" << std::endl;
    
    auto asks = book.getAskDepth(levels);
    for (auto it = asks.rbegin(); it != asks.rend(); ++it) {
        std::cout << "  ASK: " << std::fixed << std::setprecision(2) 
                  << it->price << " x " << it->totalQty 
                  << " (" << it->orderCount << " orders)" << std::endl;
    }
    
    auto bbo = book.getTopOfBook();
    std::cout << "  --- SPREAD: ";
    if (bbo.spread()) {
        std::cout << std::fixed << std::setprecision(2) << *bbo.spread();
    } else {
        std::cout << "N/A";
    }
    std::cout << " ---" << std::endl;
    
    auto bids = book.getBidDepth(levels);
    for (const auto& level : bids) {
        std::cout << "  BID: " << std::fixed << std::setprecision(2) 
                  << level.price << " x " << level.totalQty 
                  << " (" << level.orderCount << " orders)" << std::endl;
    }
}

int main(int argc, char* argv[]) {
    std::cout << "=== HFT Market Simulator ===" << std::endl;
    std::cout << "Phase 2: Simulation with Market Participants\n" << std::endl;

    // Parse command line for duration
    double duration = 10.0;  // Default 10 seconds
    bool verbose = false;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "-d" || arg == "--duration") {
            if (i + 1 < argc) {
                duration = std::stod(argv[++i]);
            }
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "  -d, --duration <seconds>  Simulation duration (default: 10)" << std::endl;
            std::cout << "  -v, --verbose            Show all execution reports" << std::endl;
            std::cout << "  -h, --help               Show this help" << std::endl;
            return 0;
        }
    }

    // Create simulation
    Simulation::Config simConfig;
    simConfig.durationSeconds = duration;
    simConfig.snapshotIntervalSeconds = 0.5;
    simConfig.verbose = verbose;

    Simulation sim("AAPL", simConfig);

    // Add market makers (they provide liquidity)
    MarketMaker::Config mmConfig;
    mmConfig.fairValue = 150.0;
    mmConfig.halfSpread = 0.03;
    mmConfig.quoteSize = 100;
    mmConfig.levels = 3;
    mmConfig.levelSpacing = 0.02;
    mmConfig.requoteIntervalSeconds = 0.25;
    
    sim.addMarketMaker("MM1", mmConfig);
    
    mmConfig.halfSpread = 0.04;  // Slightly wider spread
    sim.addMarketMaker("MM2", mmConfig);

    // Add noise traders (they consume liquidity)
    NoiseTrader::Config ntConfig;
    ntConfig.orderRatePerSecond = 5.0;  // 5 orders per second
    ntConfig.priceRange = 0.30;
    ntConfig.minQuantity = 10;
    ntConfig.maxQuantity = 50;
    ntConfig.marketOrderProbability = 0.4;

    sim.addNoiseTrader("NT1", ntConfig);
    sim.addNoiseTrader("NT2", ntConfig);
    
    ntConfig.orderRatePerSecond = 2.0;  // Slower trader
    ntConfig.marketOrderProbability = 0.6;  // More aggressive
    sim.addNoiseTrader("NT3", ntConfig);

    // Run simulation
    sim.run();

    // Print results
    sim.stats().printSummary();
    sim.printParticipantSummary();
    printBook(sim.book());

    return 0;
}
