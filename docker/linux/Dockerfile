FROM debian:bookworm
RUN apt-get update && apt-get install -y build-essential cmake ninja-build pkg-config libsdl2-dev libavformat-dev libavcodec-dev libavutil-dev libdrm-dev && rm -rf /var/lib/apt/lists/*
ENTRYPOINT ["/bin/sh", "/svrt/docker/linux/build.sh"]
