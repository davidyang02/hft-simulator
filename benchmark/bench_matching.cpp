#include "matching_engine.hpp"
#include "latency.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <random>
#include <vector>

using namespace hft;

void benchmarkMatching(size_t numOrders, bool showLatencyDetails = false) {
    OrderBook book("BENCH");
    MatchingEngine engine(book);
    
    // Enable latency tracking for this benchmark
    engine.setLatencyTracking(true);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> priceDist(99.0, 101.0);
    std::uniform_int_distribution<> qtyDist(10, 100);
    std::uniform_int_distribution<> sideDist(0, 1);
    
    // Pre-generate orders
    std::vector<Order> orders;
    orders.reserve(numOrders);
    for (size_t i = 0; i < numOrders; ++i) {
        Side side = sideDist(gen) == 0 ? Side::BUY : Side::SELL;
        double price = std::round(priceDist(gen) * 100) / 100;  // Round to cents
        uint32_t qty = qtyDist(gen);
        orders.push_back(Order::create(i + 1, side, OrderType::LIMIT, price, qty, "BENCH"));
    }
    
    // Benchmark
    auto start = std::chrono::high_resolution_clock::now();
    
    for (const auto& order : orders) {
        engine.submitOrder(order);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double ordersPerSecond = (numOrders * 1'000'000.0) / duration.count();
    
    std::cout << std::fixed;
    std::cout << "Orders processed:    " << numOrders << std::endl;
    std::cout << "Total time:          " << duration.count() << " µs" << std::endl;
    std::cout << "Throughput:          " << std::setprecision(0) << ordersPerSecond << " orders/sec" << std::endl;
    std::cout << "Trades executed:     " << engine.tradesExecuted() << std::endl;
    std::cout << "Orders resting:      " << book.orderCount() << std::endl;
    
    // Latency percentiles
    auto& latency = engine.matchingLatency();
    std::cout << std::setprecision(3);
    std::cout << "\nLatency Percentiles:" << std::endl;
    std::cout << "  Min:     " << LatencyHistogram::toUs(latency.minNs()) << " µs" << std::endl;
    std::cout << "  p50:     " << LatencyHistogram::toUs(latency.p50Ns()) << " µs" << std::endl;
    std::cout << "  p95:     " << LatencyHistogram::toUs(latency.p95Ns()) << " µs" << std::endl;
    std::cout << "  p99:     " << LatencyHistogram::toUs(latency.p99Ns()) << " µs" << std::endl;
    std::cout << "  p99.9:   " << LatencyHistogram::toUs(latency.p999Ns()) << " µs" << std::endl;
    std::cout << "  Max:     " << LatencyHistogram::toUs(latency.maxNs()) << " µs" << std::endl;
    
    if (showLatencyDetails) {
        latency.printSummary();
    }
}

int main(int argc, char* argv[]) {
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "-v" || std::string(argv[i]) == "--verbose") {
            verbose = true;
        }
    }

    std::cout << "=== HFT Matching Engine Benchmark ===" << std::endl;
    std::cout << "(with latency instrumentation)" << std::endl;
    std::cout << std::endl;
    
    std::cout << "--- Warm-up (1,000 orders) ---" << std::endl;
    benchmarkMatching(1'000);
    std::cout << std::endl;
    
    std::cout << "--- Small (10,000 orders) ---" << std::endl;
    benchmarkMatching(10'000);
    std::cout << std::endl;
    
    std::cout << "--- Medium (100,000 orders) ---" << std::endl;
    benchmarkMatching(100'000);
    std::cout << std::endl;
    
    std::cout << "--- Large (1,000,000 orders) ---" << std::endl;
    benchmarkMatching(1'000'000, verbose);
    
    return 0;
}
