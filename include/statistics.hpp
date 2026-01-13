#pragma once

#include "simulation_clock.hpp"
#include "matching_engine.hpp"
#include <vector>
#include <cmath>
#include <iostream>
#include <iomanip>

namespace hft {

/// Collects and computes statistics during simulation
class StatisticsCollector {
public:
    explicit StatisticsCollector(const OrderBook& book, const MatchingEngine& engine)
        : book_(book), engine_(engine) {}

    /// Record a trade
    void recordTrade(double price, uint32_t qty) {
        trades_.push_back({price, qty});
        totalVolume_ += qty;
        if (!lastPrice_) {
            openPrice_ = price;
        }
        lastPrice_ = price;
        highPrice_ = highPrice_ ? std::max(*highPrice_, price) : price;
        lowPrice_ = lowPrice_ ? std::min(*lowPrice_, price) : price;
    }

    /// Record a BBO snapshot
    void recordBBO(const BBO& bbo) {
        if (bbo.spread()) {
            spreadSamples_.push_back(*bbo.spread());
        }
    }

    /// Calculate VWAP
    [[nodiscard]] double vwap() const {
        if (trades_.empty()) return 0.0;
        double sumPQ = 0.0;
        uint64_t sumQ = 0;
        for (const auto& [price, qty] : trades_) {
            sumPQ += price * qty;
            sumQ += qty;
        }
        return sumQ > 0 ? sumPQ / sumQ : 0.0;
    }

    /// Calculate average spread
    [[nodiscard]] double averageSpread() const {
        if (spreadSamples_.empty()) return 0.0;
        double sum = 0.0;
        for (double s : spreadSamples_) sum += s;
        return sum / spreadSamples_.size();
    }

    /// Get total volume
    [[nodiscard]] uint64_t totalVolume() const { return totalVolume_; }

    /// Get trade count
    [[nodiscard]] size_t tradeCount() const { return trades_.size(); }

    /// Get OHLC
    [[nodiscard]] std::optional<double> openPrice() const { return openPrice_; }
    [[nodiscard]] std::optional<double> highPrice() const { return highPrice_; }
    [[nodiscard]] std::optional<double> lowPrice() const { return lowPrice_; }
    [[nodiscard]] std::optional<double> closePrice() const { return lastPrice_; }

    /// Get orders processed
    [[nodiscard]] uint64_t ordersProcessed() const { return engine_.ordersProcessed(); }

    /// Get current book state
    [[nodiscard]] size_t bookOrderCount() const { return book_.orderCount(); }

    /// Print summary
    void printSummary() const {
        std::cout << "\n=== Simulation Statistics ===" << std::endl;
        std::cout << "Orders processed:  " << ordersProcessed() << std::endl;
        std::cout << "Trades executed:   " << tradeCount() << std::endl;
        std::cout << "Total volume:      " << totalVolume() << std::endl;
        std::cout << "Orders in book:    " << bookOrderCount() << std::endl;
        
        if (!trades_.empty()) {
            std::cout << std::fixed << std::setprecision(2);
            std::cout << "VWAP:              " << vwap() << std::endl;
            std::cout << "Open:              " << (openPrice_ ? *openPrice_ : 0) << std::endl;
            std::cout << "High:              " << (highPrice_ ? *highPrice_ : 0) << std::endl;
            std::cout << "Low:               " << (lowPrice_ ? *lowPrice_ : 0) << std::endl;
            std::cout << "Close:             " << (lastPrice_ ? *lastPrice_ : 0) << std::endl;
        }
        
        if (!spreadSamples_.empty()) {
            std::cout << "Avg spread:        " << averageSpread() << std::endl;
        }
    }

    /// Reset all statistics
    void reset() {
        trades_.clear();
        spreadSamples_.clear();
        totalVolume_ = 0;
        openPrice_ = std::nullopt;
        highPrice_ = std::nullopt;
        lowPrice_ = std::nullopt;
        lastPrice_ = std::nullopt;
    }

private:
    const OrderBook& book_;
    const MatchingEngine& engine_;
    
    std::vector<std::pair<double, uint32_t>> trades_;  // {price, qty}
    std::vector<double> spreadSamples_;
    uint64_t totalVolume_ = 0;
    std::optional<double> openPrice_;
    std::optional<double> highPrice_;
    std::optional<double> lowPrice_;
    std::optional<double> lastPrice_;
};

} // namespace hft
