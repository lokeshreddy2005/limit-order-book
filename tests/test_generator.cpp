#include "third_party/catch.hpp"

#include "orderbook/OrderGenerator.hpp"
#include "orderbook/OrderBook.hpp"

using namespace orderbook;

TEST_CASE("the generator produces the requested number of orders", "[generator]") {
    GeneratorConfig cfg;
    cfg.num_orders = 500;
    OrderGenerator gen(cfg);
    auto orders = gen.generate();
    CHECK(orders.size() == 500);
}

TEST_CASE("the same seed produces an identical sequence of orders", "[generator][determinism]") {
    GeneratorConfig cfg;
    cfg.seed = 777;
    cfg.num_orders = 1000;

    OrderGenerator gen_a(cfg);
    OrderGenerator gen_b(cfg);
    auto a = gen_a.generate();
    auto b = gen_b.generate();

    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].side == b[i].side);
        CHECK(a[i].type == b[i].type);
        CHECK(a[i].price == b[i].price);
        CHECK(a[i].quantity == b[i].quantity);
        CHECK(a[i].arrival_offset.count() == b[i].arrival_offset.count());
    }
}

TEST_CASE("different seeds produce different sequences", "[generator][determinism]") {
    GeneratorConfig cfg_a;
    cfg_a.seed = 1;
    cfg_a.num_orders = 200;
    GeneratorConfig cfg_b = cfg_a;
    cfg_b.seed = 2;

    auto a = OrderGenerator(cfg_a).generate();
    auto b = OrderGenerator(cfg_b).generate();

    bool any_difference = false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].side != b[i].side || a[i].price != b[i].price || a[i].quantity != b[i].quantity) {
            any_difference = true;
            break;
        }
    }
    CHECK(any_difference);
}

TEST_CASE("arrival offsets are non-decreasing (a valid Poisson process realization)", "[generator]") {
    GeneratorConfig cfg;
    cfg.num_orders = 2000;
    auto orders = OrderGenerator(cfg).generate();
    for (std::size_t i = 1; i < orders.size(); ++i) {
        CHECK(orders[i].arrival_offset.count() >= orders[i - 1].arrival_offset.count());
    }
}

TEST_CASE("generated quantities and prices respect configured bounds", "[generator]") {
    GeneratorConfig cfg;
    cfg.num_orders = 5000;
    cfg.min_quantity = 10;
    cfg.max_quantity = 50;
    auto orders = OrderGenerator(cfg).generate();
    for (const auto& o : orders) {
        CHECK(o.quantity >= cfg.min_quantity);
        CHECK(o.quantity <= cfg.max_quantity);
        if (o.type == OrderType::Limit) {
            CHECK(o.price >= 1);
        }
    }
}

TEST_CASE("an extreme buy_probability of 1.0 generates only buy orders", "[generator]") {
    GeneratorConfig cfg;
    cfg.num_orders = 300;
    cfg.buy_probability = 1.0;
    auto orders = OrderGenerator(cfg).generate();
    for (const auto& o : orders) {
        CHECK(o.side == Side::Buy);
    }
}

TEST_CASE("generated order flow can be fed into a real OrderBook without error", "[generator][integration]") {
    GeneratorConfig cfg;
    cfg.num_orders = 2000;
    cfg.seed = 123;
    auto orders = OrderGenerator(cfg).generate();

    OrderBook book;
    OrderId next_id = 1;
    std::size_t trade_count = 0;
    for (const auto& o : orders) {
        std::vector<Trade> trades;
        if (o.type == OrderType::Limit) {
            trades = book.submit_limit_order(next_id++, o.side, o.price, o.quantity);
        } else {
            trades = book.submit_market_order(next_id++, o.side, o.quantity);
        }
        trade_count += trades.size();
    }
    // A reasonably dense, two-sided random order flow around a fixed mid
    // price should produce at least some crossing trades.
    CHECK(trade_count > 0);
}
