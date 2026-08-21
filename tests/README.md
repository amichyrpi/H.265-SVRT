# Tests

`ctest` runs the protocol/FEC, transport URL/bounds, and DRM geometry tests. Hardware behavior is tested separately because CI has no Pi HEVC request decoder or KMS display. On a Pi, `scripts/test-pi.sh` generates a 120-frame 2880x1600@60 HEVC stream, sends it through the real 1200-byte UDP 10+2 FEC transport, and requires 120 DRM PRIME frames with zero drops.

`pi_2880x1600_90_test.sh` is the decoder-limit test for the fixed stereo
transport. Give it a pre-generated 2880x1600@90 HEVC elementary stream; it
requires every frame to be decoded through V4L2 request/DRM PRIME with zero
drops and reports failure when measured throughput is below 90 FPS.
