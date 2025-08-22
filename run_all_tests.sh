#!/bin/bash
# Run all ChronoTrade test executables after build
set -e

BUILD_DIR="$(dirname "$0")/build"

if [[ ! -d "$BUILD_DIR" ]]; then
    echo "❌ Build directory not found. Run cmake .. && ninja in ./build first."
    exit 1
fi

cd "$BUILD_DIR"

for t in ./test_*; do
    if [[ -x "$t" ]]; then
        echo "===== Running $t ====="
        "$t"
        echo
    fi
done