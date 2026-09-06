#include "third_party/catch.hpp"

#include "orderbook/OrderBook.hpp"

using namespace orderbook;

namespace {

// Checks the fundamental per-order invariant that must hold after every
// operation: filled + remaining == current working quantity, and neither
// filled nor remaining is ever negative.
void check_order_invariants(const Order& o) {
    CHECK(o.remaining_quantity >= 0);
    CHECK(o.filled_quantity() >= 0);
    CHECK(o.filled_quantity() + o.remaining_quantity == o.quantity);
}

} // namespace

// ---------------------------------------------------------------------
// Empty-book behavior
// ---------------------------------------------------------------------

TEST_CASE("empty book has no best bid/ask", "[orderbook][empty]") {
    OrderBook book;
    CHECK_FALSE(book.best_bid().has_value());
    CHECK_FALSE(book.best_ask().has_value());
    CHECK(book.bid_level_count() == 0);
    CHECK(book.ask_level_count() == 0);
}

TEST_CASE("unknown order id operations are clean no-ops / lookups", "[orderbook][empty][invalid]") {
    OrderBook book;
    CHECK(book.get_order(12345) == nullptr);
    CHECK_FALSE(book.cancel_order(12345));
    CHECK_FALSE(book.modify_order(12345, Price{100}, std::nullopt));
}

TEST_CASE("a market order against an empty opposite book fills nothing and is cancelled", "[orderbook][market][empty]") {
    OrderBook book;
    auto trades = book.submit_market_order(1, Side::Buy, 100);
    CHECK(trades.empty());
    const Order* o = book.get_order(1);
    REQUIRE(o != nullptr);
    CHECK(o->status == OrderStatus::Cancelled);
    CHECK(o->remaining_quantity == 100);
    check_order_invariants(*o);
}

// ---------------------------------------------------------------------
// Basic insertion
// ---------------------------------------------------------------------

TEST_CASE("a single resting limit order updates best bid/ask and book depth", "[orderbook][insert]") {
    OrderBook book;
    auto trades = book.submit_limit_order(1, Side::Buy, 100, 10);
    CHECK(trades.empty());
    REQUIRE(book.best_bid().has_value());
    CHECK(*book.best_bid() == 100);
    CHECK_FALSE(book.best_ask().has_value());
    CHECK(book.order_count_at(Side::Buy, 100) == 1);
    CHECK(book.quantity_at(Side::Buy, 100) == 10);

    const Order* o = book.get_order(1);
    REQUIRE(o != nullptr);
    CHECK(o->status == OrderStatus::New);
    CHECK(o->remaining_quantity == 10);
    check_order_invariants(*o);
}

TEST_CASE("submitting a duplicate order id throws", "[orderbook][invalid]") {
    OrderBook book;
    book.submit_limit_order(1, Side::Buy, 100, 10);
    CHECK_THROWS_AS(book.submit_limit_order(1, Side::Sell, 100, 5), std::invalid_argument);
}

TEST_CASE("non-positive price or quantity is rejected", "[orderbook][invalid]") {
    OrderBook book;
    CHECK_THROWS_AS(book.submit_limit_order(1, Side::Buy, 0, 10), std::invalid_argument);
    CHECK_THROWS_AS(book.submit_limit_order(2, Side::Buy, 100, 0), std::invalid_argument);
    CHECK_THROWS_AS(book.submit_limit_order(3, Side::Buy, -5, 10), std::invalid_argument);
    CHECK_THROWS_AS(book.submit_limit_order(4, Side::Buy, 100, -1), std::invalid_argument);
    CHECK_THROWS_AS(book.submit_market_order(5, Side::Buy, 0), std::invalid_argument);
}

// ---------------------------------------------------------------------
// Matching: basic buy/sell, complete and partial fills
// ---------------------------------------------------------------------

