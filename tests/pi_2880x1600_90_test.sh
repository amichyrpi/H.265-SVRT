#!/bin/sh
set -eu

receiver=${1:-./build-pi/pi-receiver/svrt-receiver}
stream=${2:-/tmp/svrt-test-2880x1600-90.hevc}
port=${SVRT_TEST_PORT:-19944}
frames=${SVRT_TEST_FRAMES:-900}
log=/tmp/svrt-test-2880x1600-90.log
pid=

cleanup() {
    test -z "$pid" || kill "$pid" 2>/dev/null || true
    rm -f "$log"
}
trap cleanup EXIT INT TERM

test -s "$stream"
"$receiver" --headless "$port" >"$log" 2>&1 &
pid=$!
sleep 1
started=$(date +%s%N)
ffmpeg -hide_banner -loglevel error -r 90 -f hevc -i "$stream" -c:v copy \
  -f mpegts "tcp://127.0.0.1:${port}?tcp_nodelay=1"
complete=0
for attempt in $(seq 1 100); do
    if grep -q "$frames decoded, $frames shown, 0 dropped" "$log"; then
        complete=1
        break
    fi
    sleep 0.1
done
ended=$(date +%s%N)
kill -TERM "$pid"
wait "$pid"
pid=
elapsed_ns=$((ended - started))

if test "$complete" != 1; then
    cat "$log"
    exit 1
fi
grep -q 'Hwaccel V4L2 HEVC stateless' "$log"
grep -q "$frames decoded, $frames shown, 0 dropped" "$log"
awk -v frames="$frames" -v elapsed_ns="$elapsed_ns" 'BEGIN {
    fps = frames * 1000000000 / elapsed_ns
    printf "decoded %d frames in %.3f seconds: %.2f FPS\n", frames,
           elapsed_ns / 1000000000, fps
    if (fps < 90) exit 1
}'
grep -E 'decoder=|frame_id=|decoded,|Hwaccel' "$log"
