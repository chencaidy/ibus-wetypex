<p align="center">
  <img src="data/icons/hicolor/256x256/apps/wetypex.png" width="112" alt="WeTypeX 图标">
</p>

<h1 align="center">WeTypeX</h1>

<p align="center">
  在 Linux 上通过 IBus 使用微信输入法的兼容项目。
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Linux-x86__64-FCC624?style=flat-square&logo=linux&logoColor=black" alt="Linux x86-64">
  <img src="https://img.shields.io/badge/Input-IBus-27A7E7?style=flat-square" alt="IBus">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue?style=flat-square" alt="MIT 许可证"></a>
</p>

> [!IMPORTANT]
> WeTypeX 是一个非官方的第三方开源项目，仅供个人学习交流使用，请于下载后 24 小时内删除。
>
> 本项目并非由腾讯公司（Tencent）或其关联公司开发、维护或认可。本项目中提及的“微信输入法”及其相关商标、标识均为腾讯公司（Tencent）的注册商标或财产。本项目的存在不意味着与腾讯公司（Tencent）存在任何关联、合作关系，也不代表腾讯公司（Tencent）对本项目有任何形式的认可或背书。
>
> 本项目的开发者尊重他人的知识产权。本项目仅旨在实现技术上的兼容与互通，不包含、不复制、不传播任何微信输入法的专有代码、二进制文件或受版权保护的资源。用户在使用本项目的过程中，需自行获取并遵守微信输入法官方软件的使用条款与许可协议。任何单位或个人如认为本项目中包含的内容（包括但不限于代码、文档、配置信息等）可能涉嫌侵犯其合法权益（包括但不限于著作权、商标权、专利权等），请及时[联系项目维护者](mailto:chen@caidy.cc)。项目维护者在收到通知后，将在合理期限内移除、下架或断开被控侵权内容的访问链接。
>
> 您使用本项目资料的风险完全由您自行承担。开发者不保证本项目资料的功能不会中断或没有错误，不保证缺陷会被修正，也不保证本项目资料兼容所有系统环境。

> [!IMPORTANT]
> 可能包含微信输入法官方服务的遥测。

