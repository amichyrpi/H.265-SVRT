[![Build](https://github.com/amichyrpi/H.265-SVRT/actions/workflows/build.yml/badge.svg)](https://github.com/amichyrpi/H.265-SVRT/actions/workflows/build.yml)

# H.265 SVRT

This is a **work in progress**. SteamVR driver and Raspberry Pi 4 receiver. This project is in development and is not stable, consider it as **alpha**.

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
    libsdl2-dev libavformat-dev libavcodec-dev libavutil-dev libdrm-dev
  ```

The build process is otherwise normal for a CMake program:

```sh
git clone https://github.com/amichyrpi/H.265-SVRT.git
cd H.265-SVRT
mkdir build && cd build
cmake .. -G Ninja -DSVRT_BUILD_DRIVER=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
ctest --output-on-failure
```

Optionally, to install the program:

```sh
sudo useradd --system --no-create-home --shell /usr/sbin/nologin svrt-receiver
sudo usermod -aG video svrt-receiver
sudo cmake --install .
sudo systemctl daemon-reload
sudo systemctl enable --now svrt-receiver.service
```

The service runs as the `svrt-receiver` user with membership in the `video` group, which grants access to `/dev/dri/card0` for `SDL_VIDEODRIVER=kmsdrm` without root.

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

- **Linux**

  The SteamVR driver currently requires Windows and D3D11. A native Linux driver is not available yet.

### Starting order

The Raspberry Pi receiver is started at Raspberry Pi boot, and the SteamVR driver is started at SteamVR boot. The driver automatically detects when the Raspberry Pi receiver becomes available.

To start the installed Raspberry Pi receiver manually:

```sh
sudo systemctl start svrt-receiver.service
```

To run it directly without the systemd service:

```sh
sudo systemctl stop svrt-receiver.service
SDL_VIDEODRIVER=kmsdrm svrt-receiver 9944
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
