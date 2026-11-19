FROM ubuntu:22.04

RUN apt-get update -qq && \
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
build-essential ca-certificates clang cmake git pkg-config \
libboost-dev libc++-dev libc++abi-dev libjson-c-dev libssl-dev \
libibus-1.0-dev libadwaita-1-dev libgtk-4-dev libwayland-dev && \
rm -rf /var/lib/apt/lists/*
