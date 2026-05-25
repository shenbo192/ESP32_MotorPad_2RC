#pragma once

#include <Arduino.h>

// ==================== 6轮攀爬车配置 ====================
// L298N 四路驱动模块 + 7.4V锂电池供电
// SF-631-3LED 状态指示灯 + LED灯带 + 硬件电源开关

// ===== L298N 电机驱动引脚定义（优化版）=====
// ★ 前/中/后轴共用方向引脚（三轴永远同向运动），各轴PWM独立控制速度
#define DRIVE_IN1  12   // ✅ 共用方向控制1 (已确认)
#define DRIVE_IN2  13   // ✅ 共用方向控制2 (已确认)

// 前轴驱动电机 (L298N 电机1路) - 仅PWM独立
#define FRONT_ENA  14   // ✅ PWM速度控制 (已确认)

// 中轴驱动电机 (L298N 电机2路) - 仅PWM独立
#define MIDDLE_ENA 19   // PWM速度控制

// 后轴驱动电机 (L298N 电机3路) - 仅PWM独立
#define REAR_ENA  18    // ✅ PWM速度控制 (已确认)

// 转向电机 (L298N 电机4路) - ENA接5V，只控制IN1/IN2
#define STEER_IN1  32   // ✅ 方向控制1 (已确认)
#define STEER_IN2  33   // ✅ 方向控制2 (已确认)
#define STEER_ENA  4    // ✅ PWM速度控制 (安全引脚)

// ===== LED指示灯系统 =====
// SF-631-3LED 主状态灯（蓝牙/连接状态）
#define STATUS_LED 22   // 状态灯

// ===== 扩展功能引脚 =====
#define LED_STRIP_PIN     2    // LED灯带

/*
 * 已验证好用的GPIO: 12, 13, 14, 32, 33, 21, 18, 5
 * 当前空闲可用:     4, 5, 15, 16, 17, 21, 23
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
