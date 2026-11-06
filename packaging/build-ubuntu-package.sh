#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
version=$(sed -n 's/^project([^ ]* VERSION \([^ ]*\).*/\1/p' "$project_root/CMakeLists.txt" | head -n1)
release=$(sed -n 's/^pkgrel=//p' "$project_root/packaging/PKGBUILD.in")
output="$project_root/dist"
docker_network_args=()
if [[ -n "${WETYPEX_DOCKER_NETWORK:-}" ]]; then
    docker_network_args=(--network "$WETYPEX_DOCKER_NETWORK")
fi

command -v docker >/dev/null 2>&1 || {
    printf 'Docker is required to build the IBus package.\n' >&2
    exit 1
}
command -v nfpm >/dev/null 2>&1 || {
    printf 'nFPM is required to package the IBus build.\n' >&2
    exit 1
}

# Build Ubuntu 22.04
echo "==> Build WeTypeX with Ubuntu 22.04"
mkdir -p "$output/ubuntu22"
stage=$(mktemp -d "$output/.stage-ubuntu.XXXXXX")
manifest=$(mktemp "$output/.nfpm-ubuntu22.XXXXXX.yaml")
trap 'rm -rf "$stage"; rm -f "$manifest"' EXIT

docker build "${docker_network_args[@]}" \
    -f "$project_root/packaging/docker/ubuntu22.Dockerfile" \
    -t wetypex-build-ubuntu22 "$project_root"
docker run --rm "${docker_network_args[@]}" --user "$(id -u):$(id -g)" \
    -e WETYPEX_BUILD_JOBS="${WETYPEX_BUILD_JOBS:-2}" \
    -v "$project_root:/src:ro" -v "$stage:/out" \
    wetypex-build-ubuntu22 bash /src/packaging/stage-ubuntu.sh /out/root

sed -e "s|@WETYPEX_VERSION@|$version|g" \
    -e "s|@WETYPEX_RELEASE@|${release}jammy1|g" \
    -e "s|@WETYPEX_PROJECT_ROOT@|$project_root|g" \
    -e "s|@WETYPEX_STAGE@|$stage/root|g" \
    "$project_root/packaging/nfpm-ubuntu.yaml" >"$manifest"
nfpm package --config "$manifest" --packager deb --target "$output/ubuntu22"

# Build Ubuntu 24.04
echo "==> Build WeTypeX with Ubuntu 24.04"
mkdir -p "$output/ubuntu24"
stage=$(mktemp -d "$output/.stage-ubuntu.XXXXXX")
manifest=$(mktemp "$output/.nfpm-ubuntu24.XXXXXX.yaml")
trap 'rm -rf "$stage"; rm -f "$manifest"' EXIT

docker build "${docker_network_args[@]}" \
    -f "$project_root/packaging/docker/ubuntu24.Dockerfile" \
    -t wetypex-build-ubuntu24 "$project_root"
docker run --rm "${docker_network_args[@]}" --user "$(id -u):$(id -g)" \
    -e WETYPEX_BUILD_JOBS="${WETYPEX_BUILD_JOBS:-2}" \
    -v "$project_root:/src:ro" -v "$stage:/out" \
    wetypex-build-ubuntu24 bash /src/packaging/stage-ubuntu.sh /out/root

sed -e "s|@WETYPEX_VERSION@|$version|g" \
    -e "s|@WETYPEX_RELEASE@|${release}noble1|g" \
    -e "s|@WETYPEX_PROJECT_ROOT@|$project_root|g" \
    -e "s|@WETYPEX_STAGE@|$stage/root|g" \
    "$project_root/packaging/nfpm-ubuntu.yaml" >"$manifest"
nfpm package --config "$manifest" --packager deb --target "$output/ubuntu24"

# Build Ubuntu 26.04
echo "==> Build WeTypeX with Ubuntu 26.04"
mkdir -p "$output/ubuntu26"
stage=$(mktemp -d "$output/.stage-ubuntu.XXXXXX")
manifest=$(mktemp "$output/.nfpm-ubuntu26.XXXXXX.yaml")
trap 'rm -rf "$stage"; rm -f "$manifest"' EXIT

docker build "${docker_network_args[@]}" \
    -f "$project_root/packaging/docker/ubuntu26.Dockerfile" \
    -t wetypex-build-ubuntu26 "$project_root"
docker run --rm "${docker_network_args[@]}" --user "$(id -u):$(id -g)" \
    -e WETYPEX_BUILD_JOBS="${WETYPEX_BUILD_JOBS:-2}" \
    -v "$project_root:/src:ro" -v "$stage:/out" \
    wetypex-build-ubuntu26 bash /src/packaging/stage-ubuntu.sh /out/root

sed -e "s|@WETYPEX_VERSION@|$version|g" \
    -e "s|@WETYPEX_RELEASE@|${release}resolute1|g" \
    -e "s|@WETYPEX_PROJECT_ROOT@|$project_root|g" \
    -e "s|@WETYPEX_STAGE@|$stage/root|g" \
    "$project_root/packaging/nfpm-ubuntu.yaml" >"$manifest"
nfpm package --config "$manifest" --packager deb --target "$output/ubuntu26"
