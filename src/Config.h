#pragma once

#include <Arduino.h>

// ==================== 6轮攀爬车配置 ====================
// 2×TB6612FNG 四路驱动（U2驱动前/中轴，U3驱动后/转向）+ 7.4V锂电池供电
// USB Type-C 5V 供电（CC1/CC2 5.1kΩ 下拉） + AMS1117 → 5V
// SF-631-3LED 状态指示灯 + LED灯带 + HC-05蓝牙预留（D18/D19）

// ===== TB6612FNG 电机驱动引脚定义（每路独立 AIN1/AIN2 + PWMA）=====
// TB6612 每片: AIN1, AIN2, PWMA, BIN1, BIN2, PWMB (IN 同为 5V 逻辑)
// 前轴（U2 A路） — AIN1/AIN2/PWMA
#define FRONT_IN1   13
#define FRONT_IN2   12
#define FRONT_ENA   14

// 中轴（U2 B路） — BIN1/BIN2/PWMB
#define MIDDLE_IN1  27
#define MIDDLE_IN2  26
#define MIDDLE_ENA  25

// 后轴（U3 A路） — AIN1/AIN2/PWMA
#define REAR_IN1    15
#define REAR_IN2    2
#define REAR_ENA    4

// 转向（U3 B路） — BIN1/BIN2/PWMB
#define STEER_IN1   16
#define STEER_IN2   17
#define STEER_ENA   5

// ===== TB6612 公共使能 STBY =====
// 两片 TB6612 的 STBY 并联在 D23，拉 HIGH 才有输出（初始化时置高）
#define MOTOR_STBY  23

// ===== HC-05 蓝牙串口（预留）=====
// 当前固件用 Bluepad32(BTHID) 手柄；HC-05 为原理图预留接口
// HC05_RX ← 模块 TXD (GPIO18), HC05_TX → 模块 RXD (GPIO19)
#define HC05_RX     18
#define HC05_TX     19

// ===== LED指示灯系统 =====
// SF-631-3LED 主状态灯（蓝牙/连接状态）
#define STATUS_LED 22   // 状态灯

// ===== 扩展功能引脚 =====
// RGB 灯带数据线保留在 GPIO 21，避免与当前电机控制脚位冲突
#define LED_STRIP_PIN     21   // RGB 灯带数据线

/*
 * 已使用 GPIO: 2,4,5,12,13,14,15,16,17,18,19,21,22,23,25,26,27
 * 注: 18/19 为 HC-05 预留（未启用时保持高阻即可），23 为 TB6612 STBY
 * 空闲引出至 J11 排针: 32,33,34,35,36(vn),39(vp),en,3V,RX0,TX0(见原理图)
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
