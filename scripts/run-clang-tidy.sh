#!/bin/bash
set -euo pipefail

# Ensure build directory with compile commands exists
if [ ! -f build/compile_commands.json ]; then
  cmake -S . -B build >/dev/null 2>&1
fi

files=$(git ls-files '*.cpp')
clang-tidy -p build --warnings-as-errors='*' $files
