#pragma once

#include <Arduino.h>

#include "Config.h"

// ==================== 6轮攀爬车电机控制 ====================
// L298N 四路驱动模块（A/B/C/D 全用）
// A路 → 前轴驱动, B路 → 中轴驱动, C路 → 后轴驱动, D路 → 转向
//
// 控制方式：IN1=HIGH, IN2=LOW + ENA=PWM → 正转
//          IN1=LOW, IN2=HIGH + ENA=PWM → 反转
//          IN1=LOW, IN2=LOW → 停止
//          ENA=PWM(0~255) → 速度控制
// PWM方式: 手动LEDC通道 (绕开analogWrite不确定性)

// 初始化所有电机引脚（含LEDC PWM通道配置）
void initMotorPins();

// ===== 前轴控制 (L298N A路) =====
void frontForward();
void frontBackward();
void frontStop();
void frontSpeed(int speed, bool forward);

// ===== 中轴控制 (L298N B路) =====
void middleForward();
void middleBackward();
void middleStop();
void middleSpeed(int speed, bool forward);

// ===== 后轴控制 (L298N C路) =====
void rearForward();
void rearBackward();
void rearStop();
void rearSpeed(int speed, bool forward);

// ===== 转向控制 (L298N D路) =====
void steerRight();
void steerLeft();
void steerStop();

// ===== 全车统一控制 =====
void allForward();
void allBackward();
void allStop();

// 非阻塞软启动更新（每loop调用一次）
void updateSoftStarts();