TEST_CASE("a crossing limit order fully fills a single resting order", "[orderbook][match]") {
    OrderBook book;
    book.submit_limit_order(1, Side::Sell, 100, 50);
    auto trades = book.submit_limit_order(2, Side::Buy, 100, 50);

    REQUIRE(trades.size() == 1);
    CHECK(trades[0].maker_order_id == 1);
    CHECK(trades[0].taker_order_id == 2);
    CHECK(trades[0].price == 100);
    CHECK(trades[0].quantity == 50);

    const Order* maker = book.get_order(1);
    const Order* taker = book.get_order(2);
    REQUIRE(maker != nullptr);
    REQUIRE(taker != nullptr);
    CHECK(maker->status == OrderStatus::Filled);
    CHECK(taker->status == OrderStatus::Filled);
    CHECK(maker->remaining_quantity == 0);
    CHECK(taker->remaining_quantity == 0);
    check_order_invariants(*maker);
    check_order_invariants(*taker);

    CHECK_FALSE(book.best_bid().has_value());
    CHECK_FALSE(book.best_ask().has_value());
}

TEST_CASE("a partially-filling limit order leaves the maker resting with reduced quantity", "[orderbook][match][partial]") {
    OrderBook book;
    book.submit_limit_order(1, Side::Sell, 100, 100);
    auto trades = book.submit_limit_order(2, Side::Buy, 100, 40);

    REQUIRE(trades.size() == 1);
    CHECK(trades[0].quantity == 40);

    const Order* maker = book.get_order(1);
    REQUIRE(maker != nullptr);
    CHECK(maker->status == OrderStatus::PartiallyFilled);
    CHECK(maker->remaining_quantity == 60);
    check_order_invariants(*maker);

    const Order* taker = book.get_order(2);
    REQUIRE(taker != nullptr);
    CHECK(taker->status == OrderStatus::Filled);

    REQUIRE(book.best_ask().has_value());
    CHECK(*book.best_ask() == 100);
    CHECK(book.quantity_at(Side::Sell, 100) == 60);
}

TEST_CASE("an incoming order that is not fully satisfied rests with the leftover quantity", "[orderbook][match][partial]") {
    OrderBook book;
    book.submit_limit_order(1, Side::Sell, 100, 20);
    auto trades = book.submit_limit_order(2, Side::Buy, 100, 50);

    REQUIRE(trades.size() == 1);
    CHECK(trades[0].quantity == 20);

    const Order* taker = book.get_order(2);
    REQUIRE(taker != nullptr);
    CHECK(taker->status == OrderStatus::PartiallyFilled);
    CHECK(taker->remaining_quantity == 30);
    check_order_invariants(*taker);

    REQUIRE(book.best_bid().has_value());
    CHECK(*book.best_bid() == 100);
    CHECK(book.quantity_at(Side::Buy, 100) == 30);
}

TEST_CASE("a non-crossing limit order does not match and simply rests", "[orderbook][match]") {
    OrderBook book;
    book.submit_limit_order(1, Side::Sell, 110, 50);
    auto trades = book.submit_limit_order(2, Side::Buy, 100, 50);
    CHECK(trades.empty());
    REQUIRE(book.best_bid().has_value());
    REQUIRE(book.best_ask().has_value());
    CHECK(*book.best_bid() == 100);
    CHECK(*book.best_ask() == 110);
}

// ---------------------------------------------------------------------
// Price priority
// ---------------------------------------------------------------------

TEST_CASE("an incoming buy matches the best (lowest) ask first regardless of arrival order", "[orderbook][priority][price]") {
    OrderBook book;
    book.submit_limit_order(1, Side::Sell, 110, 50); // worse price, arrives first
    book.submit_limit_order(2, Side::Sell, 100, 50); // better price, arrives second

    auto trades = book.submit_limit_order(3, Side::Buy, 110, 50);
    REQUIRE(trades.size() == 1);
    CHECK(trades[0].maker_order_id == 2); // the better (lower) ask price, not the earlier order
    CHECK(trades[0].price == 100);
}

