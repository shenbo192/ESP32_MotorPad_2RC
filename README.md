# ESP32 六轮攀爬车蓝牙遥控系统
# ESP32 Six-Wheel Crawler RC System

基于 **ESP32 + L298N** 的六轮独立驱动攀爬车，通过蓝牙 HID 游戏手柄（PS4/Xbox/Switch 等）无线操控。

A six-wheel independently-driven crawler vehicle built on **ESP32 & L298N**, wirelessly controlled via Bluetooth HID gamepads (PS4/Xbox/Switch, etc.).

---

## ✨ 功能特性 / Features

| 功能 | 说明 |
|------|------|
| 🔷 三轴六轮独立驱动 | 前/中/后轴各自 PWM 独立调速 |
| 🎮 蓝牙手柄操控 | 基于 Bluepad32，支持 PS4/Xbox/Switch 手柄 |
| 🏎️ 漂移模式 | LB 键一键切换，后轮减速漂移过弯 |
| 🛑 手刹制动 | RB 键后轮急停，前中轮继续推进 |
| ⚡ 电机软启动 | 非阻塞式渐变加速，保护电机与电源 |
| 🎚️ 双速档位 | A 键高速 / B 键低速切换 |
| 💡 LED 状态反馈 | 状态灯 + 可编程灯带实时指示 |

---

## 🔌 硬件接线 / Wiring Diagram

### 主控 / MCU
- **ESP32 开发板** (PlatformIO `esp32dev`)

### 电机驱动 / Motor Driver
- **L298N 四路 H 桥模块** × 1
- 供电：7.4V 锂电池

### 引脚分配 / Pin Assignment

```
╔══════════════════════════════════════════╗
║     ESP32 引脚分配表 (6轮攀爬车)        ║
╠══════════════════════════════════════════╣
║  ██ 三轴共用方向 (L298N A/B/C路)       ║
║    IN1 ──── GPIO 12   IN2 ──── GPIO 13  ║
╠══════════════════════════════════════════╣
║  ██ 各轴PWM独立控制                    ║
║  前轴 ENA ── GPIO 14                   ║
║  中轴 ENA ── GPIO 19                   ║
║  后轴 ENA ── GPIO 18                   ║
╠══════════════════════════════════════════╣
║  ██ 转向电机 (L298N D路)               ║
║    IN1 ──── GPIO 32   IN2 ──── GPIO 33  ║
║    ENA ──── GPIO 4                     ║
╠══════════════════════════════════════════╣
║  ██ LED系统                            ║
║  状态灯 ─── GPIO 22                    ║
║  灯带 ────── GPIO 2                     ║
╚══════════════════════════════════════════╝
```

---

## 🎮 手柄操作说明 / Controls

| 按键 / 摆杆 | 功能 | 说明 |
|-------------|------|------|
| ⬅️🔄 **左摇杆** | 油门控制 | 上推前进，后退后退 |
| ➡️🔄 **右摇杆** | 转向控制 | 左转/右转 |
| **LB** | 漂移模式开关 | 按一次开启/关闭 |
| **RB (按住)** | 手刹 | 后轮锁死，前中轮继续推进 |
| **A** | 高速档位 | PWM 最大值 ~170 |
| **B** | 低速档位 | PWM 最大值 ~100 |

> **注意**: 左摇杆 Y 轴控制油门，右摇杆 X 轴控制转向（常规赛车布局）

---

## 🛠️ 编译环境 / Build Environment

### 要求 / Requirements
- [VS Code](https://code.visualstudio.com/) + [PlatformIO](https://platformio.org/) 扩展
- 或命令行安装 PlatformIO CLI

### 依赖库 / Dependencies
| 库名 | 版本 | 用途 |
|------|------|------|
| Bluepad32 (本地框架) | 4.2.0 | 蓝牙 HID Host 通信 |
| ESP32Servo | ^1.1.1 | 舵机支持 |

### 编译上传 / Build & Upload
```bash
# VS Code 中:
# 1. 打开项目文件夹
# 2. 左下角选择 esp32dev 环境
# 3. 点击 ✔️ 编译 或 → 上传

# 或命令行:
pio run -t upload
```

### 串口监视 / Serial Monitor
- 波特率: **115200**
- 启动后会打印完整引脚分配表和系统状态

---

## 📁 项目结构 / Project Structure

```
ESP32_MotorPad_2RC/
├── src/
│   ├── main.cpp           # 主程序入口、初始化
│   ├── Config.h / .cpp    # 引脚定义、全局配置
│   ├── MotorControl.h/.cpp # L298N 三轴驱动+软启动
│   ├── Gamepad.h/.cpp     # 手柄数据解析、漂移/手刹逻辑
│   ├── BTHIDHandler.h/.cpp # 蓝牙HID连接管理
│   └── LedControl.h/.cpp  # 状态灯+灯带控制
├── platformio.ini          # PIO 构建配置
└── pio_bp32_framework/     # Bluepad32 本地框架
```

---

## 📋 系统架构 / Architecture

```
┌──────────────┐    Bluetooth HID    ┌──────────────┐
│  游戏手柄     │ ◄────────────────► │  Bluepad32   │
│ (PS4/Xbox/..) │                    │  (BT Host)   │
└──────────────┘                    └──────┬───────┘
                                           │
                                    ┌──────▼───────┐
                                    │  Gamepad      │
                                    │  数据解析      │
                                    └──────┬───────┘
                                           │
                        ┌──────────────────┼──────────────────┐
                        ▼                  ▼                  ▼
                 ┌──────────┐       ┌──────────┐       ┌──────────┐
                 │ 前轴驱动  │       │ 中轴驱动  │       │ 后轴驱动  │
                 │ (L298N A)│       │ (L298N B)│       │ (L298N C)│
                 └──────────┘       └──────────┘       └──────────┘
                        ▲                  ▲                  ▲
                        └──────────────────┴──────────────────┘
                                           │
                                    ┌──────▼───────┐
                                    │ 共用方向控制   │
                                    │ IN1=GPIO12    │
                                    │ IN2=GPIO13    │
                                    └──────────────┘

                    ┌──────────────────────────────┐
                    │         转向电机 (L298N D)     │
                    │  IN1=GPIO32 / IN2=GPIO33      │
                    └──────────────────────────────┘
```

---

## 📜 License

MIT License

---

## 🙏 致谢 / Credits
- [Bluepad32](https://github.com/ricardo-q-Bluepad32) — 蓝牙 HID Host 库
- [ESP32Servo](https://github.com/madhephaestus/ESP32Servo) — ESP32 舵机库
- [PlatformIO](https://platformio.org/) — 嵌入式开发框架
