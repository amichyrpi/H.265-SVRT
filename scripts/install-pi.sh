#!/bin/sh
set -eu
src_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir="${src_dir}/build-pi"
if ! dpkg-query -W -f='${Status}' libasound2-dev 2>/dev/null | grep -q 'install ok installed'; then
  echo "Install the audio development package first: sudo apt install -y libasound2-dev" >&2
  exit 1
fi
cmake -S "$src_dir" -B "$build_dir" -DSVRT_BUILD_DRIVER=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" -j4
echo "Built ${build_dir}/pi-receiver/svrt-receiver"
