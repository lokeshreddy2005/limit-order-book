#pragma once

#include <chrono>

#include "orderbook/Types.hpp"

namespace orderbook {

// A single execution produced by matching one incoming ("taker") order
// against one resting ("maker") order. A single call into the matching
// engine can produce zero, one, or many trades.
struct Trade {
    TradeId id{};

    OrderId taker_order_id{}; // the incoming order that triggered the match
    OrderId maker_order_id{}; // the resting order it matched against

    // Trades execute at the resting (maker) order's price, never the
    // taker's limit price. This is the standard price-time-priority
    // convention: the maker posted a firm price and is entitled to it; the
    // taker only specified a *worst acceptable* price (or, for a market
    // order, no price at all).
    Price price{};
    Quantity quantity{};

    Side taker_side{};
    Sequence sequence{}; // engine-assigned, monotonically increasing
    std::chrono::steady_clock::time_point timestamp{};
};

} // namespace orderbook
