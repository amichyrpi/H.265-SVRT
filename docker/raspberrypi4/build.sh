#!/bin/sh
set -eu
make -C /buildroot O=/build BR2_EXTERNAL=/external
gzip -c /build/images/sdcard.img > /install/svrt-raspberrypi4.img.gz
