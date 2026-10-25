#!/usr/bin/env bash
# Build and run the standalone unit tests for the pure protocol layers.
set -euo pipefail

cd "$(dirname "$0")"

CXX="${CXX:-c++}"
CXXFLAGS="-std=c++17 -Wall -Wextra -Werror -O2 -I../../esphome/components/ebus -I../../esphome/components/vaillant_vrc720 -I../../esphome/components/vaillant_energy"

mkdir -p build

echo "== building test_ebus_frame =="
$CXX $CXXFLAGS test_ebus_frame.cpp ../../esphome/components/ebus/ebus_frame.cpp -o build/test_ebus_frame

echo "== building test_vaillant_b524 =="
$CXX $CXXFLAGS test_vaillant_b524.cpp ../../esphome/components/vaillant_vrc720/vaillant_b524.cpp -o build/test_vaillant_b524

echo "== building test_vaillant_b516 =="
$CXX $CXXFLAGS test_vaillant_b516.cpp ../../esphome/components/vaillant_energy/vaillant_b516.cpp -o build/test_vaillant_b516

echo "== running test_ebus_frame =="
./build/test_ebus_frame

echo "== running test_vaillant_b524 =="
./build/test_vaillant_b524

echo "== running test_vaillant_b516 =="
./build/test_vaillant_b516

echo "All unit tests passed."
