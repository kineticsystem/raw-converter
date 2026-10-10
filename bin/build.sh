#!/bin/bash -e

# Configure and build the library, the dng-convert tool and the tests, in
# ./build. Extra arguments go to CMake, e.g. `build.sh -DCMAKE_BUILD_TYPE=Debug`.

# Act on the workspace root, so that the script can be called from anywhere,
# e.g. via the build alias in the container.
cd "$(dirname "$(readlink -f "$0")")/.."

cmake -S . -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "$@"
cmake --build build
