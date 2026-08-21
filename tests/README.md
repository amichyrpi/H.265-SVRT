# Tests

`ctest` runs the transport URL/bounds tests and DRM aspect-ratio geometry tests on every Linux GitHub build. Hardware behavior is tested separately because CI has no Pi HEVC request decoder or KMS display. On a Pi, `scripts/test-pi.sh` checks kernel devices and FFmpeg build flags, builds the project, runs the unit tests, generates a 120-frame 3840x2160@60 HEVC stream, sends it through the real MPEG-TS pipe, and requires 120 DRM PRIME frames from the V4L2 stateless decoder with zero drops.

`pi_2880x1600_90_test.sh` is the decoder-limit test for the fixed stereo
transport. Give it a pre-generated 2880x1600@90 HEVC elementary stream; it
requires every frame to be decoded through V4L2 request/DRM PRIME with zero
drops and reports failure when measured throughput is below 90 FPS.
