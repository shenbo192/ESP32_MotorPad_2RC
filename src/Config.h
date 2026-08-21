#pragma once

#include <Arduino.h>

// ==================== 6轮攀爬车配置 ====================
// L298N 四路驱动模块 + 7.4V锂电池供电
// SF-631-3LED 状态指示灯 + LED灯带 + 硬件电源开关

// ===== L298N 电机驱动引脚定义（每路独立 IN1/IN2 + ENA）=====
// 每一路 L298N: IN1, IN2, ENA(PWM)
// 前轴（A 路） — 使用你提供第一行的前三个：D13, D12, D14
#define FRONT_IN1   13
#define FRONT_IN2   12
#define FRONT_ENA   14

// 中轴（B 路） — 使用第一行的下一个三组：27,26,25
#define MIDDLE_IN1  27
#define MIDDLE_IN2  26
#define MIDDLE_ENA  25

// 后轴（C 路） — 使用第二行的前三个：D15, D2, D4
#define REAR_IN1    15
#define REAR_IN2    2
#define REAR_ENA    4

// 转向（D 路） — D6/D7 是 SPI Flash 脚，不能当 GPIO 用，改用空闲的 16/17
#define STEER_IN1   16
#define STEER_IN2   17
#define STEER_ENA   5

// ===== LED指示灯系统 =====
// SF-631-3LED 主状态灯（蓝牙/连接状态）
#define STATUS_LED 22   // 状态灯

// ===== 扩展功能引脚 =====
// RGB 灯带数据线保留在 GPIO 21，避免与当前电机控制脚位冲突
#define LED_STRIP_PIN     21   // RGB 灯带数据线

/*
 * 已使用 GPIO: 2,4,5,12,13,14,15,16,17,21,22,25,26,27
 * 注: 6/7 是 SPI Flash 脚, 已弃用, 转向改用 16/17
 * 当前空闲可用: 32,33 (以及 0,1,3,18,19,23 等)
 */

extern bool deviceConnected;

struct GamepadData {
  uint8_t buttons1;
  uint8_t buttons2;
  uint8_t leftX;
  uint8_t leftY;
  uint8_t rightX;
  uint8_t rightY;
  uint8_t leftTrigger;
  uint8_t rightTrigger;
};

extern GamepadData gamepad;
