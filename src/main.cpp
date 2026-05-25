#include "Config.h"
#include "BTHIDHandler.h"
#include "MotorControl.h"
#include "LedControl.h"
#include "Gamepad.h"

void printPinConfig() {
  Serial.println();
  Serial.println("╔══════════════════════════════════════════╗");
  Serial.println("║     ESP32 引脚分配表 (6轮攀爬车)        ║");
  Serial.println("╠══════════════════════════════════════════╣");
  Serial.println("║  ██ 三轴共用方向 (L298N A/B/C路)       ║");
  Serial.printf("║    IN1 ──── GPIO %-2d  IN2 ──── GPIO %-2d   ║\n", DRIVE_IN1, DRIVE_IN2);
  Serial.println("╠══════════════════════════════════════════╣");
  Serial.println("║  ██ 各轴PWM独立控制                    ║");
  Serial.printf("║  前轴ENA ── GPIO %-2d                     ║\n", FRONT_ENA);
  Serial.printf("║  中轴ENB ── GPIO %-2d                     ║\n", MIDDLE_ENA);
  Serial.printf("║  后轴ENC ── GPIO %-2d                     ║\n", REAR_ENA);
  Serial.println("╠══════════════════════════════════════════╣");
  Serial.println("║  ██ 转向 (L298N D路)                   ║");
  Serial.printf("║    IN1 ──── GPIO %-2d  IN2 ──── GPIO %-2d   ║\n", STEER_IN1, STEER_IN2);
  Serial.printf("║    ENA ──── GPIO %-2d                     ║\n", STEER_ENA);
  Serial.println("╠══════════════════════════════════════════╣");
  Serial.println("║  ██ 其他                                ║");
  Serial.printf("║  STATUS_LED ── GPIO %-2d                  ║\n", STATUS_LED);
  Serial.printf("║  LED_STRIP ─── GPIO %-2d                  ║\n", LED_STRIP_PIN);
  Serial.println("╚══════════════════════════════════════════╝");
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("=== ESP32 6轮攀爬车 初始化开始 ===");
  Serial.println("驱动: L298N 四路模块 (三轴共用方向，PWM独立控制)");

  // 初始化三轴驱动电机 (L298N)
  initMotorPins();
  Serial.println("L298N 四轴电机初始化完成 (A路前轴/B路中轴/C路后轴/D路转向)");

  // 初始化LED系统（状态灯 + 灯带）
  initLEDs();

  // ★ 打印完整引脚功能分配表
  printPinConfig();

  // 初始化蓝牙HID（保持不变）
  initBTHID();

  Serial.println("=== ESP32 6轮攀爬车 初始化完成 ===");
  Serial.println("布局: 三轴共用方向GPIO12/13 + 独立PWM(14/19/18) + 转向D路");
}

void loop() {
  processBTHID();
  processGamepadData();
  updateSoftStarts();
  updateStatusLed();
}
