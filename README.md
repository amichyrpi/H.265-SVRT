<p align="center">
    <img src="/assets/stearlight.svg">
</p>

[![Build](https://github.com/amichyrpi/H.265-SVRT/actions/workflows/build.yml/badge.svg)](https://github.com/amichyrpi/H.265-SVRT/actions/workflows/build.yml)

# H.265 SVRT

This is a **work in progress**. SteamVR driver and Raspberry Pi 4 receiver. This project is in development and is not stable, consider it as **alpha**.

## TODO

- [ ] Create the SVRT Utility App
  - [ ] Framerate and latency measurements
  - [ ] Framerate changer with 30fps and 60fps presets
  - [ ] SteamVr driver installer with sync with latest version and automatic updates
  - [ ] Usage time measurements
  - [ ] Longest session time measurements
  - [ ] Start steamvr button that load the driver in steamvr otherwise it will not be loaded
  - [ ] Tracking support to pair a vive tracker to the headset
  - [ ] SVRT Firmware updater

## Usage/Installing

Builds and driver are available on the [Releases](https://github.com/amichyrpi/H.265-SVRT/releases) page.

### Compiling on Raspberry Pi

Before compilling H.265 SVRT you need to be running a 64-bit Raspberry Pi OS image with KMS enabled and FFmpeg. If you don't have a 64-bit Raspberry Pi OS image, you can install it by using [Raspberry Pi Imager](https://www.raspberrypi.com/software/). You can enable KMS and install FFmpeg using the following commands:

- **KMS**
  ```sh
  config=/boot/firmware/config.txt
  test -f "$config" || config=/boot/config.txt
  grep -qxF 'dtoverlay=vc4-kms-v3d' "$config" || \
    echo 'dtoverlay=vc4-kms-v3d' | sudo tee -a "$config"
  sudo reboot
  ```

- **FFmpeg**
  ```sh
  sudo apt update
  sudo apt install -y ffmpeg
  ffmpeg -hide_banner -decoders 2>&1 | grep hevc_v4l2request
  ```

- **Disable Wi-Fi power saving**

  Wi-Fi power saving can introduce periodic latency spikes, bitrate stalls,
  and dropped stream frames. Disable it persistently for the active `wlan0`
  NetworkManager connection, then reboot to apply the radio setting:

  ```sh
  wifi_connection="$(nmcli -g GENERAL.CONNECTION device show wlan0)"
  sudo nmcli connection modify "$wifi_connection" 802-11-wireless.powersave 2
  sudo reboot
  ```

  After reconnecting, verify that the profile reports the raw value `2`:

  ```sh
  wifi_connection="$(nmcli -g GENERAL.CONNECTION device show wlan0)"
  nmcli -g 802-11-wireless.powersave connection show "$wifi_connection"
  ```

- **Lock the Pi 4 core clock for native KMS output**

  Native dual-stream reconstruction uses several scaled KMS/HVS planes. Lock
  the core clock at its normal 500 MHz performance value to avoid a display
  clock transition while video is being scanned out:

  ```sh
  config=/boot/firmware/config.txt
  test -f "$config" || config=/boot/config.txt
  grep -qxF 'force_turbo=1' "$config" || echo 'force_turbo=1' | sudo tee -a "$config"
  grep -qxF 'core_freq_min=500' "$config" || echo 'core_freq_min=500' | sudo tee -a "$config"
  sudo reboot
  ```

  This increases idle power use and heat. Verify adequate cooling and power
  after reboot:

  ```sh
  vcgencmd measure_clock core
  vcgencmd measure_temp
  vcgencmd get_throttled
  ```

H.265 SVRT requires the following libraries to be installed:

- **Before dependencies installation**
  ```sh
  sudo apt update
  sudo apt full-upgrade -y
  ```

- **Dependencies installation**
  ```sh
  sudo apt install -y build-essential cmake git ninja-build pkg-config \
    libsdl2-dev libavformat-dev libavcodec-dev libavutil-dev libdrm-dev \
    libegl1-mesa-dev libgles2-mesa-dev libgbm-dev libudev-dev libasound2-dev
  ```

The build process is otherwise normal for a CMake program:

```sh
git clone https://github.com/amichyrpi/H.265-SVRT.git
cd H.265-SVRT
mkdir build && cd build
cmake .. -G Ninja -DSVRT_BUILD_DRIVER=OFF \
  -DSVRT_BUILD_VENDORED_SDL=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
ctest --output-on-failure
```

Optionally, to install the program:

```sh
sudo useradd --system --no-create-home --shell /usr/sbin/nologin svrt-receiver
sudo usermod -aG video,render svrt-receiver
sudo cmake --install .
sudo systemctl daemon-reload
sudo systemctl enable --now svrt-receiver.service
```

The service runs as the `svrt-receiver` user with membership in the `video` and `render` groups, which grants access to the DRM devices for `SDL_VIDEODRIVER=kmsdrm` without root.

### SteamVR driver setup

You can easily install the driver by using the SVRT Utility App on both Windows and Linux, you can find the app in the [Releases](https://github.com/amichyrpi/H.265-SVRT/releases) page.

You can also build and install the driver manually by using the following commands:

- **Windows**
  ```powershell
  git clone https://github.com/amichyrpi/H.265-SVRT.git
  Set-Location H.265-SVRT
  cmake -S . -B build -A x64 `
    -DSVRT_BUILD_PI_LIBRARY=OFF `
    -DSVRT_BUILD_RECEIVER=OFF `
    -DSVRT_BUILD_TESTS=OFF
  cmake --build build --config Release --parallel
  $vrpathreg = Join-Path ${env:ProgramFiles(x86)} `
    'Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe'
  & $vrpathreg adddriver (Resolve-Path 'build\svrt')
  ```

### Starting order

The Raspberry Pi receiver is started at Raspberry Pi boot, and the SteamVR driver is started at SteamVR boot. The driver automatically detects when the Raspberry Pi receiver becomes available.

### Stereo resolutions and native 4K transport

The resolution setting is per eye:

- 1080 mode renders `1080x1080` per eye and sends one `2160x1080` HEVC stream.
- 4K/native mode renders `2160x2160` per eye (`4320x2160` total). Because the Pi 4 HEVC block is limited to standard `3840x2160`, the driver sends a `3840x2160` HEVC main stream on the configured video port and packs the remaining pixels into a `960x1080` H.264 stream on `video port + 3` (port `9947` by default).

SteamVR may run the headset compositor at 60 Hz, but native dual-decoder transport is paced at 45 FPS. This stays below the measured long-run rate of the Pi 4 HEVC+H.264 path; pacing and bounded socket buffers prevent decoder queues from turning overload into delayed or frozen video. Each transmitted frame is still selected from the newest SteamVR compositor frame. The 1080 mode continues to stream at the selected 30 or 60 FPS.

Both native-mode streams are produced from the same source frame. The receiver matches their MPEG timestamps as a shared frame ID, decodes HEVC with `rpivid` and H.264 with `bcm2835-codec`, and reconstructs the two square eyes with DRM/KMS planes. H.264 companion frames are detached from the limited V4L2 capture-buffer pool before timestamp matching, so decoder surfaces are never retained by the reconstruction queue. A frame is not presented if its matching companion is unavailable, preventing a one-frame seam between the main image and reconstructed pixels. Video uses low-latency TCP with a short write timeout, so a wedged decoder connection is restarted instead of accumulating seconds of stale frames. Ensure TCP ports `9944` and `9947` are reachable when using the defaults.

To start the installed Raspberry Pi receiver manually:

```sh
sudo systemctl start svrt-receiver.service
```

To run it directly without the systemd service:

```sh
sudo systemctl stop svrt-receiver.service
cd ~/H.265-SVRT/build
sudo env SDL_VIDEODRIVER=kmsdrm ./pi-receiver/svrt-receiver 9944
```

To run at boot without a connected HDMI display:

```sh
sudo systemctl edit svrt-receiver.service
```

Enter the following override, save it, and restart the service:

```ini
[Service]
ExecStart=
ExecStart=/usr/local/bin/svrt-receiver --headless 9944
```

```sh
sudo systemctl daemon-reload
sudo systemctl restart svrt-receiver.service
```

## Testing stream latency

Stop SteamVR before running the latency tester so it can use the Raspberry Pi video connection.

- **Windows**
  ```powershell
  py -m pip install av
  py scripts\stream-latency-test.py ROOT.local --frames 30
  ```

- **Linux**
  ```sh
  python3 -m pip install --user av
  python3 scripts/stream-latency-test.py ROOT.local --frames 30
  ```

## License

This project is licensed under the Functional Source License, Version 1.1,
with Apache License 2.0 as the Future License (`FSL-1.1-ALv2`). See the
[LICENSE](LICENSE) file for details.

This project uses parts of the [Vanilla](https://github.com/vanilla-wiiu/vanilla) code, to handle the DRM scanout and the KMS overlay plane. The Vanilla code is licensed under the GPL-2.0 License. See the [LICENSE](https://github.com/vanilla-wiiu/vanilla/blob/master/LICENSE) file for details.
