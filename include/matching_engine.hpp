#pragma once

#include "order_book.hpp"
#include "latency.hpp"
#include <vector>
#include <functional>
#include <atomic>

namespace hft {

/// Matching engine that processes orders against the order book
class MatchingEngine {
public:
    using ExecutionCallback = std::function<void(const ExecutionReport&)>;

    explicit MatchingEngine(OrderBook& book);

    /// Set callback for execution reports
    void setExecutionCallback(ExecutionCallback callback);

    /// Submit an order for matching
    /// Returns execution reports generated
    std::vector<ExecutionReport> submitOrder(Order order);

    /// Cancel an order
    std::optional<ExecutionReport> cancelOrder(uint64_t orderId);

    /// Get next order ID
    uint64_t nextOrderId() { return nextOrderId_++; }

    /// Get next execution ID
    uint64_t nextExecId() { return nextExecId_++; }

    /// Get total orders processed
    [[nodiscard]] uint64_t ordersProcessed() const { return ordersProcessed_; }

    /// Get total trades executed
    [[nodiscard]] uint64_t tradesExecuted() const { return tradesExecuted_; }

    /// Get latency histogram for matching operations
    LatencyHistogram& matchingLatency() { return matchingLatency_; }
    const LatencyHistogram& matchingLatency() const { return matchingLatency_; }

    /// Enable/disable latency tracking (slight overhead)
    void setLatencyTracking(bool enabled) { trackLatency_ = enabled; }
    [[nodiscard]] bool isLatencyTrackingEnabled() const { return trackLatency_; }

private:
    OrderBook& book_;
    ExecutionCallback callback_;
    std::atomic<uint64_t> nextOrderId_{1};
    std::atomic<uint64_t> nextExecId_{1};
    uint64_t ordersProcessed_ = 0;
    uint64_t tradesExecuted_ = 0;
    
    // Latency tracking
    LatencyHistogram matchingLatency_{"Matching Latency"};
    bool trackLatency_ = false;

    /// Match a buy order against asks
    std::vector<ExecutionReport> matchBuy(Order& order);

    /// Match a sell order against bids
    std::vector<ExecutionReport> matchSell(Order& order);

    /// Generate and optionally publish an execution report
    ExecutionReport makeReport(uint64_t orderId, ExecType type, 
                               double price = 0.0, uint32_t qty = 0,
                               const std::string& traderId = "");
};

} // namespace hft
