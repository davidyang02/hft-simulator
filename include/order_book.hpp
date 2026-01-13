#pragma once

#include "order.hpp"
#include <map>
#include <deque>
#include <unordered_map>
#include <optional>
#include <vector>

namespace hft {

/// Best bid and offer (top of book)
struct BBO {
    std::optional<double> bidPrice;
    std::optional<double> askPrice;
    uint32_t bidQty = 0;
    uint32_t askQty = 0;

    [[nodiscard]] std::optional<double> midPrice() const {
        if (bidPrice && askPrice) {
            return (*bidPrice + *askPrice) / 2.0;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<double> spread() const {
        if (bidPrice && askPrice) {
            return *askPrice - *bidPrice;
        }
        return std::nullopt;
    }
};

/// Price level with aggregate quantity
struct PriceLevel {
    double price;
    uint32_t totalQty;
    size_t orderCount;
};

/// Limit Order Book with price-time priority
class OrderBook {
public:
    // Type aliases for the two sides
    using BidMap = std::map<double, std::deque<Order>, std::greater<double>>;  // Descending
    using AskMap = std::map<double, std::deque<Order>>;  // Ascending (default)

    explicit OrderBook(const std::string& symbol = "");

    /// Add an order to the book (for resting orders only)
    /// Returns true if order was added, false if rejected
    bool addOrder(const Order& order);

    /// Cancel an order by ID
    /// Returns the canceled order if found
    std::optional<Order> cancelOrder(uint64_t orderId);

    /// Get an order by ID
    [[nodiscard]] std::optional<Order> getOrder(uint64_t orderId) const;

    /// Get best bid and offer
    [[nodiscard]] BBO getTopOfBook() const;

    /// Get market depth (N levels each side)
    [[nodiscard]] std::vector<PriceLevel> getBidDepth(size_t levels = 5) const;
    [[nodiscard]] std::vector<PriceLevel> getAskDepth(size_t levels = 5) const;

    /// Get all orders at a specific price level
    [[nodiscard]] std::vector<Order> getOrdersAtPrice(Side side, double price) const;

    /// Get total number of orders in the book
    [[nodiscard]] size_t orderCount() const { return orderIndex_.size(); }

    /// Get symbol
    [[nodiscard]] const std::string& symbol() const { return symbol_; }

    /// Clear all orders
    void clear();

    /// Access to the bid side (for matching engine)
    BidMap& bids() { return bids_; }
    AskMap& asks() { return asks_; }
    const BidMap& bids() const { return bids_; }
    const AskMap& asks() const { return asks_; }

    /// Update order index after modification
    void updateOrderIndex(const Order& order);
    void removeFromIndex(uint64_t orderId);

private:
    std::string symbol_;
    
    // Bids sorted descending (highest price first)
    BidMap bids_;
    
    // Asks sorted ascending (lowest price first)
    AskMap asks_;
    
    // Order ID to price/side lookup for O(1) cancel
    struct OrderLocation {
        Side side;
        double price;
    };
    std::unordered_map<uint64_t, OrderLocation> orderIndex_;
    
    // Helper to find order in bids
    std::optional<Order> findOrderInBids(double price, uint64_t orderId) const;
    
    // Helper to find order in asks
    std::optional<Order> findOrderInAsks(double price, uint64_t orderId) const;
};

} // namespace hft
