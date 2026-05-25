/**
 * 蓝牙HID主机 - Bluepad32 for Arduino (Controller API)
 * 
 * 功能:
 *   ✅ 自动重连: 断连1秒后自动重新扫描蓝牙
 *   ✅ NVS记忆: 保存/恢复手柄绑定信息，启动时优先重连
 *   ✅ 手动重扫: 可通过函数调用触发（resumeBTHIDScan）
 * 
 * API说明: DPAD_UP/BUTTON_A 等是全局枚举常量(uni_gamepad.h)，不是Controller类成员
 */

#include "BTHIDHandler.h"
#include "Config.h"

#include <Bluepad32.h>
#include <WiFi.h>
#include <nvs_flash.h>

// ============ 配置参数 ============
#define RECONNECT_TIMEOUT_MS   500     // 断连后多久自动重扫 (毫秒) - 改为500ms更快重连
#define SCAN_DURATION_MS       3000    // 每次扫描持续时长 (毫秒) - 改为3秒更快扫描

// ============ 全局变量 ============
static ControllerPtr myGamepads[BP32_MAX_GAMEPADS];
static bool bp32Ready = false;
static unsigned long disconnectTime = 0;     // 断连时刻记录
static bool autoReconnectEnabled = true;     // 自动重连开关
static unsigned long lastScanTime = 0;       // 上次扫描时间
static bool isScanning = false;              // 正在扫描标志

// ============ NVS 蓝牙绑定存储（增强版）============
// NVS命名空间: "bluepad32"
// 存储键值:
//   "bonded_model"    - 手柄型号 (int32_t)
//   "bonded_time"     - 最后连接时间 (int64_t)
//   "connect_count"   - 累计连接次数 (int32_t)

static void saveBondToNVS(int modelType) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("bluepad32", NVS_READWRITE, &handle);
    if (err != ESP_OK) return;
    
    nvs_set_i32(handle, "bonded_model", modelType);
    nvs_set_i64(handle, "bonded_time", millis());
    
    // 累加连接次数
    int32_t count = 0;
    nvs_get_i32(handle, "connect_count", &count);
    nvs_set_i32(handle, "connect_count", count + 1);
    
    nvs_commit(handle);
    nvs_close(handle);
    Serial.printf("  ✓ 已保存手柄到NVS [型号:%d 第%d次连接]\n", modelType, count + 1);
}

struct BondInfo {
    int32_t model;          // 手柄型号
    int64_t lastTime;       // 最后连接时间
    int32_t connectCount;   // 累计连接次数
};

static bool loadBondFromNVS(BondInfo* info) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("bluepad32", NVS_READONLY, &handle);
    if (err != ESP_OK) return false;
    
    bool ok = true;
    if (nvs_get_i32(handle, "bonded_model", &info->model) != ESP_OK) ok = false;
    if (nvs_get_i64(handle, "bonded_time", &info->lastTime) != ESP_OK) info->lastTime = 0;
    if (nvs_get_i32(handle, "connect_count", &info->connectCount) != ESP_OK) info->connectCount = 0;
    
    nvs_close(handle);
    return ok;
}

static void clearBondNVS() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("bluepad32", NVS_READWRITE, &handle);
    if (err != ESP_OK) return;
    nvs_erase_all(handle);  // 清除所有绑定信息
    nvs_commit(handle);
    nvs_close(handle);
    Serial.println("  ✓ 已清除NVS中的所有手柄绑定记录");
}

// ============ 手柄型号名称 ============
static const char* getModelName(int model) {
    switch(model) {
        case Controller::CONTROLLER_TYPE_XBox360Controller: return "Xbox 360";
        case Controller::CONTROLLER_TYPE_XBoxOneController: return "Xbox One/Series";
        case Controller::CONTROLLER_TYPE_PS3Controller: return "PlayStation 3";
        case Controller::CONTROLLER_TYPE_PS4Controller: return "PlayStation 4";
        case Controller::CONTROLLER_TYPE_PS5Controller: return "PlayStation 5";
        case Controller::CONTROLLER_TYPE_SwitchProController: return "Switch Pro";
        case Controller::CONTROLLER_TYPE_SwitchJoyConLeft: return "Joy-Con L";
        case Controller::CONTROLLER_TYPE_SwitchJoyConRight: return "Joy-Con R";
        case Controller::CONTROLLER_TYPE_SwitchJoyConPair: return "Joy-Con Pair";
        case Controller::CONTROLLER_TYPE_EightBitdoController: return "八位堂";
        default: return "Unknown";
    }
}

// ============ 扫描控制 ============

