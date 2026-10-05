# GameSir G7 Pro Bluetooth ↔ Switch (MissionControl)

中文 | [English](#english)

本页描述 **Xbox 无线授权 / ZZZ 联名 G7 Pro** 在 **Horizon 17.0.1 + Atmosphère 1.6.2** 上，经本 fork 的 `mc_mitm` 做蓝牙 HID 翻译时的协议与按键行为。上游是 [ndeadly/MissionControl](https://github.com/ndeadly/MissionControl) **v0.10.0**（GPL-2.0）。

官方 MissionControl **0.16.0** 已有基础 G7 Pro 条目，但 **没有** Home/截图锁存、字母面键、以及本 fork 里的震动/IMU 尝试。且 0.16.0 **不能** 跑在 AMS 1.6.2 上。

---

## 中文

### 目标与限制

| 项目 | 说明 |
|------|------|
| 主机 | 17.0.1 \| AMS 1.6.2 \| emuMMC |
| 安装 | `atmosphere/contents/010000000000bd00` + 随包 `exefs_patches` |
| 连接 | 仅 **蓝牙 Android / DInput HID** |
| USB / 2.4G | Xbox GIP，`3537:105E`，MissionControl **管不到** |
| 官方 Switch 模式 | 本 SKU 固件 **没有** 任天堂 HID；日志里的 `switch` 多指 USB/BT 切换 |

不要把其它型号（风行者 2 等）的「原生 Switch」固件刷到本授权板上。

### 配对

1. 拨杆 **蓝牙**，长按配对键至灯圈旋转。
2. 主机：**控制器 → 更改握法/顺序**（不是「搜索控制器」）。
3. 名称常见 `GameSir-G7 Pro`，蓝牙 PID **`3537:1022`**。
4. 覆盖 sysmodule 后 **重启**。

### 输入报表

#### `0x07` 手柄（约 10 字节）

| 偏移（含 ID） | 内容 |
|---------------|------|
| 0 | Report ID `0x07` |
| 1–4 | LX LY RX RY，uint8，中心 128 |
| 5 | 低 4 位 hat：0=上，顺时针，`0x0F` 松开 |
| 6 | 面键/肩键：bit0 南(A) bit1 东(B) bit3 西(X) bit4 北(Y) bit6 LB bit7 RB |
| 7 | bit0/1 数字 LT/RT，bit2 View(−)，bit3 Menu(+)，bit5 L3，bit6 R3。**无 Home、无截图** |
| 8–9 | R2、L2 模拟 |

面键按 **印刷字母** 映射到 Switch A/B/X/Y（Xbox 布局字母，不再按任天堂「位置对调」）。

扩展描述符可在 `0x07` 后再跟最多 38 字节 vendor（IMU）。手机/主机常见只有短包。

#### `0x02` Home（Xbox 键）

Consumer 报表，payload **bit `0x80`**。不得在每个 `0x07` 里把 home 写成 0，否则按下会被立刻清掉。

#### `0x01` 截图（Xbox 键下方 Share）

键盘集合，键码 **`0x46` PrintScreen**。不得把 `0x01` 当 `0x07` 摇杆包解析（会随机触发「减号+上 = 截图」组合，或完全没反应）。Capture 只从 `0x01` 锁存，松开清零，若无抬起包则约 2 秒超时。

### 震动与体感

固件有四路 PWM 马达；扩展 HID 输出 **`0x05`**（32 字节 vendor）。短 Android 描述符 **没有** 输出报表。连接时尝试写 `0x12`/`0x14`/`0x0f`/`0x05`；游戏震动发 `05 L R LT RT` 以及 Xbox 风格 `0x03`（0–100）。

官方体感 **仅 PC**。默认蓝牙 `0x07` 无 IMU。若出现超长 `0x07` 或 `0x10`/`0x12`/`0x14`，按 6×int16 解析。

`missioncontrol.ini` 中 `enable_rumble` / `enable_motion` 默认开启。

### 源码入口

- `mc_mitm/source/controllers/gamesir_controller.cpp`
- `mc_mitm/source/controllers/controller_management.cpp`（VID `0x3537` / 名称优先于 Nintendo 名）

### 固件备忘（不含二进制）

Nexus 2.5.8 内嵌杰理 UFW。去掉文件头 `0x1E` 后，公开工具链上 chipkey/`jl_enc_cipher` 为 `0xFFFF`。本仓库 **不发布** 手柄/接收器固件镜像或解密后的 `app.bin`。

---

## English

### Scope

Fork of [ndeadly/MissionControl](https://github.com/ndeadly/MissionControl) **v0.10.0** (GPL-2.0) for **HOS 17.0.1 + Atmosphère 1.6.2**. Bluetooth MITM only.

The Xbox-licensed G7 Pro **does not** speak Nintendo HID. Use **Android/DInput Bluetooth** (`3537:1022`). USB/2.4G GIP (`3537:105E`) is out of scope. Do not flash other GameSir “native Switch” firmware onto this SKU.

Official MC **0.16.0** lists G7 Pro but does not include this fork’s Home/Capture latching or face-button letter mapping, and **will not run** on AMS 1.6.2.

### Pairing

Bluetooth mode → hold sync until the ring spins → Switch **Controllers → Change Grip/Order** → reboot after installing `010000000000bd00`.

### Reports

- **`0x07`**: sticks, hat, face/shoulders, analog triggers. Home and Share are **not** in this frame.
- **`0x02`**: Xbox/Home, consumer bit `0x80`. Do not clear Home on every `0x07`.
- **`0x01`**: Share/Capture as keyboard **PrintScreen (`0x46`)**. Do not parse as a gamepad frame.
- Face buttons map **by printed Xbox letters** to Switch A/B/X/Y.

### Rumble / gyro

PWM motors; output report **`0x05`** exists only on the extended HID descriptor. Stock Android HID often has **no** output reports and **no** IMU (vendor documents gyro as **PC only**). The sysmodule still tries enable packets and parses IMU if the report grows.

### License

GPL-2.0. Copyright in original files: ndeadly (2020–2023). This fork does not ship GameSir firmware blobs.
