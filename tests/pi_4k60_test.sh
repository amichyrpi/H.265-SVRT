#!/bin/sh
set -eu
receiver=${1:-./build-pi/pi-receiver/svrt-receiver}
port=${SVRT_TEST_PORT:-19944}
stream=/tmp/svrt-test-2880x1600.hevc
log=/tmp/svrt-test-receiver.log
pid=
cleanup(){ test -z "$pid" || kill "$pid" 2>/dev/null || true; rm -f "$stream" "$log"; }
trap cleanup EXIT INT TERM
ffmpeg -y -hide_banner -loglevel error -f lavfi -i testsrc2=size=2880x1600:rate=60 \
  -frames:v 120 -c:v libx265 -preset ultrafast -pix_fmt yuv420p \
  -x265-params 'pools=4:frame-threads=2:bframes=0:keyint=60:aud=1:log-level=error' "$stream"
"$receiver" --headless "$port" >"$log" 2>&1 & pid=$!
sleep 1
python3 "$root/scripts/stearlight-send.py" "$stream" 127.0.0.1 "$port" --fps 60
for attempt in $(seq 1 100); do grep -q '120 decoded, 120 shown, 0 dropped' "$log" && break; sleep 0.1; done
kill -TERM "$pid"; wait "$pid"; pid=
grep -q 'Hwaccel V4L2 HEVC stateless' "$log"
grep -q '120 decoded, 120 shown, 0 dropped' "$log"
cat "$log"
