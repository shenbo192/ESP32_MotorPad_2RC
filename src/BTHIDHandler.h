

#pragma once

#include "Config.h"

void initBTHID();
void processBTHID();
void resumeBTHIDScan();  // 断开后重新开启蓝牙扫描，支持自动重连
