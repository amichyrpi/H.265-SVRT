# Tests

`ctest` runs the transport URL/bounds tests and DRM aspect-ratio geometry tests on every Linux GitHub build. Hardware behavior is tested separately because CI has no Pi HEVC request decoder or KMS display. On a Pi, `scripts/test-pi.sh` checks kernel devices and FFmpeg build flags, builds the project, runs the unit tests, generates a 120-frame 3840x2160@60 HEVC stream, sends it through the real MPEG-TS pipe, and requires 120 DRM PRIME frames from the V4L2 stateless decoder with zero drops.
