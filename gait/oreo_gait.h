// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
#pragma once
// Oreo's walk, on its own: the CPG gait engine with the tuned walks, the calibration mapping and a choice of servo
// output, in one header. Use it to try new gaits on a bare robot (no camera, display, WiFi), then carry the numbers
// back into firmware/src/main.cpp (WALK_FWD_* / WALK_BACK_* / the turn tables there hold the same values).
//
// Servo output - define ONE before including this file:
//   #define OREO_GAIT_PCA9685   the servos on a PCA9685 board (I2C), like Oreo  (needs "Adafruit PWM Servo Driver Library")
//   #define OREO_GAIT_DIRECT    the servos straight on ESP32 GPIOs (LEDC PWM, 8 channels = the 8 legs)
//   (neither)                  you provide: void oreoGaitWriteUs(uint8_t leg, uint16_t us)
//
// How the walk works: every leg servo follows   angle = base + amp * cos(freq * t + phase)
// - base   the leg's middle angle in the walk (deg)
// - amp    how far it swings either way (deg; the sign sets the direction)
// - phase  where in the cycle it is (rad): diagonal legs together = a trot
// - freq   rad per ms (0.012 = one step cycle in ~524 ms; negative = the walk played backward)
// The angles are Oreo's built-in angles. Each one is mapped onto YOUR calibration (min / mid / max per leg, the
// same values as the calibration page), so a walk tuned on one robot works on another.

#include <Arduino.h>
#include <math.h>

