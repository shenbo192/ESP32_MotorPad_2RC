#include "MotorControl.h"

// ===== L298N 软启动参数 =====
#define SOFT_START_STEP    15
#define SOFT_START_DELAY   20
#define MAX_MOTOR_PWM      180

// ===== LEDC PWM 手动配置（绕开 analogWrite 不确定性）=====
#define PWM_FREQ_HZ        1000
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
static void _setDriveDirection(bool forward);
static void _setAxisSpeed(uint8_t ch, int *currentSpeed, int targetSpeed);
static void _steerStart(int direction);

// ===== PWM 输出封装 =====
static void _pwmWrite(uint8_t ch, uint32_t value) {
  ledcWrite(ch, constrain(value, 0, 255));
}

static void _pwmSetup(byte gpio, uint8_t ch) {
  ledcSetup(ch, PWM_FREQ_HZ, PWM_RESOLUTION);
  ledcAttachPin(gpio, ch);
}

// ===== 方向控制 =====
static void _setDriveDirection(bool forward) {
  digitalWrite(DRIVE_IN1, forward ? HIGH : LOW);
  digitalWrite(DRIVE_IN2, forward ? LOW : HIGH);
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
  pinMode(DRIVE_IN1, OUTPUT);
  pinMode(DRIVE_IN2, OUTPUT);

  _pwmSetup(FRONT_ENA,  0);
  _pwmSetup(MIDDLE_ENA, 1);
  _pwmSetup(REAR_ENA,   2);

  pinMode(STEER_IN1, OUTPUT);
  pinMode(STEER_IN2, OUTPUT);
  _pwmSetup(STEER_ENA, 3);

  allStop();

  Serial.printf("[MOTOR] 初始化完成 | 方向GPIO%d/%d | 最大PWM=%d\n",
                DRIVE_IN1, DRIVE_IN2, MAX_MOTOR_PWM);
  Serial.printf("[MOTOR] LEDC: CH0=GPIO%d CH1=GPIO%d CH2=GPIO%d CH3=GPIO%d @%dHz\n",
                FRONT_ENA, MIDDLE_ENA, REAR_ENA, STEER_ENA, PWM_FREQ_HZ);
}

// ==================== 前轴 ====================
void frontForward()  { _setDriveDirection(true);  _setAxisSpeed(0, &currentFrontSpeed, MAX_MOTOR_PWM); }
void frontBackward() { _setDriveDirection(false); _setAxisSpeed(0, &currentFrontSpeed, MAX_MOTOR_PWM); }
void frontStop()     { _setAxisSpeed(0, &currentFrontSpeed, 0); }
void frontSpeed(int speed, bool forward) {
  _setDriveDirection(forward);
  _setAxisSpeed(0, &currentFrontSpeed, speed);
}

// ==================== 中轴 ====================
void middleForward()  { _setDriveDirection(true);  _setAxisSpeed(1, &currentMiddleSpeed, MAX_MOTOR_PWM); }
void middleBackward() { _setDriveDirection(false); _setAxisSpeed(1, &currentMiddleSpeed, MAX_MOTOR_PWM); }
void middleStop()     { _setAxisSpeed(1, &currentMiddleSpeed, 0); }
void middleSpeed(int speed, bool forward) {
  _setDriveDirection(forward);
  _setAxisSpeed(1, &currentMiddleSpeed, speed);
}

// ==================== 后轴 ====================
void rearForward()  { _setDriveDirection(true);  _setAxisSpeed(2, &currentRearSpeed, MAX_MOTOR_PWM); }
void rearBackward() { _setDriveDirection(false); _setAxisSpeed(2, &currentRearSpeed, MAX_MOTOR_PWM); }
void rearStop()     { _setAxisSpeed(2, &currentRearSpeed, 0); }
void rearSpeed(int speed, bool forward) {
  _setDriveDirection(forward);
  _setAxisSpeed(2, &currentRearSpeed, speed);
}

// ==================== 转向 (L298N D路) ====================
#define STEER_TIMEOUT_MS  400

static int steerDirection = 0;
static unsigned long steerStartTime = 0;

static void _steerStart(int direction) {
  if (direction != steerDirection) {
    steerDirection = direction;
    steerStartTime = millis();
  }
  unsigned long elapsed = millis() - steerStartTime;

  if (elapsed < STEER_TIMEOUT_MS) {
    _pwmWrite(3, MAX_MOTOR_PWM);
    if (direction > 0)
      digitalWrite(STEER_IN1, HIGH), digitalWrite(STEER_IN2, LOW);
    else
      digitalWrite(STEER_IN1, LOW), digitalWrite(STEER_IN2, HIGH);
  } else {
    _pwmWrite(3, 0);
    digitalWrite(STEER_IN1, HIGH), digitalWrite(STEER_IN2, HIGH);
  }
}

void steerRight() { _steerStart(1); }
void steerLeft()  { _steerStart(-1); }

void steerStop() {
  steerDirection = 0;
  _pwmWrite(3, 0);
  digitalWrite(STEER_IN1, LOW);
  digitalWrite(STEER_IN2, LOW);
}

// ==================== 全车统一控制 ====================
void allForward() {
  _setDriveDirection(true);
  _setAxisSpeed(0, &currentFrontSpeed, MAX_MOTOR_PWM);
  _setAxisSpeed(1, &currentMiddleSpeed, MAX_MOTOR_PWM);
  _setAxisSpeed(2, &currentRearSpeed, MAX_MOTOR_PWM);
}
void allBackward() {
  _setDriveDirection(false);
  _setAxisSpeed(0, &currentFrontSpeed, MAX_MOTOR_PWM);
  _setAxisSpeed(1, &currentMiddleSpeed, MAX_MOTOR_PWM);
  _setAxisSpeed(2, &currentRearSpeed, MAX_MOTOR_PWM);
}
void allStop() {
  digitalWrite(DRIVE_IN1, LOW);
  digitalWrite(DRIVE_IN2, LOW);
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
