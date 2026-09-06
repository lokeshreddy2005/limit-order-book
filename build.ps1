# Build script for the order book project (native PowerShell equivalent of
# build.sh, for developers without a bash/MSYS2 shell). No CMake/Make
# dependency is required. See README.md "Build instructions".
#
# Usage:
#   .\build.ps1

$ErrorActionPreference = "Stop"
Set-Location -Path $PSScriptRoot

$CXX = if ($env:CXX) { $env:CXX } else { "g++" }
$STD = "-std=c++20"
$WARN = @("-Wall", "-Wextra", "-Wpedantic")
$INCLUDES = "-Iinclude"

$ReleaseFlags = @($STD, "-O3", "-DNDEBUG") + $WARN + @($INCLUDES)
$TestFlags = @($STD, "-O2") + $WARN + @($INCLUDES, "-Itests")
$BenchDesc = "-std=c++20 -O3 -march=native -DNDEBUG -Wall -Wextra -Wpedantic"
$BenchFlags = @($STD, "-O3", "-march=native", "-DNDEBUG") + $WARN + @($INCLUDES)

New-Item -ItemType Directory -Force -Path "build\obj" | Out-Null
New-Item -ItemType Directory -Force -Path "bin" | Out-Null

Write-Host "== compiler =="
& $CXX --version | Select-Object -First 1
Write-Host ""

Write-Host "== building library objects (release) =="
& $CXX @ReleaseFlags -c src/OrderBook.cpp -o build/obj/OrderBook.o
& $CXX @ReleaseFlags -c src/OrderGenerator.cpp -o build/obj/OrderGenerator.o

Write-Host "== building demo executable (bin/orderbook_demo.exe) =="
& $CXX @ReleaseFlags src/main.cpp build/obj/OrderBook.o build/obj/OrderGenerator.o -o bin/orderbook_demo.exe

Write-Host "== building test executable (bin/orderbook_tests.exe, Catch2) =="
& $CXX @TestFlags tests/test_main.cpp tests/test_orderbook.cpp tests/test_generator.cpp src/OrderBook.cpp src/OrderGenerator.cpp -o bin/orderbook_tests.exe

Write-Host "== building benchmark executable (bin/orderbook_benchmark.exe) =="
Set-Content -Path "build/obj/BuildInfo.hpp" -Value "#pragma once`n#define OB_BUILD_FLAGS `"$BenchDesc`"`n" -Encoding ascii
& $CXX @BenchFlags -Ibuild/obj benchmarks/benchmark_main.cpp src/OrderBook.cpp src/OrderGenerator.cpp -o bin/orderbook_benchmark.exe

Write-Host ""
Write-Host "== build complete =="
Write-Host "  bin/orderbook_demo.exe"
Write-Host "  bin/orderbook_tests.exe"
Write-Host "  bin/orderbook_benchmark.exe"
