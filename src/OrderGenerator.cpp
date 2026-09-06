#include "orderbook/OrderGenerator.hpp"

#include <algorithm>
#include <cmath>
#include <random>

namespace orderbook {

OrderGenerator::OrderGenerator(GeneratorConfig config) : config_(std::move(config)) {}

std::vector<GeneratedOrder> OrderGenerator::generate() {
    std::mt19937_64 rng(config_.seed);

    std::bernoulli_distribution buy_dist(config_.buy_probability);
    std::bernoulli_distribution market_dist(config_.market_order_probability);
    std::normal_distribution<double> price_dist(static_cast<double>(config_.mid_price), config_.price_stddev_ticks);
    std::uniform_int_distribution<Quantity> qty_dist(config_.min_quantity, config_.max_quantity);
    std::exponential_distribution<double> interarrival_dist(1.0 / config_.mean_interarrival.count());

    std::vector<GeneratedOrder> orders;
    orders.reserve(config_.num_orders);

    std::chrono::duration<double> t{0.0};

    for (std::size_t i = 0; i < config_.num_orders; ++i) {
        t += std::chrono::duration<double>(interarrival_dist(rng));

        GeneratedOrder order;
        order.side = buy_dist(rng) ? Side::Buy : Side::Sell;
        order.type = market_dist(rng) ? OrderType::Market : OrderType::Limit;
        order.quantity = qty_dist(rng);
        order.arrival_offset = t;

        if (order.type == OrderType::Limit) {
            Price price = static_cast<Price>(std::llround(price_dist(rng)));
            order.price = std::max<Price>(price, 1);
        } else {
            order.price = 0;
        }

        orders.push_back(order);
    }

    return orders;
}

} // namespace orderbook