> [!NOTE]
> 本项目是 [panxuc/fcitx5-wetypex](https://github.com/panxuc/fcitx5-wetypex) 的 IBus 重构版本，目标是为使用GNOME桌面的用户提供更稳定的输入体验；目前项目处于早期开发阶段，许多功能尚未经过测试。

## 功能概览

| 能力 | Linux 集成状态 |
| --- | --- |
| 全拼、双拼、五笔 | 使用原版 macOS 2.2.3.657 输入核心和词典 |
| 智能拼写与云候选 | 原版本地候选与桌面云候选合并，云端新增候选带 ☁ 标记 |
| 微信表情 | 仅在可识别的微信客户端中推荐，候选显示用户安装包中的原图；普通 emoji 保留 |
| IBus 原生输入体验 | GNOME原生集成，标准系统样式 |
| 账户与设备 | 官方六位匹配码、确认配对、设备组和功能开关 |
| 跨设备剪贴板 | 文字与原始 PNG 图片双向同步 |
| 个人词库与常用语 | 本地持久化，支持远程同步 |

目前仅在 Ubuntu 24.04 和 26.04 上进行测试，各项能力的支持范围见[功能状态](docs/feature-status.md)。

## 工作方式

WeTypeX 安装程序从官方包中提取输入核心与词典，生成固定地址的 Linux 可加载映像；运行时由一个小型 Mach-O ABI 兼容宿主提供原版核心实际使用的 Darwin C/C++ 接口，并将网络传输接到 Linux 的 TLS 与 WebSocket 实现。

```text
应用程序
  │
  │ IBus 输入法框架
  ▼
WeTypeX IBus 引擎 ──────── 候选、预编辑、快捷键、剪贴板
  │
  │ 私有管道
  ▼
后端引擎沙箱 ────────────── 原版输入核心、词典与 Flurry/WXP2P 文件通道
  │                       │
  │ Linux ABI             └─ 用户词库 / 常用语
  ▼
原版业务服务 ────────────── 云候选、配对、同步
```

* 输入宿主和账户命令运行在 Bubblewrap 沙箱中。
* IBus 在检测到密码输入时会直接停止原版核心；浏览器标记为敏感的普通输入仍可使用输入法。
* 详细边界见[架构说明](docs/architecture.md)。

## 安装

### Ubuntu

从 Release 界面下载对应发行版的最新版本：

```bash
# Ubuntu 22.04
sudo apt install ./ibus-wetypex_2.2.3.657-1jammy1_amd64.deb
# Ubuntu 24.04
sudo apt install ./ibus-wetypex_2.2.3.657-1noble1_amd64.deb
# Ubuntu 26.04
sudo apt install ./ibus-wetypex_2.2.3.657-1resolute1_amd64.deb
```

### 从源码安装

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build --parallel
sudo cmake --install build
```

构建要求 CMake 3.22、Clang、C++17 编译器。完整依赖和打包流程见[发行与打包](docs/distribution.md)。

## 首次设置

### 准备官方运行时

准备官方运行时有两种方式，选择其中一种进行配置。

1. 使用已经下载的官方安装包：

    固定版本的官方下载地址：[WeType_2.2.3_657.zip](https://download.weread.qq.com/app/wxkb/mac/2.2.3/WeType_2.2.3_657.zip)

    ```bash
    wetypex-setup --archive /path/to/WeType_2.2.3_657.zip
    ```

2. 明确接受上游许可后，从脚本内固定的腾讯地址下载：

    ```bash
    wetypex-setup --download --accept-upstream-license
    ```

安装完成后检查提取结果：

```bash
wetypex-setup --check
```

* 确认 runtime_installed，ui_installed，core_ready 均为 true。

为了能让系统发现输入法和 GNOME 扩展，建议直接重启系统：

```bash
reboot
```

随后在【GNOME设置 - 键盘 - 输入源】中添加 **WeTypeX**。

跨设备剪贴板由随包安装的 GNOME Shell 扩展 `wetypex-clip-watcher` 读写本地剪贴板，需手动启用：

```bash
gnome-extensions enable wetypex-clip-watcher@caidy.cc
```

配置、原版运行时、配对身份、词库和同步状态都位于用户目录，与 `/usr` 下的软件文件分离；正常升级不会重建账户。

## 日常使用

常用操作：

* `Shift`：切换中英文。

常用命令：

```bash
wetypex-settings             # 打开完整设置窗口
wetypex-setup --check        # 检查原版运行时
wetypex-account group-info   # 查看设备组和同步状态
wetypex-account pairing-code # 生成新的六位匹配码
wetypex-account network-info # 检查实际 libcurl 与 WS/WSS 支持
wetypex-account sync-now     # 请求个人词库与常用语同步
```

## 数据目录

| 内容 | 默认位置 |
| --- | --- |
| 原版核心与词典 | `~/.local/share/wetypex/runtime` |
| 账户身份与原版业务状态 | `~/.local/share/wetypex/account` |
| 用户词库、常用语与同步状态 | `~/.local/share/wetypex/state` |
| 从官方包提取的界面资源 | `~/.local/share/wetypex/ui` |
| WeTypeX 配置 | `~/.config/fcitx5/wetypex.json` |

卸载系统软件包不会自动删除这些用户数据。需要重新配对时，可先备份后自行删除 `account` 与 `state/sync-state.json`；普通升级不应删除它们。

## 故障排查

首先运行：

```bash
wetypex-setup --check
```

常见问题：

* 看不到 WeTypeX：重新登录系统，触发IBus重新加载。
* 核心启动失败：检查系统 apparmor 是否阻止了 bwrap 访问 userns。
* 没有云候选：确认未开启单机模式，并检查设备身份是否已经建立。

更多诊断步骤见[故障排查](docs/troubleshooting.md)。

## 文档

* [架构说明](docs/architecture.md)
* [功能状态](docs/feature-status.md)
* [配置参考](docs/configuration.md)
* [Linux 微信表情原图与应用识别](docs/wechat-emoticons.md)
* [故障排查](docs/troubleshooting.md)
* [发行与打包](docs/distribution.md)

## 参与开发

提交问题时请附上桌面环境、显示协议、输入法框架及版本和去除隐私信息后的日志。界面变更应注明参考平台与版本。具体要求见[贡献指南](CONTRIBUTING.md)和[安全策略](SECURITY.md)。

## 许可与声明

WeTypeX 自有适配代码使用 [MIT License](LICENSE)。libkqueue 保留其上游许可。原版程序、词典、模型、界面资源、服务和商标归各自权利人所有，不受本项目许可证覆盖；详见 [NOTICE](NOTICE)。

WeTypeX 不提供绕过账户验证、伪造同步状态或替代官方服务的实现。上游协议、接口或服务随时可能变化。
