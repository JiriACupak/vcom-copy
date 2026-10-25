FROM docker.io/debian:12-slim

RUN apt-get update && apt-get install -y \
    cargo \
    git \
    libsocketcan-dev \
    meson \
    ninja-build \
    pkg-config \
    && \
    apt-get clean
