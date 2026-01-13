#include "matching_engine.hpp"

namespace hft {

MatchingEngine::MatchingEngine(OrderBook& book) : book_(book) {}

void MatchingEngine::setExecutionCallback(ExecutionCallback callback) {
    callback_ = std::move(callback);
}

ExecutionReport MatchingEngine::makeReport(uint64_t orderId, ExecType type, 
                                           double price, uint32_t qty,
                                           const std::string& traderId) {
    auto report = ExecutionReport::create(orderId, nextExecId(), type, price, qty, traderId);
    if (callback_) {
        callback_(report);
    }
    return report;
}

std::vector<ExecutionReport> MatchingEngine::submitOrder(Order order) {
    std::vector<ExecutionReport> reports;
    ++ordersProcessed_;

    // Track latency if enabled
    Timer timer;
    if (trackLatency_) {
        timer.start();
    }

    // Match against opposite side
    if (order.side == Side::BUY) {
        reports = matchBuy(order);
    } else {
        reports = matchSell(order);
    }

    // Handle remaining quantity based on order type
    if (order.remainingQty() > 0) {
        switch (order.type) {
            case OrderType::LIMIT:
                // Add resting order to book
                if (book_.addOrder(order)) {
                    reports.push_back(makeReport(order.orderId, ExecType::NEW, 0, 0, order.traderId));
                }
                break;

            case OrderType::IOC:
                // Cancel remaining quantity
                reports.push_back(makeReport(order.orderId, ExecType::CANCELED, 0, 0, order.traderId));
                break;

            case OrderType::FOK:
                // This should not happen - FOK is handled in match functions
                // If we get here, FOK was not fully filled
                break;

            case OrderType::MARKET:
                // Market order exhausted available liquidity
                if (order.filledQty > 0) {
                    // Partially filled, cancel rest
                    reports.push_back(makeReport(order.orderId, ExecType::CANCELED, 0, 0, order.traderId));
                } else {
                    // No fill at all - reject
                    reports.push_back(makeReport(order.orderId, ExecType::REJECTED, 0, 0, order.traderId));
                }
                break;
        }
    }

    // Record latency
    if (trackLatency_) {
        matchingLatency_.record(timer.stopNs());
    }

    return reports;
}

std::vector<ExecutionReport> MatchingEngine::matchBuy(Order& order) {
    std::vector<ExecutionReport> reports;
    auto& asks = book_.asks();

    // FOK check: ensure full quantity is available
    if (order.type == OrderType::FOK) {
        uint32_t availableQty = 0;
        for (const auto& [price, queue] : asks) {
            if (order.type == OrderType::LIMIT && price > order.price) {
                break;  // No more matchable prices
            }
            for (const auto& askOrder : queue) {
                availableQty += askOrder.remainingQty();
                if (availableQty >= order.quantity) {
                    break;
                }
            }
            if (availableQty >= order.quantity) {
                break;
            }
        }
        if (availableQty < order.quantity) {
            reports.push_back(makeReport(order.orderId, ExecType::REJECTED, 0, 0, order.traderId));
            order.filledQty = order.quantity;  // Mark as "done" to prevent resting
            return reports;
        }
    }

    // Match against asks (lowest first)
    while (order.remainingQty() > 0 && !asks.empty()) {
        auto askIt = asks.begin();
        double askPrice = askIt->first;

        // Check price compatibility for limit orders
        if (order.type == OrderType::LIMIT && askPrice > order.price) {
            break;  // No more matchable prices
        }

        auto& queue = askIt->second;

        while (order.remainingQty() > 0 && !queue.empty()) {
            auto& restingOrder = queue.front();
            
            // Calculate fill quantity
            uint32_t fillQty = std::min(order.remainingQty(), restingOrder.remainingQty());

            // Update quantities
            order.filledQty += fillQty;
            restingOrder.filledQty += fillQty;

            // Generate execution reports
            reports.push_back(makeReport(
                order.orderId,
                order.isFilled() ? ExecType::FILLED : ExecType::PARTIAL_FILL,
                askPrice,
                fillQty,
                order.traderId
            ));
            reports.push_back(makeReport(
                restingOrder.orderId,
                restingOrder.isFilled() ? ExecType::FILLED : ExecType::PARTIAL_FILL,
                askPrice,
                fillQty,
                restingOrder.traderId
            ));

            ++tradesExecuted_;

            // Remove filled order from book
            if (restingOrder.isFilled()) {
                book_.removeFromIndex(restingOrder.orderId);
                queue.pop_front();
            }
        }

        // Clean up empty price level
        if (queue.empty()) {
            asks.erase(askIt);
        }
    }

    return reports;
}

std::vector<ExecutionReport> MatchingEngine::matchSell(Order& order) {
    std::vector<ExecutionReport> reports;
    auto& bids = book_.bids();

    // FOK check: ensure full quantity is available
    if (order.type == OrderType::FOK) {
        uint32_t availableQty = 0;
        for (const auto& [price, queue] : bids) {
            if (order.type == OrderType::LIMIT && price < order.price) {
                break;  // No more matchable prices
            }
            for (const auto& bidOrder : queue) {
                availableQty += bidOrder.remainingQty();
                if (availableQty >= order.quantity) {
                    break;
                }
            }
            if (availableQty >= order.quantity) {
                break;
            }
        }
        if (availableQty < order.quantity) {
            reports.push_back(makeReport(order.orderId, ExecType::REJECTED, 0, 0, order.traderId));
            order.filledQty = order.quantity;  // Mark as "done" to prevent resting
            return reports;
        }
    }

    // Match against bids (highest first)
    while (order.remainingQty() > 0 && !bids.empty()) {
        auto bidIt = bids.begin();
        double bidPrice = bidIt->first;

        // Check price compatibility for limit orders
        if (order.type == OrderType::LIMIT && bidPrice < order.price) {
            break;  // No more matchable prices
        }

        auto& queue = bidIt->second;

        while (order.remainingQty() > 0 && !queue.empty()) {
            auto& restingOrder = queue.front();
            
            // Calculate fill quantity
            uint32_t fillQty = std::min(order.remainingQty(), restingOrder.remainingQty());

            // Update quantities
            order.filledQty += fillQty;
            restingOrder.filledQty += fillQty;

            // Generate execution reports
            reports.push_back(makeReport(
                order.orderId,
                order.isFilled() ? ExecType::FILLED : ExecType::PARTIAL_FILL,
                bidPrice,
                fillQty,
                order.traderId
            ));
            reports.push_back(makeReport(
                restingOrder.orderId,
                restingOrder.isFilled() ? ExecType::FILLED : ExecType::PARTIAL_FILL,
                bidPrice,
                fillQty,
                restingOrder.traderId
            ));

            ++tradesExecuted_;

            // Remove filled order from book
            if (restingOrder.isFilled()) {
                book_.removeFromIndex(restingOrder.orderId);
                queue.pop_front();
            }
        }

        // Clean up empty price level
        if (queue.empty()) {
            bids.erase(bidIt);
        }
    }

    return reports;
}

std::optional<ExecutionReport> MatchingEngine::cancelOrder(uint64_t orderId) {
    auto canceled = book_.cancelOrder(orderId);
    if (canceled) {
        return makeReport(orderId, ExecType::CANCELED, 0, 0, canceled->traderId);
    }
    return std::nullopt;
}

} // namespace hft