TEST_CASE("an incoming sell matches the best (highest) bid first regardless of arrival order", "[orderbook][priority][price]") {
    OrderBook book;
    book.submit_limit_order(1, Side::Buy, 90, 50);  // worse price, arrives first
    book.submit_limit_order(2, Side::Buy, 100, 50); // better price, arrives second

    auto trades = book.submit_limit_order(3, Side::Sell, 90, 50);
    REQUIRE(trades.size() == 1);
    CHECK(trades[0].maker_order_id == 2);
    CHECK(trades[0].price == 100);
}

// ---------------------------------------------------------------------
// Time priority (FIFO) within a price level
// ---------------------------------------------------------------------

TEST_CASE("several orders at the same price are matched strictly FIFO", "[orderbook][priority][time]") {
    OrderBook book;
    book.submit_limit_order(1, Side::Sell, 100, 10);
    book.submit_limit_order(2, Side::Sell, 100, 10);
    book.submit_limit_order(3, Side::Sell, 100, 10);

    auto ids = book.orders_at(Side::Sell, 100);
    REQUIRE(ids.size() == 3);
    CHECK(ids[0] == 1);
    CHECK(ids[1] == 2);
    CHECK(ids[2] == 3);

    // Consume exactly the first two orders' worth of quantity.
    auto trades = book.submit_limit_order(4, Side::Buy, 100, 20);
    REQUIRE(trades.size() == 2);
    CHECK(trades[0].maker_order_id == 1);
    CHECK(trades[1].maker_order_id == 2);

    CHECK(book.get_order(1)->status == OrderStatus::Filled);
    CHECK(book.get_order(2)->status == OrderStatus::Filled);
    CHECK(book.get_order(3)->status == OrderStatus::New);

    auto remaining_ids = book.orders_at(Side::Sell, 100);
    REQUIRE(remaining_ids.size() == 1);
    CHECK(remaining_ids[0] == 3);
}

TEST_CASE("one resting order can receive multiple partial fills from separate incoming orders", "[orderbook][partial][time]") {
    OrderBook book;
    book.submit_limit_order(1, Side::Sell, 100, 100);

    auto t1 = book.submit_limit_order(2, Side::Buy, 100, 30);
    REQUIRE(t1.size() == 1);
    CHECK(t1[0].quantity == 30);
    CHECK(book.get_order(1)->remaining_quantity == 70);
    CHECK(book.get_order(1)->status == OrderStatus::PartiallyFilled);

    auto t2 = book.submit_limit_order(3, Side::Buy, 100, 30);
    REQUIRE(t2.size() == 1);
    CHECK(t2[0].quantity == 30);
    CHECK(book.get_order(1)->remaining_quantity == 40);

    auto t3 = book.submit_limit_order(4, Side::Buy, 100, 40);
    REQUIRE(t3.size() == 1);
    CHECK(t3[0].quantity == 40);
    CHECK(book.get_order(1)->remaining_quantity == 0);
    CHECK(book.get_order(1)->status == OrderStatus::Filled);
    check_order_invariants(*book.get_order(1));
}

// ---------------------------------------------------------------------
// Multi-level matching
// ---------------------------------------------------------------------

TEST_CASE("one incoming order sweeps multiple price levels and multiple resting orders", "[orderbook][match][multilevel]") {
    OrderBook book;
    book.submit_limit_order(1, Side::Sell, 100, 10);
    book.submit_limit_order(2, Side::Sell, 100, 10);
    book.submit_limit_order(3, Side::Sell, 101, 10);
    book.submit_limit_order(4, Side::Sell, 102, 50);

    auto trades = book.submit_limit_order(5, Side::Buy, 102, 35);
    REQUIRE(trades.size() == 4);
    CHECK(trades[0].maker_order_id == 1);
    CHECK(trades[0].price == 100);
    CHECK(trades[1].maker_order_id == 2);
    CHECK(trades[1].price == 100);
    CHECK(trades[2].maker_order_id == 3);
    CHECK(trades[2].price == 101);
    CHECK(trades[3].maker_order_id == 4);
    CHECK(trades[3].price == 102);
    CHECK(trades[3].quantity == 5);

    CHECK(book.get_order(4)->remaining_quantity == 45);
    CHECK(book.bid_level_count() == 0);
    REQUIRE(book.best_ask().has_value());
    CHECK(*book.best_ask() == 102);
}

