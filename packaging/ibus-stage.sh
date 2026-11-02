#!/usr/bin/env bash
set -euo pipefail

destination=${1:?usage: ibus-stage.sh DESTDIR}

cmake -S /src -B /tmp/wetypex-ibus-build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_INSTALL_BINDIR=lib/ibus-wetypex/bin \
    -DCMAKE_INSTALL_LIBDIR=lib/ibus-wetypex \
    -DCMAKE_INSTALL_LIBEXECDIR=lib/ibus-wetypex/libexec \
    -DCMAKE_INSTALL_DATADIR=lib/ibus-wetypex/share \
    -DWETYPE_BUILD_FCITX5=OFF \
    -DWETYPE_BUILD_IBUS=ON \
    -DWETYPE_QT_VERSION=6 \
    -DWETYPE_LAYER_SHELL=OFF
cmake --build /tmp/wetypex-ibus-build --parallel "${WETYPEX_BUILD_JOBS:-2}"
mkdir -p "$destination"
DESTDIR="$destination" cmake --install /tmp/wetypex-ibus-build