namespace oreo {

// The leg ids (the firmware's order: calibration values, channel maps and poses are indexed by these)
enum Leg : uint8_t { R1 = 0, R2 = 1, L1 = 2, L2 = 3, R4 = 4, R3 = 5, L3 = 6, L4 = 7 };
//   R1 front-right hip   R2 back-right hip   L1 front-left hip   L2 back-left hip
//   R4 back-right knee   R3 front-right knee L3 front-left knee  L4 back-left knee

// The gait tables use their own order: FL hip, BL hip, FL knee, BL knee, FR hip, BR hip, FR knee, BR knee
const uint8_t GAIT_LEG[8] = {L1, L2, L3, L4, R1, R2, R3, R4};

// The built-in calibration: every walk below was tuned on these angles (deg, page scale 0..180 = 732..2929 us)
const float LEG_MIN0[8] = {62.0, 80.0, -18.0, 76.0, 150.0, -18.0, 150.0, -18.0};
const float LEG_MID0[8] = {23.0, 115.0, 23.0, 39.0, -8.0, 107.0, 20.0, 132.0};
const float LEG_MAX0[8] = {-16.0, 150.0, 64.0, 0.0, -17.0, 130.0, -18.0, 140.0};

struct Gait { float base[8], amp[8], phase[8], freq; bool kneeLiftOnly[8]; };

const float PH_TROT[8] = {0.0, -1.5708, 3.14159, 1.5708, 3.14159, 1.5708, 0.0, -1.5708};

// The tuned walks (the same numbers as the firmware)
const Gait WALK_FORWARD = {{44.0, 24.0, 41.0, 117.0, 2.0, 130.0, 93.0, 7.0},
                           {-20.0, 20.0, -17.0, 17.0, 20.0, -20.0, 17.0, -17.0},
                           {0.0, -1.5708, 3.14159, 1.5708, 3.14159, 1.5708, 0.0, -1.5708}, 0.012, {}};
const Gait WALK_BACKWARD = {{44.0, 24.0, 41.0, 97.0, 2.0, 130.0, 92.0, 18.0},
                            {-20.0, 20.0, -17.0, 18.0, 20.0, -20.0, 17.0, -17.0},
                            {0.0, -1.5708, 3.14159, 1.5708, 3.14159, 1.5708, 0.0, -1.5708}, -0.012, {}};
// Curves: the inner side's hips stride half as far
const Gait WALK_LEFT = {{44.0, 24.0, 41.0, 117.0, 2.0, 130.0, 93.0, 7.0},
                        {-10.0, 10.0, -17.0, 17.0, 20.0, -20.0, 17.0, -17.0},
                        {0.0, -1.5708, 3.14159, 1.5708, 3.14159, 1.5708, 0.0, -1.5708}, 0.012, {}};
const Gait WALK_RIGHT = {{44.0, 24.0, 41.0, 117.0, 2.0, 130.0, 93.0, 7.0},
                         {-20.0, 20.0, -17.0, 17.0, 10.0, -10.0, 17.0, -17.0},
                         {0.0, -1.5708, 3.14159, 1.5708, 3.14159, 1.5708, 0.0, -1.5708}, 0.012, {}};
// Turning on the spot: the inner hips stride backward, the outer ones forward (skid steer), a little slower
const Gait TURN_LEFT = {{44.0, 24.0, 41.0, 117.0, 2.0, 130.0, 93.0, 7.0},
                        {12.0, -12.0, -17.0, 18.0, 20.0, -20.0, 17.0, -17.0},
                        {0.0, -1.5708, 3.14159, 1.5708, 3.14159, 1.5708, 0.0, -1.5708}, 0.010, {}};
const Gait TURN_RIGHT = {{44.0, 24.0, 41.0, 117.0, 2.0, 130.0, 93.0, 7.0},
                         {-20.0, 20.0, -17.0, 17.0, -12.0, 12.0, 17.0, -17.0},
                         {0.0, -1.5708, 3.14159, 1.5708, 3.14159, 1.5708, 0.0, -1.5708}, 0.010, {}};

// The balanced trot: every leg the same numbers from its own calibration (symmetric front / back, left / right),
// the knee lifts only while its foot swings forward. lift / stride in deg.
inline Gait balancedTrot(float lift = 24, float stride = 18, bool backward = false) {
  Gait g = {};
  const uint8_t HIP[4] = {0, 1, 4, 5}, KNEE[4] = {2, 3, 6, 7};
  const uint8_t EH[4] = {L1, L2, R1, R2}, EK[4] = {L3, L4, R3, R4};
  const bool FRONT[4] = {true, false, true, false};
  const float PH[4] = {0.0f, 3.14159265f, 3.14159265f, 0.0f};
  const float H0 = 18, K0 = 15;
  for (int k = 0; k < 4; k++) {
    const float sOut = LEG_MAX0[EH[k]] > LEG_MID0[EH[k]] ? 1.0f : -1.0f;
    const float sLift = LEG_MIN0[EK[k]] > LEG_MID0[EK[k]] ? 1.0f : -1.0f;
    g.base[HIP[k]] = LEG_MID0[EH[k]] + sOut * H0;
    g.amp[HIP[k]] = sOut * (FRONT[k] ? -1.0f : 1.0f) * stride;
    g.phase[HIP[k]] = PH[k] - 1.5707963f;
    g.base[KNEE[k]] = LEG_MID0[EK[k]] + sLift * K0;
    g.amp[KNEE[k]] = sLift * lift;
    g.phase[KNEE[k]] = PH[k];
    g.kneeLiftOnly[KNEE[k]] = true;
  }
  g.freq = backward ? -0.012f : 0.012f;
  return g;
}

// ---------------------------------------------------------------- calibration
// Your robot's min / mid / max per leg (leg-id order), from the calibration page. Defaults = the built-in ones.
struct Calibration {
  float min[8], mid[8], max[8];
};
inline Calibration builtInCalibration() {
  Calibration c;
  for (int i = 0; i < 8; i++) { c.min[i] = LEG_MIN0[i]; c.mid[i] = LEG_MID0[i]; c.max[i] = LEG_MAX0[i]; }
  return c;
}

// A built-in angle onto the calibration: min..mid and mid..max each stretched linearly
inline float calPiece(float a, float a0, float aMin, float aMax, float b0, float bMin, float bMax) {
  const bool minSide = (a - a0) * (aMin - a0) > 0;
  const float aE = minSide ? aMin : aMax, bE = minSide ? bMin : bMax;
  if (fabsf(aE - a0) < 0.5f) return b0 + (a - a0);
  return b0 + (a - a0) * (bE - b0) / (aE - a0);
}

// Page angle -> pulse. MG90S servos only follow ~-19..150 on this scale.
inline uint16_t angleToUs(float a) { return (uint16_t)lroundf(732 + a * (2929 - 732) / 180.0f); }

}  // namespace oreo

// ---------------------------------------------------------------- servo output
#if defined(OREO_GAIT_PCA9685)
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
namespace oreo {
// PCA9685 channel per leg (leg-id order). Oreo's default wiring; change to yours.
int legChannel[8] = {14, 4, 12, 13, 8, 9, 11, 10};
Adafruit_PWMServoDriver pca(0x40);
inline void outputBegin(int sda = 1, int scl = 2) {
  Wire.begin(sda, scl);
  pca.begin();
  pca.setOscillatorFrequency(25000000);
  pca.setPWMFreq(50);
}
inline void writeUs(uint8_t leg, uint16_t us) { pca.writeMicroseconds(legChannel[leg], us); }
inline void release(uint8_t leg) { pca.setPWM(legChannel[leg], 0, 4096); }
}  // namespace oreo