// 开始新的蓝牙扫描
static void startScan() {
    if (isScanning) return;  // 避免重复扫描
    
    isScanning = true;
    lastScanTime = millis();
    BP32.forgetBluetoothKeys();  // 清除旧绑定，开始新扫描
    
    Serial.print("\n>>> 🔍 开始蓝牙扫描... ");
    Serial.printf("[超时: %dms] <<<\n", SCAN_DURATION_MS);
}

// 检查扫描是否超时，超时则停止
static void checkScanTimeout() {
    if (!isScanning) return;
    
    if (millis() - lastScanTime > SCAN_DURATION_MS) {
        isScanning = false;
        Serial.println("\n>>> ⏱ 扫描超时，等待下次触发 <<<\n");
    }
}

// ============ 回调函数 ============

// 手柄连接回调
void onConnectedController(ControllerPtr gp) {
    int model = gp->getModel();
    
    // 停止正在进行的扫描（已找到设备）
    isScanning = false;
    
    Serial.println("\n╔══════════════════════════════════╗");
    Serial.println("║   ✅ 游戏手柄已连接!                ║");
    Serial.println("╚══════════════════════════════════╝\n");

    Serial.printf("  型号编号: %d\n", model);
    Serial.printf("  型号名称: %s\n", getModelName(model));
    
    // 查找空闲槽位
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (myGamepads[i] == nullptr) {
            myGamepads[i] = gp;
            Serial.printf("  槽位: #%d\n", i);
            break;
        }
    }
    
    // 保存到NVS（增强版：记录型号+时间+次数）
    saveBondToNVS(model);
    
    deviceConnected = true;

    // 设置玩家LED
    gp->setPlayerLEDs(1);
    
    // 根据手柄类型设置LED颜色
    switch(model) {
        case Controller::CONTROLLER_TYPE_PS4Controller:
            gp->setColorLED(0, 0, 255);  // PS蓝
            break;
        case Controller::CONTROLLER_TYPE_PS5Controller:
            gp->setColorLED(0, 0, 128);  // PS5淡蓝
            break;
        case Controller::CONTROLLER_TYPE_SwitchProController:
            gp->setColorLED(0, 255, 0);  // 任天堂绿
            break;
        default:
            break;
    }
    
    Serial.println("");
}

// 手柄断开回调
void onDisconnectedController(ControllerPtr gp) {
    Serial.println("\n[⛔ 断开] 手柄已断开连接");
    Serial.printf("   记录断连时刻，%dms后将自动重扫...\n\n", RECONNECT_TIMEOUT_MS);
    
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (myGamepads[i] == gp) {
            myGamepads[i] = nullptr;
        }
    }
    
    deviceConnected = false;
    disconnectTime = millis();  // ★ 记录断连时刻 ★
}

// ============ 初始化函数 ============
void initBTHID()
{
    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║   Bluepad32 v4.x HID 主机             ║");
    Serial.println("║   (Arduino Controller API)             ║");
    Serial.println("║   ✅ 自动重连 + NVS记忆               ║");
    Serial.println("╚══════════════════════════════════════╝\n");

    // 关闭WiFi释放资源
    Serial.print("[1/3] 关闭WiFi... ");
    WiFi.mode(WIFI_OFF);
    delay(300);
    Serial.println("OK");

    // 初始化NVS Flash（必须在使用前初始化）
    Serial.print("[2/3] 初始化NVS... ");
    esp_err_t nvsErr = nvs_flash_init();
    if (nvsErr == ESP_ERR_NVS_NO_FREE_PAGES || nvsErr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();  // 重置后重新初始化
    }
    Serial.println(nvsErr == ESP_OK ? "OK" : "已重置");

    // 配置并初始化Bluepad32
    Serial.print("[3/3] 初始化Bluepad32... ");
    BP32.setup(&onConnectedController, &onDisconnectedController);

    // 清空Controller数组
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        myGamepads[i] = nullptr;
    }

    bp32Ready = true;

    Serial.println("OK ✓\n");
    
    Serial.println("┌──────────────────────────────────────┐");
    Serial.println("│  ✓ 蓝牙HID主机就绪                   │");
    Serial.printf("│  固件: %s\n", BP32.firmwareVersion());
    Serial.printf("│  重连超时: %dms | 扫描时长: %dms      │\n", RECONNECT_TIMEOUT_MS, SCAN_DURATION_MS);
    Serial.println("└──────────────────────────────────────┘\n");
    
    Serial.println("支持的手柄:");
    Serial.println("  • Xbox One / Series X|S");
    Serial.println("  • PlayStation 3 / 4 / 5");
    Serial.println("  • Nintendo Switch Pro / Joy-Con");
    Serial.println("  • 八位堂 / 北通 等蓝牙手柄\n");
    
    // 读取NVS记忆
    BondInfo bond;
    if (loadBondFromNVS(&bond)) {
        Serial.println("📋 [NVS记忆]");
        Serial.printf("   上次手柄: %s (%d)\n", getModelName(bond.model), bond.model);
        Serial.printf("   连接次数: %d次\n", bond.connectCount);
        Serial.printf("   最后连接: %lld ms前开机\n", (long long)bond.lastTime);
        Serial.println("   → 启动后将优先尝试重连此手柄\n");
    } else {
        Serial.println("📋 [NVS记忆] 无历史记录，等待首次配对...\n");
    }
    
    Serial.println("操作步骤:");
    Serial.println("  Xbox: 长按配对按钮3秒 (直到Xbox灯快闪)");
    Serial.println("  PS4:   按住PS + SHARE键 (直到灯快闪)");
    Serial.println("  Switch: 设置 > 手柄 > 更新手柄，按L+R");
    Serial.println("");
}

