#pragma once

// Minimal latency-statistics helper for the benchmark harness. Deliberately
// not part of the orderbook library itself -- this is measurement
// infrastructure, not a matching-engine concern.

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

namespace bench {

struct LatencyStats {
    std::size_t count = 0;
    double mean_ns = 0.0;
    std::uint64_t min_ns = 0;
    std::uint64_t max_ns = 0;
    std::uint64_t p50_ns = 0;
    std::uint64_t p95_ns = 0;
    std::uint64_t p99_ns = 0;
};

class LatencyRecorder {
public:
    explicit LatencyRecorder(std::size_t reserve_hint = 0) {
        if (reserve_hint) samples_.reserve(reserve_hint);
    }

    void record(std::uint64_t ns) { samples_.push_back(ns); }

    std::size_t size() const { return samples_.size(); }

    // Sorts a *copy* of the recorded samples to compute percentiles;
    // does not mutate recording order, so this can safely be called
    // mid-benchmark if ever needed.
    LatencyStats compute() const {
        LatencyStats stats;
        stats.count = samples_.size();
        if (samples_.empty()) return stats;

        std::vector<std::uint64_t> sorted = samples_;
        std::sort(sorted.begin(), sorted.end());

        const auto sum = std::accumulate(sorted.begin(), sorted.end(), std::uint64_t{0});
        stats.mean_ns = static_cast<double>(sum) / static_cast<double>(sorted.size());
        stats.min_ns = sorted.front();
        stats.max_ns = sorted.back();
        stats.p50_ns = percentile(sorted, 0.50);
        stats.p95_ns = percentile(sorted, 0.95);
        stats.p99_ns = percentile(sorted, 0.99);
        return stats;
    }

private:
    static std::uint64_t percentile(const std::vector<std::uint64_t>& sorted, double p) {
        if (sorted.empty()) return 0;
        std::size_t idx = static_cast<std::size_t>(p * static_cast<double>(sorted.size() - 1));
        return sorted[idx];
    }

    std::vector<std::uint64_t> samples_;
};

} // namespace bench
