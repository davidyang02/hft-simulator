#pragma once

#include "participant.hpp"

namespace hft {

/// Noise trader that submits random orders around the mid-market price
class NoiseTrader : public Participant {
public:
    struct Config {
        double orderRatePerSecond = 1.0;   // Average orders per second
        double priceRange = 0.50;          // +/- range around mid price
        uint32_t minQuantity = 10;
        uint32_t maxQuantity = 100;
        double marketOrderProbability = 0.3;  // Probability of market vs limit order
    };

    NoiseTrader(const std::string& id, MatchingEngine& engine, EventScheduler& scheduler,
                OrderBook& book, const Config& config = {})
        : Participant(id, engine, scheduler), book_(book), config_(config) {}

    void initialize() override {
        scheduleNextOrder();
    }

private:
    OrderBook& book_;
    Config config_;

    void scheduleNextOrder() {
        auto delay = randomExponentialDelay(config_.orderRatePerSecond);
        scheduler_.scheduleAfter(delay, [this]() {
            generateOrder();
            scheduleNextOrder();  // Schedule next order
        });
    }

    void generateOrder() {
        auto bbo = book_.getTopOfBook();
        
        // Need at least one side to have a price reference
        double midPrice = 100.0;  // Default if book is empty
        if (bbo.bidPrice && bbo.askPrice) {
            midPrice = (*bbo.bidPrice + *bbo.askPrice) / 2.0;
        } else if (bbo.bidPrice) {
            midPrice = *bbo.bidPrice + 0.10;
        } else if (bbo.askPrice) {
            midPrice = *bbo.askPrice - 0.10;
        }

        // Random side
        Side side = randomInt(0, 1) == 0 ? Side::BUY : Side::SELL;
        
        // Random quantity
        uint32_t qty = randomInt(config_.minQuantity, config_.maxQuantity);

        // Market or limit order
        bool isMarket = randomDouble(0, 1) < config_.marketOrderProbability;
        
        if (isMarket) {
            auto order = Order::create(
                engine_.nextOrderId(),
                side,
                OrderType::MARKET,
                0.0,
                qty,
                id_
            );
            submitOrder(order);
        } else {
            // Random price around mid
            double offset = randomDouble(-config_.priceRange, config_.priceRange);
            double price = std::round((midPrice + offset) * 100) / 100;  // Round to cents
            
            // Adjust price to be marketable some of the time
            if (side == Side::BUY && bbo.askPrice && randomDouble(0, 1) < 0.2) {
                price = *bbo.askPrice;  // Cross the spread
            } else if (side == Side::SELL && bbo.bidPrice && randomDouble(0, 1) < 0.2) {
                price = *bbo.bidPrice;  // Cross the spread
            }

            auto order = Order::create(
                engine_.nextOrderId(),
                side,
                OrderType::LIMIT,
                price,
                qty,
                id_
            );
            submitOrder(order);
        }
    }
};

} // namespace hft
