#pragma once

#include <cstdint>

namespace orderbook {

// Order identifiers and sequencing.
using OrderId   = std::uint64_t;
using TradeId   = std::uint64_t;
using Sequence  = std::uint64_t;

// Prices are represented as signed 64-bit integer *ticks*, not floating point.
//
// Rationale: matching correctness depends on exact price comparisons and
// exact price-level equality (two orders at "the same price" must map to the
// same std::map key). IEEE-754 doubles cannot represent most decimal prices
// (e.g. 0.1) exactly, so repeated arithmetic or comparisons can silently
// misroute an order to the wrong price level or fail an equality check that
// should have succeeded. Integer ticks make every comparison and every map
// lookup exact. Callers pick a tick size appropriate to the instrument (e.g.
// 1 tick = $0.01 for an equity-like instrument) and convert at the edges of
// the system (order entry / display), never inside the matching engine.
using Price = std::int64_t;

// Quantities are integer share/contract counts. Negative quantities are
// never valid; see Order.hpp for the invariants this type is expected to
// satisfy.
using Quantity = std::int64_t;

enum class Side : std::uint8_t {
    Buy,
    Sell,
};

enum class OrderType : std::uint8_t {
    Limit,
    Market,
};

// Lifecycle states for an order. See Order.hpp for the full state machine
// and the transitions the matching engine is allowed to make.
enum class OrderStatus : std::uint8_t {
    New,             // accepted, resting, no fills yet
    PartiallyFilled, // resting, some quantity has traded
    Filled,          // fully traded, terminal
    Cancelled,       // removed before being fully filled, terminal
};

inline constexpr Side opposite(Side side) noexcept {
    return side == Side::Buy ? Side::Sell : Side::Buy;
}

} // namespace orderbook
