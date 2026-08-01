RPI_FFMPEG_VERSION = 7babe664191350e932a75a623a5681084a8a5ece
RPI_FFMPEG_SITE = $(call github,jc-kynesim,rpi-ffmpeg,$(RPI_FFMPEG_VERSION))
RPI_FFMPEG_LICENSE = LGPL-2.1+
RPI_FFMPEG_LICENSE_FILES = COPYING.LGPLv2.1
RPI_FFMPEG_INSTALL_STAGING = YES
RPI_FFMPEG_DEPENDENCIES = host-pkgconf libdrm zlib udev
define RPI_FFMPEG_CONFIGURE_CMDS
	(cd $(@D) && $(TARGET_CONFIGURE_OPTS) ./configure \
		--prefix=/usr --enable-cross-compile --cross-prefix=$(TARGET_CROSS) \
		--sysroot=$(STAGING_DIR) --arch=$(BR2_ARCH) --target-os=linux \
		--pkg-config=$(PKG_CONFIG_HOST_BINARY) --enable-shared --disable-static \
		--disable-programs --disable-doc --disable-debug --disable-everything \
		--enable-avutil --enable-avcodec --enable-avformat --enable-network \
		--enable-libdrm --enable-libudev --enable-zlib --enable-pthreads \
		--enable-v4l2-request --enable-sand --enable-decoder=hevc \
		--enable-demuxer=mpegts --enable-parser=hevc \
		--enable-protocol=tcp --enable-protocol=file)
endef
define RPI_FFMPEG_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) -j$(PARALLEL_JOBS)
endef
define RPI_FFMPEG_INSTALL_STAGING_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) DESTDIR=$(STAGING_DIR) install
endef
define RPI_FFMPEG_INSTALL_TARGET_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) DESTDIR=$(TARGET_DIR) install
endef
$(eval $(generic-package))
