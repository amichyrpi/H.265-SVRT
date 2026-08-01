# H.265 SVRT

This is a **work in progress**. SteamVR driver and Raspberry Pi 4 receiver. This project is in development and is not stable, consider it as **alpha**.



H.265 SVRT is a SteamVR direct-mode virtual headset and a Raspberry Pi 4 receiver. SteamVR renders both eyes into D3D11 swap textures owned by the driver. A non-blocking worker sends the stereo frame through FFmpeg's low-latency hardware HEVC encoder as MPEG-TS over TCP. The Pi decodes with its HEVC block and presents DRM PRIME buffers directly on a KMS overlay plane.

## Layout

- `steamvr-driver/`: Windows OpenVR driver and D3D11 direct-mode component.
- `lib/`: reusable C library for HEVC ingest, hardware decode, SDL KMSDRM lifecycle and zero-copy DRM scanout.
- `pi-receiver/`: small terminal/fullscreen receiver built on `libsvrt`.
- `pipe/`: transport description.
- `third_party/openvr/`: pinned OpenVR SDK.
- `vanilla-master/`: original reference tree. The adapted KMS/DRM lifecycle came from `gui/ui/ui_sdl_drm.c`.

## Raspberry Pi 4

Use a 64-bit Raspberry Pi OS image with KMS enabled (`dtoverlay=vc4-kms-v3d`) and Raspberry Pi's downstream FFmpeg built with `--enable-v4l2-request --enable-sand --enable-libdrm`. The receiver deliberately fails when the HEVC decoder does not return DRM PRIME frames; silently falling back to software would not meet the real-time requirement.

```sh
sudo apt update
sudo apt install -y build-essential cmake pkg-config libsdl2-dev \
  libavformat-dev libavcodec-dev libavutil-dev libdrm-dev
cmake -S . -B build -DSVRT_BUILD_DRIVER=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
sudo cmake --install build
sudo usermod -aG video,render "$USER"
```

Log out/in after changing groups, switch away from a graphical desktop to a VT, then run:

```sh
./build/pi-receiver/svrt-receiver 9944
```

If the distribution SDL package reports `kmsdrm not available`, configure with `-DSVRT_BUILD_VENDORED_SDL=ON`; this builds the same SDL KMSDRM backend lifecycle used by Vanilla.

Validate the decoder before SteamVR:

```sh
ffmpeg -hide_banner -buildconf | grep -E 'v4l2-request|sand|libdrm'
modetest -M vc4
```

Pi 4's HEVC block is intended for up to 4K60, but actual 4K60 operation still depends on the firmware/kernel FFmpeg request implementation, compatible stream level/profile, HDMI mode, cooling, and Wi-Fi throughput. Runtime testing on the target is mandatory.

Run the complete Pi test with `./scripts/test-pi.sh`. KMS scanout requires a connected HDMI display (or a deliberately configured forced HDMI mode); without an active connector SDL correctly refuses to create a KMSDRM window. Headless mode is only for decoder/transport validation.

## Windows driver

Requirements: Visual Studio 2022 C++ workload, CMake, SteamVR, and an FFmpeg build whose executable is either on `PATH` or configured in `default.vrsettings`. The default encoder is NVIDIA `hevc_nvenc`; use `hevc_amf` or `hevc_qsv` when appropriate and adjust encoder options in `direct_mode.cpp` if that encoder does not accept NVIDIA's preset/tune names.

```powershell
cmake -S . -B build -A x64 -DSVRT_BUILD_PI_LIBRARY=OFF -DSVRT_BUILD_RECEIVER=OFF
cmake --build build --config Release
& "$env:ProgramFiles(x86)\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe" `
  adddriver "$PWD\build\svrt"
```

Configure `steamvr-driver/svrt/resources/settings/default.vrsettings` before building:

- `receiver_host`: Pi hostname or IPv4 address.
- `receiver_port`: MPEG-TS TCP port.
- `render_width` and `render_height`: resolution per eye.
- `display_frequency`: requested frame rate.
- `bitrate_mbps`: HEVC target bitrate.
- `ffmpeg_path` and `encoder`: sender executable and hardware encoder.

The current sender uses a three-slot D3D11 staging ring, so compositor submission is bounded and frames are dropped if encoding falls behind. It is functional across vendors through FFmpeg, but it performs a GPU-to-CPU readback. A vendor-specific D3D11/NVENC or D3D11/Media Foundation surface path is required to guarantee high-resolution 60 Hz encoding without that readback.

## Stream start order

1. Start `svrt-receiver` on the Pi from a local terminal.
2. Start SteamVR on Windows.
3. SteamVR loads `driver_svrt.dll`, creates direct-mode textures, and launches FFmpeg when the first frame arrives.
4. Stop SteamVR before stopping the receiver.
