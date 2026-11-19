FROM fedora:44

RUN dnf -y update && \
dnf -y install \
ca-certificates clang cmake gcc gcc-c++ git \
pkgconf-pkg-config boost-devel json-c-devel openssl-devel \
ibus-devel gtk4-devel libadwaita-devel libcxx-devel libcxxabi-devel \
make perl-core patch tar xz bzip2 curl && \
dnf clean all
