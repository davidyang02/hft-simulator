#pragma once

#include <cstdint>
#include <string>

namespace hft {

/// Order side
enum class Side : uint8_t {
    BUY,
    SELL
};

/// Order type
enum class OrderType : uint8_t {
    LIMIT,      // Resting limit order
    MARKET,     // Execute immediately at best price
    IOC,        // Immediate-or-Cancel: fill what you can, cancel rest
    FOK         // Fill-or-Kill: fill entirely or cancel
};

/// Execution report type
enum class ExecType : uint8_t {
    NEW,            // Order accepted
    FILLED,         // Order fully filled
    PARTIAL_FILL,   // Order partially filled
    CANCELED,       // Order canceled
    REJECTED        // Order rejected
};

/// Convert Side to string
inline const char* to_string(Side side) {
    return side == Side::BUY ? "BUY" : "SELL";
}

/// Convert OrderType to string
inline const char* to_string(OrderType type) {
    switch (type) {
        case OrderType::LIMIT:  return "LIMIT";
        case OrderType::MARKET: return "MARKET";
        case OrderType::IOC:    return "IOC";
        case OrderType::FOK:    return "FOK";
        default:                return "UNKNOWN";
    }
}

/// Convert ExecType to string
inline const char* to_string(ExecType type) {
    switch (type) {
        case ExecType::NEW:          return "NEW";
        case ExecType::FILLED:       return "FILLED";
        case ExecType::PARTIAL_FILL: return "PARTIAL_FILL";
        case ExecType::CANCELED:     return "CANCELED";
        case ExecType::REJECTED:     return "REJECTED";
        default:                     return "UNKNOWN";
    }
}

} // namespace hft