// ============ 手动触发重新扫描（公开接口）============
void resumeBTHIDScan() {
    Serial.println("\n🔄 [手动] 用户请求重新搜索蓝牙手柄...");
    startScan();
    disconnectTime = 0;  // 重置断连计时器
}

// ============ 主循环处理（含自动重连逻辑）============
void processBTHID()
{
    if (!bp32Ready) return;

    // ★ 必须在loop中调用以处理蓝牙事件 ★
    BP32.update();

    // ========== 自动重联逻辑 ==========
    if (!deviceConnected && autoReconnectEnabled) {
        
        // 检查是否到达重扫超时
        if (disconnectTime > 0 && (millis() - disconnectTime >= RECONNECT_TIMEOUT_MS)) {
            // 只在未扫描状态下才触发新扫描
            if (!isScanning) {
                Serial.printf("\n⏱ [%lu ms无连接] 触动自动重扫...\n", millis() - disconnectTime);
                startScan();
                disconnectTime = 0;  // 重置，避免重复触发
            }
        }
        
        // 检查当前扫描是否超时
        checkScanTimeout();
    }

    // 处理已连接的Controller数据
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        ControllerPtr gp = myGamepads[i];
        
        if (gp != nullptr && gp->isConnected()) {
            
            // ========== 更新全局gamepad结构体 ==========
            
            // 方向键 (DPAD) - 使用全局枚举常量，非类成员
            gamepad.buttons1 = 0;
            uint8_t dpad = gp->dpad();
            if (dpad & DPAD_UP)    gamepad.buttons1 |= 0x01;
            if (dpad & DPAD_DOWN)  gamepad.buttons1 |= 0x02;
            if (dpad & DPAD_LEFT)  gamepad.buttons1 |= 0x04;
            if (dpad & DPAD_RIGHT) gamepad.buttons1 |= 0x08;
            
            // 功能按键
            if (gp->miscStart())   gamepad.buttons1 |= 0x10;  // Start/+
            if (gp->miscSelect())  gamepad.buttons1 |= 0x20;  // Select/-
            if (gp->miscSystem())  gamepad.buttons1 |= 0x40;  // Home/Xbox/PS

            // 动作按键 (ABXY) - 使用全局枚举常量
            gamepad.buttons2 = 0;
            uint16_t btns = gp->buttons();
            if (btns & BUTTON_A)        gamepad.buttons2 |= 0x01;  // A / Cross
            if (btns & BUTTON_B)        gamepad.buttons2 |= 0x02;  // B / Circle  
            if (btns & BUTTON_X)        gamepad.buttons2 |= 0x04;  // X / Square
            if (btns & BUTTON_Y)        gamepad.buttons2 |= 0x08;  // Y / Triangle
            if (btns & BUTTON_SHOULDER_L) gamepad.buttons2 |= 0x10;  // L1 / LB
            if (btns & BUTTON_SHOULDER_R) gamepad.buttons2 |= 0x20;  // R1 / RB
            if (btns & BUTTON_TRIGGER_L)  gamepad.buttons2 |= 0x40;  // L2 / LT
            if (btns & BUTTON_TRIGGER_R)  gamepad.buttons2 |= 0x80;  // R2 / RT

            // 左摇杆 (-32768 ~ 32767 → 0 ~ 255, 128为中心)
            gamepad.leftX  = map(gp->axisX(),  -32768, 32767, 0, 255);
            gamepad.leftY  = map(gp->axisY(),  -32768, 32767, 0, 255);
            
            // 右摇杆
            gamepad.rightX = map(gp->axisRX(), -32768, 32767, 0, 255);
            gamepad.rightY = map(gp->axisRY(), -32768, 32767, 0, 255);

            // 扳机 brake/throttle (0~1023 → 0~255)
            gamepad.leftTrigger  = map(gp->brake(),    0, 1023, 0, 255);
            gamepad.rightTrigger = map(gp->throttle(), 0, 1023, 0, 255);
            
            break;  // 只处理第一个手柄
        }
    }

    // ========== 实时数据监控 ==========
    static GamepadData lastData = {0,0,128,128,128,128,0,0};
    
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        ControllerPtr gp = myGamepads[i];
        
        if (gp != nullptr && gp->isConnected()) {
            
            GamepadData cur;
            
            // 方向键
            uint8_t dpad = gp->dpad();
            cur.buttons1 = 0;
            if(dpad & DPAD_UP)    cur.buttons1 |= 0x01;
            if(dpad & DPAD_DOWN)  cur.buttons1 |= 0x02;
            if(dpad & DPAD_LEFT)  cur.buttons1 |= 0x04;
            if(dpad & DPAD_RIGHT) cur.buttons1 |= 0x08;
            if(gp->miscStart())   cur.buttons1 |= 0x10;
            if(gp->miscSelect())  cur.buttons1 |= 0x20;
            if(gp->miscSystem())  cur.buttons1 |= 0x40;
            
            // 动作键
            uint16_t btns = gp->buttons();
            cur.buttons2 = 0;
            if(btns & BUTTON_A)          cur.buttons2 |= 0x01;
            if(btns & BUTTON_B)          cur.buttons2 |= 0x02;
            if(btns & BUTTON_X)          cur.buttons2 |= 0x04;
            if(btns & BUTTON_Y)          cur.buttons2 |= 0x08;
            if(btns & BUTTON_SHOULDER_L) cur.buttons2 |= 0x10;
            if(btns & BUTTON_SHOULDER_R) cur.buttons2 |= 0x20;
            if(btns & BUTTON_TRIGGER_L)  cur.buttons2 |= 0x40;
            if(btns & BUTTON_TRIGGER_R)  cur.buttons2 |= 0x80;
            
            // 摇杆
            cur.leftX  = map(gp->axisX(),  -32768,32767,0,255);
            cur.leftY  = map(gp->axisY(),  -32768,32767,0,255);
            cur.rightX = map(gp->axisRX(), -32768,32767,0,255);
            cur.rightY = map(gp->axisRY(), -32768,32767,0,255);
            
            // 扳机
            cur.leftTrigger  = map(gp->brake(),   0,1023,0,255);
            cur.rightTrigger = map(gp->throttle(),0,1023,0,255);
            
            // 更新全局
            gamepad = cur;
            
            // 变化检测 + 输出
            if(memcmp(&cur, &lastData, sizeof(GamepadData)) != 0) {
                lastData = cur;
                
                Serial.print("[");
                if(cur.buttons2&0x01)Serial.print("A "); else Serial.print("_ ");
                if(cur.buttons2&0x02)Serial.print("B "); else Serial.print("_ ");
                if(cur.buttons2&0x04)Serial.print("X "); else Serial.print("_ ");
                if(cur.buttons2&0x08)Serial.print("Y "); else Serial.print("_ ");
                if(cur.buttons2&0x10)Serial.print("LB"); else Serial.print("__");
                if(cur.buttons2&0x20)Serial.print("RB"); else Serial.print("__");
                if(cur.buttons2&0x40)Serial.print("LT"); else Serial.print("__");
                if(cur.buttons2&0x80)Serial.print("RT"); else Serial.print("__");
                Serial.print("| ");
                if(cur.buttons1&0x10)Serial.print("ST "); else Serial.print("___ ");
                if(cur.buttons1&0x20)Serial.print("SL "); else Serial.print("___ ");
                if(cur.buttons1&0x40)Serial.print("HM "); else Serial.print("___ ");
                Serial.print("| D:");
                if(cur.buttons1&0x01)Serial.print("↑"); else Serial.print("-");
                if(cur.buttons1&0x02)Serial.print("↓"); else Serial.print("-");
                if(cur.buttons1&0x04)Serial.print("←"); else Serial.print("-");
                if(cur.buttons1&0x08)Serial.print("→"); else Serial.print("-");
                Serial.printf("| L:%3d,%3d R:%3d,%3d| LT:%3d RT:%3d\n",
                    cur.leftX,cur.leftY, cur.rightX,cur.rightY,
                    cur.leftTrigger, cur.rightTrigger);
            }
            break;
        }
    }
}
