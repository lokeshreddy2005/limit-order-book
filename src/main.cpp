// Small runnable demo of the OrderBook API: submits a handful of limit and
// market orders, prints each resulting trade, walks through cancellation
// and modification, and prints the resulting book state. This is meant to
// be read alongside README.md "Example usage", not as a test suite --
// see tests/ for correctness coverage.

#include <iomanip>
#include <iostream>

#include "orderbook/OrderBook.hpp"

using namespace orderbook;

namespace {

const char* side_name(Side side) { return side == Side::Buy ? "BUY" : "SELL"; }
const char* status_name(OrderStatus status) {
    switch (status) {
        case OrderStatus::New: return "New";
        case OrderStatus::PartiallyFilled: return "PartiallyFilled";
        case OrderStatus::Filled: return "Filled";
        case OrderStatus::Cancelled: return "Cancelled";
    }
    return "?";
}

void print_trades(const std::vector<Trade>& trades) {
    for (const auto& t : trades) {
        std::cout << "  TRADE  taker=" << t.taker_order_id
                  << " maker=" << t.maker_order_id
                  << " price=" << t.price
                  << " qty=" << t.quantity << "\n";
    }
    if (trades.empty()) std::cout << "  (no trades)\n";
}

void print_book(const OrderBook& book) {
    std::cout << "  best_bid=";
    if (auto b = book.best_bid()) std::cout << *b; else std::cout << "-";
    std::cout << "  best_ask=";
    if (auto a = book.best_ask()) std::cout << *a; else std::cout << "-";
    std::cout << "\n";
}

void print_order(const OrderBook& book, OrderId id) {
    const Order* o = book.get_order(id);
    if (!o) { std::cout << "  order " << id << " not found\n"; return; }
    std::cout << "  order " << id << ": " << side_name(o->side)
              << " price=" << o->price
              << " qty=" << o->quantity
              << " remaining=" << o->remaining_quantity
              << " filled=" << o->filled_quantity()
              << " status=" << status_name(o->status) << "\n";
}

} // namespace

int main() {
    OrderBook book("DEMO");
    OrderId next_id = 1;

    std::cout << "1) Resting liquidity: three sell orders at increasing prices\n";
    print_trades(book.submit_limit_order(next_id++, Side::Sell, 10'010, 100));
    print_trades(book.submit_limit_order(next_id++, Side::Sell, 10'020, 200));
    print_trades(book.submit_limit_order(next_id++, Side::Sell, 10'020, 150)); // same price, behind order 2
    print_book(book);

    std::cout << "\n2) Aggressive buy limit order sweeps two price levels\n";
    auto trades = book.submit_limit_order(next_id++, Side::Buy, 10'020, 250);
    print_trades(trades);
    print_book(book);
    print_order(book, 2); // fully filled
    print_order(book, 3); // partially filled, 50 remaining, still first-in-queue at 10020

    std::cout << "\n3) Market order consumes remaining liquidity at 10020, then rests nothing (IOC)\n";
    OrderId market_buyer = next_id++;
    trades = book.submit_market_order(market_buyer, Side::Buy, 1000);
    print_trades(trades);
    print_order(book, market_buyer);
    print_book(book);

    std::cout << "\n4) New resting orders, then cancel and modify\n";
    OrderId a = next_id++;
    OrderId b = next_id++;
    book.submit_limit_order(a, Side::Buy, 9'990, 300);
    book.submit_limit_order(b, Side::Buy, 9'990, 100);
    std::cout << "  before cancel: "; for (auto id : book.orders_at(Side::Buy, 9'990)) std::cout << id << " "; std::cout << "\n";
    book.cancel_order(a);
    std::cout << "  after cancelling " << a << ": "; for (auto id : book.orders_at(Side::Buy, 9'990)) std::cout << id << " "; std::cout << "\n";
    print_order(book, a);

    std::cout << "\n  modify " << b << " to a smaller size at the same price (priority preserved)\n";
    book.modify_order(b, std::nullopt, Quantity{40});
    print_order(book, b);

    std::cout << "\n  modify " << b << " to a new price (priority reset: new sequence number)\n";
    book.modify_order(b, Price{9'995}, std::nullopt);
    print_order(book, b);
    print_book(book);

    return 0;
}
