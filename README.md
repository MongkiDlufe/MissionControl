# MissionControl — GameSir G7 Pro fork (HOS 17.0.1 / AMS 1.6.2)

[中文](#中文) · [English](#english) · [G7 Pro protocol](docs/G7Pro-Bluetooth.md) · [Upstream README](README.upstream.md)

This is a **GPL-2.0** fork of [ndeadly/MissionControl](https://github.com/ndeadly/MissionControl).  
Base: **v0.10.0** (`38c9879`). Target: **Nintendo Switch 17.0.1 + Atmosphère 1.6.2**.  
Not affiliated with ndeadly, Nintendo, or GameSir.

**Do not** install this build on Atmosphère ≥ 1.9 / HOS 18+ expecting official 0.11–0.16 behaviour. Latest upstream (0.16.0) needs **AMS ≥ 1.12 / HOS 23**.

---

## 中文

### 这是什么

在大气层里用 `mc_mitm` 把非官方蓝牙手柄翻译成 Pro 手柄。本 fork 在 v0.10.0 上补齐 **小鸡 G7 Pro（Xbox 授权 / ZZZ 联名）** 的识别与 HID 映射，使 17.0.1 + AMS 1.6.2 可以配对使用。

官方 0.16.0 虽有 G7 Pro 条目，但 **无法** 在 AMS 1.6.2 上运行；本仓库也不把 0.16.0 硬编到旧大气层。

### 安装

1. 从本仓库 [Releases](https://github.com/MongkiDlufe/MissionControl/releases) 下载 zip（或自行 `make dist`）。
2. 解压到 SD 卡根目录，覆盖 `atmosphere/contents/010000000000bd00` 与 `atmosphere/exefs_patches/`。
3. 重启。

卸载：删除上述目录后重启。详见上游 README。

### G7 Pro 用法

- 手柄拨 **蓝牙**，长按配对；主机用 **更改握法/顺序**。
- PID `3537:1022`。不要用 2.4G/USB 的 Xbox 身份（`3537:105E`）。
- **Xbox 键** = Home（报表 `0x02`）。**下方 Share** = 截图（键盘 `0x01` / PrintScreen）。
- 面键按手柄上的 **A/B/X/Y 字母** 对应 Switch。
- 震动取决于固件是否在蓝牙上打开扩展 HID。
- **陀螺仪 / 体感：蓝牙下已确认不受支持，本 fork 不再研究**（官方亦标为仅 PC）。详见 [docs/G7Pro-Bluetooth.md](docs/G7Pro-Bluetooth.md)。

### 从源码构建

需要 [devkitPro](https://devkitpro.org/wiki/Getting_Started) Switch 工具链。克隆后：

```bash
git clone --recurse-submodules -b g7pro-hos17-ams162 https://github.com/MongkiDlufe/MissionControl.git
cd MissionControl
make dist
```

Windows 交叉编译还需要能跑 `aarch64-none-elf-gcc` 的环境；本仓库不包含 devkit 安装包。

### 开源协议

[GNU GPL v2](LICENSE)。保留 ndeadly 的版权声明。本 fork 的 HID 映射与文档以同样许可证发布。

**未包含**：手柄/接收器 UFW、解密 `app.bin`、Nexus/UWP 安装包、devkit 二进制。那些受厂商版权约束，不能当本项目源码分发。

### 致谢

- **ndeadly** 与 MissionControl 贡献者
- Atmosphère / libstratosphere（SciresM 等）
- 协议对照：公开 HID 抓包（如 OpenMicro 的 `3537:1022` 布局）与手柄固件内 HID 描述符（自行分析，不 redistributable）

---

## English

### What this is

Atmosphère sysmodule `mc_mitm` that presents third-party Bluetooth pads as Switch Pro Controllers. This fork backports **GameSir G7 Pro** (Xbox-licensed / ZZZ collab) identification and HID mapping onto **MissionControl v0.10.0** so it runs on **HOS 17.0.1 + AMS 1.6.2**.

Upstream **0.16.0** already names G7 Pro but **cannot** be used on AMS 1.6.2.

### Install

Extract a release zip to the SD card root (`010000000000bd00` + `exefs_patches`) and reboot. Pair from **Controllers → Change Grip/Order**. Bluetooth Android/DInput only (`3537:1022`).

Xbox button → Home (`0x02`). Share under Xbox → Capture (keyboard `0x01`, usage `0x46`). Face buttons follow **printed letters**. Rumble may work if extended HID is present. **Gyro/motion over Bluetooth is confirmed unsupported and will not be pursued.** See [docs/G7Pro-Bluetooth.md](docs/G7Pro-Bluetooth.md).

### Build

devkitPro Switch toolchain, then `git clone --recurse-submodules` this branch and `make dist`.

### License

[GPL-2.0](LICENSE). Original copyright: ndeadly. No GameSir firmware images are shipped.

---

The remainder of the original v0.10.0 documentation (supported pads, FAQ, how MITM works) is in [README.upstream.md](README.upstream.md).
