# 故障排查

## 输入法安装后没有出现在 IBus 列表中

先重启 IBus：

```bash
ibus restart
```

若仍未出现，请注销并重新登录，再确认已在桌面输入法设置中启用 WeTypeX。

## IBus 属性面板显示 X 或“核心不可用”

运行检查：

```bash
wetypex-setup --check
```

查看 `runtime_installed`、`ui_installed` 和 `core_ready`。若运行时文件缺失或核心验证失败，可用已取得的官方归档重新准备运行时：

```bash
wetypex-setup --archive /path/to/WeType_2.2.3_657.zip
```

不要绕过散列校验；散列不匹配可能表示归档版本或 ABI 不兼容。也可在 IBus 属性面板中选择“重启核心”后重试。

## 中文输入没有候选或无法上屏

确认 IBus 属性面板当前显示“中文”，并检查是否在密码或 PIN 输入框中；这类输入框会停用输入核心。然后运行 `wetypex-setup --check`。若 `core_ready` 为 `false`，按上节处理；若 `core_ready` 为 `true` 但问题仍在，先从输入法属性面板重启核心，再复现问题。

如 `wetypex-setup --check` 报告 `UTF-8 JSON compatibility check failed`，请升级到包含兼容修复的 WeTypeX 构建。此修复用于兼容 json-c 0.18 严格模式对中文 UTF-8 的解析问题；检查通过后无需替换系统 json-c。

## 本地候选正常，但没有云候选

运行 `wetypex-account network-info` 检查网络桥接能力。默认构建应显示 `provider: bundled-static`、`ws: true`、`wss: true` 和 `utf8_json: true`。该命令只检查本地运行库，不读取配对身份，也不会联网。

1. 在设置中确认“单机模式”已关闭。
2. 确认 `~/.local/share/wetypex/account/identity.json` 存在。若使用了自定义 `XDG_DATA_HOME`，请检查对应目录下的 `wetypex/account/identity.json`。
3. 运行 `wetypex-account group-info` 检查设备组状态。
4. 若账户请求失败，查看 `~/.local/share/wetypex/account/last-network.log`。分享日志前请移除账户标识等个人信息。

## 个人词库或常用语没有同步

运行 `wetypex-account group-info` 确认设备已关联；`func_switch` 的位 `4` 表示个人词库同步，位 `2` 表示常用语同步。在设置中开启对应功能，并关闭“单机模式”。WeTypeX 输入法须处于启用状态，然后可点击设置中的“立即请求同步”，或运行：

```bash
wetypex-account sync-now
```

命令返回 `requested: true` 表示输入核心已接受同步请求，不代表远端下载已完成。`~/.local/share/wetypex/state/sync-runtime.json` 中的 `remote_version`、`local_kind` 和 `local_digest` 是剪贴板同步状态，不是个人词库下载状态。排查时不要删除核心的用户数据目录。

## 跨设备剪贴板没有同步

确认关联设备组的 `func_switch` 包含剪贴板功能位 `1`，并确认 GNOME Shell 扩展 `wetypex-clip-watcher@caidy.cc` 已安装且启用：

```bash
gnome-extensions list --enabled
```

若扩展未启用，可执行：

```bash
gnome-extensions enable wetypex-clip-watcher@caidy.cc
```

GNOME Shell 扩展负责读取本地剪贴板并写入收到的内容；启用扩展后若仍无效，请注销并重新登录。不要同时手动启动多个 `wetypex-clipboard` 实例。

## 关联设备失败或需要重新关联

在 WeTypeX 设置的“跨设备”页选择“获取匹配码”。匹配码有效期为 5 分钟；在另一台设备的“微信输入法设置 → 跨设备粘贴同步 → 关联设备”中输入该码，并按提示完成确认。

若无法获取匹配码，先确认“单机模式”已关闭，再运行 `wetypex-account network-info` 和 `wetypex-account group-info` 检查本地运行库与账户状态。普通重试不需要删除 `account` 或 `state` 目录；若账户请求失败，检查 `~/.local/share/wetypex/account/last-network.log`，分享前移除其中的账户标识。