#elif defined(OREO_GAIT_DIRECT)
namespace oreo {
// GPIO per leg (leg-id order): set these to the pins your servo signal wires are on. Each uses one LEDC channel.
int legPin[8] = {4, 5, 6, 7, 15, 16, 17, 18};
const uint32_t SERVO_HZ = 50;
const uint8_t SERVO_BITS = 14;
inline void outputBegin() {
  for (int i = 0; i < 8; i++) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(legPin[i], SERVO_HZ, SERVO_BITS);
#else
    ledcSetup(i, SERVO_HZ, SERVO_BITS);
    ledcAttachPin(legPin[i], i);
#endif
  }
}
inline void ledcOut(uint8_t leg, uint32_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(legPin[leg], duty);
#else
  ledcWrite(leg, duty);
#endif
}
inline void writeUs(uint8_t leg, uint16_t us) { ledcOut(leg, (uint32_t)us * ((1u << SERVO_BITS) - 1) / (1000000u / SERVO_HZ)); }
inline void release(uint8_t leg) { ledcOut(leg, 0); }
}  // namespace oreo

#else
void oreoGaitWriteUs(uint8_t leg, uint16_t us);
namespace oreo {
inline void writeUs(uint8_t leg, uint16_t us) { oreoGaitWriteUs(leg, us); }
}  // namespace oreo
#endif

namespace oreo {

// ---------------------------------------------------------------- the engine
// Call update() every ~20 ms. Switching gaits cross-fades over 400 ms from the gait (or pose) the legs were in.
class GaitEngine {
 public:
  Calibration cal = builtInCalibration();
  float speed = 1.0f;                  // the clock's rate: 0.5 = half speed, 1.5 = faster steps
  float strideLeft = 1, strideRight = 1;  // each side's hip stride, 0..1 (shorten one side to steer or trim a pull)
  uint16_t softStartMs = 1200;         // the strides grow from 0 to full over this long (0 = at once)

  // Put the legs in the stand pose (each leg's mid) right away
  void stand(uint32_t nowMs) {
    for (int i = 0; i < 8; i++) setLeg(i, LEG_MID0[i]);
    running = false;
    t = 0;
    lastMs = nowMs;
  }

  void start(const Gait& g, uint32_t nowMs) {
    if (running) { from = cur; fromLive = true; }
    else { fromLive = false; }
    for (int i = 0; i < 8; i++) fromAngle[i] = angle[GAIT_LEG[i]];
    cur = g;
    blendMs = nowMs;
    if (!running) { lastMs = nowMs; startMs = nowMs; }
    running = true;
  }

  void stop(uint32_t nowMs) { stand(nowMs); }

  void update(uint32_t nowMs) {
    if (!running) return;
    t += (double)(nowMs - lastMs) * speed;
    lastMs = nowMs;
    const float bu = fminf(1.0f, (nowMs - blendMs) / 400.0f), be = bu * bu * (3 - 2 * bu);
    float env = 1;
    if (softStartMs) { const float u = fminf(1.0f, (nowMs - startMs) / (float)softStartMs); env = u * u * (3 - 2 * u); }
    for (int i = 0; i < 8; i++) {
      float amp = cur.amp[i] * env;
      if (i == 0 || i == 1) amp *= strideLeft;
      if (i == 4 || i == 5) amp *= strideRight;
      float c = cosf(cur.freq * t + cur.phase[i]);
      if (cur.kneeLiftOnly[i] && c < 0) c = 0;
      float a = cur.base[i] + amp * c;
      if (be < 1) {
        const float f = fromLive ? from.base[i] + from.amp[i] * cosf(from.freq * t + from.phase[i]) : fromAngle[i];
        a = f + (a - f) * be;
      }
      setLeg(GAIT_LEG[i], a);
    }
  }

  // One leg to a built-in angle: clamped to its range, mapped onto the calibration, sent out
  void setLeg(uint8_t leg, float a) {
    const float lo = fminf(LEG_MIN0[leg], LEG_MAX0[leg]), hi = fmaxf(LEG_MIN0[leg], LEG_MAX0[leg]);
    a = fminf(hi, fmaxf(lo, a));
    angle[leg] = a;
    writeUs(leg, angleToUs(calPiece(a, LEG_MID0[leg], LEG_MIN0[leg], LEG_MAX0[leg], cal.mid[leg], cal.min[leg], cal.max[leg])));
  }

  float legAngle(uint8_t leg) const { return angle[leg]; }
  bool walking() const { return running; }
  // The gait's phase now (rad, 0..2pi) and cycle length (ms)
  float phase() const { float p = fmodf(cur.freq * (float)t, 6.2831853f); return p < 0 ? p + 6.2831853f : p; }
  float periodMs() const { return 6.2831853f / fabsf(cur.freq) / speed; }

 private:
  Gait cur = WALK_FORWARD, from = WALK_FORWARD;
  bool running = false, fromLive = false;
  double t = 0;
  uint32_t lastMs = 0, blendMs = 0, startMs = 0;
  float fromAngle[8] = {0};
  float angle[8] = {23.0, 115.0, 23.0, 39.0, -8.0, 107.0, 20.0, 132.0};
};

}  // namespace oreo
