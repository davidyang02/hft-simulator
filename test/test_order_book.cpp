#include "order_book.hpp"
#include <iostream>
#include <cassert>

using namespace hft;

void testAddOrder() {
    std::cout << "Testing addOrder..." << std::endl;
    OrderBook book("TEST");
    
    auto order = Order::create(1, Side::BUY, OrderType::LIMIT, 100.0, 100, "T1");
    assert(book.addOrder(order));
    assert(book.orderCount() == 1);
    
    // Duplicate order ID should fail
    assert(!book.addOrder(order));
    assert(book.orderCount() == 1);
    
    // Non-limit order should fail
    auto marketOrder = Order::create(2, Side::BUY, OrderType::MARKET, 0, 100, "T1");
    assert(!book.addOrder(marketOrder));
    
    std::cout << "  PASSED" << std::endl;
}

void testCancelOrder() {
    std::cout << "Testing cancelOrder..." << std::endl;
    OrderBook book("TEST");
    
    auto order = Order::create(1, Side::BUY, OrderType::LIMIT, 100.0, 100, "T1");
    book.addOrder(order);
    
    // Cancel existing order
    auto canceled = book.cancelOrder(1);
    assert(canceled.has_value());
    assert(canceled->orderId == 1);
    assert(book.orderCount() == 0);
    
    // Cancel non-existent order
    auto notFound = book.cancelOrder(999);
    assert(!notFound.has_value());
    
    std::cout << "  PASSED" << std::endl;
}

void testBBO() {
    std::cout << "Testing getTopOfBook..." << std::endl;
    OrderBook book("TEST");
    
    // Empty book
    auto bbo = book.getTopOfBook();
    assert(!bbo.bidPrice.has_value());
    assert(!bbo.askPrice.has_value());
    
    // Add bid
    book.addOrder(Order::create(1, Side::BUY, OrderType::LIMIT, 99.0, 100, "T1"));
    bbo = book.getTopOfBook();
    assert(bbo.bidPrice.has_value());
    assert(*bbo.bidPrice == 99.0);
    assert(bbo.bidQty == 100);
    assert(!bbo.askPrice.has_value());
    
    // Add ask
    book.addOrder(Order::create(2, Side::SELL, OrderType::LIMIT, 101.0, 50, "T1"));
    bbo = book.getTopOfBook();
    assert(bbo.askPrice.has_value());
    assert(*bbo.askPrice == 101.0);
    assert(bbo.askQty == 50);
    
    // Check spread
    assert(bbo.spread().has_value());
    assert(*bbo.spread() == 2.0);
    
    std::cout << "  PASSED" << std::endl;
}

void testPriceOrdering() {
    std::cout << "Testing price ordering..." << std::endl;
    OrderBook book("TEST");
    
    // Add bids in random order
    book.addOrder(Order::create(1, Side::BUY, OrderType::LIMIT, 98.0, 100, "T1"));
    book.addOrder(Order::create(2, Side::BUY, OrderType::LIMIT, 100.0, 100, "T1"));
    book.addOrder(Order::create(3, Side::BUY, OrderType::LIMIT, 99.0, 100, "T1"));
    
    // Best bid should be highest price
    auto bbo = book.getTopOfBook();
    assert(*bbo.bidPrice == 100.0);
    
    // Add asks in random order
    book.addOrder(Order::create(4, Side::SELL, OrderType::LIMIT, 102.0, 100, "T1"));
    book.addOrder(Order::create(5, Side::SELL, OrderType::LIMIT, 101.0, 100, "T1"));
    book.addOrder(Order::create(6, Side::SELL, OrderType::LIMIT, 103.0, 100, "T1"));
    
    // Best ask should be lowest price
    bbo = book.getTopOfBook();
    assert(*bbo.askPrice == 101.0);
    
    std::cout << "  PASSED" << std::endl;
}

void testDepth() {
    std::cout << "Testing market depth..." << std::endl;
    OrderBook book("TEST");
    
    // Add 3 price levels on each side
    book.addOrder(Order::create(1, Side::BUY, OrderType::LIMIT, 100.0, 100, "T1"));
    book.addOrder(Order::create(2, Side::BUY, OrderType::LIMIT, 99.0, 200, "T1"));
    book.addOrder(Order::create(3, Side::BUY, OrderType::LIMIT, 98.0, 300, "T1"));
    book.addOrder(Order::create(4, Side::BUY, OrderType::LIMIT, 100.0, 50, "T2")); // Same level
    
    auto bidDepth = book.getBidDepth(3);
    assert(bidDepth.size() == 3);
    assert(bidDepth[0].price == 100.0);
    assert(bidDepth[0].totalQty == 150);  // 100 + 50
    assert(bidDepth[0].orderCount == 2);
    assert(bidDepth[1].price == 99.0);
    assert(bidDepth[2].price == 98.0);
    
    std::cout << "  PASSED" << std::endl;
}

int main() {
    std::cout << "=== OrderBook Unit Tests ===" << std::endl;
    
    testAddOrder();
    testCancelOrder();
    testBBO();
    testPriceOrdering();
    testDepth();
    
    std::cout << "\nAll tests PASSED!" << std::endl;
    return 0;
}
