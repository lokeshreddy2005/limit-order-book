#include "orderbook/OrderBook.hpp"

#include <algorithm>
#include <stdexcept>

namespace orderbook {

OrderBook::OrderBook(std::string symbol) : symbol_(std::move(symbol)) {}

void OrderBook::reserve(std::size_t expected_orders) {
    orders_.reserve(expected_orders);
    locations_.reserve(expected_orders);
}

std::vector<Trade> OrderBook::submit_limit_order(OrderId id, Side side, Price price, Quantity quantity) {
    if (quantity <= 0) {
        throw std::invalid_argument("OrderBook::submit_limit_order: quantity must be positive");
    }
    if (price <= 0) {
        throw std::invalid_argument("OrderBook::submit_limit_order: price must be positive");
    }
    if (orders_.find(id) != orders_.end()) {
        throw std::invalid_argument("OrderBook::submit_limit_order: duplicate order id");
    }

    Order order;
    order.id = id;
    order.side = side;
    order.type = OrderType::Limit;
    order.price = price;
    order.quantity = quantity;
    order.remaining_quantity = quantity;
    order.sequence = next_sequence_++;
    order.timestamp = std::chrono::steady_clock::now();
    order.status = OrderStatus::New;

    std::vector<Trade> trades = match(order);

    if (order.remaining_quantity > 0) {
        order.status = trades.empty() ? OrderStatus::New : OrderStatus::PartiallyFilled;
        insert_resting(order);
    } else {
        order.status = OrderStatus::Filled;
        orders_[id] = order;
    }

    return trades;
}

std::vector<Trade> OrderBook::submit_market_order(OrderId id, Side side, Quantity quantity) {
    if (quantity <= 0) {
        throw std::invalid_argument("OrderBook::submit_market_order: quantity must be positive");
    }
    if (orders_.find(id) != orders_.end()) {
        throw std::invalid_argument("OrderBook::submit_market_order: duplicate order id");
    }

    Order order;
    order.id = id;
    order.side = side;
    order.type = OrderType::Market;
    order.price = 0;
    order.quantity = quantity;
    order.remaining_quantity = quantity;
    order.sequence = next_sequence_++;
    order.timestamp = std::chrono::steady_clock::now();
    order.status = OrderStatus::New;

    std::vector<Trade> trades = match(order);

    // IOC: market orders never rest. Any unfilled remainder is dropped.
    order.status = (order.remaining_quantity == 0) ? OrderStatus::Filled : OrderStatus::Cancelled;
    orders_[id] = order;

    return trades;
}

std::vector<Trade> OrderBook::match(Order& incoming) {
    std::vector<Trade> trades;
    const bool is_buy = incoming.side == Side::Buy;

    auto crosses = [&](Price level_price) {
        if (incoming.type == OrderType::Market) return true;
        return is_buy ? (level_price <= incoming.price) : (level_price >= incoming.price);
    };

    // The two branches are structurally identical (drain the opposite
    // book's best level while it exists and crosses); they are written out
    // separately rather than merged behind a runtime side-comparison in the
    // hot path, since `bids_`/`asks_` have different std::map comparator
    // types and cannot share a reference.
    if (is_buy) {
        while (incoming.remaining_quantity > 0 && !asks_.empty()) {
            auto level_it = asks_.begin();
            const Price level_price = level_it->first;
            if (!crosses(level_price)) break;

            auto& queue = level_it->second;
            while (incoming.remaining_quantity > 0 && !queue.empty()) {
                OrderId resting_id = queue.front();
                Order& resting = orders_.at(resting_id);

                Quantity traded = std::min(incoming.remaining_quantity, resting.remaining_quantity);
                incoming.remaining_quantity -= traded;
                resting.remaining_quantity -= traded;

                trades.push_back(make_trade(incoming, resting, level_price, traded));

                if (resting.remaining_quantity == 0) {
                    resting.status = OrderStatus::Filled;
                    queue.pop_front();
                    locations_.erase(resting_id);
                } else {
                    resting.status = OrderStatus::PartiallyFilled;
                }
            }
            if (queue.empty()) {
                asks_.erase(level_it);
            }
        }
    } else {
        while (incoming.remaining_quantity > 0 && !bids_.empty()) {
            auto level_it = bids_.begin();
            const Price level_price = level_it->first;
            if (!crosses(level_price)) break;

            auto& queue = level_it->second;
            while (incoming.remaining_quantity > 0 && !queue.empty()) {
                OrderId resting_id = queue.front();
                Order& resting = orders_.at(resting_id);

                Quantity traded = std::min(incoming.remaining_quantity, resting.remaining_quantity);
                incoming.remaining_quantity -= traded;
                resting.remaining_quantity -= traded;

                trades.push_back(make_trade(incoming, resting, level_price, traded));

                if (resting.remaining_quantity == 0) {
                    resting.status = OrderStatus::Filled;
                    queue.pop_front();
                    locations_.erase(resting_id);
                } else {
                    resting.status = OrderStatus::PartiallyFilled;
                }
            }
            if (queue.empty()) {
                bids_.erase(level_it);
            }
        }
    }

    return trades;
}

Trade OrderBook::make_trade(const Order& taker, const Order& maker, Price price, Quantity quantity) {
    Trade trade;
    trade.id = next_trade_id_++;
    trade.taker_order_id = taker.id;
    trade.maker_order_id = maker.id;
    trade.price = price;
    trade.quantity = quantity;
    trade.taker_side = taker.side;
    trade.sequence = next_sequence_;
    trade.timestamp = std::chrono::steady_clock::now();
    return trade;
}

void OrderBook::insert_resting(Order order) {
    const OrderId id = order.id;
    const Side side = order.side;
    const Price price = order.price;

    if (side == Side::Buy) {
        bids_[price].push_back(id);
    } else {
        asks_[price].push_back(id);
    }
    locations_[id] = OrderLocation{side, price};
    orders_[id] = std::move(order);
}

void OrderBook::erase_from_level(Side side, Price price, OrderId id) {
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        if (it == bids_.end()) return;
        auto& queue = it->second;
        auto qit = std::find(queue.begin(), queue.end(), id);
        if (qit != queue.end()) queue.erase(qit);
        if (queue.empty()) bids_.erase(it);
    } else {
        auto it = asks_.find(price);
        if (it == asks_.end()) return;
        auto& queue = it->second;
        auto qit = std::find(queue.begin(), queue.end(), id);
        if (qit != queue.end()) queue.erase(qit);
        if (queue.empty()) asks_.erase(it);
    }
}

