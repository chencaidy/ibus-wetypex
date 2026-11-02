#!/bin/sh
set -eu

command -v update-desktop-database >/dev/null 2>&1 && \
  update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && \
  gtk-update-icon-cache -q -t /usr/share/icons/hicolor >/dev/null 2>&1 || true

cat <<'EOF'

WeTypeX 已安装。首次使用请依次执行：

  1. 准备官方运行时（二选一）：
     wetypex-setup --download --accept-upstream-license
     wetypex-setup --archive /path/to/WeType_2.2.3_657.zip

  2. 检查输入核心：
     wetypex-setup --check

  3. 打开「设置 → 键盘」，将 WeTypeX 添加到当前输入法列表。

  4. 重启 IBus：
     ibus restart

完整说明：https://github.com/chencaidy/ibus-wetypex#首次设置
EOF
