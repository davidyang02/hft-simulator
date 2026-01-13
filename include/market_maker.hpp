#pragma once

#include "participant.hpp"
#include <unordered_set>

namespace hft {

/// Market maker that maintains bid/ask quotes around fair value
class MarketMaker : public Participant {
public:
    struct Config {
        double fairValue = 100.0;           // Initial fair value
        double halfSpread = 0.05;           // Half the bid-ask spread
        uint32_t quoteSize = 100;           // Size of each quote
        int levels = 3;                     // Number of price levels to quote
        double levelSpacing = 0.02;         // Spacing between levels
        double requoteIntervalSeconds = 0.5; // How often to refresh quotes
        double inventoryLimit = 1000;       // Max inventory before skewing
        double skewFactor = 0.01;           // Price skew per unit of inventory
    };

    MarketMaker(const std::string& id, MatchingEngine& engine, EventScheduler& scheduler,
                OrderBook& book, const Config& config = {})
        : Participant(id, engine, scheduler), book_(book), config_(config),
          fairValue_(config.fairValue) {}

    void initialize() override {
        postQuotes();
        scheduleRequote();
    }

    void onExecutionReport(const ExecutionReport& report) override {
        Participant::onExecutionReport(report);
        
        // Update inventory on fills
        if (report.type == ExecType::FILLED || report.type == ExecType::PARTIAL_FILL) {
            // Find if this was our order and which side
            auto it = activeOrders_.find(report.orderId);
            if (it != activeOrders_.end()) {
                if (it->second == Side::BUY) {
                    inventory_ += static_cast<int64_t>(report.execQty);
                } else {
                    inventory_ -= static_cast<int64_t>(report.execQty);
                }
            }
        }
    }

    [[nodiscard]] int64_t inventory() const { return inventory_; }
    [[nodiscard]] double fairValue() const { return fairValue_; }

    void setFairValue(double value) { fairValue_ = value; }

private:
    OrderBook& book_;
    Config config_;
    double fairValue_;
    int64_t inventory_ = 0;
    std::unordered_map<uint64_t, Side> activeOrders_;  // Order ID -> Side

    void scheduleRequote() {
        auto delay = SimulationClock::fromSeconds(config_.requoteIntervalSeconds);
        scheduler_.scheduleAfter(delay, [this]() {
            cancelAllQuotes();
            postQuotes();
            scheduleRequote();
        });
    }

    void cancelAllQuotes() {
        for (const auto& [orderId, side] : activeOrders_) {
            engine_.cancelOrder(orderId);
        }
        activeOrders_.clear();
    }

    void postQuotes() {
        // Calculate skewed mid based on inventory
        double skew = -inventory_ * config_.skewFactor;
        double skewedMid = fairValue_ + skew;

        // Post multiple levels on each side
        for (int level = 0; level < config_.levels; ++level) {
            double levelOffset = level * config_.levelSpacing;
            
            // Bid quote
            double bidPrice = skewedMid - config_.halfSpread - levelOffset;
            bidPrice = std::round(bidPrice * 100) / 100;
            
            auto bidOrder = Order::create(
                engine_.nextOrderId(),
                Side::BUY,
                OrderType::LIMIT,
                bidPrice,
                config_.quoteSize,
                id_
            );
            activeOrders_[bidOrder.orderId] = Side::BUY;
            submitOrder(bidOrder);

            // Ask quote
            double askPrice = skewedMid + config_.halfSpread + levelOffset;
            askPrice = std::round(askPrice * 100) / 100;
            
            auto askOrder = Order::create(
                engine_.nextOrderId(),
                Side::SELL,
                OrderType::LIMIT,
                askPrice,
                config_.quoteSize,
                id_
            );
            activeOrders_[askOrder.orderId] = Side::SELL;
            submitOrder(askOrder);
        }
    }
};

} // namespace hft
