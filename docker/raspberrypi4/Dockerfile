FROM debian:bookworm
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    bash \
    bc \
    bison \
    bzip2 \
    build-essential \
    ca-certificates \
    coreutils \
    cpio \
    diffutils \
    file \
    findutils \
    flex \
    gawk \
    git \
    grep \
    gzip \
    libncurses-dev \
    libssl-dev \
    patch \
    perl \
    python3 \
    qemu-user-static \
    rsync \
    sed \
    tar \
    unzip \
    wget \
    which \
    && update-ca-certificates \
    && git ls-remote https://github.com/buildroot/buildroot.git HEAD >/dev/null \
    && rm -rf /var/lib/apt/lists/*
RUN git clone --depth 1 --branch 2025.02.3 https://github.com/buildroot/buildroot.git /buildroot
ENV FORCE_UNSAFE_CONFIGURE=1
COPY buildroot /external
RUN make -C /buildroot O=/build BR2_EXTERNAL=/external raspberrypi4_64_defconfig && \
    /buildroot/utils/config --file /build/.config -d BR2_ROOTFS_DEVICE_CREATION_STATIC && \
    /buildroot/utils/config --file /build/.config -d BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_DEVTMPFS && \
    /buildroot/utils/config --file /build/.config -d BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_MDEV && \
    /buildroot/utils/config --file /build/.config -e BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_EUDEV && \
    /buildroot/utils/config --file /build/.config -e BR2_PACKAGE_EUDEV && \
    /buildroot/utils/config --file /build/.config -e BR2_INSTALL_LIBSTDCPP && \
    /buildroot/utils/config --file /build/.config -e BR2_PACKAGE_UTIL_LINUX && \
    /buildroot/utils/config --file /build/.config -e BR2_PACKAGE_UTIL_LINUX_LIBS && \
    /buildroot/utils/config --file /build/.config -e BR2_PACKAGE_ZLIB && \
    /buildroot/utils/config --file /build/.config -e BR2_PACKAGE_MESA3D && \
    /buildroot/utils/config --file /build/.config -e BR2_PACKAGE_MESA3D_GALLIUM_DRIVER_V3D && \
    /buildroot/utils/config --file /build/.config -e BR2_PACKAGE_MESA3D_OPENGL_EGL && \
    /buildroot/utils/config --file /build/.config -e BR2_PACKAGE_MESA3D_GBM && \
    /buildroot/utils/config --file /build/.config -e BR2_PACKAGE_MESA3D_OPENGL_ES && \
    /buildroot/utils/config --file /build/.config -e BR2_PACKAGE_SDL2 && \
    /buildroot/utils/config --file /build/.config -e BR2_PACKAGE_SDL2_OPENGLES && \
    /buildroot/utils/config --file /build/.config -e BR2_PACKAGE_SDL2_KMSDRM && \
    /buildroot/utils/config --file /build/.config -e BR2_PACKAGE_RPI_FFMPEG && \
    /buildroot/utils/config --file /build/.config -e BR2_PACKAGE_SVRT && \
    /buildroot/utils/config --file /build/.config --set-str BR2_PACKAGE_SVRT_LOCAL_PATH /svrt && \
    /buildroot/utils/config --file /build/.config --set-str BR2_PACKAGE_RPI_FIRMWARE_CONFIG_FILE /external/board/raspberrypi4/config.txt && \
    /buildroot/utils/config --file /build/.config --set-str BR2_PACKAGE_RPI_FIRMWARE_CMDLINE_FILE /external/board/raspberrypi4/cmdline.txt && \
    make -C /buildroot O=/build BR2_EXTERNAL=/external olddefconfig && \
    for option in \
        BR2_PACKAGE_MESA3D_GALLIUM_DRIVER_V3D \
        BR2_PACKAGE_MESA3D_OPENGL_EGL \
        BR2_PACKAGE_MESA3D_GBM \
        BR2_PACKAGE_MESA3D_OPENGL_ES \
        BR2_PACKAGE_SDL2 \
        BR2_PACKAGE_SDL2_OPENGLES \
        BR2_PACKAGE_SDL2_KMSDRM \
        BR2_PACKAGE_EUDEV \
        BR2_PACKAGE_ZLIB \
        BR2_PACKAGE_RPI_FFMPEG \
        BR2_PACKAGE_SVRT; do \
        grep -qx "${option}=y" /build/.config || { \
            echo "ERROR: Buildroot dropped required option ${option}" >&2; \
            exit 1; \
        }; \
    done && \
    make -C /buildroot O=/build BR2_EXTERNAL=/external show-info >/dev/null

# Match Vanilla's lifecycle: cache the cross-toolchain, kernel, graphics stack,
# FFmpeg and root filesystem in the Docker image.  The mounted local SVRT source
# is enabled again only after the cacheable base build has completed.
RUN /buildroot/utils/config --file /build/.config -d BR2_PACKAGE_SVRT && \
    make -C /buildroot O=/build BR2_EXTERNAL=/external olddefconfig && \
    make -C /buildroot O=/build BR2_EXTERNAL=/external && \
    /buildroot/utils/config --file /build/.config -e BR2_PACKAGE_SVRT && \
    /buildroot/utils/config --file /build/.config --set-str BR2_PACKAGE_SVRT_LOCAL_PATH /svrt && \
    make -C /buildroot O=/build BR2_EXTERNAL=/external olddefconfig && \
    grep -qx 'BR2_PACKAGE_SVRT=y' /build/.config
ENTRYPOINT ["/bin/sh", "/svrt/docker/raspberrypi4/build.sh"]
