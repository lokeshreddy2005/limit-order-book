#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

#include "orderbook/Types.hpp"

namespace orderbook {

// Configuration for the synthetic order-flow generator. All fields have
// defaults so a caller can override just the ones they care about.
struct GeneratorConfig {
    std::uint64_t seed = 42;
    std::size_t num_orders = 10'000;

    // Probability the generated order is a Buy (vs Sell).
    double buy_probability = 0.5;

    // Probability the generated order is a Market order (vs Limit).
    double market_order_probability = 0.10;

    // Limit order prices are drawn from a normal distribution centered on
    // `mid_price` with standard deviation `price_stddev_ticks`, then rounded
    // to the nearest integer tick and clamped to be >= 1 tick. This
    // approximates the way real limit order flow clusters near the touch
    // and thins out away from it, without claiming to model any specific
    // market's real price distribution.
    Price mid_price = 10'000; // in ticks (e.g. 1 tick = $0.01 => $100.00)
    double price_stddev_ticks = 25.0;

    // Quantities are drawn uniformly from [min_quantity, max_quantity].
    Quantity min_quantity = 1;
    Quantity max_quantity = 500;

    // Order arrivals follow a Poisson process: inter-arrival times are
    // drawn from an exponential distribution with this mean, so the
    // resulting arrival counting process is Poisson with rate
    // 1 / mean_interarrival. This is the standard first-order model for
    // "orders arrive independently of each other at some average rate" and
    // is what most queueing-theoretic treatments of order flow assume.
    std::chrono::duration<double> mean_interarrival{0.001}; // seconds
};

// A single generated order, not yet submitted to any book.
struct GeneratedOrder {
    Side side;
    OrderType type;
    Price price; // meaningless for Market orders
    Quantity quantity;

    // Offset from t=0 of this order's arrival, per the Poisson process
    // described in GeneratorConfig::mean_interarrival. Strictly
    // non-decreasing across the returned sequence.
    std::chrono::duration<double> arrival_offset;
};

// Deterministic synthetic order-flow generator. Given the same
// GeneratorConfig (in particular the same seed), `generate()` always
// produces the exact same sequence of orders -- this is what makes
// benchmark runs and regression tests reproducible.
class OrderGenerator {
public:
    explicit OrderGenerator(GeneratorConfig config);

    std::vector<GeneratedOrder> generate();

    const GeneratorConfig& config() const { return config_; }

private:
    GeneratorConfig config_;
};

} // namespace orderbook
