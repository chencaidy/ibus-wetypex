FROM ubuntu:26.04

RUN apt-get update -qq && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
      build-essential cmake clang git pkg-config libboost-dev \
      libibus-1.0-dev libjson-c-dev libssl-dev libwayland-dev \
      ca-certificates libc++-dev libc++abi-dev \
      qt6-base-dev qt6-svg-dev qt6-webengine-dev && \
    rm -rf /var/lib/apt/lists/*
