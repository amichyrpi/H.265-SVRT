FROM debian:bookworm
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    bc \
    bison \
    build-essential \
    ca-certificates \
    cpio \
    file \
    flex \
    git \
    libncurses-dev \
    libssl-dev \
    python3 \
    qemu-user-static \
    rsync \
    unzip \
    wget \
    && update-ca-certificates \
    && git ls-remote https://github.com/buildroot/buildroot.git HEAD >/dev/null \
    && rm -rf /var/lib/apt/lists/*
RUN git clone --depth 1 --branch 2025.02.3 https://github.com/buildroot/buildroot.git /buildroot
COPY buildroot /external
RUN make -C /buildroot O=/build BR2_EXTERNAL=/external raspberrypi4_64_defconfig && \
    /buildroot/utils/config --file /build/.config -e BR2_PACKAGE_SVRT && \
    /buildroot/utils/config --file /build/.config --set-str BR2_PACKAGE_SVRT_LOCAL_PATH /svrt && \
    /buildroot/utils/config --file /build/.config --set-str BR2_PACKAGE_RPI_FIRMWARE_CONFIG_FILE /external/board/raspberrypi4/config.txt && \
    /buildroot/utils/config --file /build/.config --set-str BR2_PACKAGE_RPI_FIRMWARE_CMDLINE_FILE /external/board/raspberrypi4/cmdline.txt && \
    make -C /buildroot O=/build BR2_EXTERNAL=/external olddefconfig
ENTRYPOINT ["/bin/sh", "/svrt/docker/raspberrypi4/build.sh"]