TEST_CASE("a limit order stops sweeping once price no longer crosses", "[orderbook][match][multilevel]") {
    OrderBook book;
    book.submit_limit_order(1, Side::Sell, 100, 10);
    book.submit_limit_order(2, Side::Sell, 105, 10);

    auto trades = book.submit_limit_order(3, Side::Buy, 100, 20);
    REQUIRE(trades.size() == 1);
    CHECK(trades[0].price == 100);
    CHECK(book.get_order(3)->remaining_quantity == 10); // rests, does not reach the 105 level
    REQUIRE(book.best_ask().has_value());
    CHECK(*book.best_ask() == 105);
}

// ---------------------------------------------------------------------
// Market orders
// ---------------------------------------------------------------------

TEST_CASE("a market order matches at the best available price without a limit", "[orderbook][market]") {
    OrderBook book;
    book.submit_limit_order(1, Side::Sell, 100, 50);
    auto trades = book.submit_market_order(2, Side::Buy, 50);
    REQUIRE(trades.size() == 1);
    CHECK(trades[0].price == 100);
    CHECK(book.get_order(2)->status == OrderStatus::Filled);
}

TEST_CASE("a market order sweeps multiple levels and drops any unfilled remainder", "[orderbook][market][multilevel]") {
    OrderBook book;
    book.submit_limit_order(1, Side::Sell, 100, 10);
    book.submit_limit_order(2, Side::Sell, 101, 10);
    book.submit_limit_order(3, Side::Sell, 102, 10);

    auto trades = book.submit_market_order(4, Side::Buy, 100); // far more than available
    REQUIRE(trades.size() == 3);
    CHECK(trades[0].price == 100);
    CHECK(trades[1].price == 101);
    CHECK(trades[2].price == 102);

    const Order* o = book.get_order(4);
    REQUIRE(o != nullptr);
    CHECK(o->remaining_quantity == 70);
    CHECK(o->status == OrderStatus::Cancelled); // IOC: leftover dropped, not resting
    check_order_invariants(*o);

    CHECK(book.ask_level_count() == 0);
}

TEST_CASE("market orders never rest in the book", "[orderbook][market]") {
    OrderBook book;
    auto trades = book.submit_market_order(1, Side::Sell, 100);
    CHECK(trades.empty());
    CHECK_FALSE(book.best_ask().has_value());
    CHECK(book.get_order(1)->status == OrderStatus::Cancelled);
}

// ---------------------------------------------------------------------
// Cancellation
// ---------------------------------------------------------------------

TEST_CASE("cancelling a resting order removes it from the book", "[orderbook][cancel]") {
    OrderBook book;
    book.submit_limit_order(1, Side::Buy, 100, 10);
    CHECK(book.cancel_order(1));
    CHECK_FALSE(book.best_bid().has_value());
    CHECK(book.get_order(1)->status == OrderStatus::Cancelled);
}

TEST_CASE("cancelling twice fails cleanly the second time", "[orderbook][cancel][invalid]") {
    OrderBook book;
    book.submit_limit_order(1, Side::Buy, 100, 10);
    CHECK(book.cancel_order(1));
    CHECK_FALSE(book.cancel_order(1));
}

