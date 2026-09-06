#pragma once

#include <chrono>
#include <stdexcept>

#include "orderbook/Types.hpp"

namespace orderbook {

// A single order in the book.
//
// Design notes
// ------------
// `quantity` is the order's *current* working size, not necessarily the size
// it was first submitted with. Two operations mutate it after submission:
//   - A fill reduces `remaining_quantity` and increases `filled_quantity_`.
//   - A priority-preserving modification (a reduction in size at the same
//     price, see OrderBook::modify_order) reduces both `quantity` and
//     `remaining_quantity` by the same amount, leaving `filled_quantity_`
//     untouched.
// In both cases the invariant
//     filled_quantity() + remaining_quantity == quantity
// holds after every operation. This is checked by the test suite
// (see tests/test_orderbook.cpp, "invariants" section).
//
// A modification that increases size or changes price is implemented as an
// atomic cancel + replace (new sequence number, loses queue priority) rather
// than an in-place mutation; see OrderBook::modify_order for the exchange
// rule this follows and the rationale.
struct Order {
    OrderId id{};
    Side side{Side::Buy};
    OrderType type{OrderType::Limit};

    // Ignored (and always 0) for market orders.
    Price price{0};

    Quantity quantity{0};
    Quantity remaining_quantity{0};

    // Assigned by OrderBook on (re)submission. This -- not the wall-clock
    // timestamp below -- is the authoritative tie-breaker for time priority:
    // it is a strictly monotonically increasing counter, so it is immune to
    // clock resolution/skew and gives deterministic, reproducible ordering.
    Sequence sequence{0};

    // Informational only; not used by the matching engine for ordering
    // decisions. Useful for logging/latency analysis.
    std::chrono::steady_clock::time_point timestamp{};

    OrderStatus status{OrderStatus::New};

    Quantity filled_quantity() const noexcept {
        return quantity - remaining_quantity;
    }

    bool is_active() const noexcept {
        return status == OrderStatus::New || status == OrderStatus::PartiallyFilled;
    }
};

} // namespace orderbook
