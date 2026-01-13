#include "order_book.hpp"
#include <algorithm>

namespace hft {

OrderBook::OrderBook(const std::string& symbol) : symbol_(symbol) {}

bool OrderBook::addOrder(const Order& order) {
    // Only limit orders should rest in the book
    if (order.type != OrderType::LIMIT) {
        return false;
    }

    // Order must have remaining quantity
    if (order.remainingQty() == 0) {
        return false;
    }

    // Check for duplicate order ID
    if (orderIndex_.contains(order.orderId)) {
        return false;
    }

    // Add to appropriate side
    if (order.side == Side::BUY) {
        bids_[order.price].push_back(order);
    } else {
        asks_[order.price].push_back(order);
    }

    // Update index
    orderIndex_[order.orderId] = {order.side, order.price};
    return true;
}

std::optional<Order> OrderBook::cancelOrder(uint64_t orderId) {
    auto it = orderIndex_.find(orderId);
    if (it == orderIndex_.end()) {
        return std::nullopt;
    }

    auto location = it->second;
    
    if (location.side == Side::BUY) {
        auto levelIt = bids_.find(location.price);
        if (levelIt == bids_.end()) {
            orderIndex_.erase(it);
            return std::nullopt;
        }

        auto& queue = levelIt->second;
        for (auto orderIt = queue.begin(); orderIt != queue.end(); ++orderIt) {
            if (orderIt->orderId == orderId) {
                Order canceled = *orderIt;
                queue.erase(orderIt);
                
                if (queue.empty()) {
                    bids_.erase(levelIt);
                }
                
                orderIndex_.erase(it);
                return canceled;
            }
        }
    } else {
        auto levelIt = asks_.find(location.price);
        if (levelIt == asks_.end()) {
            orderIndex_.erase(it);
            return std::nullopt;
        }

        auto& queue = levelIt->second;
        for (auto orderIt = queue.begin(); orderIt != queue.end(); ++orderIt) {
            if (orderIt->orderId == orderId) {
                Order canceled = *orderIt;
                queue.erase(orderIt);
                
                if (queue.empty()) {
                    asks_.erase(levelIt);
                }
                
                orderIndex_.erase(it);
                return canceled;
            }
        }
    }

    orderIndex_.erase(it);
    return std::nullopt;
}

std::optional<Order> OrderBook::findOrderInBids(double price, uint64_t orderId) const {
    auto levelIt = bids_.find(price);
    if (levelIt == bids_.end()) {
        return std::nullopt;
    }
    for (const auto& order : levelIt->second) {
        if (order.orderId == orderId) {
            return order;
        }
    }
    return std::nullopt;
}

std::optional<Order> OrderBook::findOrderInAsks(double price, uint64_t orderId) const {
    auto levelIt = asks_.find(price);
    if (levelIt == asks_.end()) {
        return std::nullopt;
    }
    for (const auto& order : levelIt->second) {
        if (order.orderId == orderId) {
            return order;
        }
    }
    return std::nullopt;
}

std::optional<Order> OrderBook::getOrder(uint64_t orderId) const {
    auto it = orderIndex_.find(orderId);
    if (it == orderIndex_.end()) {
        return std::nullopt;
    }

    auto location = it->second;
    if (location.side == Side::BUY) {
        return findOrderInBids(location.price, orderId);
    } else {
        return findOrderInAsks(location.price, orderId);
    }
}

BBO OrderBook::getTopOfBook() const {
    BBO bbo;

    if (!bids_.empty()) {
        const auto& [price, queue] = *bids_.begin();
        bbo.bidPrice = price;
        bbo.bidQty = 0;
        for (const auto& order : queue) {
            bbo.bidQty += order.remainingQty();
        }
    }

    if (!asks_.empty()) {
        const auto& [price, queue] = *asks_.begin();
        bbo.askPrice = price;
        bbo.askQty = 0;
        for (const auto& order : queue) {
            bbo.askQty += order.remainingQty();
        }
    }

    return bbo;
}

std::vector<PriceLevel> OrderBook::getBidDepth(size_t levels) const {
    std::vector<PriceLevel> result;
    result.reserve(levels);

    size_t count = 0;
    for (const auto& [price, queue] : bids_) {
        if (count >= levels) break;
        
        uint32_t totalQty = 0;
        for (const auto& order : queue) {
            totalQty += order.remainingQty();
        }
        
        result.push_back({price, totalQty, queue.size()});
        ++count;
    }

    return result;
}

std::vector<PriceLevel> OrderBook::getAskDepth(size_t levels) const {
    std::vector<PriceLevel> result;
    result.reserve(levels);

    size_t count = 0;
    for (const auto& [price, queue] : asks_) {
        if (count >= levels) break;
        
        uint32_t totalQty = 0;
        for (const auto& order : queue) {
            totalQty += order.remainingQty();
        }
        
        result.push_back({price, totalQty, queue.size()});
        ++count;
    }

    return result;
}

std::vector<Order> OrderBook::getOrdersAtPrice(Side side, double price) const {
    if (side == Side::BUY) {
        auto it = bids_.find(price);
        if (it == bids_.end()) {
            return {};
        }
        return {it->second.begin(), it->second.end()};
    } else {
        auto it = asks_.find(price);
        if (it == asks_.end()) {
            return {};
        }
        return {it->second.begin(), it->second.end()};
    }
}

void OrderBook::clear() {
    bids_.clear();
    asks_.clear();
    orderIndex_.clear();
}

void OrderBook::updateOrderIndex(const Order& order) {
    orderIndex_[order.orderId] = {order.side, order.price};
}

void OrderBook::removeFromIndex(uint64_t orderId) {
    orderIndex_.erase(orderId);
}

} // namespace hft
