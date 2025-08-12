#!/bin/bash
set -euo pipefail
BUILD_DIR=build-fuzz
cmake -S . -B ${BUILD_DIR} -DBUILD_FUZZERS=ON -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang
cmake --build ${BUILD_DIR} --target fuzz_csv_parser
${BUILD_DIR}/fuzz/fuzz_csv_parser -runs=1000
