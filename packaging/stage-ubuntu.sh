#!/usr/bin/env bash
set -euo pipefail

destination=${1:?usage: stage-ubuntu.sh DESTDIR}

cmake -S /src -B /tmp/wetypex-build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DWETYPE_QT_VERSION=6 \
    -DWETYPE_LAYER_SHELL=OFF
cmake --build /tmp/wetypex-build --parallel "${WETYPEX_BUILD_JOBS:-2}"
mkdir -p "$destination"
DESTDIR="$destination" cmake --install /tmp/wetypex-build
