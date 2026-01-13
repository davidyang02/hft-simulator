#pragma once

#include "matching_engine.hpp"
#include "simulation_clock.hpp"
#include <string>
#include <random>
#include <memory>

namespace hft {

/// Base class for all simulated market participants
class Participant {
public:
    Participant(const std::string& id, MatchingEngine& engine, EventScheduler& scheduler)
        : id_(id), engine_(engine), scheduler_(scheduler), rng_(std::random_device{}()) {}
    
    virtual ~Participant() = default;

    /// Initialize the participant (schedule initial events)
    virtual void initialize() = 0;

    /// Get participant ID
    [[nodiscard]] const std::string& id() const { return id_; }

    /// Get number of orders submitted
    [[nodiscard]] uint64_t ordersSubmitted() const { return ordersSubmitted_; }

    /// Get number of fills received
    [[nodiscard]] uint64_t fillsReceived() const { return fillsReceived_; }

    /// Get total volume traded
    [[nodiscard]] uint64_t volumeTraded() const { return volumeTraded_; }

    /// Handle execution report (called by engine)
    virtual void onExecutionReport(const ExecutionReport& report) {
        if (report.type == ExecType::FILLED || report.type == ExecType::PARTIAL_FILL) {
            ++fillsReceived_;
            volumeTraded_ += report.execQty;
        }
    }

protected:
    std::string id_;
    MatchingEngine& engine_;
    EventScheduler& scheduler_;
    std::mt19937 rng_;
    
    uint64_t ordersSubmitted_ = 0;
    uint64_t fillsReceived_ = 0;
    uint64_t volumeTraded_ = 0;

    /// Submit an order through the engine
    void submitOrder(Order order) {
        order.traderId = id_;
        engine_.submitOrder(std::move(order));
        ++ordersSubmitted_;
    }

    /// Generate random double in range [min, max]
    double randomDouble(double min, double max) {
        std::uniform_real_distribution<> dist(min, max);
        return dist(rng_);
    }

    /// Generate random int in range [min, max]
    int randomInt(int min, int max) {
        std::uniform_int_distribution<> dist(min, max);
        return dist(rng_);
    }

    /// Generate exponentially distributed delay (for Poisson process)
    SimulationClock::Duration randomExponentialDelay(double ratePerSecond) {
        std::exponential_distribution<> dist(ratePerSecond);
        double delaySeconds = dist(rng_);
        return SimulationClock::fromSeconds(delaySeconds);
    }
};

} // namespace hft