TEST_CASE("cancelling a fully-filled order fails cleanly", "[orderbook][cancel][invalid]") {
    OrderBook book;
    book.submit_limit_order(1, Side::Sell, 100, 10);
    book.submit_limit_order(2, Side::Buy, 100, 10);
    CHECK_FALSE(book.cancel_order(1));
    CHECK_FALSE(book.cancel_order(2));
}

TEST_CASE("a cancelled order cannot execute against a later matching order", "[orderbook][cancel][invariant]") {
    OrderBook book;
    book.submit_limit_order(1, Side::Sell, 100, 10);
    REQUIRE(book.cancel_order(1));

    auto trades = book.submit_limit_order(2, Side::Buy, 100, 10);
    CHECK(trades.empty());
    CHECK(book.get_order(2)->status == OrderStatus::New);
    CHECK(book.get_order(1)->status == OrderStatus::Cancelled);
}

TEST_CASE("cancelling the front order of a price queue preserves FIFO order of the rest", "[orderbook][cancel][time]") {
    OrderBook book;
    book.submit_limit_order(1, Side::Sell, 100, 10);
    book.submit_limit_order(2, Side::Sell, 100, 10);
    book.submit_limit_order(3, Side::Sell, 100, 10);

    REQUIRE(book.cancel_order(1));
    auto ids = book.orders_at(Side::Sell, 100);
    REQUIRE(ids.size() == 2);
    CHECK(ids[0] == 2);
    CHECK(ids[1] == 3);

    auto trades = book.submit_limit_order(4, Side::Buy, 100, 10);
    REQUIRE(trades.size() == 1);
    CHECK(trades[0].maker_order_id == 2); // not order 1 (cancelled) nor order 3 (behind order 2)
}

TEST_CASE("cancelling a middle order in a price queue preserves order of the others", "[orderbook][cancel][time]") {
    OrderBook book;
    book.submit_limit_order(1, Side::Sell, 100, 10);
    book.submit_limit_order(2, Side::Sell, 100, 10);
    book.submit_limit_order(3, Side::Sell, 100, 10);

    REQUIRE(book.cancel_order(2));
    auto ids = book.orders_at(Side::Sell, 100);
    REQUIRE(ids.size() == 2);
    CHECK(ids[0] == 1);
    CHECK(ids[1] == 3);
}

// ---------------------------------------------------------------------
// Modification
// ---------------------------------------------------------------------

TEST_CASE("decreasing quantity at the same price preserves queue priority", "[orderbook][modify][time]") {
    OrderBook book;
    book.submit_limit_order(1, Side::Sell, 100, 10);
    book.submit_limit_order(2, Side::Sell, 100, 10);

    REQUIRE(book.modify_order(1, std::nullopt, Quantity{4}));
    CHECK(book.get_order(1)->remaining_quantity == 4);
    CHECK(book.get_order(1)->quantity == 4);
    check_order_invariants(*book.get_order(1));

    auto ids = book.orders_at(Side::Sell, 100);
    REQUIRE(ids.size() == 2);
    CHECK(ids[0] == 1); // still first: priority preserved

    auto trades = book.submit_limit_order(3, Side::Buy, 100, 4);
    REQUIRE(trades.size() == 1);
    CHECK(trades[0].maker_order_id == 1);
}

TEST_CASE("increasing quantity resets queue priority to the back", "[orderbook][modify][time]") {
    OrderBook book;
    book.submit_limit_order(1, Side::Sell, 100, 10);
    book.submit_limit_order(2, Side::Sell, 100, 10);

    REQUIRE(book.modify_order(1, std::nullopt, Quantity{20}));
    auto ids = book.orders_at(Side::Sell, 100);
    REQUIRE(ids.size() == 2);
    CHECK(ids[0] == 2); // order 1 lost priority
    CHECK(ids[1] == 1);
    CHECK(book.get_order(1)->quantity == 20);
    CHECK(book.get_order(1)->remaining_quantity == 20);

    auto trades = book.submit_limit_order(3, Side::Buy, 100, 10);
    REQUIRE(trades.size() == 1);
    CHECK(trades[0].maker_order_id == 2);
}

