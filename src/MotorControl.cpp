#include "MotorControl.h"

// ===== L298N 软启动参数 =====
#define SOFT_START_STEP    15
#define SOFT_START_DELAY   20
#define MAX_MOTOR_PWM      180
// 静止起步保底占空比：从 0 起步先给到这个值，足以克服减速箱静摩擦，
// 避免"低占空比堵转不转、手动推一下才转"。约 100/255≈39%。可调 80~130。
#define MIN_START_PWM      100

// ===== LEDC PWM 手动配置（绕开 analogWrite 不确定性）=====
// 频率原 1000Hz 落人耳可闻区 → 电机线圈开关振动发出"嗯嗯"啸叫；
// 提到 8000Hz 已超出人耳敏感范围，啸叫基本消失（转向电机同走此频率）。
#define PWM_FREQ_HZ        8000
#define PWM_RESOLUTION     8

// ===== 当前各轴实际PWM值 =====
static int currentFrontSpeed = 0;
static int currentMiddleSpeed = 0;
static int currentRearSpeed = 0;

// ===== 软启动状态结构体 =====
struct SoftStartState {
  uint8_t channel;
  int* currentSpeed;
  int targetSpeed;
  unsigned long lastStepTime;
  bool active;
};

static SoftStartState ssFront  = { 0, &currentFrontSpeed,  0, 0, false };
static SoftStartState ssMiddle = { 1, &currentMiddleSpeed, 0, 0, false };
static SoftStartState ssRear   = { 2, &currentRearSpeed,   0, 0, false };

// ===== 前向声明 =====
static void _pwmWrite(uint8_t ch, uint32_t value);
static void _pwmSetup(byte gpio, uint8_t ch);
static void _setChannelDirection(uint8_t in1, uint8_t in2, bool forward);
static void _setAxisSpeed(uint8_t ch, int *currentSpeed, int targetSpeed);
static void _steerSet(int direction);

// ===== PWM 输出封装 =====
static void _pwmWrite(uint8_t ch, uint32_t value) {
  ledcWrite(ch, constrain(value, 0, 255));
}

static void _pwmSetup(byte gpio, uint8_t ch) {
  ledcSetup(ch, PWM_FREQ_HZ, PWM_RESOLUTION);
  ledcAttachPin(gpio, ch);
}

// ===== 方向控制 =====
// per-channel direction control
static void _setChannelDirection(uint8_t in1, uint8_t in2, bool forward) {
  digitalWrite(in1, forward ? HIGH : LOW);
  digitalWrite(in2, forward ? LOW : HIGH);
}

// ===== 单轴PWM速度软启动（非阻塞）=====
static void _setAxisSpeed(uint8_t ch, int *currentSpeed, int targetSpeed) {
  targetSpeed = constrain(targetSpeed, 0, MAX_MOTOR_PWM);

  SoftStartState* ss = (ch == 0) ? &ssFront : (ch == 1) ? &ssMiddle : &ssRear;

  if (targetSpeed == 0) {
    _pwmWrite(ch, 0);
    *currentSpeed = 0;
    ss->active = false; ss->targetSpeed = 0;
    return;
  }

  if (*currentSpeed >= targetSpeed) {
    *currentSpeed = targetSpeed;
    _pwmWrite(ch, *currentSpeed);
    ss->active = false; ss->targetSpeed = targetSpeed;
    return;
  }

  // ★ 启动 boost：从静止/低速起步时先给保底占空比，避免开头力气太小堵转不转
  if (*currentSpeed < MIN_START_PWM) {
    *currentSpeed = MIN_START_PWM;
    _pwmWrite(ch, *currentSpeed);
  }

  bool wasInactive = !ss->active;
  ss->targetSpeed = targetSpeed;
  ss->active = true;
  if (wasInactive) ss->lastStepTime = millis();
}

