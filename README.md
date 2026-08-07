[![Build](https://github.com/amichyrpi/H.265-SVRT/actions/workflows/build.yml/badge.svg)](https://github.com/amichyrpi/H.265-SVRT/actions/workflows/build.yml)

# H.265 SVRT

This is a **work in progress**. SteamVR driver and Raspberry Pi 4 receiver. This project is in development and is not stable, consider it as **alpha**.

## TODO

- [ ] Finish receiver and driver optimizations to reduce latency on the decoder and get a 60fps framerate
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

This project is licensed under the Apache License 2.0. See the [LICENSE](LICENSE) file for details.

This project uses parts of the [Vanilla](https://github.com/vanilla-wiiu/vanilla) code, to handle the DRM scanout and the KMS overlay plane. The Vanilla code is licensed under the GPL-2.0 License. See the [LICENSE](https://github.com/vanilla-wiiu/vanilla/blob/master/LICENSE) file for details.
