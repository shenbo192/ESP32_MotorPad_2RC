#include "Gamepad.h"
#include "MotorControl.h"
#include "LedControl.h"

// ===== 漂移模式状态 =====
bool driftMode = false;
static bool lastLB     = false;  // LB边沿检测
static bool lastRB     = false;  // RB边沿检测（手柄用）
static bool lastA      = false;  // A键边沿检测
static bool lastB      = false;  // B键边沿检测
static int  lastCmd    = -1;     // 上次命令: 0=停止,1=前进,2=后退,3=手刹,4=漂进,5=漂退
static int  lastSteer  = -1;     // 上次转向: 0=直行,1=左转,2=右转
static int  lastSpd    = 0;      // 上次速度

// ===== 速度档位 =====
#define SPEED_LOW  100   // 低速档（省电）
#define SPEED_HIGH 170   // 高速档（防过流）
int speedLevel = SPEED_LOW;  // 默认低速

void processGamepadData() {
  // ========== 解析手柄原始值 ==========
  // 注意：手柄格式 L:Y,X （第一个值是Y左右，第二个值是X前后）
  int rawLY  = gamepad.leftX;   // 左摇杆左右 (127=静止, <127=左转, >127=右转)
  int rawLX  = gamepad.leftY;   // 左摇杆前后 (127=静止, <127=前进, >127=后退)
  int rawRY  = gamepad.rightX;  // 右摇杆左右 (127=静止, <127=左转, >127=右转)
  int rawRX  = gamepad.rightY;  // 右摇杆前后 (127=静止, <127=前进, >127=后退)

  // 转换为有符号值（中心点为0）
  int leftSteer     = rawLY - 127;   // 左摇杆左右：负=左转，正=右转
  int leftThrottle  = rawLX - 127;   // 左摇杆前后：负=前进，正=后退
  int rightSteer    = rawRY - 127;   // 右摇杆左右：负=左转，正=右转
  int rightThrottle = rawRX - 127;   // 右摇杆前后：负=前进，正=后退
  
  // 摇杆死区处理（防止抖动）- 手柄摇杆幅度较小，死区设为2
  #define STICK_DEADZONE 2
  if (abs(leftSteer) < STICK_DEADZONE) leftSteer = 0;
  if (abs(leftThrottle) < STICK_DEADZONE) leftThrottle = 0;
  if (abs(rightSteer) < STICK_DEADZONE) rightSteer = 0;
  if (abs(rightThrottle) < STICK_DEADZONE) rightThrottle = 0;
  
  bool lb    = (gamepad.buttons2 & 0x10);  // LB键
  bool rb    = (gamepad.buttons2 & 0x20);  // RB键（手刹）
  bool btnA  = (gamepad.buttons1 & 0x01);  // A键
  bool btnB  = (gamepad.buttons1 & 0x02);  // B键

  // ★ 速度档位切换（A=高速，B=低速）
  if (btnA && !lastA) {
    speedLevel = SPEED_HIGH;
    Serial.printf("\n■■■ 速度档位: 高速 (%d) ■■■\n", speedLevel);
  }
  if (btnB && !lastB) {
    speedLevel = SPEED_LOW;
    Serial.printf("\n■■■ 速度档位: 低速 (%d) ■■■\n", speedLevel);
  }
  lastA = btnA;
  lastB = btnB;

  // ★ 控制逻辑：左摇杆控制油门，右摇杆控制转向（常规布局）
  int throttle = leftThrottle;   // L摇杆控制前进后退
  int steer = rightSteer;         // R摇杆控制左右转向

  // ★ 提前声明所有变量，避免 goto 跨初始化
  bool hasThrottle;
  bool goingForward;
  bool handbrake;
  int baseSpeed = 0;
  int driftFactor = 0;
  int rearSpd = 0;
  int frontSpd = 0;
  uint8_t ledMask;

  // ========== 漂移模式切换（LB按一次切换） ==========
  if (lb && !lastLB) {
    driftMode = !driftMode;
    Serial.printf("\n■■■ 漂移模式: %s ■■■\n", driftMode ? "开启" : "关闭");
  }
  lastLB = lb;

  // ========== 判断方向 ==========
  // 使用智能油门值（负值=前进，正值=后退）
  hasThrottle = (abs(throttle) >= 1);
  goingForward = (throttle < 0);  // 摇杆上推=前进（X值减小）

  // ========== 手刹（RB按住） ==========
  handbrake = rb;
  lastRB = rb;

  // 转向状态判断（★ 转向独立于油门）
  // 转向值：负=左转，正=右转
  int steerState = 0;   // 0=直行, 1=左转, 2=右转
  if (steer > 0)        steerState = 2;  // 右转
  else if (steer < 0)   steerState = 1;  // 左转

  // ★ 转向始终有效（即使无油门也能转）
  if (steer > 0) steerRight();
  else if (steer < 0) steerLeft();
  else steerStop();

  if (!hasThrottle) {
    allStop();
    if (lastCmd != 0 || lastSteer != 0) {
      lastCmd = 0; lastSteer = 0; lastSpd = 0;
    }
    goto led_update;
  }

  // 基础速度（根据速度档位：低速110，高速220）
  // abs(throttle): 1=刚出死区, 126=摇杆推到底
  baseSpeed = map(abs(throttle), 1, 126, 50, speedLevel);

  if (handbrake) {
    // ===== 手刹模式（RB按住）：后轮急停，前中轮继续推 =====
    if (goingForward) {
      frontSpeed(baseSpeed, true);
      middleSpeed(baseSpeed, true);
      rearStop();
    } else {
      frontSpeed(baseSpeed, false);
      middleSpeed(baseSpeed, false);
      rearStop();
    }
    // 手刹时配合转向
    if (steer > 0) steerRight();
    else if (steer < 0) steerLeft();
    else steerStop();

    lastCmd = 3; lastSteer = steerState; lastSpd = baseSpeed;
    goto led_update;
  }

  if (driftMode && abs(steer) >= 1) {
    driftFactor = map(abs(steer), 1, 126, 60, 220);
    if (goingForward) {
      frontSpeed(baseSpeed, true);
      middleSpeed(baseSpeed, true);
      rearSpd = constrain(baseSpeed - driftFactor, 30, 255);
      rearSpeed(rearSpd, true);
      if (steer > 0) steerRight(); else steerLeft();
      if (lastCmd != 4 || abs(lastSpd-baseSpeed)>5 || lastSteer!=steerState) {
        lastCmd = 4; lastSteer = steerState; lastSpd = baseSpeed;
      }
    } else {
      rearSpeed(baseSpeed, false);
      middleSpeed(baseSpeed, false);
      frontSpd = constrain(baseSpeed - driftFactor, 30, 255);
      frontSpeed(frontSpd, false);
      if (steer > 0) steerRight(); else steerLeft();
      if (lastCmd != 5 || abs(lastSpd-baseSpeed)>5 || lastSteer!=steerState) {
        lastCmd = 5; lastSteer = steerState; lastSpd = baseSpeed;
      }
    }
  } else {
    if (goingForward) allForward();
    else allBackward();

    if (steer > 0) steerRight();
    else if (steer < 0) steerLeft();
    else steerStop();

    int newCmd = goingForward ? 1 : 2;
    if (newCmd != lastCmd || abs(lastSpd-baseSpeed)>3 || lastSteer!=steerState) {
      lastCmd = newCmd; lastSteer = steerState; lastSpd = baseSpeed;
    }
  }

led_update:
  // ========== LED指示 ==========
  ledMask = gamepad.buttons1;
  if (driftMode) ledMask |= 0x80;     // LED8亮 = 漂移模式
  if (handbrake)  ledMask |= 0x40;     // LED7亮 = 手刹中
  updateLEDs(ledMask);
}

void printStatus() {
  Serial.println("=== 6轮攀爬车系统状态 ===");
  Serial.print("蓝牙连接: ");
  Serial.println(deviceConnected ? "已连接" : "未连接");
  Serial.printf("漂移模式: %s\n", driftMode ? "开启" : "关闭");

  if (deviceConnected) {
    Serial.printf("手柄数据: LX=%d LY=%d RX=%d RY=%d\n",
                  gamepad.leftX, gamepad.leftY,
                  gamepad.rightX, gamepad.rightY);
  }

  Serial.print("转向电机: D路(L298N)");
  Serial.println("电机状态: 前/中/后三轴");
  Serial.println("==========================");
}
