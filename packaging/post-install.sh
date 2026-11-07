#!/bin/sh
set -eu

if [ -r /etc/os-release ]; then
  . /etc/os-release
fi

# Import apparmor policy
ubuntu_version=${VERSION_ID:-0}
ubuntu_major=${ubuntu_version%%.*}

if [ "${ID:-}" = ubuntu ] && [ "$ubuntu_major" -gt 23 ] && [ ! -f /etc/apparmor.d/bwrap-userns-restrict ]; then
mkdir -p /etc/apparmor.d
cat <<'EOF' > /etc/apparmor.d/bwrap-userns-restrict
# This profile allows almost everything and only exists to allow
# bwrap to work on a system with user namespace restrictions
# being enforced.
# bwrap is allowed access to user namespaces and capabilities
# within the user namespace, but its children do not have
# capabilities, blocking bwrap from being able to be used to
# arbitrarily by-pass the user namespace restrictions.
#
# Note: the bwrap child is stacked against the bwrap profile due to
# bwraps use of no-new-privs

# disabled by default as it can break some use cases on a system that
# doesn't have or has disable user namespace restrictions for unconfined
# use aa-enforce to enable it

abi <abi/4.0>,

include <tunables/global>

profile bwrap /usr/bin/bwrap flags=(attach_disconnected) {
  allow capability,
  # not allow all, to allow for pix stack
  # sadly we have to allow  m every where to allow children to work under
  # stacking.
  allow file rwlkm /{**,},
  allow network,
  allow unix,
  allow ptrace,
  allow signal,
  allow mqueue,
  allow io_uring,
  allow userns,
  allow mount,
  allow umount,
  allow pivot_root,
  allow dbus,
  allow px /** -> bwrap//&unpriv_bwrap,

  # the local include should not be used without understanding the userns
  # restriction.
  # Site-specific additions and overrides. See local/README for details.
  include if exists <local/bwrap-userns-restrict>
}

profile unpriv_bwrap flags=(attach_disconnected) {
  # not allow all, to allow for pix stack
  allow file rwlkm /{**,},
  allow network,
  allow unix,
  allow ptrace,
  allow signal,
  allow mqueue,
  allow io_uring,
  allow userns,
  allow mount,
  allow umount,
  allow pivot_root,
  allow dbus,

  allow pix /** -> &unpriv_bwrap,

  audit deny capability,

  # the local include should not be used without understanding the userns
  # restriction.
  # Site-specific additions and overrides. See local/README for details.
  include if exists <local/unpriv_bwrap>
}
EOF
apparmor_parser -r /etc/apparmor.d/bwrap-userns-restrict
fi

# Show usage
cat <<'EOF'
WeTypeX已安装, 请完成下述配置:

  1. 准备官方运行时:
     wetypex-setup --download --accept-upstream-license
     wetypex-setup --check

  2. 登出或重启系统, 使系统重新发现输入法和扩展.

  3. 打开[设置-键盘], 将WeTypeX添加到输入法列表.

  4. 启用跨设备剪贴板扩展:
     gnome-extensions enable wetypex-clip-watcher@caidy.cc
   
完整说明: https://github.com/chencaidy/ibus-wetypex
EOF
