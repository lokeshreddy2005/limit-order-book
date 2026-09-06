#!/usr/bin/env bash
# Convenience wrapper: build (if needed) and run the correctness test suite.
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

if [ ! -f bin/orderbook_tests ] && [ ! -f bin/orderbook_tests.exe ]; then
    ./build.sh
fi

if [ -f bin/orderbook_tests.exe ]; then
    ./bin/orderbook_tests.exe "$@"
else
    ./bin/orderbook_tests "$@"
fi
