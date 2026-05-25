#pragma once

#include "Config.h"

// ==================== LED控制系统 ====================
// SF-631-3LED 状态指示灯 + LED灯带
// 电源开关：硬件级（串联在电源回路中，无需软件检测）

// 初始化所有LED引脚
void initLEDs();

// 更新8路LED状态（根据手柄按钮）
void updateLEDs(uint8_t buttons1);

// 更新主状态灯 (SF-631-3LED)
void updateStatusLed();

// ===== LED灯带控制 (SF-631-3LED 单色) =====
void ledStripInit();
void ledStripOn();
void ledStripOff();
void ledStripToggle();
bool isLedStripOn();
