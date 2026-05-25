#include "LedControl.h"

extern bool deviceConnected;
extern bool driftMode;

// LED灯带状态
static bool ledStripState = false;

void initLEDs() {
  // 初始化主状态灯 (SF-631-3LED)
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);
  
  // 初始化LED灯带
  ledStripInit();
  
  Serial.println("[LED] 系统初始化完成: 状态灯 + LED灯带");
}

void updateLEDs(uint8_t buttons1) {
  // 普通LED已移至转向电机使用，此函数保留供扩展
}

// ===== SF-631-3LED 状态指示灯 =====
// 蓝牙未连接 → 慢闪(500ms) | 已连接 → 常亮 | 漂移模式 → 快闪(100ms)
static unsigned long lastToggle = 0;
static bool ledState = false;

void updateStatusLed() {
  unsigned long now = millis();
  unsigned long interval;

  if (driftMode) {
    interval = 100;  // 漂移模式 → 快闪
  } else if (deviceConnected) {
    digitalWrite(STATUS_LED, HIGH);
    return;
  } else {
    interval = 500;  // 未连接 → 慢闪
  }

  if (now - lastToggle >= interval) {
    lastToggle = now;
    ledState = !ledState;
    digitalWrite(STATUS_LED, ledState ? HIGH : LOW);
  }
}

// ===== LED灯带控制 (SF-631-3LED 单色) =====
void ledStripInit() {
  pinMode(LED_STRIP_PIN, OUTPUT);
  digitalWrite(LED_STRIP_PIN, LOW);
  ledStripState = false;
  Serial.printf("[LED] 灯带初始化完成 | GPIO=%d\n", LED_STRIP_PIN);
}

void ledStripOn() {
  digitalWrite(LED_STRIP_PIN, HIGH);
  ledStripState = true;
}

void ledStripOff() {
  digitalWrite(LED_STRIP_PIN, LOW);
  ledStripState = false;
}

void ledStripToggle() {
  ledStripState = !ledStripState;
  digitalWrite(LED_STRIP_PIN, ledStripState ? HIGH : LOW);
}

bool isLedStripOn() {
  return ledStripState;
}
