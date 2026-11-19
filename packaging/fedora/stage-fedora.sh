#!/usr/bin/env bash
set -euo pipefail

destination=${1:?usage: stage-fedora.sh DESTDIR}

cmake -S /src -B /tmp/wetypex-build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_INSTALL_LIBDIR=lib
cmake --build /tmp/wetypex-build --parallel "${WETYPEX_BUILD_JOBS:-2}"
mkdir -p "$destination"
DESTDIR="$destination" cmake --install /tmp/wetypex-build
