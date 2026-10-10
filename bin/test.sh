#!/bin/bash -e

# Build, then run the tests: the unit tests and, on the raw files of
# ./samples (see fetch-samples.sh) and of ~/user-samples when it holds any,
# the end-to-end tests.

cd "$(dirname "$(readlink -f "$0")")/.."

dirs="$(pwd)/samples"
if [ -d ~/user-samples ]; then
  dirs="$dirs:$HOME/user-samples"
fi

cmake -S . -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DDNGCONV_SAMPLE_DIRS="$dirs" "$@"
cmake --build build
ctest --test-dir build --output-on-failure