void updateSoftStarts() {
  unsigned long now = millis();
  for (int i = 0; i < 3; i++) {
    SoftStartState* ss = (i == 0) ? &ssFront : (i == 1) ? &ssMiddle : &ssRear;
    if (!ss->active) continue;
    if (*(ss->currentSpeed) >= ss->targetSpeed) { ss->active = false; continue; }
    if (now - ss->lastStepTime >= SOFT_START_DELAY) {
      *(ss->currentSpeed) += SOFT_START_STEP;
      if (*(ss->currentSpeed) > ss->targetSpeed)
        *(ss->currentSpeed) = ss->targetSpeed;
      _pwmWrite(ss->channel, *(ss->currentSpeed));
      ss->lastStepTime = now;
      if (*(ss->currentSpeed) >= ss->targetSpeed) ss->active = false;
    }
  }
}

void initMotorPins() {
  // TB6612 公共使能：STBY 必须先拉高，否则所有通道都不输出
  pinMode(MOTOR_STBY, OUTPUT);
  digitalWrite(MOTOR_STBY, HIGH);

  // 前轴
  pinMode(FRONT_IN1, OUTPUT);
  pinMode(FRONT_IN2, OUTPUT);
  _pwmSetup(FRONT_ENA,  0);

  // 中轴
  pinMode(MIDDLE_IN1, OUTPUT);
  pinMode(MIDDLE_IN2, OUTPUT);
  _pwmSetup(MIDDLE_ENA, 1);

  // 后轴
  pinMode(REAR_IN1, OUTPUT);
  pinMode(REAR_IN2, OUTPUT);
  _pwmSetup(REAR_ENA,   2);

  // 转向
  pinMode(STEER_IN1, OUTPUT);
  pinMode(STEER_IN2, OUTPUT);
  _pwmSetup(STEER_ENA, 3);

  allStop();

  Serial.printf("[MOTOR] 初始化完成 | 最大PWM=%d\n", MAX_MOTOR_PWM);
  Serial.printf("[MOTOR] LEDC: CH0=GPIO%d CH1=GPIO%d CH2=GPIO%d CH3=GPIO%d @%dHz\n",
                FRONT_ENA, MIDDLE_ENA, REAR_ENA, STEER_ENA, PWM_FREQ_HZ);
}

// ==================== 前轴 ====================
void frontForward()  { _setChannelDirection(FRONT_IN1, FRONT_IN2, true);  _setAxisSpeed(0, &currentFrontSpeed, MAX_MOTOR_PWM); }
void frontBackward() { _setChannelDirection(FRONT_IN1, FRONT_IN2, false); _setAxisSpeed(0, &currentFrontSpeed, MAX_MOTOR_PWM); }
void frontStop()     { _setAxisSpeed(0, &currentFrontSpeed, 0); digitalWrite(FRONT_IN1, LOW); digitalWrite(FRONT_IN2, LOW); }
void frontSpeed(int speed, bool forward) {
  _setChannelDirection(FRONT_IN1, FRONT_IN2, forward);
  _setAxisSpeed(0, &currentFrontSpeed, speed);
}

// ==================== 中轴 ====================
void middleForward()  { _setChannelDirection(MIDDLE_IN1, MIDDLE_IN2, true);  _setAxisSpeed(1, &currentMiddleSpeed, MAX_MOTOR_PWM); }
void middleBackward() { _setChannelDirection(MIDDLE_IN1, MIDDLE_IN2, false); _setAxisSpeed(1, &currentMiddleSpeed, MAX_MOTOR_PWM); }
void middleStop()     { _setAxisSpeed(1, &currentMiddleSpeed, 0); digitalWrite(MIDDLE_IN1, LOW); digitalWrite(MIDDLE_IN2, LOW); }
void middleSpeed(int speed, bool forward) {
  _setChannelDirection(MIDDLE_IN1, MIDDLE_IN2, forward);
  _setAxisSpeed(1, &currentMiddleSpeed, speed);
}