bool OrderBook::cancel_order(OrderId id) {
    auto loc_it = locations_.find(id);
    if (loc_it == locations_.end()) return false;

    auto order_it = orders_.find(id);
    if (order_it == orders_.end() || !order_it->second.is_active()) return false;

    erase_from_level(loc_it->second.side, loc_it->second.price, id);
    order_it->second.status = OrderStatus::Cancelled;
    locations_.erase(loc_it);
    return true;
}

bool OrderBook::modify_order(OrderId id, std::optional<Price> new_price, std::optional<Quantity> new_remaining_quantity) {
    if (new_remaining_quantity && *new_remaining_quantity <= 0) return false;
    if (new_price && *new_price <= 0) return false;

    auto loc_it = locations_.find(id);
    if (loc_it == locations_.end()) return false;

    auto order_it = orders_.find(id);
    if (order_it == orders_.end() || !order_it->second.is_active()) return false;

    Order& order = order_it->second;
    if (order.type != OrderType::Limit) return false;

    const Price effective_price = new_price.value_or(order.price);
    const Quantity effective_remaining = new_remaining_quantity.value_or(order.remaining_quantity);

    const bool price_changed = new_price.has_value() && *new_price != order.price;
    const bool qty_increased = new_remaining_quantity.has_value() && *new_remaining_quantity > order.remaining_quantity;

    if (price_changed || qty_increased) {
        // Priority reset: cancel + replace at the back of the (possibly
        // new) price level with a fresh sequence number.
        const Side side = order.side;
        erase_from_level(side, order.price, id);
        locations_.erase(loc_it);

        const Quantity filled_so_far = order.filled_quantity();
        Order replacement = order;
        replacement.price = effective_price;
        replacement.remaining_quantity = effective_remaining;
        replacement.quantity = filled_so_far + effective_remaining;
        replacement.sequence = next_sequence_++;
        replacement.status = (filled_so_far > 0) ? OrderStatus::PartiallyFilled : OrderStatus::New;

        insert_resting(replacement);
    } else {
        // Priority preserved: same price, same-or-smaller size. Shrink
        // `quantity` by the same delta so filled_quantity() stays correct.
        const Quantity delta = order.remaining_quantity - effective_remaining;
        order.remaining_quantity = effective_remaining;
        order.quantity -= delta;
    }

    return true;
}

std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

const Order* OrderBook::get_order(OrderId id) const {
    auto it = orders_.find(id);
    return it == orders_.end() ? nullptr : &it->second;
}

Quantity OrderBook::quantity_at(Side side, Price price) const {
    Quantity total = 0;
    const auto accumulate = [&](const std::deque<OrderId>& queue) {
        for (OrderId id : queue) {
            total += orders_.at(id).remaining_quantity;
        }
    };
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        if (it != bids_.end()) accumulate(it->second);
    } else {
        auto it = asks_.find(price);
        if (it != asks_.end()) accumulate(it->second);
    }
    return total;
}

std::size_t OrderBook::order_count_at(Side side, Price price) const {
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        return it == bids_.end() ? 0 : it->second.size();
    } else {
        auto it = asks_.find(price);
        return it == asks_.end() ? 0 : it->second.size();
    }
}

std::vector<OrderId> OrderBook::orders_at(Side side, Price price) const {
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        if (it == bids_.end()) return {};
        return std::vector<OrderId>(it->second.begin(), it->second.end());
    } else {
        auto it = asks_.find(price);
        if (it == asks_.end()) return {};
        return std::vector<OrderId>(it->second.begin(), it->second.end());
    }
}

} // namespace orderbook
