#!/usr/bin/env bash
# Convenience wrapper: build (if needed) and run the benchmark harness.
# Usage: ./scripts/run_benchmark.sh [num_orders] [seed]
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

if [ ! -f bin/orderbook_benchmark ] && [ ! -f bin/orderbook_benchmark.exe ]; then
    ./build.sh
fi

if [ -f bin/orderbook_benchmark.exe ]; then
    ./bin/orderbook_benchmark.exe "$@"
else
    ./bin/orderbook_benchmark "$@"
fi
