#!/bin/sh
set -eu
src_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir="${src_dir}/build-pi"
cmake -S "$src_dir" -B "$build_dir" -DSVRT_BUILD_DRIVER=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" -j4
echo "Built ${build_dir}/pi-receiver/svrt-receiver"