// ==================== 后轴 ====================
void rearForward()  { _setChannelDirection(REAR_IN1, REAR_IN2, true);  _setAxisSpeed(2, &currentRearSpeed, MAX_MOTOR_PWM); }
void rearBackward() { _setChannelDirection(REAR_IN1, REAR_IN2, false); _setAxisSpeed(2, &currentRearSpeed, MAX_MOTOR_PWM); }
void rearStop()     { _setAxisSpeed(2, &currentRearSpeed, 0); digitalWrite(REAR_IN1, LOW); digitalWrite(REAR_IN2, LOW); }
void rearSpeed(int speed, bool forward) {
  _setChannelDirection(REAR_IN1, REAR_IN2, forward);
  _setAxisSpeed(2, &currentRearSpeed, speed);
}

// ==================== 转向 (L298N D路) ====================
// 弹簧回中式转向：摇杆推着 → 持续通电保持角度（到限位即堵转，靠中等PWM控流）；
// 松开摇杆 → 断电释放（IN全LOW），由弹簧把轮子拉回中位。
// 不能断电刹车（IN1=IN2=HIGH），否则弹簧拉不动。
#define STEER_HOLD_PWM  120   // 保持PWM：堵转电流≈占空比×满堵转电流，L298N 2A/路长期安全

static void _steerSet(int direction) {
  if (direction > 0) {
    digitalWrite(STEER_IN1, HIGH); digitalWrite(STEER_IN2, LOW);
    _pwmWrite(3, STEER_HOLD_PWM);
  } else if (direction < 0) {
    digitalWrite(STEER_IN1, LOW); digitalWrite(STEER_IN2, HIGH);
    _pwmWrite(3, STEER_HOLD_PWM);
  } else {
    // 断电释放，弹簧回中
    _pwmWrite(3, 0);
    digitalWrite(STEER_IN1, LOW);
    digitalWrite(STEER_IN2, LOW);
  }
}

void steerRight() { _steerSet(1); }
void steerLeft()  { _steerSet(-1); }
void steerStop()  { _steerSet(0); }

// ==================== 全车统一控制 ====================
void allForward(int speed) {
  _setChannelDirection(FRONT_IN1, FRONT_IN2, true);
  _setChannelDirection(MIDDLE_IN1, MIDDLE_IN2, true);
  _setChannelDirection(REAR_IN1, REAR_IN2, true);
  _setAxisSpeed(0, &currentFrontSpeed, speed);
  _setAxisSpeed(1, &currentMiddleSpeed, speed);
  _setAxisSpeed(2, &currentRearSpeed, speed);
}
void allBackward(int speed) {
  _setChannelDirection(FRONT_IN1, FRONT_IN2, false);
  _setChannelDirection(MIDDLE_IN1, MIDDLE_IN2, false);
  _setChannelDirection(REAR_IN1, REAR_IN2, false);
  _setAxisSpeed(0, &currentFrontSpeed, speed);
  _setAxisSpeed(1, &currentMiddleSpeed, speed);
  _setAxisSpeed(2, &currentRearSpeed, speed);
}
void allStop() {
  digitalWrite(FRONT_IN1, LOW); digitalWrite(FRONT_IN2, LOW);
  digitalWrite(MIDDLE_IN1, LOW); digitalWrite(MIDDLE_IN2, LOW);
  digitalWrite(REAR_IN1, LOW); digitalWrite(REAR_IN2, LOW);
  _pwmWrite(0, 0);
  _pwmWrite(1, 0);
  _pwmWrite(2, 0);
  currentFrontSpeed = 0;
  currentMiddleSpeed = 0;
  currentRearSpeed = 0;
  ssFront.active  = false; ssFront.targetSpeed  = 0;
  ssMiddle.active = false; ssMiddle.targetSpeed = 0;
  ssRear.active   = false; ssRear.targetSpeed   = 0;
}
