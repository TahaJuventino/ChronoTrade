#!/bin/bash
set -euo pipefail

if [ ! -f build/compile_commands.json ]; then
  cmake -S . -B build >/dev/null 2>&1
fi

cppcheck --std=c++20 --enable=warning,style,performance,portability --error-exitcode=1 --project=build/compile_commands.json
