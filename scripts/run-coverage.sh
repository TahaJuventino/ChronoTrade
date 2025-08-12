#!/bin/bash
set -euo pipefail

if [ ! -d build ]; then
  echo "build directory not found" >&2
  exit 1
fi

cd build
# Fail if line coverage below 95%
gcovr -r .. --exclude='tests' --fail-under-line 95
