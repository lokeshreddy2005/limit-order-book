#!/usr/bin/env bash
# Build script for the order book project. No CMake/Make dependency is
# required -- this repo is small enough that a flat compile of each
# translation unit, invoked directly, is simpler and more transparent than
# introducing a build-system dependency. See README.md "Build instructions".
#
# Usage:
#   ./build.sh            # build demo, tests, and benchmark (release mode)
#   CXX=clang++ ./build.sh  # override the compiler

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

CXX="${CXX:-g++}"
STD="-std=c++20"
WARN="-Wall -Wextra -Wpedantic"
INCLUDES="-Iinclude"

# Release flags: used for the library, the demo, and the test binary.
# -O2 (not -O3) for the correctness-focused test binary keeps its build fast
# without giving up any optimization level that would meaningfully affect
# whether a bug reproduces.
RELEASE_FLAGS="$STD -O3 -DNDEBUG $WARN $INCLUDES"
TEST_FLAGS="$STD -O2 $WARN $INCLUDES -Itests"

# Benchmark flags additionally enable -march=native: standard practice for
# micro-benchmarking on the machine you are measuring on, at the cost of
# producing a binary that is not portable to other CPUs. See README.md
# "Design tradeoffs".
BENCH_DESC="-std=c++20 -O3 -march=native -DNDEBUG -Wall -Wextra -Wpedantic"
BENCH_FLAGS="$STD -O3 -march=native -DNDEBUG $WARN $INCLUDES"

mkdir -p build/obj bin

echo "== compiler =="
"$CXX" --version | head -1
echo

echo "== building library objects (release: $RELEASE_FLAGS) =="
"$CXX" $RELEASE_FLAGS -c src/OrderBook.cpp -o build/obj/OrderBook.o
"$CXX" $RELEASE_FLAGS -c src/OrderGenerator.cpp -o build/obj/OrderGenerator.o

echo "== building demo executable (bin/orderbook_demo) =="
"$CXX" $RELEASE_FLAGS src/main.cpp build/obj/OrderBook.o build/obj/OrderGenerator.o -o bin/orderbook_demo

echo "== building test executable (bin/orderbook_tests, Catch2) =="
"$CXX" $TEST_FLAGS \
    tests/test_main.cpp tests/test_orderbook.cpp tests/test_generator.cpp \
    src/OrderBook.cpp src/OrderGenerator.cpp \
    -o bin/orderbook_tests

echo "== building benchmark executable (bin/orderbook_benchmark: $BENCH_FLAGS) =="
printf '#pragma once\n#define OB_BUILD_FLAGS "%s"\n' "$BENCH_DESC" > build/obj/BuildInfo.hpp
"$CXX" $BENCH_FLAGS -Ibuild/obj \
    benchmarks/benchmark_main.cpp src/OrderBook.cpp src/OrderGenerator.cpp \
    -o bin/orderbook_benchmark

echo
echo "== build complete =="
echo "  bin/orderbook_demo"
echo "  bin/orderbook_tests"
echo "  bin/orderbook_benchmark"
