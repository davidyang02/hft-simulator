#pragma once

#include "simulation_clock.hpp"
#include "matching_engine.hpp"
#include "statistics.hpp"
#include "noise_trader.hpp"
#include "market_maker.hpp"
#include <memory>
#include <vector>
#include <iostream>
#include <iomanip>

namespace hft {

/// Main simulation runner
class Simulation {
public:
    struct Config {
        double durationSeconds = 60.0;
        double snapshotIntervalSeconds = 1.0;  // How often to sample BBO
        bool verbose = false;
    };

    Simulation(const std::string& symbol, const Config& config = {})
        : config_(config),
          book_(symbol),
          engine_(book_),
          scheduler_(clock_),
          stats_(book_, engine_) {
        
        // Set up execution callback to track trades
        engine_.setExecutionCallback([this](const ExecutionReport& report) {
            if (report.type == ExecType::FILLED || report.type == ExecType::PARTIAL_FILL) {
                stats_.recordTrade(report.execPrice, report.execQty);
                
                // Notify only the participant whose order was filled
                for (auto& participant : participants_) {
                    if (participant->id() == report.traderId) {
                        participant->onExecutionReport(report);
                        break;
                    }
                }
            }
            
            if (config_.verbose) {
                std::cout << "[" << std::fixed << std::setprecision(3) 
                          << SimulationClock::toSeconds(clock_.now()) << "s] "
                          << to_string(report.type) << " Order " << report.orderId
                          << " (" << report.traderId << ")";
                if (report.execQty > 0) {
                    std::cout << " " << report.execQty << " @ " << report.execPrice;
                }
                std::cout << std::endl;
            }
        });
    }

    /// Add a noise trader
    void addNoiseTrader(const std::string& id, const NoiseTrader::Config& config = {}) {
        participants_.push_back(
            std::make_unique<NoiseTrader>(id, engine_, scheduler_, book_, config)
        );
    }

    /// Add a market maker
    void addMarketMaker(const std::string& id, const MarketMaker::Config& config = {}) {
        participants_.push_back(
            std::make_unique<MarketMaker>(id, engine_, scheduler_, book_, config)
        );
    }

    /// Run the simulation
    void run() {
        std::cout << "=== Starting Simulation ===" << std::endl;
        std::cout << "Duration: " << config_.durationSeconds << " seconds" << std::endl;
        std::cout << "Participants: " << participants_.size() << std::endl;
        std::cout << std::endl;

        // Initialize all participants
        for (auto& participant : participants_) {
            participant->initialize();
        }

        // Schedule periodic BBO snapshots
        scheduleSnapshot();

        // Run until end of simulation
        auto endTime = SimulationClock::fromSeconds(config_.durationSeconds);
        scheduler_.runUntil(endTime);

        std::cout << "\n=== Simulation Complete ===" << std::endl;
    }

    /// Get statistics
    const StatisticsCollector& stats() const { return stats_; }

    /// Get order book
    const OrderBook& book() const { return book_; }

    /// Print participant summary
    void printParticipantSummary() const {
        std::cout << "\n=== Participant Summary ===" << std::endl;
        std::cout << std::left << std::setw(12) << "ID" 
                  << std::right << std::setw(10) << "Orders"
                  << std::setw(10) << "Fills"
                  << std::setw(12) << "Volume" << std::endl;
        std::cout << std::string(44, '-') << std::endl;
        
        for (const auto& p : participants_) {
            std::cout << std::left << std::setw(12) << p->id()
                      << std::right << std::setw(10) << p->ordersSubmitted()
                      << std::setw(10) << p->fillsReceived()
                      << std::setw(12) << p->volumeTraded() << std::endl;
        }
    }

private:
    Config config_;
    SimulationClock clock_;
    OrderBook book_;
    MatchingEngine engine_;
    EventScheduler scheduler_;
    StatisticsCollector stats_;
    std::vector<std::unique_ptr<Participant>> participants_;

    void scheduleSnapshot() {
        auto interval = SimulationClock::fromSeconds(config_.snapshotIntervalSeconds);
        auto endTime = SimulationClock::fromSeconds(config_.durationSeconds);
        
        for (auto t = interval; t <= endTime; t += interval) {
            scheduler_.scheduleAt(t, [this]() {
                stats_.recordBBO(book_.getTopOfBook());
            }, 1000);  // Lower priority than trades
        }
    }
};

} // namespace hft
