#!/bin/sh
set -eu
cmake -S /svrt -B /build -G Ninja -DSVRT_BUILD_DRIVER=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build /build
ctest --test-dir /build --output-on-failure