TEST_CASE("changing price resets queue priority and moves the order to the new level", "[orderbook][modify][price]") {
    OrderBook book;
    book.submit_limit_order(1, Side::Sell, 100, 10);
    book.submit_limit_order(2, Side::Sell, 101, 10);

    REQUIRE(book.modify_order(1, Price{101}, std::nullopt));
    CHECK(book.order_count_at(Side::Sell, 100) == 0);
    CHECK(book.order_count_at(Side::Sell, 101) == 2);

    auto ids = book.orders_at(Side::Sell, 101);
    REQUIRE(ids.size() == 2);
    CHECK(ids[0] == 2); // order 2 was already at 101 and keeps priority
    CHECK(ids[1] == 1); // order 1 moved here and goes to the back
}

TEST_CASE("modifying a partially-filled order keeps its accumulated fill history", "[orderbook][modify][partial]") {
    OrderBook book;
    book.submit_limit_order(1, Side::Sell, 100, 100);
    book.submit_limit_order(2, Side::Buy, 100, 30); // order 1 now has filled=30, remaining=70

    REQUIRE(book.get_order(1)->filled_quantity() == 30);

    REQUIRE(book.modify_order(1, Price{99}, std::nullopt)); // priority reset via price change
    const Order* o = book.get_order(1);
    REQUIRE(o != nullptr);
    CHECK(o->filled_quantity() == 30);
    CHECK(o->remaining_quantity == 70);
    CHECK(o->quantity == 100);
    CHECK(o->status == OrderStatus::PartiallyFilled);
    check_order_invariants(*o);
}

TEST_CASE("modify rejects a non-positive new quantity or price", "[orderbook][modify][invalid]") {
    OrderBook book;
    book.submit_limit_order(1, Side::Sell, 100, 10);
    CHECK_FALSE(book.modify_order(1, std::nullopt, Quantity{0}));
    CHECK_FALSE(book.modify_order(1, std::nullopt, Quantity{-5}));
    CHECK_FALSE(book.modify_order(1, Price{0}, std::nullopt));
    CHECK(book.get_order(1)->remaining_quantity == 10); // unchanged
}

TEST_CASE("modify fails on an unknown, filled, cancelled, or market order", "[orderbook][modify][invalid]") {
    OrderBook book;

    book.submit_limit_order(1, Side::Sell, 100, 10);
    book.submit_limit_order(2, Side::Buy, 100, 10); // fills order 1
    CHECK_FALSE(book.modify_order(1, std::nullopt, Quantity{5}));

    book.submit_limit_order(3, Side::Sell, 200, 10);
    book.cancel_order(3);
    CHECK_FALSE(book.modify_order(3, std::nullopt, Quantity{5}));

    book.submit_market_order(4, Side::Sell, 10);
    CHECK_FALSE(book.modify_order(4, std::nullopt, Quantity{5}));

    CHECK_FALSE(book.modify_order(999, std::nullopt, Quantity{5}));
}

// ---------------------------------------------------------------------
// Best bid/ask maintenance under churn
// ---------------------------------------------------------------------

TEST_CASE("best bid/ask update correctly as levels are exhausted and re-added", "[orderbook][invariant]") {
    OrderBook book;
    book.submit_limit_order(1, Side::Buy, 100, 10);
    book.submit_limit_order(2, Side::Buy, 99, 10);
    CHECK(*book.best_bid() == 100);

    book.cancel_order(1);
    CHECK(*book.best_bid() == 99);

    book.submit_limit_order(3, Side::Buy, 105, 10);
    CHECK(*book.best_bid() == 105);

    book.submit_limit_order(4, Side::Sell, 105, 10); // fully fills order 3
    CHECK(*book.best_bid() == 99);
}
