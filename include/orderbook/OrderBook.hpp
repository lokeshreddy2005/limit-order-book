#pragma once

#include <cstddef>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "orderbook/Order.hpp"
#include "orderbook/Trade.hpp"
#include "orderbook/Types.hpp"

namespace orderbook {

// A single-instrument limit order book and matching engine.
//
// Data structure choice
// ----------------------
// Each side of the book is a `std::map<Price, std::deque<OrderId>>`:
//   - `std::map` keeps price levels sorted by key at all times (bids
//     descending via `std::greater<Price>`, asks ascending via the default
//     `std::less<Price>`), so the best price on either side is always
//     `begin()` -- O(1) to read. It is implemented as a red-black tree, so
//     inserting a brand-new price level, erasing an exhausted one, and
//     locating an existing one are all O(log P) where P is the number of
//     distinct resting price levels. That logarithmic cost buys iterator/
//     reference stability for elements that are *not* touched by an
//     operation, which matters because `locations_` (below) caches
//     (side, price) for O(1) order lookup and must not be invalidated by
//     unrelated inserts/erases elsewhere in the map.
//   - `std::deque<OrderId>` holds the orders resting at a given price in
//     strict arrival order, giving FIFO time priority for free: a new order
//     is `push_back`ed (O(1) amortized) and the engine always matches
//     against `front()` (O(1)). A `std::vector` would need to shift
//     elements on front-removal; a `std::list` would avoid that but not the
//     iterator-invalidation issue described below, and has worse cache
//     locality for the (typically short) queue at one price level.
//
// This module stores `OrderId` in the deque, not the `Order` itself, and
// keeps the single authoritative copy of every order's mutable state in
// `orders_` (an `unordered_map<OrderId, Order>`, average O(1) lookup). This
// is deliberate: a `std::deque` invalidates *all* of its iterators when an
// element is erased from the middle (only front/back erasure is cheap and
// iterator-stable for a deque), so long-lived iterators into a price-level
// deque are not safe to cache. Cancelling or modifying an order therefore
// costs O(log P + k) -- O(log P) to find the price level, plus a linear
// scan of the k orders resting at that level to find the one being removed
// -- rather than O(log P) with a cached iterator. This is the main
// deliberate tradeoff in this design; see README.md "Design tradeoffs" for
// the alternative (an intrusive linked list per level, giving O(1) cancel)
// and why it was not used here.
//
// Complexity summary (P = distinct price levels on one side, k = orders
// resting at one price level, n = total resting orders):
//   - best bid / best ask                          O(1)
//   - locate a price level                         O(log P)
//   - insert a new resting order                   O(log P) amortized
//   - cancel / modify an existing order             O(log P + k)
//   - submit an order that matches m resting orders O(m log P) worst case
//     (each of the m fills can exhaust a price level, requiring an erase
//     from the map; a fill that only partially consumes the level's front
//     order is O(1))
//
// Thread-safety: none. This class is intentionally single-threaded --
// see README.md "Out of scope" for why concurrent matching was not
// attempted here.
class OrderBook {
public:
    explicit OrderBook(std::string symbol = "SYMBOL");

    // Pre-sizes the internal hash tables for an expected number of orders.
    // Optional, but recommended when the approximate order volume for a
    // session is known ahead of time: `orders_`/`locations_` are
    // `std::unordered_map`s, and an unreserved map that grows one insert at
    // a time will occasionally rehash the entire table on a single insert,
    // which is where this project's benchmark traced its worst tail
    // latencies to (see README.md "Benchmark methodology" /
    // "Design tradeoffs"). Reserving up front does not change any
    // asymptotic complexity -- it just avoids paying for those rehashes
    // during the operations being measured (or during live trading).
    void reserve(std::size_t expected_orders);

    // Submits a new limit order. Matches immediately against the opposite
    // book while a crossing price is available; any unfilled remainder
    // rests in the book. Returns the trades generated (possibly empty).
    // Throws std::invalid_argument if `quantity <= 0`, `price <= 0`, or
    // `id` has already been used by a previous order.
    std::vector<Trade> submit_limit_order(OrderId id, Side side, Price price, Quantity quantity);

    // Submits a market order: matches immediately against the best
    // available opposite-side liquidity regardless of price, sweeping
    // additional price levels if the first is fully consumed. Market
    // orders are IOC (immediate-or-cancel): they never rest in the book, so
    // any quantity that cannot be filled immediately is dropped. Returns
    // the trades generated. Throws std::invalid_argument if `quantity <= 0`
    // or `id` has already been used.
    std::vector<Trade> submit_market_order(OrderId id, Side side, Quantity quantity);

    // Cancels a resting order. Returns false (a clean no-op) if the id is
    // unknown, or refers to an order that is not currently resting (already
    // filled or already cancelled).
    bool cancel_order(OrderId id);

    // Modifies a resting limit order's price and/or remaining quantity.
    // Either parameter may be omitted (std::nullopt) to leave it unchanged.
    //
    // Exchange-style priority rule (see README.md "Order lifecycle" for the
    // full discussion): a change of price, or an *increase* in quantity,
    // is treated as a cancel + replace -- the order loses its place in the
    // queue and is re-inserted at the back of the (possibly new) price
    // level with a fresh sequence number. A quantity *decrease* at the same
    // price preserves the order's existing queue position and sequence
    // number, exactly like real exchanges (e.g. Nasdaq, NYSE) do, on the
    // reasoning that shrinking an order cannot let it unfairly jump ahead
    // of orders that were already resting behind it.
    //
    // Returns false (a clean no-op) if the id is unknown, not currently
    // resting, is a market order, or if the requested new quantity is <= 0.
    bool modify_order(OrderId id, std::optional<Price> new_price, std::optional<Quantity> new_remaining_quantity);

    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;

    // Returns nullptr if `id` has never been submitted to this book.
    // Otherwise returns the current state of that order, whether it is
    // resting, filled, or cancelled -- this book keeps a full history of
    // every order it has ever seen.
    const Order* get_order(OrderId id) const;

    std::size_t bid_level_count() const { return bids_.size(); }
    std::size_t ask_level_count() const { return asks_.size(); }

    // Total remaining quantity resting at `price` on `side`. Returns 0 if
    // there is no such level.
    Quantity quantity_at(Side side, Price price) const;

    // Number of distinct orders resting at `price` on `side`.
    std::size_t order_count_at(Side side, Price price) const;

    // Order ids resting at `price` on `side`, in strict FIFO (arrival)
    // order. Intended for tests/introspection, not the hot path.
    std::vector<OrderId> orders_at(Side side, Price price) const;

    const std::string& symbol() const { return symbol_; }

private:
    struct OrderLocation {
        Side side;
        Price price;
    };

    std::string symbol_;

    std::map<Price, std::deque<OrderId>, std::greater<Price>> bids_;
    std::map<Price, std::deque<OrderId>, std::less<Price>> asks_;

    std::unordered_map<OrderId, Order> orders_;
    std::unordered_map<OrderId, OrderLocation> locations_;

    Sequence next_sequence_{0};
    TradeId next_trade_id_{1};

    std::vector<Trade> match(Order& incoming);
    void insert_resting(Order order);
    void erase_from_level(Side side, Price price, OrderId id);
    Trade make_trade(const Order& taker, const Order& maker, Price price, Quantity quantity);
};

} // namespace orderbook
