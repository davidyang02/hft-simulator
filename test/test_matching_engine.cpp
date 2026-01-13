#include "matching_engine.hpp"
#include <iostream>
#include <cassert>

using namespace hft;

void testLimitOrderMatching() {
    std::cout << "Testing LIMIT order matching..." << std::endl;
    OrderBook book("TEST");
    MatchingEngine engine(book);
    
    // Add resting sell order
    engine.submitOrder(Order::create(1, Side::SELL, OrderType::LIMIT, 100.0, 100, "MM"));
    assert(book.orderCount() == 1);
    
    // Submit buy order that crosses
    auto reports = engine.submitOrder(Order::create(2, Side::BUY, OrderType::LIMIT, 100.0, 50, "TRADER"));
    
    // Should have 2 fills (one for each side)
    assert(reports.size() == 2);
    assert(reports[0].type == ExecType::FILLED);  // Buy order filled
    assert(reports[0].execQty == 50);
    assert(reports[1].type == ExecType::PARTIAL_FILL);  // Sell order partially filled
    
    // 50 remaining on the sell order
    auto bbo = book.getTopOfBook();
    assert(bbo.askQty == 50);
    
    std::cout << "  PASSED" << std::endl;
}

void testMarketOrder() {
    std::cout << "Testing MARKET order..." << std::endl;
    OrderBook book("TEST");
    MatchingEngine engine(book);
    
    // Add resting orders
    engine.submitOrder(Order::create(1, Side::SELL, OrderType::LIMIT, 100.0, 100, "MM"));
    engine.submitOrder(Order::create(2, Side::SELL, OrderType::LIMIT, 101.0, 100, "MM"));
    
    // Market buy sweeps through levels
    auto reports = engine.submitOrder(Order::create(3, Side::BUY, OrderType::MARKET, 0, 150, "TRADER"));
    
    // Should fill at both price levels
    bool foundFillAt100 = false;
    bool foundFillAt101 = false;
    for (const auto& r : reports) {
        if (r.orderId == 3 && r.execPrice == 100.0) foundFillAt100 = true;
        if (r.orderId == 3 && r.execPrice == 101.0) foundFillAt101 = true;
    }
    assert(foundFillAt100 && foundFillAt101);
    
    std::cout << "  PASSED" << std::endl;
}

void testIOCOrder() {
    std::cout << "Testing IOC order..." << std::endl;
    OrderBook book("TEST");
    MatchingEngine engine(book);
    
    // Add limited liquidity
    engine.submitOrder(Order::create(1, Side::SELL, OrderType::LIMIT, 100.0, 50, "MM"));
    
    // IOC order for more than available
    auto reports = engine.submitOrder(Order::create(2, Side::BUY, OrderType::IOC, 100.0, 100, "TRADER"));
    
    // Should have partial fill + cancel
    bool foundPartialFill = false;
    bool foundCancel = false;
    for (const auto& r : reports) {
        if (r.orderId == 2 && r.type == ExecType::PARTIAL_FILL) foundPartialFill = true;
        if (r.orderId == 2 && r.type == ExecType::CANCELED) foundCancel = true;
    }
    assert(foundPartialFill && foundCancel);
    
    // Book should be empty (resting order filled, IOC remainder canceled)
    assert(book.orderCount() == 0);
    
    std::cout << "  PASSED" << std::endl;
}

void testFOKOrder() {
    std::cout << "Testing FOK order..." << std::endl;
    OrderBook book("TEST");
    MatchingEngine engine(book);
    
    // Add limited liquidity
    engine.submitOrder(Order::create(1, Side::SELL, OrderType::LIMIT, 100.0, 50, "MM"));
    
    // FOK order for more than available - should be rejected
    auto reports = engine.submitOrder(Order::create(2, Side::BUY, OrderType::FOK, 100.0, 100, "TRADER"));
    assert(reports.size() == 1);
    assert(reports[0].type == ExecType::REJECTED);
    
    // Resting order should still be there
    assert(book.orderCount() == 1);
    
    // FOK order that can be filled
    reports = engine.submitOrder(Order::create(3, Side::BUY, OrderType::FOK, 100.0, 50, "TRADER2"));
    bool foundFill = false;
    for (const auto& r : reports) {
        if (r.orderId == 3 && r.type == ExecType::FILLED) foundFill = true;
    }
    assert(foundFill);
    
    std::cout << "  PASSED" << std::endl;
}

void testFIFOPriority() {
    std::cout << "Testing FIFO priority..." << std::endl;
    OrderBook book("TEST");
    MatchingEngine engine(book);
    
    // Add multiple orders at same price
    engine.submitOrder(Order::create(1, Side::SELL, OrderType::LIMIT, 100.0, 100, "FIRST"));
    engine.submitOrder(Order::create(2, Side::SELL, OrderType::LIMIT, 100.0, 100, "SECOND"));
    engine.submitOrder(Order::create(3, Side::SELL, OrderType::LIMIT, 100.0, 100, "THIRD"));
    
    // Buy should match against FIRST order first
    auto reports = engine.submitOrder(Order::create(4, Side::BUY, OrderType::MARKET, 0, 50, "BUYER"));
    
    // Find the sell side fill - it should be order 1
    uint64_t filledOrderId = 0;
    for (const auto& r : reports) {
        if (r.orderId != 4 && r.execQty > 0) {
            filledOrderId = r.orderId;
            break;
        }
    }
    assert(filledOrderId == 1);  // FIFO: first order gets filled first
    
    std::cout << "  PASSED" << std::endl;
}

void testStatistics() {
    std::cout << "Testing statistics..." << std::endl;
    OrderBook book("TEST");
    MatchingEngine engine(book);
    
    // Add and match some orders
    engine.submitOrder(Order::create(1, Side::SELL, OrderType::LIMIT, 100.0, 100, "MM"));
    engine.submitOrder(Order::create(2, Side::BUY, OrderType::MARKET, 0, 100, "TRADER"));
    
    assert(engine.ordersProcessed() == 2);
    assert(engine.tradesExecuted() == 1);
    
    std::cout << "  PASSED" << std::endl;
}

int main() {
    std::cout << "=== MatchingEngine Unit Tests ===" << std::endl;
    
    testLimitOrderMatching();
    testMarketOrder();
    testIOCOrder();
    testFOKOrder();
    testFIFOPriority();
    testStatistics();
    
    std::cout << "\nAll tests PASSED!" << std::endl;
    return 0;
}
