// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
#pragma once

#include <Arduino.h>

enum ServoName : uint8_t {
  R1 = 0, 
  R2 = 1,
  L1 = 2,
  L2 = 3,
  R4 = 4,
  R3 = 5,
  L3 = 6,
  L4 = 7
};

const String ServoNames[]={"R1","R2","L1","L2","R4","R3","L3","L4"};

inline int servoNameToIndex(const String& servo) {
  if (servo == "L1") return L1;
  if (servo == "L2") return L2;
  if (servo == "L3") return L3;
  if (servo == "L4") return L4;
  if (servo == "R1") return R1;
  if (servo == "R2") return R2;
  if (servo == "R3") return R3;
  if (servo == "R4") return R4;
  return -1;
}

enum FaceAnimMode : uint8_t {
  FACE_ANIM_LOOP = 0,
  FACE_ANIM_ONCE = 1,
  FACE_ANIM_BOOMERANG = 2
};

// External globals and helpers used by movement/pose sequences
extern int frameDelayFB;
extern int frameDelayLR;
extern int walkCycles;
extern String currentCommand;

extern void setServoAngle(uint8_t channel, int angle);
extern void setHeadServoAngle(uint8_t pin, int angle);
extern void setHeadServoF(uint8_t pin, float angle);  // main.cpp: sub-degree precision (no 1-degree steps)
extern void playSfx(const char* prog);  // main.cpp: a sound ("w:name|synth" = /sfx/name.wav or the synth)
extern bool legsPowered;    // main.cpp: the leg servos are driven (false: limp)
extern float currentAngle[8];  // main.cpp: where each leg servo was last sent
extern bool legsPoseKnown;  // main.cpp: we know where they are (false at power-up)
extern void moveSmoothHelper(int duration, int r1, int r2, int l1, int l2, int r4, int r3, int l3, int l4);
extern void setFace(const String& faceName);
extern void setFaceMode(FaceAnimMode mode);
extern void setFaceWithMode(const String& faceName, FaceAnimMode mode);
extern void delayWithFace(unsigned long ms);
extern void legsRelax();
extern void enterIdle();
extern bool pressingCheck(String cmd, int ms);

// Pose/animation prototypes
void runRestPose();
void runStandPose(int face = 1);
void runWavePose();
void runDancePose();
void runSwimPose();
void runPointPose();
void runPushupPose();
void runBowPose();
void runCutePose();
void runFreakyPose();
void runWormPose();
void runShakePose();
void runShrugPose();
void runDeadPose();
void runCrabPose();
void runWalkPose();
void runWalkBackward();
void runTurnLeft();
void runTurnRight();
void runNodYes();
void runNodNo();

// ====== POSES ======
inline void runRestPose() { 
  Serial.println(F("REST")); 
  setFaceWithMode("rest", FACE_ANIM_BOOMERANG); 
  moveSmoothHelper(500, -16, 156, 64, 0, REST_BACK[0], REST_BACK[1], REST_BACK[2], REST_BACK[3]);  // (back legs: main.cpp)
}

extern int POSE_STAND[8];  // (below: the calibrated middles - /servoCal)
inline void runStandPose(int face) {
  Serial.println(F("STAND"));
  if (face == 1) setFaceWithMode("stand", FACE_ANIM_ONCE);
  if (!legsPowered && legsPoseKnown) {  // relaxed after "rest" (so lying): rise smoothly
    moveSmoothHelper(900, POSE_STAND[0], POSE_STAND[1], POSE_STAND[2], POSE_STAND[3], POSE_STAND[4], POSE_STAND[5], POSE_STAND[6], POSE_STAND[7]);
    if (face == 1) enterIdle();
    return;
  }
  // Eased (350 ms), not all 8 servos at once at full speed. Already standing: nothing to do.
  const int* ST = POSE_STAND;  // R1 R2 L1 L2 R4 R3 L3 L4 (the calibration page's middles)
  bool there = legsPowered && legsPoseKnown;
  for (int i = 0; i < 8 && there; i++) there = fabsf(currentAngle[i] - ST[i]) <= 1;
  if (!there) moveSmoothHelper(350, ST[0], ST[1], ST[2], ST[3], ST[4], ST[5], ST[6], ST[7]);
  if (face == 1) enterIdle();
}

int PAT_BRAIN = 1;  // head pats: the dynamic pat brain (setting "patbrain"; 0 = a simple hold-for-a-second "patted")
int WAVE_GYRO = 1;  // the wave checks its lean with the gyro (setting "wavegyro"; 0 = the fixed lean)
extern bool imuOk;
extern bool imuReadRaw(float acc[3], float gyro[3]);
extern int REST_BACK[4];
// The body's pitch right now, read straight from the IMU (deg, + = nose down; an emote blocks the
// loop, so the loop's IMU values don't move while it runs). NAN: no IMU.
inline float emoPitchNow() {
  if (!imuOk) return NAN;
  float a[3], g[3], sum = 0;
  int n = 0;
  for (int k = 0; k < 4; k++) {  // (a few reads: the servos shake it)
    if (imuReadRaw(a, g)) { sum += asinf(constrain(a[1], -1.0f, 1.0f)) * 57.2958f; n++; }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return n ? sum / n : NAN;
}

// The body's roll right now (deg), like emoPitchNow
inline float emoRollNow() {
  if (!imuOk) return NAN;
  float a[3], g[3];
  if (!imuReadRaw(a, g)) return NAN;
  return asinf(constrain(a[0], -1.0f, 1.0f)) * 57.2958f;
}
int WAVE_R4 = 62;  // how far R4 (back-right knee) goes down for the lean (setting "waver4")
// R4 alone rocks it on the front-right / back-left diagonal, so at the same time R2 (back-right hip) goes a
// little toward its tucked min and L4 (back-left knee) a little down - the whole back sinks and the weight goes
// onto the back-right leg. Percent of the way (R2: stand -> min, L4: stand -> lying). Settings "waver2" / "wavel4".
int WAVE_R2_PCT = 40;  // (40 / 40 with R4 62 holds it steady; 0 / 0 rocks it)
int WAVE_L4_PCT = 40;

// The wave's stance, for either hand. Left (side 0): R4 (back-right knee)
// down to WAVE_R4, R2 (back-right hip) WAVE_R2_PCT % toward its min, L4 (back-left knee) WAVE_L4_PCT % toward
// lying, R1 forward (-7), L2 back (-26) - then the front-left foot up (L3 to 140: a foot lifts with its knee toward
// its MIN). Right (side 1) is its mirror image (the same fractions on the other legs). Built-in angles: r1 r2 l1 l2 r4 r3 l3 l4.
inline void waveLean(int side, int ms) {
  const float pr = WAVE_R2_PCT / 100.0f, pl = WAVE_L4_PCT / 100.0f;
  const float fR4 = (WAVE_R4 - POSE_STAND[4]) / (float)(REST_BACK[0] - POSE_STAND[4]);  // ~0.88 toward lying
  if (side == 0) {
    const int r2 = (int)lroundf(POSE_STAND[1] + pr * (80 - POSE_STAND[1]));
    const int l4 = (int)lroundf(POSE_STAND[7] + pl * (REST_BACK[3] - POSE_STAND[7]));
    moveSmoothHelper(ms, -7, r2, POSE_STAND[2], -26, WAVE_R4, POSE_STAND[5], POSE_STAND[6], l4);
  } else {
    const int l2 = (int)lroundf(POSE_STAND[3] + pr * (76 - POSE_STAND[3]));
    const int r4 = (int)lroundf(POSE_STAND[4] + pl * (REST_BACK[0] - POSE_STAND[4]));
    const int l4 = (int)lroundf(POSE_STAND[7] + fR4 * (REST_BACK[3] - POSE_STAND[7]));
    moveSmoothHelper(ms, POSE_STAND[0], 150, 55, l2, r4, POSE_STAND[5], POSE_STAND[6], l4);
  }
}
// the raised foot: up = 1 (high), 0 = the wave's low point; the knee of that side only
inline int waveKnee(int side, float up) {
  // left: L3 60..140 (stand 20, min 150); right: the same fractions toward R3's min (-18) from its stand (107)
  const float fl = (60 + up * 80 - POSE_STAND[6]) / (150.0f - POSE_STAND[6]);
  return side == 0 ? (int)lroundf(60 + up * 80) : (int)lroundf(POSE_STAND[5] + fl * (-18 - POSE_STAND[5]));
}
inline void waveFoot(int side, int ms, float up) {
  if (side == 0) moveSmoothHelper(ms, -999, -999, -999, -999, -999, -999, waveKnee(0, up), -999);
  else moveSmoothHelper(ms, -999, -999, -999, -999, -999, waveKnee(1, up), -999, -999);
}

// Wave: with one hand, then smoothly over to the other and wave with that one.
// The lean first and settled, then the foot up; down again before the weight moves across.
inline void runWavePose(bool both = false) {  // one hand (the left), or both in turn
  const char* c = both ? "wave2" : "wave";
  Serial.println(F("WAVE"));
  setFaceWithMode("wave", FACE_ANIM_ONCE);
  runStandPose(0);
  delayWithFace(150);
  const int first = both ? random(2) : 0;
  for (int k = 0; k < (both ? 2 : 1); k++) {
    const int side = k ? 1 - first : first;
    waveLean(side, 450);                       // the weight over to the other side, slowly
    waveFoot(side, 300, 1.0f);                 // the hand up - straight after the lean
    for (int i = 0; i < 3; i++) {
      if (currentCommand != c) { runStandPose(1); return; }
      waveFoot(side, 280, 0.0f);
      waveFoot(side, 280, 1.0f);
    }
    if (WAVE_GYRO) xlogSys("WAVE %s hand: waving pitch %.1f roll %.1f", side ? "right" : "left", emoPitchNow(), emoRollNow());
    // the hand down before the weight moves across (a foot in the air while shifting: it would tip)
    moveSmoothHelper(300, -999, -999, -999, -999, -999, POSE_STAND[5], POSE_STAND[6], -999);
    if (k == 0 && both) {
      setFace("happy");
      moveSmoothHelper(350, POSE_STAND[0], POSE_STAND[1], POSE_STAND[2], POSE_STAND[3], POSE_STAND[4], POSE_STAND[5], POSE_STAND[6], POSE_STAND[7]);
      setFace("wave");
    }
  }
  runStandPose(1);
  if (currentCommand == c) currentCommand = "";
}



inline void runPattedPose() {
  Serial.println(F("PATTED"));
  setFaceWithMode("cute", FACE_ANIM_LOOP);
  // moveSmoothHelper(dur, r1, r2, l1, l2, r4, r3, l3, l4)
  moveSmoothHelper(500, 62, 115, -18, 39, 64, 107, 20, 64);
}

void runDanceEmote();  // (below with the other emotes: it needs their helpers)
inline void runDancePose() { runDanceEmote(); }

inline void runSwimPose() { 
  Serial.println(F("SWIM")); 
  setFaceWithMode("swim", FACE_ANIM_ONCE); 
  moveSmoothHelper(500, -16, 156, 64, 0, 64, 50, 72, 64);
  for (int i = 0; i < 4; i++) { 
    moveSmoothHelper(400, 23, 115, 23, 39, -999, -999, -999, -999);
    moveSmoothHelper(400, -16, 156, 64, 0, -999, -999, -999, -999);
  } 
  runStandPose(1); 
  if (currentCommand == "swim") currentCommand = "";
}

inline void runPointPose() {
  Serial.println(F("POINT"));
  setFaceWithMode("point", FACE_ANIM_BOOMERANG);
  // eased into the pose (not all 8 at full speed)
  moveSmoothHelper(450, 23, 165, 5, -26, 56, 101, 104, 132);  // r1 r2 l1 l2 r4 r3 l3 l4
  delayWithFace(1550);
  runStandPose(1);
  if (currentCommand == "point") currentCommand = "";
}

inline void runPushupPose() {
  Serial.println(F("PUSHUP"));
  setFaceWithMode("pushup", FACE_ANIM_ONCE);
  runStandPose(0);
  delayWithFace(200);
  moveSmoothHelper(350, 62, -999, -18, -999, -999, 50, 72, -999);
  delayWithFace(150);
  for (int i = 0; i < 4; i++) {  // each push eased, the same rhythm (600 / 500 ms)
    moveSmoothHelper(300, -999, -999, -999, -999, -999, 107, 20, -999);
    delayWithFace(300);
    moveSmoothHelper(300, -999, -999, -999, -999, -999, 50, 72, -999);
    delayWithFace(200);
  }
  runStandPose(1);
  if (currentCommand == "pushup") currentCommand = "";
}

inline void runBowPose() {
  Serial.println(F("BOW"));
  setFaceWithMode("bow", FACE_ANIM_ONCE);
  runStandPose(0); 
  delayWithFace(200);
  moveSmoothHelper(600, 62, 74, -18, 78, -8, 107, 20, 132);
  moveSmoothHelper(1000, -999, -999, -999, -999, -999, 50, 72, -999);
  delayWithFace(2000);
  runStandPose(1);
  if (currentCommand == "bow") currentCommand = "";
}

inline void runCutePose() {
  Serial.println(F("CUTE"));
  setFaceWithMode("cute", FACE_ANIM_ONCE);
  runStandPose(0); 
  delayWithFace(200);
  // Lie down gently: front legs first, then the back ones (all at once = a belly flop)
  // moveSmoothHelper(dur, r1, r2, l1, l2, r4, r3, l3, l4)
  moveSmoothHelper(700, 62, 92, -18, 61, -999, -999, -999, -999);
  moveSmoothHelper(800, -999, -999, -999, -999, 136, -7, 124, -4);
  delayWithFace(200);
  for (int i = 0; i < 5; i++) {  // the wiggle, eased (same 300 ms rhythm)
    moveSmoothHelper(200, -999, -999, -999, -999, 136, -999, -999, 30);
    delayWithFace(100);
    moveSmoothHelper(200, -999, -999, -999, -999, 100, -999, -999, -4);
    delayWithFace(100);
  }
  moveSmoothHelper(800, POSE_STAND[0], POSE_STAND[1], POSE_STAND[2], POSE_STAND[3], POSE_STAND[4], POSE_STAND[5], POSE_STAND[6], POSE_STAND[7]);  // get up gently too
  runStandPose(1);
  if (currentCommand == "cute") currentCommand = "";
}

inline void runFreakyPose() {
  Serial.println(F("FREAKY"));
  setFaceWithMode("freaky", FACE_ANIM_ONCE);
  runStandPose(0); 
  delayWithFace(200);
  moveSmoothHelper(800, 62, 74, -18, 78, 64, -7, -999, -999);
  for (int i = 0; i < 3; i++) {
    moveSmoothHelper(800, -999, -999, -999, -999, -999, 9, -999, -999);
    moveSmoothHelper(800, -999, -999, -999, -999, -999, -7, -999, -999);
  }
  runStandPose(1);
  if (currentCommand == "freaky") currentCommand = "";
}

inline void runWormPose() {
  Serial.println(F("WORM"));
  setFaceWithMode("worm", FACE_ANIM_ONCE);
  runStandPose(0);
  delayWithFace(200);
  moveSmoothHelper(400, 62, 74, -18, 78, 64, 50, 72, 64);
  for(int i=0; i<5; i++) {
    moveSmoothHelper(300, -999, -999, -999, -999, 28, 22, 98, 98);
    moveSmoothHelper(300, -999, -999, -999, -999, 100, 78, 46, 30);
  }
  runStandPose(1);
  if (currentCommand == "worm") currentCommand = "";
}

inline void runShakePose() {
  Serial.println(F("SHAKE"));
  setFaceWithMode("shake", FACE_ANIM_ONCE);
  runStandPose(0);
  delayWithFace(200);
  moveSmoothHelper(300, 23, 156, 23, 0, -999, 50, 72, -999);
  delayWithFace(200);
  for (int i = 0; i < 5; i++) {  // each shake eased, the same 300 ms rhythm
    moveSmoothHelper(200, -999, -999, -999, -999, 28, -999, -999, 98);
    delayWithFace(100);
    moveSmoothHelper(200, -999, -999, -999, -999, -8, -999, -999, 132);
    delayWithFace(100);
  }
  runStandPose(1);
  if (currentCommand == "shake") currentCommand = "";
}

inline void runShrugPose() {
  Serial.println(F("SHRUG"));
  runStandPose(0);
  setFaceWithMode("dead", FACE_ANIM_ONCE);
  delayWithFace(200);
  moveSmoothHelper(500, -999, -999, -999, -999, 64, 50, 72, 64);
  delayWithFace(500);
  setFaceWithMode("shrug", FACE_ANIM_ONCE);
  moveSmoothHelper(500, -999, -999, -999, -999, 136, -7, 124, -4);
  delayWithFace(1000);
  runStandPose(1);
  if (currentCommand == "shrug") currentCommand = "";
}

inline void runDeadPose() {
  Serial.println(F("DEAD"));
  runStandPose(0);
  setFaceWithMode("dead", FACE_ANIM_BOOMERANG);
  delayWithFace(200);
  moveSmoothHelper(600, -999, -999, -999, -999, 64, 50, 72, 64);
  if (currentCommand == "dead") currentCommand = "";
}

inline void runCrabPose() {
  Serial.println(F("CRAB"));
  setFaceWithMode("crab", FACE_ANIM_ONCE);
  runStandPose(0);
  delayWithFace(200);
  moveSmoothHelper(350, -16, 156, 64, 0, -8, 107, 46, 98);
  for (int i = 0; i < 5; i++) {  // each step eased, the same 300 ms rhythm
    moveSmoothHelper(220, -999, -999, -999, -999, 28, 78, 20, 132);
    delayWithFace(80);
    moveSmoothHelper(220, -999, -999, -999, -999, -8, 107, 46, 98);
    delayWithFace(80);
  }
  runStandPose(1);
  if (currentCommand == "crab") currentCommand = "";
}

// ====== MORE EMOTES: body + head + face ======
// Built from poses the robot already does well (stand, crouch = partway to lying down,
// the bow's front, the sit from "dead") plus the head servos and the expression faces.
// Head (calibrated): tilt pin 5, 59..150, + = down, mid 99; pan pin 7, -18..150, mid 60.
// They stop early - and stand up - when STOP or another command comes in.
#define EMO_TILT_PIN headTiltCh  // (servo_map.h)
#define EMO_PAN_PIN headPanCh
// The head's calibration (main.cpp, set on the page): straight ahead and the limits
extern float PAN_MIN, PAN_MID, PAN_MAX, TILT_MIN, TILT_MID, TILT_MAX;
#define EMO_PAN_MID ((int)lroundf(PAN_MID))
#define EMO_TILT_MID ((int)lroundf(TILT_MID))
int POSE_STAND[8] = {23, 115, 23, 39, -8, 107, 20, 132};  // r1 r2 l1 l2 r4 r3 l3 l4 (= the calibrated middles: /servoCal)
const int POSE_REST[8] = {-16, 150, 64, 0, 64, 50, 72, 64};     // lying down
int emoPanNow = EMO_PAN_MID, emoTiltNow = EMO_TILT_MID;

// How big the custom emotes move: head offsets from straight ahead and leg offsets from the
// standing pose are scaled by this (1 = as designed). Servo limits still apply (setServoAngle,
// head min / max); crouching never goes past lying down. Setting "emoamp" on the page.
float EMO_AMP = 1.2f;
inline int emoScaleLeg(int i, int angle) {
  if (angle == -999) return -999;  // moveSmoothHelper: joint unchanged
  const int st = POSE_STAND[i], r = POSE_REST[i];
  const float v = st + (angle - st) * EMO_AMP;
  // Toward lying down: never past it (the sit / crouch targets are close to it already)
  if ((angle - st) * (r - st) > 0 && fabsf(v - st) > fabsf((float)(r - st)))
    return abs(angle - st) >= abs(r - st) ? angle : r;
  return (int)lroundf(v);
}
// moveSmoothHelper with the movement scaled (r1 r2 l1 l2 r4 r3 l3 l4, -999 = unchanged)
inline void emoLegs(int ms, int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7) {
  moveSmoothHelper(ms, emoScaleLeg(0, a0), emoScaleLeg(1, a1), emoScaleLeg(2, a2), emoScaleLeg(3, a3),
                   emoScaleLeg(4, a4), emoScaleLeg(5, a5), emoScaleLeg(6, a6), emoScaleLeg(7, a7));
}

// The head never all the way to its limits: a servo pressed against the hard stop stalls (current,
// heat, the horn screw works loose). 6 deg off the bottom (the hard one), 3 off the rest.
#define EMO_PAN_LO ((int)PAN_MIN + 3)
#define EMO_PAN_HI ((int)PAN_MAX - 3)
#define EMO_TILT_LO ((int)TILT_MIN + 3)
#define EMO_TILT_HI ((int)TILT_MAX - 6)
// Head to an offset from the middle (+ tilt = down). Small steps (the continuous motions) go
// straight; a bigger jump is a quick eased move (~6 ms a degree, 60-180 ms) - an instant jump
// jerks the pan servo.
extern const char* panWho;  // (main.cpp: who moved the pan last, for the PANJUMP log)
inline void emoHead(int panOff, int tiltOff) {
  const int p1 = constrain(EMO_PAN_MID + (int)lroundf(panOff * EMO_AMP), EMO_PAN_LO, EMO_PAN_HI);
  const int t1 = constrain(EMO_TILT_MID + (int)lroundf(tiltOff * EMO_AMP), EMO_TILT_LO, EMO_TILT_HI);
  const int p0 = emoPanNow, t0 = emoTiltNow;
  panWho = "emote";
  const int big = max(abs(p1 - p0), abs(t1 - t0));
  if (big > 4) {
    const int ms = constrain(big * 6, 60, 180), steps = max(3, ms / 10);
    for (int s = 1; s < steps; s++) {
      const float e = 0.5f - 0.5f * cosf(PI * s / steps);
      setHeadServoF(EMO_PAN_PIN, p0 + (p1 - p0) * e);
      setHeadServoF(EMO_TILT_PIN, t0 + (t1 - t0) * e);
      delayWithFace(10);
    }
  }
  emoPanNow = p1;
  emoTiltNow = t1;
  setHeadServoAngle(EMO_PAN_PIN, emoPanNow);
  setHeadServoAngle(EMO_TILT_PIN, emoTiltNow);
}

// Wait with the face animating; false if the emote was cancelled
inline bool emoWait(const char* cmd, int ms) {
  const unsigned long start = millis();
  while (millis() - start < (unsigned long)ms) {
    if (currentCommand != cmd) return false;
    delayWithFace(10);
  }
  return currentCommand == cmd;
}

// Smooth head move; false if cancelled
inline bool emoHeadTo(const char* cmd, int panOff, int tiltOff, int ms) {
  const int p0 = emoPanNow, t0 = emoTiltNow;
  const int p1 = constrain(EMO_PAN_MID + (int)lroundf(panOff * EMO_AMP), EMO_PAN_LO, EMO_PAN_HI);
  const int t1 = constrain(EMO_TILT_MID + (int)lroundf(tiltOff * EMO_AMP), EMO_TILT_LO, EMO_TILT_HI);
  const int steps = max(1, ms / 20);
  for (int s = 1; s <= steps; s++) {  // eased in and out (cosine), sub-degree writes
    const float e = 0.5f - 0.5f * cosf(PI * s / steps);
    const float pf = p0 + (p1 - p0) * e, tf = t0 + (t1 - t0) * e;
    emoPanNow = (int)lroundf(pf);
    emoTiltNow = (int)lroundf(tf);
    setHeadServoF(EMO_PAN_PIN, pf);
    setHeadServoF(EMO_TILT_PIN, tf);
    if (!emoWait(cmd, 20)) return false;
  }
  return true;
}

inline void emoPose(int ms, const int* p) {
  if (p == POSE_STAND || p == POSE_REST) {  // the end poses themselves: as they are
    moveSmoothHelper(ms, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
    return;
  }
  emoLegs(ms, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
}

// k = 0 standing ... 1 lying down; in between = crouching
inline void emoCrouch(float k, int ms) {
  k = min(k * EMO_AMP, 0.9f);  // deeper, but never all the way down
  int t[8];
  for (int i = 0; i < 8; i++) t[i] = (int)lroundf(POSE_STAND[i] + k * (POSE_REST[i] - POSE_STAND[i]));
  emoPose(ms, t);
}

// Head back to the middle, stand, done
inline void emoFinish(const char* cmd) {
  const int p0 = emoPanNow, t0 = emoTiltNow;
  for (int s = 1; s <= 10; s++) {
    setHeadServoAngle(EMO_PAN_PIN, p0 + (EMO_PAN_MID - p0) * s / 10);
    setHeadServoAngle(EMO_TILT_PIN, t0 + (EMO_TILT_MID - t0) * s / 10);
    delayWithFace(25);
  }
  emoPanNow = EMO_PAN_MID;
  emoTiltNow = EMO_TILT_MID;
  runStandPose(1);
  if (currentCommand == cmd) currentCommand = "";
}

// Curious: looks to one side, a little "hm?" bob, slowly over to the other side, peeks
// down there, back to the middle - "oh!" - and a happy double nod
inline void runCuriousEmote() {
  const char* c = "curious";
  Serial.println(F("CURIOUS"));
  runStandPose(0);
  setFaceWithMode("thinking", FACE_ANIM_ONCE);
  const int side = random(2) ? 1 : -1;
  if (!emoHeadTo(c, 45 * side, -10, 700) || !emoWait(c, 500)) return emoFinish(c);
  setFace("talk_thinking");
  if (!emoHeadTo(c, 45 * side, 4, 200) || !emoHeadTo(c, 45 * side, -10, 200)) return emoFinish(c);
  setFace("thinking");
  if (!emoWait(c, 300) || !emoHeadTo(c, -45 * side, -10, 1200) || !emoWait(c, 500)) return emoFinish(c);
  setFace("confused");
  if (!emoHeadTo(c, -45 * side, 15, 350) || !emoWait(c, 600)) return emoFinish(c);
  if (!emoHeadTo(c, 0, -12, 600)) return emoFinish(c);
  setFace("surprised");
  if (!emoWait(c, 350)) return emoFinish(c);
  setFace("happy");
  for (int i = 0; i < 2; i++)
    if (!emoHeadTo(c, 0, 10, 150) || !emoHeadTo(c, 0, -8, 150)) return emoFinish(c);
  emoWait(c, 400);
  emoFinish(c);
}

// Happy: wags its behind - dips one back leg then the other - head swinging along
inline void runHappyEmote() {
  const char* c = "happy";
  Serial.println(F("HAPPY"));
  runStandPose(0);
  setFaceWithMode("happy", FACE_ANIM_ONCE);
  for (int i = 0; i < 6; i++) {
    setFace(i % 2 ? "talk_happy" : "happy");
    emoLegs(170, -999, -999, -999, -999, 22, -999, -999, 132);
    emoHead(14, -6);
    if (!emoWait(c, 40)) return emoFinish(c);
    emoLegs(170, -999, -999, -999, -999, -8, -999, -999, 102);
    emoHead(-14, -6);
    if (!emoWait(c, 40)) return emoFinish(c);
  }
  setFace("excited");
  emoWait(c, 400);
  emoFinish(c);
}

// Excited: bouncy little crouch-hops with the head bobbing, then a quick look around
inline void runExcitedEmote() {
  const char* c = "excited";
  Serial.println(F("EXCITED"));
  runStandPose(0);
  setFaceWithMode("excited", FACE_ANIM_ONCE);
  for (int i = 0; i < 5; i++) {
    setFace(i % 2 ? "talk_excited" : "excited");
    emoCrouch(0.3f, 150);
    emoHead(0, 10);
    if (!emoWait(c, 30)) return emoFinish(c);
    emoCrouch(0.0f, 130);
    emoHead(0, -10);
    if (!emoWait(c, 60)) return emoFinish(c);
  }
  for (int i = 0; i < 2; i++)
    if (!emoHeadTo(c, 30, -5, 140) || !emoHeadTo(c, -30, -5, 200)) return emoFinish(c);
  setFace("happy");
  emoWait(c, 300);
  emoFinish(c);
}

// Stretch: a big front stretch (the bow's front, head up, yawning), then sits up tall and
// yawns again, stands and shakes it off
inline void runStretchEmote() {
  const char* c = "stretch";
  Serial.println(F("STRETCH"));
  runStandPose(0);
  setFaceWithMode("sleepy", FACE_ANIM_ONCE);
  if (!emoWait(c, 300)) return emoFinish(c);
  emoLegs(1000, 62, 80, -18, 76, -8, 107, 20, 132);  // front down and forward
  if (!emoHeadTo(c, 0, -28, 600)) return emoFinish(c);
  setFace("talk_sleepy");  // yawn
  if (!emoWait(c, 1300)) return emoFinish(c);
  setFace("sleepy");
  if (!emoWait(c, 300)) return emoFinish(c);
  emoLegs(800, 23, 115, 23, 39, -999, -999, -999, -999);  // front back up
  emoLegs(900, -999, -999, -999, -999, 64, 50, 72, 64);   // sit
  if (!emoHeadTo(c, 0, -30, 500)) return emoFinish(c);
  setFace("talk_sleepy");
  if (!emoWait(c, 1100)) return emoFinish(c);
  setFace("happy");
  emoPose(700, POSE_STAND);
  for (int i = 0; i < 3; i++)  // shake it off
    if (!emoHeadTo(c, 22, -5, 90) || !emoHeadTo(c, -22, -5, 90)) return emoFinish(c);
  emoFinish(c);
}

// Sleepy: head droops, jerks awake, a big yawn, then lies down and dozes off. Stays
// lying down (Stand gets it up again)
inline void runSleepyEmote() {
  const char* c = "sleepy";
  Serial.println(F("SLEEPY"));
  runStandPose(0);
  setFaceWithMode("sleepy", FACE_ANIM_ONCE);
  if (!emoHeadTo(c, 0, 22, 1400) || !emoWait(c, 300)) return emoFinish(c);
  emoHead(0, -2);  // jerks awake
  setFace("surprised");
  if (!emoWait(c, 400)) return emoFinish(c);
  setFace("sleepy");
  if (!emoHeadTo(c, 0, -18, 500)) return emoFinish(c);
  setFace("talk_sleepy");  // yawn
  if (!emoWait(c, 1400)) return emoFinish(c);
  setFace("sleepy");
  emoCrouch(0.5f, 900);
  if (!emoHeadTo(c, 0, 10, 600)) return emoFinish(c);
  emoPose(900, POSE_REST);
  if (!emoHeadTo(c, 10, 28, 900)) return emoFinish(c);
  setFaceWithMode("rest", FACE_ANIM_BOOMERANG);  // asleep
  delayWithFace(400);  // settled on the belly: no power to the legs while it sleeps (like Rest)
  legsRelax();
  if (currentCommand == c) currentCommand = "";
}

// Scared: startles (head up, flinch), cowers low with the head turned away and trembles,
// peeks out, then relaxes
inline void runScaredEmote() {
  const char* c = "scared";
  Serial.println(F("SCARED"));
  runStandPose(0);
  setFaceWithMode("surprised", FACE_ANIM_ONCE);
  emoHead(0, -22);
  emoCrouch(0.12f, 90);  // flinch
  if (!emoWait(c, 350)) return emoFinish(c);
  setFace("sad");
  emoCrouch(0.55f, 450);
  if (!emoHeadTo(c, 30, 26, 400)) return emoFinish(c);
  int cower[8];
  const float kc = min(0.55f * EMO_AMP, 0.9f);  // as deep as the crouch above
  for (int i = 0; i < 8; i++) cower[i] = (int)lroundf(POSE_STAND[i] + kc * (POSE_REST[i] - POSE_STAND[i]));
  for (int i = 0; i < 16; i++) {  // trembling
    const int d = (i % 2) ? 4 : -4;
    setServoAngle(R2, cower[1] + (int)lroundf(d * EMO_AMP));
    setServoAngle(L2, cower[3] - (int)lroundf(d * EMO_AMP));
    setServoAngle(R4, cower[4] + (int)lroundf(d * EMO_AMP));
    setServoAngle(L4, cower[7] - (int)lroundf(d * EMO_AMP));
    setHeadServoAngle(EMO_PAN_PIN, emoPanNow + d / 2);
    if (!emoWait(c, 60)) return emoFinish(c);
  }
  moveSmoothHelper(150, cower[0], cower[1], cower[2], cower[3], cower[4], cower[5], cower[6], cower[7]);  // already scaled
  setFace("confused");
  if (!emoHeadTo(c, 0, 8, 500) || !emoWait(c, 700)) return emoFinish(c);  // peeks out
  setFace("happy");
  emoCrouch(0.0f, 700);
  emoWait(c, 300);
  emoFinish(c);
}

// Sneeze: "ah... ah..." head slowly tilting up, then "ACHOO!" - head snaps down with a
// little body jolt - and a dazed head shake
inline void runSneezeEmote() {
  const char* c = "sneeze";
  Serial.println(F("SNEEZE"));
  runStandPose(0);
  setFaceWithMode("sneeze_ah", FACE_ANIM_ONCE);
  static const char* const build[3] = {"sneeze_ah", "sneeze_ahh", "sneeze_ahhh"};  // eyes closing more each time
  static const char* const buildSnd[3] = {"w:ah1|v0.18 400~560:280", "w:ah2|v0.22 450~660:300", "w:ah3|v0.26 500~780:340"};
  for (int i = 0; i < 3; i++) {
    setFace(build[i]);
    playSfx(buildSnd[i]);
    if (!emoHeadTo(c, 0, -8 - 7 * i, 350) || !emoWait(c, 150)) return emoFinish(c);
  }
  if (!emoWait(c, 250)) return emoFinish(c);
  setFace("sneeze");  // ACHOO: eyes screwed shut, a spray
  playSfx("w:achoo|v0.6 n:280");
  emoHead(0, 26);
  emoCrouch(0.35f, 90);
  emoCrouch(0.0f, 220);
  if (!emoWait(c, 350)) return emoFinish(c);
  setFace("sneeze_after");  // a sniffle
  if (!emoWait(c, 500)) return emoFinish(c);
  for (int i = 0; i < 2; i++)
    if (!emoHeadTo(c, 18, 4, 110) || !emoHeadTo(c, -18, 4, 110)) return emoFinish(c);
  setFace("happy");
  emoWait(c, 400);
  emoFinish(c);
}

// Love: shy - looks away and down, sways gently from side to side, then peeks back with a
// little happy bounce
inline void runLoveEmote() {
  const char* c = "love";
  Serial.println(F("LOVE"));
  runStandPose(0);
  setFaceWithMode("love", FACE_ANIM_ONCE);
  if (!emoHeadTo(c, 28, 16, 800)) return emoFinish(c);
  for (int i = 0; i < 3; i++) {
    setFace(i % 2 ? "love" : "talk_love");
    emoLegs(420, -999, -999, -999, -999, 14, -999, -999, 132);
    if (!emoHeadTo(c, 18, 14, 150)) return emoFinish(c);
    emoLegs(420, -999, -999, -999, -999, -8, -999, -999, 110);
    if (!emoHeadTo(c, 28, 16, 150)) return emoFinish(c);
  }
  emoPose(300, POSE_STAND);
  if (!emoHeadTo(c, 0, -10, 600)) return emoFinish(c);
  setFace("talk_love");
  emoCrouch(0.2f, 180);
  emoCrouch(0.0f, 180);
  if (!emoWait(c, 700)) return emoFinish(c);
  setFace("happy");
  emoWait(c, 300);
  emoFinish(c);
}

// Smooth head swings around a position (sine), each cycle smaller by `decay`.
// tiltUp scales the upward half of a tilt swing (a nod goes down more than up).
inline bool emoOscillate(const char* cmd, int basePan, int baseTilt, float panAmp, float tiltAmp,
                         int periodMs, int cycles, float decay, float tiltUp = 1.0f) {
  const int steps = max(4, periodMs / 20);
  for (int c = 0; c < cycles; c++) {
    for (int s = 1; s <= steps; s++) {
      const float ph = sinf(6.2831853f * s / steps);
      const float tp = ph > 0 ? ph : ph * tiltUp;
      emoHead(basePan + (int)lroundf(panAmp * ph), baseTilt + (int)lroundf(tiltAmp * tp));
      if (!emoWait(cmd, 20)) return false;
    }
    panAmp *= decay;
    tiltAmp *= decay;
  }
  return true;
}

// Poses between two poses (k = 0 -> a, 1 -> b)
inline void emoBlend(int* out, const int* a, const int* b, float k) {
  for (int i = 0; i < 8; i++) out[i] = (int)lroundf(a[i] + k * (b[i] - a[i]));
}
const int POSE_BOW_FRONT[8] = {62, 80, -18, 76, -8, 107, 20, 132};  // front down and forward, back standing

// Nod yes: a small lift, then three smooth nods - down more than up, each smaller - with
// a little body bob on the first, happy face
inline void runNodYes() {
  const char* c = "nodyes";
  Serial.println(F("NOD YES"));
  runStandPose(0);
  setFaceWithMode("happy", FACE_ANIM_ONCE);
  if (!emoHeadTo(c, 0, -6, 180)) return emoFinish(c);
  emoCrouch(0.06f, 120);
  if (!emoOscillate(c, 0, -3, 0, 17, 440, 1, 1.0f, 0.35f)) return emoFinish(c);
  emoCrouch(0.0f, 120);
  setFace("talk_happy");
  if (!emoOscillate(c, 0, -3, 0, 13, 400, 2, 0.7f, 0.35f)) return emoFinish(c);
  setFace("happy");
  emoWait(c, 250);
  emoFinish(c);
}

// Nod no: head a little down, three smooth side-to-side shakes, each smaller
inline void runNodNo() {
  const char* c = "nodno";
  Serial.println(F("NOD NO"));
  runStandPose(0);
  setFaceWithMode("sad", FACE_ANIM_ONCE);
  if (!emoHeadTo(c, 0, 7, 200)) return emoFinish(c);
  setFace("talk_sad");
  if (!emoOscillate(c, 0, 7, 26, 0, 480, 3, 0.75f)) return emoFinish(c);
  setFace("sad");
  emoWait(c, 300);
  emoFinish(c);
}

// Angry: lowers its front like a bull, head down, growls (fast little head shakes), paws
// the ground with each front leg, huffs, then turns its head away - hmph
inline void runAngryEmote() {
  const char* c = "angry";
  Serial.println(F("ANGRY"));
  runStandPose(0);
  setFaceWithMode("angry", FACE_ANIM_ONCE);
  int low[8];
  emoBlend(low, POSE_STAND, POSE_BOW_FRONT, min(0.4f * EMO_AMP, 1.0f));  // scaled once, placed as is
  moveSmoothHelper(500, low[0], low[1], low[2], low[3], low[4], low[5], low[6], low[7]);
  if (!emoHeadTo(c, 0, 18, 400)) return emoFinish(c);
  for (int i = 0; i < 8; i++) {  // growl
    setFace(i % 2 ? "talk_angry" : "angry");
    emoHead(i % 2 ? 6 : -6, 18);
    if (!emoWait(c, 70)) return emoFinish(c);
  }
  emoHead(0, 18);
  for (int i = 0; i < 2; i++) {  // paw the ground: right, then left
    setServoAngle(R2, low[1] - (int)lroundf(12 * EMO_AMP));
    if (!emoWait(c, 130)) return emoFinish(c);
    setServoAngle(R2, low[1]);
    if (!emoWait(c, 130)) return emoFinish(c);
    setServoAngle(L2, low[3] + (int)lroundf(12 * EMO_AMP));
    if (!emoWait(c, 130)) return emoFinish(c);
    setServoAngle(L2, low[3]);
    if (!emoWait(c, 130)) return emoFinish(c);
  }
  setFace("talk_angry");  // huff
  emoCrouch(0.15f, 80);
  moveSmoothHelper(150, low[0], low[1], low[2], low[3], low[4], low[5], low[6], low[7]);  // already scaled
  setFace("angry");
  if (!emoWait(c, 300) || !emoHeadTo(c, 38, 4, 350) || !emoWait(c, 700)) return emoFinish(c);  // hmph
  emoPose(500, POSE_STAND);
  emoFinish(c);
}

// Sad: sinks down slowly with the head drooping, a big sigh, glances away, then slowly
// gets up again
inline void runSadEmote() {
  const char* c = "sad";
  Serial.println(F("SAD"));
  runStandPose(0);
  setFaceWithMode("sad", FACE_ANIM_ONCE);
  emoCrouch(0.35f, 1200);
  if (!emoHeadTo(c, 0, 22, 900) || !emoWait(c, 400)) return emoFinish(c);
  setFace("talk_sad");  // sigh
  emoCrouch(0.45f, 700);
  emoCrouch(0.3f, 900);
  setFace("sad");
  if (!emoHeadTo(c, -25, 26, 1200) || !emoWait(c, 800)) return emoFinish(c);
  if (!emoHeadTo(c, 0, 20, 800) || !emoWait(c, 300)) return emoFinish(c);
  emoCrouch(0.0f, 1000);
  emoFinish(c);
}

// Think: looks up to one side and ponders (slow head sway, "hmm"), then - idea! - perks
// up with a little hop and a quick double nod
inline void runThinkEmote() {
  const char* c = "think";
  Serial.println(F("THINK"));
  runStandPose(0);
  setFaceWithMode("thinking", FACE_ANIM_ONCE);
  const int side = random(2) ? 1 : -1;
  if (!emoHeadTo(c, 30 * side, -18, 700)) return emoFinish(c);
  for (int i = 0; i < 3; i++) {
    setFace(i % 2 ? "thinking" : "talk_thinking");
    if (!emoHeadTo(c, 22 * side, -13, 450) || !emoHeadTo(c, 34 * side, -20, 450)) return emoFinish(c);
  }
  if (!emoWait(c, 300)) return emoFinish(c);
  setFace("surprised");  // idea!
  emoHead(0, -22);
  emoCrouch(0.2f, 120);
  emoCrouch(0.0f, 120);
  setFace("excited");
  if (!emoWait(c, 200) || !emoOscillate(c, 0, -10, 0, 12, 300, 2, 0.8f, 0.4f)) return emoFinish(c);
  setFace("happy");
  emoWait(c, 300);
  emoFinish(c);
}

// Peekaboo: crouches with its head down and turned away (hiding), peeks once, hides again,
// then pops up - boo! - with a happy little hop
inline void runPeekabooEmote() {
  const char* c = "peekaboo";
  Serial.println(F("PEEKABOO"));
  runStandPose(0);
  setFaceWithMode("sleepy", FACE_ANIM_ONCE);  // eyes shut: hiding
  emoCrouch(0.6f, 600);
  if (!emoHeadTo(c, 40, 45, 500) || !emoWait(c, 900)) return emoFinish(c);
  setFace("thinking");  // peek
  if (!emoHeadTo(c, 20, 22, 250) || !emoWait(c, 450)) return emoFinish(c);
  setFace("sleepy");
  if (!emoHeadTo(c, 40, 45, 200) || !emoWait(c, 700)) return emoFinish(c);
  setFace("surprised");  // boo!
  emoHead(0, -20);
  emoCrouch(0.0f, 180);
  setFace("excited");
  emoCrouch(0.25f, 120);
  emoCrouch(0.0f, 120);
  setFace("happy");
  emoWait(c, 500);
  emoFinish(c);
}

// Dizzy: the head goes round in circles, the body wobbles, then it shakes it off
inline void runDizzyEmote() {
  const char* c = "dizzy";
  Serial.println(F("DIZZY"));
  runStandPose(0);
  setFaceWithMode("confused", FACE_ANIM_ONCE);
  const int steps = 3 * 800 / 20;  // three circles, 0.8 s each
  for (int s = 0; s < steps; s++) {
    const float a = 6.2831853f * s / 40.0f;
    emoHead((int)lroundf(30 * cosf(a)), (int)lroundf(-4 + 14 * sinf(a)));
    if (s % 20 == 0) setFace((s / 20) % 2 ? "talk_confused" : "confused");
    if (!emoWait(c, 20)) return emoFinish(c);
  }
  for (int i = 0; i < 3; i++) {  // wobble
    emoLegs(300, -999, -999, -999, -999, 20, -999, -999, 132);
    emoLegs(300, -999, -999, -999, -999, -8, -999, -999, 104);
  }
  emoPose(200, POSE_STAND);
  setFace("happy");  // shake it off
  if (!emoOscillate(c, 0, 0, 22, 0, 220, 3, 0.8f)) return emoFinish(c);
  emoFinish(c);
}

// Hiccup: little surprised jolts at uneven intervals
inline void runHiccupEmote() {
  const char* c = "hiccup";
  Serial.println(F("HICCUP"));
  runStandPose(0);
  setFaceWithMode("confused", FACE_ANIM_ONCE);
  for (int i = 0; i < 4; i++) {
    if (!emoWait(c, random(500, 1100))) return emoFinish(c);
    setFace("surprised");  // hic!
    emoHead(0, -10);
    emoCrouch(0.18f, 70);
    emoCrouch(0.0f, 110);
    if (!emoHeadTo(c, 0, 0, 150)) return emoFinish(c);
    setFace("confused");
  }
  setFace("happy");
  emoWait(c, 500);
  emoFinish(c);
}

// --- MOVEMENT ANIMATIONS ---
inline void runWalkPose() {
  Serial.println(F("WALK FWD"));
  setFaceWithMode("walk", FACE_ANIM_ONCE);
  // Initial Step
  setServoAngle(R3, 78); setServoAngle(L3, 46);
  setServoAngle(R2, 165); setServoAngle(L1, 5);
  if (!pressingCheck("forward", 100)) return;
  
  for (int i = 0; i < walkCycles; i++) {
    setServoAngle(R3, 78); setServoAngle(L3, 20);
    if (!pressingCheck("forward", frameDelayFB)) return;
    setServoAngle(L4, 98); setServoAngle(L2, 0);
    setServoAngle(R4, -8); setServoAngle(R1, 62);
    if (!pressingCheck("forward", frameDelayFB)) return;    
    setServoAngle(R2, 115); setServoAngle(L1, 64);
    if (!pressingCheck("forward", frameDelayFB)) return;
    setServoAngle(R4, 28); setServoAngle(L4, 132);
    if (!pressingCheck("forward", frameDelayFB)) return;
    setServoAngle(R3, 107); setServoAngle(L3, 46);
    setServoAngle(R2, 156); setServoAngle(L1, -18);
    if (!pressingCheck("forward", frameDelayFB)) return;  
    setServoAngle(L2, 39); setServoAngle(R1, -16);
    if (!pressingCheck("forward", frameDelayFB)) return;
  }
  runStandPose(1);
}

// Logic reversed from Walk
inline void runWalkBackward() {
  Serial.println(F("WALK BACK"));
  setFaceWithMode("walk", FACE_ANIM_ONCE);
  if (!pressingCheck("backward", frameDelayFB)) return;
  
  for (int i = 0; i < walkCycles; i++) {
    setServoAngle(R3, 78); setServoAngle(L3, 20);
    if (!pressingCheck("backward", frameDelayFB)) return;
    setServoAngle(L4, 98); setServoAngle(L2, 39);
    setServoAngle(R4, -8); setServoAngle(R1, -16);
    if (!pressingCheck("backward", frameDelayFB)) return;    
    setServoAngle(R2, 156); setServoAngle(L1, -18);
    if (!pressingCheck("backward", frameDelayFB)) return;
    setServoAngle(R4, 28); setServoAngle(L4, 132);
    if (!pressingCheck("backward", frameDelayFB)) return;
    setServoAngle(R3, 107); setServoAngle(L3, 46);
    setServoAngle(R2, 115); setServoAngle(L1, 64);
    if (!pressingCheck("backward", frameDelayFB)) return;  
    setServoAngle(L2, 0); setServoAngle(R1, 62);
    if (!pressingCheck("backward", frameDelayFB)) return;
  }
  runStandPose(1);
}

// Simple turn logic
inline void runTurnLeft() {
  Serial.println(F("TURN LEFT"));
  setFaceWithMode("walk", FACE_ANIM_ONCE);
  for (int i = 0; i < walkCycles; i++) {
    //legset 1 (R1 L2)
    setServoAngle(R3, 78); setServoAngle(L4, 75); 
    if (!pressingCheck("left", frameDelayLR)) return;
    setServoAngle(R1, 62); setServoAngle(L2, 78); 
    if (!pressingCheck("left", frameDelayLR)) return;
    setServoAngle(R3, 107); setServoAngle(L4, 132); 
    if (!pressingCheck("left", frameDelayLR)) return;
    setServoAngle(R1, 23); setServoAngle(L2, 39);
    if (!pressingCheck("left", frameDelayLR)) return;
      //legset 2 (R2 L1)
    setServoAngle(R4, 50); setServoAngle(L3, 46); 
    if (!pressingCheck("left", frameDelayLR)) return;
    setServoAngle(R2, 156); setServoAngle(L1, 64); 
    if (!pressingCheck("left", frameDelayLR)) return;
    setServoAngle(R4, -8); setServoAngle(L3, 20); 
    if (!pressingCheck("left", frameDelayLR)) return;
    setServoAngle(R2, 115); setServoAngle(L1, 23);
    if (!pressingCheck("left", frameDelayLR)) return;  
  }
  runStandPose(1);
}

inline void runTurnRight() {
  Serial.println(F("TURN RIGHT"));
  setFaceWithMode("walk", FACE_ANIM_ONCE);
  for (int i = 0; i < walkCycles; i++) {
    //legset 2 (R2 L1)
    setServoAngle(R4, 50); setServoAngle(L3, 46); 
    if (!pressingCheck("right", frameDelayLR)) return;
    setServoAngle(R2, 74); setServoAngle(L1, -18); 
    if (!pressingCheck("right", frameDelayLR)) return;
    setServoAngle(R4, -8); setServoAngle(L3, 20); 
    if (!pressingCheck("right", frameDelayLR)) return;
    setServoAngle(R2, 115); setServoAngle(L1, 23);
    if (!pressingCheck("right", frameDelayLR)) return;  
    //legset 1 (R1 L2)
    setServoAngle(R3, 78); setServoAngle(L4, 75); 
    if (!pressingCheck("right", frameDelayLR)) return;
    setServoAngle(R1, -16); setServoAngle(L2, 0); 
    if (!pressingCheck("right", frameDelayLR)) return;
    setServoAngle(R3, 107); setServoAngle(L4, 132); 
    if (!pressingCheck("right", frameDelayLR)) return;
    setServoAngle(R1, 23); setServoAngle(L2, 39);
    if (!pressingCheck("right", frameDelayLR)) return;
  }
  runStandPose(1);
}

// Go crazy: the head swings corner to corner. Eased from where it is, between the calibrated limits
inline void runGoCrazyPose() {
  Serial.println(F("GO CRAZY"));
  setFaceWithMode("freaky", FACE_ANIM_LOOP);
  runStandPose(0);
  auto glide = [](float p0, float t0, float p1, float t1, int ms) {
    const int steps = max(3, ms / 10);
    for (int s = 1; s <= steps; s++) {
      const float e = 0.5f - 0.5f * cosf(PI * s / steps);
      setHeadServoF(EMO_PAN_PIN, p0 + (p1 - p0) * e);
      setHeadServoF(EMO_TILT_PIN, t0 + (t1 - t0) * e);
      delayWithFace(10);
    }
    emoPanNow = (int)lroundf(p1);
    emoTiltNow = (int)lroundf(t1);
  };
  const float pl = EMO_PAN_LO, ph = EMO_PAN_HI, tl = EMO_TILT_LO, th = EMO_TILT_HI;
  glide(emoPanNow, emoTiltNow, pl, tl, 350);
  for (int i = 0; i < 3; i++) {
    glide(pl, tl, ph, th, 650);
    glide(ph, th, pl, tl, 650);
  }
  glide(pl, tl, EMO_PAN_MID, EMO_TILT_MID, 450);
  runStandPose(1);
  if (currentCommand == "gocrazy") currentCommand = "";
}

// ====== MORE EMOTES ======
// Joints (built-in angles, r1 r2 l1 l2 r4 r3 l3 l4): the four hips R1 (front right), R2 (back right),
// L1 (front left), L2 (back left); the knees R3 (front right), R4 (back right), L3 (front left), L4 (back
// left). A knee toward its lying angle lowers that corner (or lifts the foot of a leg that carries
// nothing). k = 0 standing ... 1 lying, per joint.
inline int emoK(int i, float k) { return (int)lroundf(POSE_STAND[i] + k * (POSE_REST[i] - POSE_STAND[i])); }
// all four knees: front (R3 L3) and back (R4 L4) by their own k - the body tips forward / back
inline void emoKnees(int ms, float front, float back) {
  emoLegs(ms, -999, -999, -999, -999, emoK(4, back), emoK(5, front), emoK(6, front), emoK(7, back));
}
// each corner by its own k (0 = standing, + = that corner lower): leans left / right / forward / back
inline void emoCorners(int ms, float fr, float fl, float br, float bl) {
  emoLegs(ms, -999, -999, -999, -999, emoK(4, br), emoK(5, fr), emoK(6, fl), emoK(7, bl));
}

// Yawn: sinks onto its back legs, head up, a big slow yawn, eyes heavy - then shakes it off
inline void runYawnEmote() {
  const char* c = "yawn";
  Serial.println(F("YAWN"));
  runStandPose(0);
  setFaceWithMode("sleepy", FACE_ANIM_ONCE);
  emoKnees(700, 0.0f, 0.35f);
  if (!emoHeadTo(c, 0, -26, 600)) return emoFinish(c);
  setFace("yawn");
  if (!emoWait(c, 1500)) return emoFinish(c);
  setFace("sleepy");
  if (!emoHeadTo(c, 0, 6, 500) || !emoWait(c, 300)) return emoFinish(c);
  emoKnees(500, 0, 0);
  setFace("happy");
  for (int i = 0; i < 3; i++)
    if (!emoHeadTo(c, 18, -4, 90) || !emoHeadTo(c, -18, -4, 90)) return emoFinish(c);
  emoFinish(c);
}

// Shiver: cold - quick little trembles of the whole body, the head shaking a bit
inline void runShiverEmote() {
  const char* c = "shiver";
  Serial.println(F("SHIVER"));
  runStandPose(0);
  setFaceWithMode("scared", FACE_ANIM_ONCE);
  emoKnees(300, 0.18f, 0.18f);  // hunched up
  if (!emoHeadTo(c, 0, 10, 300)) return emoFinish(c);
  // (a tremble, not a bounce: the body only a hint, the head does most of it)
  static const float SK[2] = {0.16f, 0.2f};
  static const int SP[2] = {-4, 4};
  for (int i = 0; i < 10; i++) {
    const int o = i & 1;
    emoKnees(150, SK[o], SK[1 - o]);
    emoHead(SP[o], 10);
    if (!emoWait(c, 30)) return emoFinish(c);
  }
  setFace("sad");
  emoWait(c, 400);
  emoFinish(c);
}

// Celebrate: rears back and pumps its front feet in turn, head up, overjoyed
inline void runCelebrateEmote() {
  const char* c = "celebrate";
  Serial.println(F("CELEBRATE"));
  runStandPose(0);
  setFaceWithMode("excited", FACE_ANIM_ONCE);
  emoKnees(500, 0.0f, 0.5f);  // weight onto the back
  if (!emoHeadTo(c, 0, -22, 300)) return emoFinish(c);
  for (int i = 0; i < 6; i++) {
    setFace(i % 2 ? "starstruck" : "excited");
    const bool right = i % 2;
    emoLegs(240, -999, -999, -999, -999, -999, emoK(5, right ? 0.75f : 0.0f), emoK(6, right ? 0.0f : 0.75f), -999);
    emoHead(right ? 10 : -10, -22);
    if (!emoWait(c, 60)) return emoFinish(c);
  }
  emoLegs(250, -999, -999, -999, -999, -999, emoK(5, 0), emoK(6, 0), -999);
  setFace("happy");
  emoWait(c, 300);
  emoFinish(c);
}

// Tap foot: impatient - the front right foot taps, head tilted, a bored look
inline void runTapFootEmote() {
  const char* c = "tap";
  Serial.println(F("TAP"));
  runStandPose(0);
  setFaceWithMode("bored", FACE_ANIM_ONCE);
  emoKnees(400, 0.0f, 0.15f);  // a little back, so the front foot is free to tap
  if (!emoHeadTo(c, 14, 6, 400)) return emoFinish(c);
  for (int i = 0; i < 6; i++) {
    emoLegs(150, -999, -999, -999, -999, -999, emoK(5, 0.4f), -999, -999);
    if (!emoWait(c, 40)) return emoFinish(c);
    emoLegs(150, -999, -999, -999, -999, -999, emoK(5, 0.0f), -999, -999);
    if (!emoWait(c, i == 2 ? 450 : 120)) return emoFinish(c);
  }
  setFace("angry");
  if (!emoHeadTo(c, 0, -6, 250) || !emoWait(c, 500)) return emoFinish(c);
  emoFinish(c);
}

// Bounce: quick happy hops - dips low and springs up, head bobbing the other way
inline void runBounceEmote() {
  const char* c = "bounce";
  Serial.println(F("BOUNCE"));
  runStandPose(0);
  setFaceWithMode("happy", FACE_ANIM_ONCE);
  for (int i = 0; i < 4; i++) {  // 4 soft, slow hops (fast ones look frantic)
    setFace(i % 2 ? "talk_happy" : "excited");
    emoCrouch(0.25f, 260);
    emoHead(0, 10);
    if (!emoWait(c, 40)) return emoFinish(c);
    emoCrouch(0.0f, 240);
    emoHead(0, -10);
    if (!emoWait(c, 120)) return emoFinish(c);
  }
  setFace("happy");
  emoWait(c, 300);
  emoFinish(c);
}

// Wiggle: a playful behind-wiggle - the back hips swing it side to side, the head the other way
inline void runWiggleEmote() {
  const char* c = "wiggle";
  Serial.println(F("WIGGLE"));
  runStandPose(0);
  setFaceWithMode("silly", FACE_ANIM_ONCE);
  emoKnees(350, 0.12f, 0.0f);  // a little front-down: the behind up
  for (int i = 0; i < 8; i++) {
    const float s = i % 2 ? 1 : -1;
    // R2 / L2 mirror each other: the same k swings both one way
    emoLegs(160, -999, emoK(1, 0.3f * s), -999, emoK(3, -0.3f * s), -999, -999, -999, -999);
    emoHead((int)(-12 * s), -4);
    if (!emoWait(c, 30)) return emoFinish(c);
  }
  emoLegs(200, -999, POSE_STAND[1], -999, POSE_STAND[3], -999, -999, -999, -999);
  setFace("happy");
  emoWait(c, 300);
  emoFinish(c);
}

// Sniff: nose down to the floor, little quick sniffs, moving along side to side - then "ooh!"
inline void runSniffEmote() {
  const char* c = "sniff";
  Serial.println(F("SNIFF"));
  runStandPose(0);
  setFaceWithMode("curious", FACE_ANIM_ONCE);
  emoKnees(500, 0.35f, 0.0f);  // front down
  if (!emoHeadTo(c, -25, 24, 500)) return emoFinish(c);
  for (int side = 0; side < 3; side++) {
    const int pan = side == 0 ? -25 : side == 1 ? 0 : 25;
    if (side && !emoHeadTo(c, pan, 24, 350)) return emoFinish(c);
    for (int i = 0; i < 4; i++)
      if (!emoHeadTo(c, pan, 20, 60) || !emoHeadTo(c, pan, 25, 60)) return emoFinish(c);
  }
  emoKnees(400, 0, 0);
  setFace("surprised");
  if (!emoHeadTo(c, 0, -10, 300) || !emoWait(c, 350)) return emoFinish(c);
  setFace("happy");
  emoWait(c, 300);
  emoFinish(c);
}

// Laugh: a giggly jiggle - small fast bounces with the head tipped back
inline void runLaughEmote() {
  const char* c = "laugh";
  Serial.println(F("LAUGH"));
  runStandPose(0);
  setFaceWithMode("laugh", FACE_ANIM_ONCE);
  emoKnees(300, 0.0f, 0.2f);
  if (!emoHeadTo(c, 0, -18, 250)) return emoFinish(c);
  // (tables, not ?: on floats - that made the Xtensa compiler crash: "insn does not satisfy its constraints")
  static const float FK[2] = {0.06f, 0.0f}, BK[2] = {0.26f, 0.2f};
  static const int PN[2] = {-5, 5}, TL[2] = {-12, -18};
  for (int i = 0; i < 8; i++) {
    const int o = i & 1;
    emoKnees(170, FK[o], BK[o]);
    emoHead(PN[o], TL[o]);
    if (!emoWait(c, 25)) return emoFinish(c);
  }
  setFace("happy");
  emoWait(c, 300);
  emoFinish(c);
}

// Pounce: a cat about to pounce - front down low, head down and eyes narrow, the behind wiggles...
// then it springs up tall, all excited
inline void runPounceEmote() {
  const char* c = "pounce";
  Serial.println(F("POUNCE"));
  runStandPose(0);
  setFaceWithMode("sneaky", FACE_ANIM_ONCE);
  emoKnees(600, 0.55f, 0.0f);
  if (!emoHeadTo(c, 0, 14, 400) || !emoWait(c, 300)) return emoFinish(c);
  for (int i = 0; i < 6; i++) {
    const float s = i % 2 ? 1 : -1;
    emoLegs(110, -999, emoK(1, 0.2f * s), -999, emoK(3, -0.2f * s), -999, -999, -999, -999);
    if (!emoWait(c, 20)) return emoFinish(c);
  }
  emoLegs(150, -999, POSE_STAND[1], -999, POSE_STAND[3], -999, -999, -999, -999);
  if (!emoWait(c, 250)) return emoFinish(c);
  setFace("excited");
  emoKnees(140, 0.0f, 0.0f);  // spring!
  emoHead(0, -16);
  if (!emoWait(c, 150)) return emoFinish(c);
  emoCrouch(0.25f, 120);  // the landing
  emoCrouch(0.0f, 200);
  setFace("proud");
  emoWait(c, 400);
  emoFinish(c);
}

// Look around: "where am I?" - a wide slow look to each side and up, leaning that way
inline void runLookAroundEmote() {
  const char* c = "lookaround";
  Serial.println(F("LOOKAROUND"));
  runStandPose(0);
  setFaceWithMode("curious", FACE_ANIM_ONCE);
  const int side = random(2) ? 1 : -1;
  // leaning: the knees on the side it looks to dip a little (R3 R4 = right, L3 L4 = left)
  auto lean = [&](int s, int ms) {
    const float r = s > 0 ? 0.15f : 0.0f, l = s < 0 ? 0.15f : 0.0f;
    emoLegs(ms, -999, -999, -999, -999, emoK(4, r), emoK(5, r), emoK(6, l), emoK(7, l));
  };
  lean(side, 500);
  if (!emoHeadTo(c, 55 * side, -6, 900) || !emoWait(c, 500)) return emoFinish(c);
  lean(-side, 700);
  if (!emoHeadTo(c, -55 * side, -6, 1300) || !emoWait(c, 500)) return emoFinish(c);
  lean(0, 400);
  setFace("thinking");
  if (!emoHeadTo(c, 0, -24, 700) || !emoWait(c, 600)) return emoFinish(c);
  setFace("happy");
  if (!emoHeadTo(c, 0, 0, 400)) return emoFinish(c);
  emoWait(c, 200);
  emoFinish(c);
}

// ====== SHOW EMOTES: slower, bigger, made to be watched - nothing that bobs fast ======
// (No ?: choosing between float constants inside these loops: the Xtensa compiler crashed on one.)

// Headbang: rocking out - the head nods hard on a steady beat, the front dips a little with it
inline void runHeadbangEmote() {
  const char* c = "headbang";
  Serial.println(F("HEADBANG"));
  runStandPose(0);
  setFaceWithMode("cool", FACE_ANIM_ONCE);
  emoKnees(400, 0.0f, 0.15f);
  for (int i = 0; i < 8; i++) {
    if (i == 4) setFace("excited");
    if (!emoHeadTo(c, (i & 1) ? 8 : -8, 24, 130)) return emoFinish(c);  // down on the beat
    emoKnees(130, 0.12f, 0.15f);
    if (!emoHeadTo(c, 0, -10, 190)) return emoFinish(c);  // back up
    emoKnees(150, 0.0f, 0.15f);
  }
  setFace("proud");
  emoWait(c, 400);
  emoFinish(c);
}

// Faint: woozy, sways, slowly collapses onto one side... a beat of silence - and pops back up, "just kidding!"
inline void runFaintEmote() {
  const char* c = "faint";
  Serial.println(F("FAINT"));
  runStandPose(0);
  setFaceWithMode("dizzy", FACE_ANIM_ONCE);
  for (int i = 0; i < 2; i++) {  // the sway: a slow lean one way, then the other
    emoLegs(500, -999, -999, -999, -999, emoK(4, 0.15f), emoK(5, 0.15f), emoK(6, 0.0f), emoK(7, 0.0f));
    if (!emoHeadTo(c, 15, 8, 500)) return emoFinish(c);
    emoLegs(500, -999, -999, -999, -999, emoK(4, 0.0f), emoK(5, 0.0f), emoK(6, 0.15f), emoK(7, 0.15f));
    if (!emoHeadTo(c, -15, 8, 500)) return emoFinish(c);
  }
  setFace("sleepy");
  // the collapse: slowly down onto the right side, head drooping
  emoLegs(1100, -999, -999, -999, -999, emoK(4, 0.8f), emoK(5, 0.8f), emoK(6, 0.45f), emoK(7, 0.45f));
  if (!emoHeadTo(c, 25, 30, 700) || !emoWait(c, 1300)) return emoFinish(c);
  setFace("wink_left");  // just kidding!
  emoPose(320, POSE_STAND);
  if (!emoHeadTo(c, 0, -14, 250)) return emoFinish(c);
  setFace("silly");
  emoWait(c, 700);
  emoFinish(c);
}

// Twirl: a little spin on the spot (the turn's step pattern, glided), the head swinging the other way
inline void runTwirlEmote() {
  const char* c = "twirl";
  Serial.println(F("TWIRL"));
  runStandPose(0);
  setFaceWithMode("excited", FACE_ANIM_ONCE);
  const int side = random(2) ? 1 : -1;
  if (!emoHeadTo(c, -30 * side, -8, 300)) return emoFinish(c);
  // STEP_TURN_LEFT (main.cpp): two joints a frame
  static const int16_t F[8][4] = {{R3, 78, L4, 75}, {R1, 62, L2, 78}, {R3, 107, L4, 132}, {R1, 23, L2, 39},
                                  {R4, 50, L3, 46}, {R2, 156, L1, 64}, {R4, -8, L3, 20}, {R2, 115, L1, 23}};
  static const int16_t FR[8][4] = {{R4, 50, L3, 46}, {R2, 74, L1, -18}, {R4, -8, L3, 20}, {R2, 115, L1, 23},
                                   {R3, 78, L4, 75}, {R1, -16, L2, 0}, {R3, 107, L4, 132}, {R1, 23, L2, 39}};
  for (int cyc = 0; cyc < 4; cyc++)
    for (int f = 0; f < 8; f++) {
      const int16_t* fr = side > 0 ? F[f] : FR[f];
      int t[8] = {-999, -999, -999, -999, -999, -999, -999, -999};
      t[fr[0]] = fr[1];
      t[fr[2]] = fr[3];
      moveSmoothHelper(85, t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7]);
      if (currentCommand != c) return emoFinish(c);
    }
  setFace("starstruck");
  emoPose(300, POSE_STAND);
  if (!emoHeadTo(c, 0, -14, 300) || !emoWait(c, 500)) return emoFinish(c);
  setFace("proud");
  emoWait(c, 300);
  emoFinish(c);
}

// Yoga: weight back, one front foot up and held - calm, breathing (the head slowly up and down) -
// then the other side, and a little bow
inline void runYogaEmote() {
  const char* c = "yoga";
  Serial.println(F("YOGA"));
  runStandPose(0);
  setFaceWithMode("content", FACE_ANIM_ONCE);
  for (int side = 0; side < 2; side++) {
    emoKnees(600, 0.0f, 0.45f);  // weight onto the back legs
    if (side == 0) emoLegs(600, -999, -999, -999, -999, -999, -999, emoK(6, 0.7f), -999);  // front left foot up
    else emoLegs(600, -999, -999, -999, -999, -999, emoK(5, 0.7f), -999, -999);            // front right foot up
    for (int b = 0; b < 2; b++)  // breathing
      if (!emoHeadTo(c, 0, -14, 800) || !emoHeadTo(c, 0, 0, 800)) return emoFinish(c);
    emoKnees(500, 0.0f, 0.45f);  // the foot down again
  }
  emoKnees(500, 0.0f, 0.0f);
  setFace("happy");
  emoKnees(500, 0.3f, 0.0f);  // a little bow
  if (!emoHeadTo(c, 0, 18, 500) || !emoWait(c, 400)) return emoFinish(c);
  emoKnees(400, 0.0f, 0.0f);
  emoFinish(c);
}

// Sway: a slow dreamy side-to-side dance, the head tilting along
inline void runSwayEmote() {
  const char* c = "sway";
  Serial.println(F("SWAY"));
  runStandPose(0);
  setFaceWithMode("love", FACE_ANIM_ONCE);
  for (int i = 0; i < 6; i++) {
    const bool right = i & 1;
    if (right) emoLegs(650, -999, -999, -999, -999, emoK(4, 0.2f), emoK(5, 0.2f), emoK(6, 0.0f), emoK(7, 0.0f));
    else emoLegs(650, -999, -999, -999, -999, emoK(4, 0.0f), emoK(5, 0.0f), emoK(6, 0.2f), emoK(7, 0.2f));
    if (!emoHeadTo(c, right ? 18 : -18, right ? -6 : 2, 300)) return emoFinish(c);
    if (i == 3) setFace("happy");
  }
  emoKnees(500, 0.0f, 0.0f);
  emoWait(c, 200);
  emoFinish(c);
}

// Robot: the robot dance - every move a snap, then a hold. Head ticks, a foot, a dip, a turn of the head
inline void runRobotEmote() {
  const char* c = "robot";
  Serial.println(F("ROBOT"));
  runStandPose(0);
  setFaceWithMode("cool", FACE_ANIM_ONCE);
  auto snapHead = [&](int pan, int tilt) { emoHead(pan, tilt); return emoWait(c, 320); };
  if (!snapHead(30, 0) || !snapHead(30, -15)) return emoFinish(c);
  emoLegs(90, -999, -999, -999, -999, -999, -999, emoK(6, 0.5f), -999);  // front left foot: snap up
  if (!emoWait(c, 320)) return emoFinish(c);
  emoLegs(90, -999, -999, -999, -999, -999, -999, emoK(6, 0.0f), -999);
  if (!snapHead(-30, -15) || !snapHead(-30, 0)) return emoFinish(c);
  emoLegs(90, -999, -999, -999, -999, -999, emoK(5, 0.5f), -999, -999);  // front right
  if (!emoWait(c, 320)) return emoFinish(c);
  emoLegs(90, -999, -999, -999, -999, -999, emoK(5, 0.0f), -999, -999);
  emoCrouch(0.2f, 100);  // a dip...
  if (!snapHead(0, 10)) return emoFinish(c);
  emoCrouch(0.0f, 100);  // ...and up
  if (!snapHead(0, -10)) return emoFinish(c);
  for (int i = 0; i < 3; i++)  // tick tick tick
    if (!snapHead(-20 + i * 20, -10)) return emoFinish(c);
  setFace("proud");
  if (!snapHead(0, 0)) return emoFinish(c);
  emoFinish(c);
}

// Ta-da: gathers itself low, rises tall, head up, a flourish of the front right foot - then a small bow
inline void runTadaEmote() {
  const char* c = "tada";
  Serial.println(F("TADA"));
  runStandPose(0);
  setFaceWithMode("sneaky", FACE_ANIM_ONCE);
  emoCrouch(0.3f, 500);
  if (!emoHeadTo(c, 0, 12, 400) || !emoWait(c, 250)) return emoFinish(c);
  setFace("starstruck");
  emoPose(300, POSE_STAND);  // up!
  if (!emoHeadTo(c, 0, -20, 250)) return emoFinish(c);
  emoKnees(300, 0.0f, 0.4f);  // weight back, then the flourish
  emoLegs(250, -999, -999, -999, -999, -999, emoK(5, 0.8f), -999, -999);
  if (!emoHeadTo(c, -20, -20, 300) || !emoWait(c, 700)) return emoFinish(c);
  emoLegs(300, -999, -999, -999, -999, -999, emoK(5, 0.0f), -999, -999);
  emoKnees(300, 0.0f, 0.0f);
  setFace("proud");
  emoKnees(450, 0.3f, 0.0f);  // a small bow
  if (!emoHeadTo(c, 0, 18, 450) || !emoWait(c, 450)) return emoFinish(c);
  emoKnees(400, 0.0f, 0.0f);
  emoFinish(c);
}

// ====== DOG / CAT EMOTES: what a real four-legged pet does, so it reads at a glance ======
// (Moderate speeds - nothing that bobs fast; no ?: between float constants in loops: an Xtensa compiler crash.)

// Play bow: "let's play!" - front down low, head up at you, the behind wiggles; it pops up and bows again
inline void runPlayBowEmote() {
  const char* c = "playbow";
  Serial.println(F("PLAYBOW"));
  runStandPose(0);
  setFaceWithMode("excited", FACE_ANIM_ONCE);
  for (int rep = 0; rep < 2; rep++) {
    emoKnees(380, 0.75f, 0.0f);  // front down, behind up
    if (!emoHeadTo(c, 0, -22, 300)) return emoFinish(c);
    for (int i = 0; i < 6; i++) {
      if (i & 1) emoLegs(150, -999, emoK(1, 0.25f), -999, emoK(3, -0.25f), -999, -999, -999, -999);
      else emoLegs(150, -999, emoK(1, -0.25f), -999, emoK(3, 0.25f), -999, -999, -999, -999);
      if (!emoWait(c, 30)) return emoFinish(c);
    }
    emoLegs(150, -999, POSE_STAND[1], -999, POSE_STAND[3], -999, -999, -999, -999);
    setFace("happy");
    emoKnees(220, 0.0f, 0.0f);  // pop up
    if (!emoHeadTo(c, rep ? -20 : 20, -10, 200) || !emoWait(c, 250)) return emoFinish(c);
    setFace("excited");
  }
  emoWait(c, 200);
  emoFinish(c);
}

// Huh?: the dog's head tilt at a strange sound - leans and turns its head to one side and holds it,
// then the other way, puzzled
inline void runHuhEmote() {
  const char* c = "huh";
  Serial.println(F("HUH"));
  runStandPose(0);
  setFaceWithMode("curious", FACE_ANIM_ONCE);
  const int side = random(2) ? 1 : -1;
  for (int k = 0; k < 2; k++) {
    const int s = k ? -side : side;
    if (s > 0) emoLegs(350, -999, -999, -999, -999, emoK(4, 0.14f), emoK(5, 0.14f), emoK(6, 0.0f), emoK(7, 0.0f));
    else emoLegs(350, -999, -999, -999, -999, emoK(4, 0.0f), emoK(5, 0.0f), emoK(6, 0.14f), emoK(7, 0.14f));
    if (!emoHeadTo(c, 22 * s, -10, 300) || !emoWait(c, 900)) return emoFinish(c);
    if (k == 0) setFace("confused");
  }
  emoKnees(300, 0.0f, 0.0f);
  if (!emoHeadTo(c, 0, -4, 300)) return emoFinish(c);
  setFace("happy");
  emoWait(c, 300);
  emoFinish(c);
}

// Scratch: a dog scratching an itch - sits back, the back right foot comes up and scratches, head turned
// toward it, eyes blissful
inline void runScratchEmote() {
  const char* c = "scratch";
  Serial.println(F("SCRATCH"));
  runStandPose(0);
  setFaceWithMode("content", FACE_ANIM_ONCE);
  emoKnees(500, 0.0f, 0.4f);  // sit back a bit
  emoLegs(300, -999, -999, -999, -999, -999, -999, emoK(6, 0.15f), emoK(7, 0.5f));  // weight to the left
  emoLegs(250, -999, -999, -999, -999, emoK(4, 0.9f), -999, -999, -999);             // back right foot up
  if (!emoHeadTo(c, 30, 8, 300)) return emoFinish(c);
  setFace("love");
  for (int i = 0; i < 8; i++) {  // the scratching: the back right hip strokes
    if (i & 1) emoLegs(130, -999, emoK(1, 0.35f), -999, -999, -999, -999, -999, -999);
    else emoLegs(130, -999, emoK(1, -0.1f), -999, -999, -999, -999, -999, -999);
    if (!emoWait(c, 10)) return emoFinish(c);
  }
  emoLegs(250, -999, POSE_STAND[1], -999, -999, emoK(4, 0.4f), -999, -999, -999);
  setFace("happy");
  emoKnees(450, 0.0f, 0.0f);
  emoWait(c, 200);
  emoFinish(c);
}

// Shake off: the wet-dog shake - the head swings side to side and the body rolls along with it
inline void runShakeOffEmote() {
  const char* c = "shakeoff";
  Serial.println(F("SHAKEOFF"));
  runStandPose(0);
  setFaceWithMode("surprised", FACE_ANIM_ONCE);
  if (!emoWait(c, 250)) return emoFinish(c);
  setFace("dizzy");
  for (int i = 0; i < 7; i++) {
    if (i & 1) emoLegs(130, -999, -999, -999, -999, emoK(4, 0.14f), emoK(5, 0.14f), emoK(6, 0.0f), emoK(7, 0.0f));
    else emoLegs(130, -999, -999, -999, -999, emoK(4, 0.0f), emoK(5, 0.0f), emoK(6, 0.14f), emoK(7, 0.14f));
    emoHead((i & 1) ? 26 : -26, -2);
    if (!emoWait(c, 20)) return emoFinish(c);
  }
  emoKnees(250, 0.0f, 0.0f);
  setFace("happy");
  if (!emoHeadTo(c, 0, -10, 250) || !emoWait(c, 400)) return emoFinish(c);
  emoFinish(c);
}

// Knead: a cat "making biscuits" - slow alternate presses of the front paws, blissed out
inline void runKneadEmote() {
  const char* c = "knead";
  Serial.println(F("KNEAD"));
  runStandPose(0);
  setFaceWithMode("love", FACE_ANIM_ONCE);
  emoKnees(500, 0.0f, 0.35f);
  if (!emoHeadTo(c, 0, 10, 400)) return emoFinish(c);
  for (int i = 0; i < 8; i++) {
    if (i & 1) emoLegs(380, -999, -999, -999, -999, -999, emoK(5, 0.35f), emoK(6, 0.0f), -999);
    else emoLegs(380, -999, -999, -999, -999, -999, emoK(5, 0.0f), emoK(6, 0.35f), -999);
    if (i == 4) setFace("sleepy");
    if (!emoWait(c, 40)) return emoFinish(c);
  }
  emoKnees(400, 0.0f, 0.0f);
  setFace("happy");
  emoWait(c, 200);
  emoFinish(c);
}

// Beg: the puppy beg - sits back, the front paws paddle in the air in turn, head up, big eyes
inline void runBegEmote() {
  const char* c = "beg";
  Serial.println(F("BEG"));
  runStandPose(0);
  setFaceWithMode("love", FACE_ANIM_ONCE);
  emoKnees(600, 0.0f, 0.75f);  // sits back
  if (!emoHeadTo(c, 0, -24, 400)) return emoFinish(c);
  for (int i = 0; i < 6; i++) {
    if (i & 1) emoLegs(260, -999, -999, -999, -999, -999, emoK(5, 0.65f), emoK(6, 0.15f), -999);
    else emoLegs(260, -999, -999, -999, -999, -999, emoK(5, 0.15f), emoK(6, 0.65f), -999);
    if (i == 3) setFace("starstruck");
    if (!emoWait(c, 60)) return emoFinish(c);
  }
  emoLegs(300, -999, -999, -999, -999, -999, emoK(5, 0.0f), emoK(6, 0.0f), -999);
  if (!emoHeadTo(c, 14, -20, 300) || !emoWait(c, 600)) return emoFinish(c);  // "please?"
  setFace("happy");
  emoKnees(500, 0.0f, 0.0f);
  emoFinish(c);
}

// Loaf: the cat loaf - settles down neatly, slow sleepy blinks, an idle look around, then gets up
inline void runLoafEmote() {
  const char* c = "loaf";
  Serial.println(F("LOAF"));
  runStandPose(0);
  setFaceWithMode("content", FACE_ANIM_ONCE);
  emoCrouch(0.85f, 1200);
  if (!emoHeadTo(c, 0, 6, 500)) return emoFinish(c);
  for (int b = 0; b < 2; b++) {  // slow blinks
    setFace("sleepy");
    if (!emoWait(c, 700)) return emoFinish(c);
    setFace("content");
    if (!emoWait(c, 900)) return emoFinish(c);
  }
  if (!emoHeadTo(c, -25, 0, 900) || !emoWait(c, 500) || !emoHeadTo(c, 0, 4, 700)) return emoFinish(c);
  setFace("sleepy");
  if (!emoWait(c, 800)) return emoFinish(c);
  setFace("happy");
  emoPose(800, POSE_STAND);
  emoFinish(c);
}

// Paw swat: a cat spots something on the floor - crouches, watches it, swats, swats again with the other paw
inline void runSwatEmote() {
  const char* c = "swat";
  Serial.println(F("SWAT"));
  runStandPose(0);
  setFaceWithMode("sneaky", FACE_ANIM_ONCE);
  emoKnees(450, 0.3f, 0.1f);
  if (!emoHeadTo(c, 10, 26, 400) || !emoWait(c, 500)) return emoFinish(c);
  if (!emoHeadTo(c, -6, 26, 400) || !emoWait(c, 300)) return emoFinish(c);  // watching it move
  setFace("excited");
  // the swat: the front right foot up and forward, then down onto it
  emoLegs(140, emoK(0, -0.35f), -999, -999, -999, -999, emoK(5, 0.7f), -999, -999);
  emoLegs(140, POSE_STAND[0], -999, -999, -999, -999, emoK(5, 0.3f), -999, -999);
  if (!emoWait(c, 450)) return emoFinish(c);
  setFace("surprised");
  if (!emoHeadTo(c, -18, 24, 250) || !emoWait(c, 300)) return emoFinish(c);  // it moved!
  setFace("excited");
  emoLegs(140, -999, -999, emoK(2, -0.35f), -999, -999, -999, emoK(6, 0.7f), -999);  // the other paw
  emoLegs(140, -999, -999, POSE_STAND[2], -999, -999, -999, emoK(6, 0.3f), -999);
  if (!emoWait(c, 450)) return emoFinish(c);
  setFace("proud");
  emoKnees(400, 0.0f, 0.0f);
  if (!emoHeadTo(c, 0, -8, 300) || !emoWait(c, 300)) return emoFinish(c);
  emoFinish(c);
}

// Startle: jumps back from something, freezes - then slowly leans in to sniff it, and relaxes
inline void runStartleEmote() {
  const char* c = "startle";
  Serial.println(F("STARTLE"));
  runStandPose(0);
  setFaceWithMode("surprised", FACE_ANIM_ONCE);
  emoKnees(130, 0.0f, 0.5f);  // jump back
  emoHead(0, -20);
  setFace("scared");
  if (!emoWait(c, 900)) return emoFinish(c);  // frozen
  setFace("curious");
  emoKnees(1100, 0.3f, 0.0f);  // slowly leans in...
  if (!emoHeadTo(c, 0, 18, 800)) return emoFinish(c);
  for (int i = 0; i < 3; i++)  // ...sniff sniff
    if (!emoHeadTo(c, 0, 14, 120) || !emoHeadTo(c, 0, 19, 120)) return emoFinish(c);
  setFace("happy");
  emoKnees(400, 0.0f, 0.0f);
  if (!emoHeadTo(c, 0, -6, 300) || !emoWait(c, 300)) return emoFinish(c);
  emoFinish(c);
}

// ====== HEAD PATS: reactions to the touch sensor - main.cpp's pat brain picks one ======
extern bool headTouched();  // main.cpp: the touch sensor, right now

// Boop: a tap - it glances up at you, a little surprised, then a happy wiggle of the behind
inline void runBoopEmote() {
  const char* c = "pat_boop";
  Serial.println(F("BOOP"));
  runStandPose(0);
  setFaceWithMode("surprised", FACE_ANIM_ONCE);
  emoCrouch(0.35f, 130);  // a little startled dip...
  if (!emoHeadTo(c, 0, -20, 180) || !emoWait(c, 200)) return emoFinish(c);
  emoCrouch(0.0f, 160);  // ...and back up
  setFace("happy");
  for (int i = 0; i < 4; i++) {
    if (i & 1) emoLegs(170, -999, emoK(1, 0.45f), -999, emoK(3, -0.45f), -999, -999, -999, -999);
    else emoLegs(170, -999, emoK(1, -0.45f), -999, emoK(3, 0.45f), -999, -999, -999, -999);
    if (!emoWait(c, 20)) return emoFinish(c);
  }
  emoLegs(150, -999, POSE_STAND[1], -999, POSE_STAND[3], -999, -999, -999, -999);
  emoWait(c, 250);
  emoFinish(c);
}

// Nuzzle: rubs its head up against the hand that touched it, eyes closed
inline void runNuzzleEmote() {
  const char* c = "pat_nuzzle";
  Serial.println(F("NUZZLE"));
  runStandPose(0);
  setFaceWithMode("love", FACE_ANIM_ONCE);
  emoKnees(300, 0.35f, 0.55f);  // leans into the hand
  for (int i = 0; i < 2; i++)
    if (!emoHeadTo(c, 12, -16, 380) || !emoHeadTo(c, -12, -12, 380)) return emoFinish(c);
  if (!emoHeadTo(c, 0, -14, 250) || !emoWait(c, 300)) return emoFinish(c);
  setFace("happy");
  emoKnees(300, 0.0f, 0.0f);
  emoFinish(c);
}

// Petting: a hand held on its head - it leans up into it with its eyes closed and slowly melts lower
// the longer it lasts, nuzzling now and then. Afterwards: a short pet = happy, a long one = a stretch,
// a very long one = blissed out
inline void runPettingEmote() {
  const char* c = "petting";
  Serial.println(F("PETTING"));
  runStandPose(0);
  setFaceWithMode("love", FACE_ANIM_ONCE);
  if (!emoHeadTo(c, 0, -14, 300)) return emoFinish(c);
  const unsigned long t0 = millis();
  unsigned long lastTouch = millis(), nuzzleMs = millis();
  int nz = 0;
  while (currentCommand == c) {
    const unsigned long now = millis();
    if (headTouched()) lastTouch = now;
    if (now - lastTouch > 450) break;  // the hand's gone
    const float held = (now - t0) / 1000.0f;
    // melt: lower and lower over ~5 s, the back first
    const float m = fminf(held / 5.0f, 1.0f);
    emoKnees(120, 0.25f * m, 0.45f * m);
    if (held > 2.5f && nz == 0) { setFace("sleepy"); nz = 1; }
    if (now - nuzzleMs > 1400) {  // a slow nuzzle now and then
      nuzzleMs = now;
      const int side = random(2) ? 8 : -8;
      if (!emoHeadTo(c, side, -14, 400) || !emoHeadTo(c, 0, -14, 400)) break;
    }
    if (!emoWait(c, 30)) break;
  }
  const float held = (millis() - t0) / 1000.0f;
  if (currentCommand != c) return emoFinish(c);
  if (held < 2.5f) {  // short: happy, a little head shake
    setFace("happy");
    emoKnees(300, 0.0f, 0.0f);
    for (int i = 0; i < 2; i++)
      if (!emoHeadTo(c, 14, -8, 110) || !emoHeadTo(c, -14, -8, 110)) return emoFinish(c);
  } else if (held < 7.0f) {  // longer: a slow stretch on the way up
    setFace("content");
    emoKnees(600, 0.5f, 0.0f);
    if (!emoHeadTo(c, 0, -22, 500) || !emoWait(c, 500)) return emoFinish(c);
    setFace("happy");
    emoKnees(500, 0.0f, 0.0f);
  } else {  // very long: blissed out, slow to get up
    setFace("sleepy");
    if (!emoWait(c, 900)) return emoFinish(c);
    setFace("love");
    emoPose(1200, POSE_STAND);
  }
  emoFinish(c);
}

// Wake: patted while lying down - groggy, the head comes up slowly, a yawn, it gets up
inline void runPatWakeEmote() {
  const char* c = "pat_wake";
  Serial.println(F("PATWAKE"));
  setFaceWithMode("sleepy", FACE_ANIM_ONCE);
  if (!emoHeadTo(c, 0, 10, 300) || !emoWait(c, 500)) return emoFinish(c);
  if (!emoHeadTo(c, 0, -16, 900)) return emoFinish(c);
  setFace("yawn");
  if (!emoWait(c, 1300)) return emoFinish(c);
  setFace("content");
  emoPose(1300, POSE_STAND);
  setFace("happy");
  emoWait(c, 300);
  emoFinish(c);
}

// Hello: the first pat after a long while - "oh! you're here!" - a start, then all excited
inline void runPatHelloEmote() {
  const char* c = "pat_hello";
  Serial.println(F("PATHELLO"));
  runStandPose(0);
  setFaceWithMode("surprised", FACE_ANIM_ONCE);
  emoKnees(140, 0.0f, 0.6f);
  emoHead(0, -18);
  if (!emoWait(c, 450)) return emoFinish(c);
  setFace("excited");
  emoKnees(250, 0.0f, 0.0f);
  for (int i = 0; i < 2; i++) {
    emoCrouch(0.55f, 240);
    if (!emoWait(c, 40)) return emoFinish(c);
    emoCrouch(0.0f, 220);
    if (!emoWait(c, 80)) return emoFinish(c);
  }
  setFace("love");
  if (!emoHeadTo(c, 0, -14, 300) || !emoWait(c, 600)) return emoFinish(c);
  emoFinish(c);
}

// Enough: too many pats - a giggle, then it coyly turns its head away... and peeks back
inline void runPatEnoughEmote() {
  const char* c = "pat_enough";
  Serial.println(F("PATENOUGH"));
  runStandPose(0);
  setFaceWithMode("laugh", FACE_ANIM_ONCE);
  static const float BK[2] = {0.6f, 0.4f};
  for (int i = 0; i < 4; i++) {
    emoKnees(170, 0.0f, BK[i & 1]);
    emoHead((i & 1) ? 5 : -5, -14);
    if (!emoWait(c, 25)) return emoFinish(c);
  }
  setFace("shy");
  emoKnees(300, 0.0f, 0.0f);
  const int side = random(2) ? 1 : -1;
  if (!emoHeadTo(c, 40 * side, 10, 500) || !emoWait(c, 1000)) return emoFinish(c);
  setFace("happy");
  if (!emoHeadTo(c, 15 * side, 0, 300) || !emoWait(c, 400)) return emoFinish(c);  // a peek back
  emoFinish(c);
}

// ====== CRAB RAVE: the crab-rave meme, to the song. Built on the crab emote:
// the legs spread wide in the crab stance and the diagonal knee shuffle. /music/crabrave.wav = Noisestorm's Crab
// Rave cut 8 beats before the drop (the song from 55.80 s, +29.5 s), 123.05 BPM.
// The beat clock is the player's own position, so it can't drift off the music; no song: its own clock ======
extern bool crabMusicStart();          // main.cpp: the song from the SD card (false: not there)
extern void crabMusicStop();
extern long crabMusicPosMs();          // where the song is (ms), -1 = not playing
inline void runCrabRaveEmote() {
  const char* c = "crabrave";
  Serial.println(F("CRABRAVE"));
  runStandPose(0);
  setFaceWithMode("sneaky", FACE_ANIM_ONCE);
  // 125 BPM exactly (fits the drum hits over the whole clip; 123 BPM drifts ~0.4 s behind by the second claw).
  // Beat 0 at 46 ms into the clip (the drop, beat 8, at 3.886 s)
  const float BEAT = 480.0f, BEAT0 = 46.0f;
  const long LAT = 0;          // timing offset (ms)
  const long LEAD = 120;      // the moves start this early (the servos take time to swing there)
  // 8 beats build-up, then from the drop phrases of 16: shuffle, left claw, shuffle, right claw, shuffle, left claw,
  // shuffle - 120 beats, ~58 s (the clip: 60.5 s)
  const int INTRO = 8, PHR = 16, NPHR = 7;
  const int B_END = INTRO + PHR * NPHR;
  // the crab stance before the music starts, so the first beats are not late
  moveSmoothHelper(350, -16, 156, 64, 0, -8, 107, 46, 98);
  const bool music = crabMusicStart();
  const unsigned long t0 = millis();
  auto nowMs = [&]() -> long {
    if (music) { const long p = crabMusicPosMs(); if (p >= 0) return p - LAT; }
    return (long)(millis() - t0);
  };
  auto head = [&](float pan, float tilt) {  // gentle: a small nod, the pan drifting
    const float p = constrain(EMO_PAN_MID + pan, (float)EMO_PAN_LO, (float)EMO_PAN_HI);
    const float t = constrain(EMO_TILT_MID + tilt, (float)EMO_TILT_LO, (float)EMO_TILT_HI);
    emoPanNow = (int)lroundf(p);
    emoTiltNow = (int)lroundf(t);
    setHeadServoF(EMO_PAN_PIN, p);
    setHeadServoF(EMO_TILT_PIN, t);
  };
  auto panAt = [&](int b) { return 9.0f * sinf(b * 3.14159f / 4); };
  static const char* const RAVE[4] = {"crabrave", "crabrave_teal", "crabrave_red", "crabrave_drop"};
  for (int b = 0; b < B_END; b++) {
    // (the servos take ~0.15-0.2 s to get there: each beat's move starts LEAD early so it lands on the beat)
    while (nowMs() < (long)(BEAT0 + b * BEAT) - LEAD) {
      if (currentCommand != c) { crabMusicStop(); return emoFinish(c); }
      delayWithFace(3);
    }
    if (b < INTRO) {  // the build-up: a small nod a beat, sinking a little lower
      head(0, 7);
      const float k = 0.03f * b;
      moveSmoothHelper(150, -999, -999, -999, -999, -8 + (int)(36 * k), 107 - (int)(29 * k), 46 - (int)(26 * k), 98 + (int)(34 * k));
      head(0, 2);
      continue;
    }
    if (b == INTRO) setFace("crabrave_drop");  // the drop
    else if ((b & 1) == 0) setFace(RAVE[(b / 2) & 3]);  // the rave lights
    const int ph = (b - INTRO) / PHR, pb = (b - INTRO) % PHR;
    const bool claw = ph & 1;            // phrases 1, 3, 5: a claw
    const int side = (ph / 2) & 1;       // left, right, left
    if (claw) {
      if (pb == 0) {  // into it, beat 1: the crab hips in
        head(0, -8);
        moveSmoothHelper(260, POSE_STAND[0], POSE_STAND[1], POSE_STAND[2], POSE_STAND[3], -8, 107, 46, 98);
        continue;
      }
      if (pb == 1) {  // beat 2: the wave's lean, the claw up
        waveLean(side, 220);
        waveFoot(side, 160, 1.0f);
        continue;
      }
      if (pb == PHR - 2) {  // out of it, beat 1: the claw down
        moveSmoothHelper(240, -999, -999, -999, -999, -999, POSE_STAND[5], POSE_STAND[6], -999);
        continue;
      }
      if (pb == PHR - 1) {  // beat 2: back into the crab stance
        moveSmoothHelper(300, -16, 156, 64, 0, -8, 107, 46, 98);
        continue;
      }
      // half-time: swing on one beat, pinch on the next - swing + pinch in one beat
      // is too much servo travel to land on 480 ms
      if ((pb & 1) == 0) {  // the claw swings across (its front hip), to the other side each time
        const int sw = ((pb >> 1) & 1) ? 20 : -20;
        if (side == 0) moveSmoothHelper(200, -999, -999, POSE_STAND[2] + sw, -999, -999, -999, -999, -999);
        else moveSmoothHelper(200, POSE_STAND[0] - sw, -999, -999, -999, -999, -999, -999, -999);
        head(panAt(b) * 0.5f, -10);
      } else {  // the pinch: the knee dips and opens
        waveFoot(side, 100, 0.6f);
        waveFoot(side, 100, 1.0f);
        head(panAt(b) * 0.5f, -6);
      }
      continue;
    }
    // the crab shuffle on the beat, a small nod with it
    head(panAt(b), 6);
    if (b & 1) moveSmoothHelper(200, -999, -999, -999, -999, 28, 78, 20, 132);
    else moveSmoothHelper(200, -999, -999, -999, -999, -8, 107, 46, 98);
    head(panAt(b), 1);
  }
  if (music) crabMusicStop();
  setFace("proud");
  emoFinish(c);
}

// ====== CRAB RAVE 1: the original version: music first,
// then the crab stance; 8 beats build-up, 16 shuffle, 16 left claw (the wave's lean), 12 shuffle ======
inline void runCrabRave1Emote() {
  const char* c = "crabrave1";
  Serial.println(F("CRABRAVE1"));
  runStandPose(0);
  setFaceWithMode("sneaky", FACE_ANIM_ONCE);
  const float BEAT = 487.62f;
  const int B_SHUF1 = 8, B_CLAW = 24, B_SHUF2 = 40, B_END = 52;
  const bool music = crabMusicStart();
  const unsigned long t0 = millis();
  auto nowMs = [&]() -> long {
    if (music) { const long p = crabMusicPosMs(); if (p >= 0) return p; }
    return (long)(millis() - t0);
  };
  auto head = [&](float pan, float tilt) {
    const float p = constrain(EMO_PAN_MID + pan, (float)EMO_PAN_LO, (float)EMO_PAN_HI);
    const float t = constrain(EMO_TILT_MID + tilt, (float)EMO_TILT_LO, (float)EMO_TILT_HI);
    emoPanNow = (int)lroundf(p);
    emoTiltNow = (int)lroundf(t);
    setHeadServoF(EMO_PAN_PIN, p);
    setHeadServoF(EMO_TILT_PIN, t);
  };
  auto panAt = [&](int b) { return 9.0f * sinf(b * 3.14159f / 4); };
  static const char* const RAVE[4] = {"crabrave", "crabrave_teal", "crabrave_red", "crabrave_drop"};
  moveSmoothHelper(350, -16, 156, 64, 0, -8, 107, 46, 98);  // the crab stance
  for (int b = 0; b < B_END; b++) {
    while (nowMs() < (long)(b * BEAT)) {
      if (currentCommand != c) { crabMusicStop(); return emoFinish(c); }
      delayWithFace(3);
    }
    if (b < B_SHUF1) {
      head(0, 7);
      const float k = 0.03f * b;
      moveSmoothHelper(150, -999, -999, -999, -999, -8 + (int)(36 * k), 107 - (int)(29 * k), 46 - (int)(26 * k), 98 + (int)(34 * k));
      head(0, 2);
      continue;
    }
    if (b == B_SHUF1) setFace("crabrave_drop");
    else if ((b & 1) == 0) setFace(RAVE[(b / 2) & 3]);
    if (b == B_CLAW) {
      head(0, -8);
      const int r2 = (int)lroundf(POSE_STAND[1] + WAVE_R2_PCT / 100.0f * (80 - POSE_STAND[1]));
      const int l4 = (int)lroundf(POSE_STAND[7] + WAVE_L4_PCT / 100.0f * (REST_BACK[3] - POSE_STAND[7]));
      moveSmoothHelper(220, -16, r2, 64, 0, WAVE_R4, 107, 46, l4);
      moveSmoothHelper(200, -7, -999, 23, -26, -999, -999, 140, -999);
      continue;
    }
    if (b > B_CLAW && b < B_SHUF2) {
      moveSmoothHelper(150, -999, -999, (b & 1) ? 43 : 3, -999, -999, -999, 140, -999);
      head(panAt(b) * 0.5f, -10);
      delayWithFace(80);
      moveSmoothHelper(90, -999, -999, -999, -999, -999, -999, 112, -999);
      moveSmoothHelper(90, -999, -999, -999, -999, -999, -999, 140, -999);
      head(panAt(b) * 0.5f, -6);
      continue;
    }
    if (b == B_SHUF2) {
      moveSmoothHelper(200, -999, -999, -999, -999, -999, -999, 46, -999);
      moveSmoothHelper(240, -16, 156, 64, 0, -8, 107, 46, 98);
      continue;
    }
    head(panAt(b), 6);
    if (b & 1) moveSmoothHelper(200, -999, -999, -999, -999, 28, 78, 20, 132);
    else moveSmoothHelper(200, -999, -999, -999, -999, -8, 107, 46, 98);
    head(panAt(b), 1);
  }
  if (music) crabMusicStop();
  setFace("proud");
  emoFinish(c);
}

// ====== DANCE TO ANY SONG: it listens (the mic, beat.h), finds the beat, and dances on it ======
// Still while it listens (its own servos would fool its ears), then 8-beat phrases from the moves it does well,
// each started a little early so it lands on the beat. Every 32 beats a short still moment to
// listen again (the tempo / the phase); the music gone -> a bow and done. Louder music -> bigger moves.
extern float musicBeatPeriodMs();
extern unsigned long musicNextBeat(unsigned long t);
extern float musicBeatConf();
extern float musicLevelDb();
extern bool musicEarsOn();
extern void musicBeatReset();
extern void musicCharacter(float* bass, float* mid, float* hi, float* busy, float* rise);
extern void musicMuteFor(int ms);
extern void musicFreeze(bool on);
extern void musicHint(float periodMs);
inline int danceePick(const float* w, int n) {  // a weighted random choice
  float t = 0;
  for (int i = 0; i < n; i++) t += fmaxf(w[i], 0.0f);
  if (t <= 0) return random(n);
  float r = random(10000) / 10000.0f * t;
  for (int i = 0; i < n; i++) {
    r -= fmaxf(w[i], 0.0f);
    if (r < 0) return i;
  }
  return n - 1;
}
// Every 8-beat
// phrase is made up fresh from what it heard: a body move (bounce, pump, sway, rock, body circle, headbang, crab,
// paw pops), a rhythm (every beat, accents, double time, half time), a head move and maybe the back hips wiggling on
// top. Bass-heavy music leans to pumps and bounces, vocals / mids to sways and circles, hi-hats and busy drums to
// crab steps, paw pops and wiggles; a drop (much louder) to the big ones; a quiet part to small, slow ones. Never the
// same move twice running. The music pausing doesn't end it: it waits, listening, and dances on when it comes back.
inline void runMusicDanceEmote() {
  const char* c = "musicdance";
  Serial.println(F("MUSICDANCE"));
  runStandPose(0);
  const long LEAD = 110;
  auto head = [&](float pan, float tilt) {
    const float p = constrain(EMO_PAN_MID + pan, (float)EMO_PAN_LO, (float)EMO_PAN_HI);
    const float t = constrain(EMO_TILT_MID + tilt, (float)EMO_TILT_LO, (float)EMO_TILT_HI);
    emoPanNow = (int)lroundf(p);
    emoTiltNow = (int)lroundf(t);
    setHeadServoF(EMO_PAN_PIN, p);
    setHeadServoF(EMO_TILT_PIN, t);
  };
  // listen, still: up to `ms` for a confident beat with music loud enough. hintMs > 0: a quick re-check near the
  // tempo it's dancing to (~1.8 s); 0: a search from scratch (~2.6 s at least). Found: the estimate is frozen
  // again - the dance keeps time itself and only the level / character keep coming in, between its moves.
  auto listen = [&](int ms, float hintMs) -> bool {
    musicFreeze(false);
    musicHint(hintMs);
    musicBeatReset();
    const unsigned long t0 = millis(), need = hintMs > 0 ? 1700 : 2600;
    const float minConf = hintMs > 0 ? 0.25f : 0.3f;
    while (millis() - t0 < (unsigned long)ms) {
      if (currentCommand != c) break;
      delayWithFace(50);
      static unsigned long logMs = 0;
      if (millis() - logMs > 1000) {  // what it hears (for tuning)
        logMs = millis();
        xlogSys("DANCE listening%s: %.1f BPM conf %.2f level %.1f dB", hintMs > 0 ? " (re-check)" : "",
                musicBeatPeriodMs() > 0 ? 60000.0f / musicBeatPeriodMs() : 0.0f, musicBeatConf(), musicLevelDb());
      }
      if (millis() - t0 > need && musicBeatConf() > minConf && musicLevelDb() > 2.0f && musicBeatPeriodMs() > 0) {
        musicHint(0);
        musicFreeze(true);
        return true;
      }
    }
    musicHint(0);
    return false;
  };
  // no music (yet / any more): it keeps listening - an ear one way, then the other - until there is, or you pick
  // something else. No time limit.
  auto waitForMusic = [&]() -> bool {
    int tries = 0;
    while (currentCommand == c) {
      if (listen(tries == 0 ? 7000 : 4000, 0)) return true;
      if (currentCommand != c) return false;
      if (tries++ == 0) {
        setFace("confused");
        xlogSys("DANCE no beat heard (conf %.2f, level %.1f dB) - still listening", musicBeatConf(), musicLevelDb());
      }
      setFace("listening");
      musicMuteFor(600);
      emoHeadTo(c, (tries & 1) ? -14 : 14, -4, 500);
    }
    return false;
  };
  setFaceWithMode("listening", FACE_ANIM_ONCE);
  emoHeadTo(c, 14, -6, 400);  // an ear cocked
  if (!musicEarsOn()) {
    setFace("confused");
    xlogSys("DANCE no ears: the mic is busy (wake word off / a chat / a song)");
    emoWait(c, 1200);
    return emoFinish(c);
  }
  if (!waitForMusic()) return emoFinish(c);

  enum { SH_BOUNCE, SH_PUMP, SH_SWAY, SH_ROCK, SH_CIRCLE, SH_HEADBANG, SH_CRAB, SH_PAWS, SH_N };
  enum { RH_EVERY, RH_ONOFF, RH_DOUBLE, RH_HALF, RH_N };
  enum { HD_NOD, HD_SIDE, HD_CIRCLE, HD_COUNTER, HD_N };
  static const char* const SH_NAME[SH_N] = {"bounce", "pump", "sway", "rock", "body circle", "headbang", "crab", "paw pops"};
  static const char* const RH_NAME[RH_N] = {"every beat", "accents", "double time", "half time"};
  static const char* const HD_NAME[HD_N] = {"nod", "side to side", "circle", "counter"};
  int lastShape = -1, lastShape2 = -1, beatsDone = 0;
  float lastLvl = -100, busyRef = 0;
  while (currentCommand == c) {
    const float P = musicBeatPeriodMs();
    if (P <= 0) break;
    const float bpm = 60000.0f / P;
    // sure of the beat: 64 beats on its own clock before a quick re-check (the ears stay on meanwhile)
    const int phrases = musicBeatConf() > 0.6f ? 8 : 4;
    const unsigned long nb = musicNextBeat(millis() + LEAD + 20);
    if (!nb) break;
    bool musicGone = false;
    int quietSteps = 0;
    for (int phrase = 0; phrase < phrases && currentCommand == c && !musicGone; phrase++) {
      // ---- what it hears right now (between its own moves), every phrase ----
      const float lvl = musicLevelDb();
      float bass, mid, hi, busy, rise;
      musicCharacter(&bass, &mid, &hi, &busy, &rise);
      // a drop: much louder than the last ~10 s or the last phrase; a quiet part: much softer
      const bool drop = rise > 3.0f || (lastLvl > -50 && lvl - lastLvl > 4.0f);
      const bool calm = (lastLvl > -50 && lastLvl - lvl > 5.0f) || lvl < 3.0f;
      lastLvl = lvl;
      // the music's character: each band against the others (~4 dB wide)
      const float mx = fmaxf(bass, fmaxf(mid, hi));
      float wb = expf((bass - mx) / 4), wm = expf((mid - mx) / 4), wh = expf((hi - mx) / 4);
      const float ws = wb + wm + wh;
      wb /= ws; wm /= ws; wh /= ws;
      busyRef = busyRef <= 0 ? busy : busyRef + (busy - busyRef) * 0.3f;  // this song's usual busyness
      const bool busyNow = busy > busyRef * 1.15f;
      const float amp = constrain(0.6f + lvl / 20.0f, 0.6f, 1.2f) * (drop ? 1.15f : 1.0f) * (calm ? 0.75f : 1.0f);
      xlogSys("DANCE %.1f BPM conf %.2f level %.1f dB | bass %.1f mid %.1f hi %.1f busy %.2f rise %.1f%s%s",
              bpm, musicBeatConf(), lvl, bass, mid, hi, busy, rise, drop ? " DROP" : "", calm ? " calm" : "");
      setFace(drop ? "excited" : calm ? "happy" : lvl > 9 ? "excited" : "vibing");
      // ---- this phrase's groove, from what it heard ----
      float w[SH_N];
      w[SH_BOUNCE] = 1 + 2 * wb + wh;
      w[SH_PUMP] = 0.5f + 3 * wb + (drop ? 2 : 0);
      w[SH_SWAY] = 1 + 3 * wm + (calm ? 2 : 0);
      w[SH_ROCK] = 0.8f + wb + wm;
      w[SH_CIRCLE] = 0.6f + 3 * wm + (calm ? 1.5f : 0);
      w[SH_HEADBANG] = 0.3f + 2 * wb + (drop ? 2 : 0) + (bpm > 125 ? 1 : 0);
      w[SH_CRAB] = 0.5f + 2 * wh + (busyNow ? 1 : 0);
      w[SH_PAWS] = 0.4f + 1.5f * wh;
      if (calm) { w[SH_HEADBANG] *= 0.2f; w[SH_CRAB] *= 0.3f; w[SH_PAWS] *= 0.5f; }
      if (lastShape >= 0) w[lastShape] = 0;        // never the same twice running
      if (lastShape2 >= 0) w[lastShape2] *= 0.4f;  // rarely the one before that
      const int shape = danceePick(w, SH_N);
      lastShape2 = lastShape;
      lastShape = shape;
      // the rhythm: slow songs big half-time moves, busy mid-tempo ones double time, fast ones accents
      float wr[RH_N] = {3, 2, 0, 0};
      if (bpm < 92) { wr[RH_HALF] = 3; wr[RH_EVERY] = 2; }
      if (bpm > 135) { wr[RH_HALF] = 2; wr[RH_ONOFF] = 3; wr[RH_EVERY] = 0.5f; }
      if (busyNow && P >= 420 && shape != SH_CRAB && shape != SH_PAWS) wr[RH_DOUBLE] = 2.5f;
      const int rhythm = danceePick(wr, RH_N);
      const float wHd[HD_N] = {2, 1.5f, 1 + 2 * wm, (shape == SH_SWAY || shape == SH_CIRCLE) ? 2.0f : 0.3f};
      const int headMode = danceePick(wHd, HD_N);
      const bool hips = shape != SH_CRAB && shape != SH_PAWS && (wh > 0.4f || busyNow) && random(100) < 55;
      xlogSys("DANCE phrase: %s, %s, head %s%s, x%.2f", SH_NAME[shape], RH_NAME[rhythm], HD_NAME[headMode], hips ? " + hips" : "", amp);
      if (shape == SH_CRAB || shape == SH_PAWS) musicMuteFor(290);                  // (its own servos: not heard)
      if (shape == SH_CRAB) moveSmoothHelper(200, -16, 156, 64, 0, -8, 107, 46, 98);  // the crab stance first
      if (shape == SH_PAWS) emoCorners(200, 0, 0, 0.3f, 0.3f);                      // the weight back first
      const int subs = rhythm == RH_DOUBLE ? 2 : 1;
      for (int b = 0; b < 8 && currentCommand == c && !musicGone; b++, beatsDone++) {
        if (rhythm == RH_HALF && (b & 1)) continue;  // (one move every two beats)
        for (int sb = 0; sb < subs && currentCommand == c && !musicGone; sb++) {
          const unsigned long t = nb + (unsigned long)lroundf((phrase * 8 + b) * P + sb * P / subs);
          while ((long)(millis() - (t - LEAD)) < 0) {
            if (currentCommand != c) break;
            delayWithFace(3);
          }
          if (currentCommand != c) break;
          const int s = rhythm == RH_HALF ? b / 2 : b * subs + sb;  // the step in this phrase
          const bool down = !(s & 1);
          const float acc = rhythm == RH_ONOFF ? ((b % 4 == 0) ? 1.25f : (b & 1) ? 0.55f : 0.9f) : 1.0f;
          const float D = 0.24f * amp * acc;
          const float span = (rhythm == RH_HALF ? 2.0f : 1.0f) * P / subs;
          const int ms = constrain((int)(span * 0.55f), 110, 420);  // fast songs: short, snappy moves
          float pan = 0, tilt = 0;  // the head
          switch (headMode) {
            case HD_NOD: pan = 6.0f * sinf(s * 0.39f); tilt = down ? 9 : -4; break;
            case HD_SIDE: pan = down ? 12 : -12; tilt = -2; break;
            case HD_CIRCLE: pan = 12.0f * sinf(s * 1.5708f); tilt = 3 + 8.0f * cosf(s * 1.5708f); break;
            default: tilt = down ? 6 : -3; break;  // counter: set by the body below
          }
          float fr = 0, fl = 0, br = 0, bl = 0;  // the body: each corner, 0 = standing, + = lower
          switch (shape) {
            case SH_BOUNCE: fr = fl = br = bl = down ? D : 0; break;
            case SH_PUMP: fr = fl = br = bl = down ? fminf(D * 1.4f, 0.42f) : 0.06f; tilt = down ? 14 : -6; break;
            case SH_SWAY:
              if (down) fr = br = D * 1.3f; else fl = bl = D * 1.3f;
              if (headMode == HD_COUNTER) pan = down ? -12 : 12;
              break;
            case SH_ROCK: if (down) fr = fl = D * 1.2f; else br = bl = D; break;
            case SH_CIRCLE: {  // the low corner goes round: FR, BR, BL, FL
              const int q = s & 3;
              const float k = D * 1.3f;
              fr = q == 0 ? k : (q == 1 || q == 3) ? k * 0.4f : 0;
              br = q == 1 ? k : (q == 0 || q == 2) ? k * 0.4f : 0;
              bl = q == 2 ? k : (q == 1 || q == 3) ? k * 0.4f : 0;
              fl = q == 3 ? k : (q == 0 || q == 2) ? k * 0.4f : 0;
              if (headMode == HD_COUNTER) { pan = (q == 0 || q == 1) ? -12 : 12; tilt = (q == 0 || q == 3) ? -6 : 6; }
              break;
            }
            case SH_HEADBANG: fr = fl = down ? fminf(0.15f * amp * acc, 0.22f) : 0; br = bl = down ? 0.05f : 0; tilt = down ? 16 : -10; break;
            case SH_PAWS:  // the weight back, a front paw up on the down steps, left / right in turn
              br = bl = 0.3f;
              if (down) { if ((s >> 1) & 1) fr = 0.55f; else fl = 0.55f; }
              pan = ((s >> 1) & 1) ? 14 : -14;
              tilt = -8;
              break;
            default: break;  // crab: below
          }
          // the music stopped (heard in the gaps between moves, ~2 s of quiet): stop and wait for it
          quietSteps = musicLevelDb() < 1.5f ? quietSteps + 1 : 0;
          if (quietSteps * span > 2000) { musicGone = true; break; }
          musicMuteFor(ms + 90);  // its own servos (head + legs, and their settling): the ears off for the move
          head(pan, tilt);
          if (shape == SH_CRAB) {
            if (down) moveSmoothHelper(ms, -999, -999, -999, -999, 28, 78, 20, 132);
            else moveSmoothHelper(ms, -999, -999, -999, -999, -8, 107, 46, 98);
          } else {
            // + the back hips wiggling (hi-hats / busy parts): other joints, so it layers on the body move
            const float sw = hips ? (down ? 0.22f : -0.22f) * fminf(amp, 1.1f) : 0;
            auto k = [](float v) { return fminf(fmaxf(v, 0.0f), 0.55f); };
            emoLegs(ms, -999, hips ? emoK(1, sw) : -999, -999, hips ? emoK(3, -sw) : -999,
                    emoK(4, k(br)), emoK(5, k(fr)), emoK(6, k(fl)), emoK(7, k(bl)));
          }
        }
      }
      if (currentCommand != c) break;
      if (shape == SH_CRAB || shape == SH_PAWS) { musicMuteFor(250); emoPose(160, POSE_STAND); }  // out of the stance
      if (musicGone) break;
      if (phrase == phrases - 1) {  // the block's end: strike a pose - held while it listens again
        musicMuteFor(300);
        const int pose = random(3);
        if (pose == 0) { emoCorners(200, 0, 0, 0.3f, 0.3f); head(0, -16); }          // chest out, chin up
        else if (pose == 1) { emoCorners(200, 0.3f, 0, 0.3f, 0); head(-18, -6); }   // a lean, looking away
        else { emoCorners(200, 0.35f, 0.35f, 0, 0); head(0, 14); }                  // a low bow
        setFace("proud");
      }
    }
    if (currentCommand != c) break;
    // a quick re-check near the tempo it's dancing to (~1.8 s, holding the pose); not that: a full listen (a new
    // song, another tempo); still nothing: the music stopped - wait for it
    bool again = false;
    if (!musicGone) {
      setFace("listening");
      again = listen(2500, P) || listen(3000, 0);
    }
    if (!again) {
      xlogSys("DANCE the music paused after %d beats - waiting for it", beatsDone);
      musicMuteFor(400);
      emoPose(300, POSE_STAND);
      if (!waitForMusic()) break;
      lastLvl = -100;
    }
  }
  musicFreeze(false);  // (the ears back to normal)
  emoFinish(c);
}

// ---- The dance ----
// A little routine on a 120 BPM grid: bounce in, sway side to side, a quick shimmy, paw pops with the weight
// back, a head roll, then a low dip and up tall - ta-da. Every step can be stopped.
inline void runDanceEmote() {
  const char* c = "dance";
  runStandPose(0);
  setFaceWithMode("vibing", FACE_ANIM_LOOP);
  const unsigned long BEAT = 500, t0 = millis();
  int beat = 0;
  auto onBeat = [&](int b) {  // wait for beat b (a late one: straight on)
    const unsigned long until = t0 + b * BEAT;
    while ((long)(millis() - until) < 0) {
      if (currentCommand != c) return false;
      delayWithFace(5);
    }
    return currentCommand == c;
  };
  for (int i = 0; i < 4; i++) {  // 1. bounce in: a dip and a nod on each beat
    if (!onBeat(beat++)) return emoFinish(c);
    emoCorners(140, 0.25f, 0.25f, 0.25f, 0.25f);
    emoHead(0, 10);
    emoCorners(160, 0, 0, 0, 0);
    emoHead(0, -6);
  }
  setFace("happy");
  for (int i = 0; i < 8; i++) {  // 2. sway: the weight from side to side, the head the other way
    if (!onBeat(beat++)) return emoFinish(c);
    const bool r = i & 1;
    emoCorners(380, r ? 0.35f : 0, r ? 0 : 0.35f, r ? 0.35f : 0, r ? 0 : 0.35f);
    emoHead(r ? -14 : 14, -4);
  }
  setFace("excited");
  if (!onBeat(beat)) return emoFinish(c);
  for (int i = 0; i < 8; i++) {  // 3. shimmy: the front corners, double time
    const bool r = i & 1;
    emoCorners(110, r ? 0.3f : 0, r ? 0 : 0.3f, 0, 0);
    emoHead(r ? 8 : -8, -4);
    if (!emoWait(c, 120)) return emoFinish(c);
  }
  beat += 4;
  setFace("cheeky");
  emoCorners(250, 0, 0, 0.3f, 0.3f);  // 4. paw pops: the weight back, a front paw up on each beat
  for (int i = 0; i < 4; i++) {
    if (!onBeat(beat++)) return emoFinish(c);
    const bool r = i & 1;
    emoCorners(150, r ? 0.55f : 0, r ? 0 : 0.55f, 0.3f, 0.3f);
    emoHead(r ? 16 : -16, -10);
    emoCorners(150, 0, 0, 0.3f, 0.3f);
  }
  setFace("vibing");
  emoCorners(250, 0, 0, 0, 0);
  if (!emoOscillate(c, 0, -4, 22, 10, 500, 2, 1.0f)) return emoFinish(c);  // 5. a head roll
  emoCorners(250, 0.4f, 0.4f, 0.4f, 0.4f);  // 6. the finale: down low...
  emoHead(0, 12);
  if (!emoWait(c, 200)) return emoFinish(c);
  playSfx("w:v_tada|523:120 659:120 784:260");
  setFace("proud");
  emoCorners(300, 0, 0, 0.25f, 0.25f);  // ...and up tall, chest out, chin up
  emoHead(0, -18);
  emoWait(c, 900);
  emoFinish(c);
}

// ---- More emotes ----
// Shy: shrinks a little, looks away and down, peeks back, hides again, a small sway, then a happy squint
inline void runShyEmote() {
  const char* c = "shy";
  runStandPose(0);
  setFaceWithMode("shy", FACE_ANIM_ONCE);
  const int side = random(2) ? 1 : -1;
  emoCorners(500, 0.2f, 0.2f, 0.1f, 0.1f);
  if (!emoHeadTo(c, 30 * side, 14, 600) || !emoWait(c, 700)) return emoFinish(c);
  if (!emoHeadTo(c, 10 * side, 6, 400) || !emoWait(c, 300)) return emoFinish(c);
  setFace("embarrassed");
  if (!emoHeadTo(c, 34 * side, 16, 250) || !emoWait(c, 600)) return emoFinish(c);
  for (int i = 0; i < 2; i++) {
    emoCorners(350, 0.3f, 0.1f, 0.2f, 0.0f);
    emoCorners(350, 0.1f, 0.3f, 0.0f, 0.2f);
    if (!emoWait(c, 50)) return emoFinish(c);
  }
  setFace("happy_blink");
  if (!emoHeadTo(c, 0, 0, 500) || !emoWait(c, 400)) return emoFinish(c);
  emoFinish(c);
}

// Hide: ducks down low with the head tucked, peeks left and right, ducks again - then pops up laughing
inline void runHideEmote() {
  const char* c = "hide";
  runStandPose(0);
  setFaceWithMode("scared", FACE_ANIM_ONCE);
  emoHead(0, 24);
  emoCrouch(0.7f, 350);
  if (!emoWait(c, 900)) return emoFinish(c);
  setFace("sneaky");
  if (!emoHeadTo(c, -20, 10, 500) || !emoWait(c, 500)) return emoFinish(c);
  if (!emoHeadTo(c, 20, 10, 700) || !emoWait(c, 500)) return emoFinish(c);
  if (!emoHeadTo(c, 0, 24, 300) || !emoWait(c, 600)) return emoFinish(c);
  setFace("surprised");
  emoCrouch(0.0f, 250);
  emoHead(0, -14);
  setFace("laugh");
  emoWait(c, 700);
  emoFinish(c);
}

// Sneaky: low and slow, eyes left and right, a creeping lean forward, a mischievous grin
inline void runSneakyEmote() {
  const char* c = "sneaky";
  runStandPose(0);
  setFaceWithMode("sneaky", FACE_ANIM_ONCE);
  emoCrouch(0.35f, 600);
  for (int i = 0; i < 2; i++) {
    if (!emoHeadTo(c, 35, 6, 900) || !emoWait(c, 400)) return emoFinish(c);
    if (!emoHeadTo(c, -35, 6, 1100) || !emoWait(c, 400)) return emoFinish(c);
  }
  emoCorners(500, 0.5f, 0.5f, 0.2f, 0.2f);
  if (!emoHeadTo(c, 0, 4, 400) || !emoWait(c, 500)) return emoFinish(c);
  setFace("mischievous");
  emoWait(c, 700);
  emoFinish(c);
}

// Proud: chest out (back sinks), chin up, a slow look around, a ta-da and a nod
inline void runProudEmote() {
  const char* c = "proud";
  runStandPose(0);
  setFaceWithMode("proud", FACE_ANIM_ONCE);
  emoCorners(500, 0, 0, 0.35f, 0.35f);
  if (!emoHeadTo(c, 0, -22, 500) || !emoWait(c, 600)) return emoFinish(c);
  if (!emoHeadTo(c, 30, -18, 700) || !emoHeadTo(c, -30, -18, 1000)) return emoFinish(c);
  if (!emoHeadTo(c, 0, -20, 500)) return emoFinish(c);
  playSfx("w:v_tada|523:120 659:120 784:260");
  if (!emoOscillate(c, 0, -14, 0, 10, 400, 1, 1.0f, 0.4f)) return emoFinish(c);
  emoWait(c, 500);
  emoFinish(c);
}

// Bored: a long sigh, the head droops, it sinks lower, a look around for anything at all, another sigh
inline void runBoredEmote() {
  const char* c = "bored";
  runStandPose(0);
  setFaceWithMode("bored", FACE_ANIM_ONCE);
  emoCrouch(0.25f, 800);
  if (!emoHeadTo(c, 0, 16, 900) || !emoWait(c, 800)) return emoFinish(c);
  playSfx("w:sigh|v0.16 440~300:500");
  if (!emoHeadTo(c, 0, 22, 600) || !emoWait(c, 500)) return emoFinish(c);
  if (!emoHeadTo(c, 30, 12, 1200) || !emoWait(c, 300) || !emoHeadTo(c, -30, 12, 1400) || !emoWait(c, 300)) return emoFinish(c);
  emoCrouch(0.45f, 700);
  if (!emoHeadTo(c, 0, 20, 700)) return emoFinish(c);
  playSfx("w:sigh|v0.16 440~300:500");
  emoWait(c, 900);
  emoFinish(c);
}

// Jam: bops to a beat only it can hear - a dip and a nod each beat, swaying every two
inline void runJamEmote() {
  const char* c = "jam";
  runStandPose(0);
  setFaceWithMode("vibing", FACE_ANIM_LOOP);
  const unsigned long BEAT = 430, t0 = millis();
  for (int b = 0; b < 12; b++) {
    const unsigned long until = t0 + b * BEAT;
    while ((long)(millis() - until) < 0) {
      if (currentCommand != c) return emoFinish(c);
      delayWithFace(5);
    }
    const float side = (b / 2) % 2 ? 1 : -1;
    const float l = side > 0 ? 0.05f : 0.3f, r = side > 0 ? 0.3f : 0.05f;
    emoCorners(150, r, l, r, l);
    emoHead((int)(10 * side), 10);
    emoCorners(150, r * 0.4f, l * 0.4f, r * 0.4f, l * 0.4f);
    emoHead((int)(10 * side), -4);
  }
  setFace("happy");
  emoWait(c, 300);
  emoFinish(c);
}

// Sulk: turns its head away with a huff, sits back, peeks to see if you noticed, looks away again
inline void runSulkEmote() {
  const char* c = "sulk";
  runStandPose(0);
  setFaceWithMode("pout", FACE_ANIM_ONCE);
  const int side = random(2) ? 1 : -1;
  if (!emoHeadTo(c, 45 * side, -4, 500)) return emoFinish(c);
  playSfx("w:v_uhuh|500~380:200");
  emoCorners(700, 0, 0, 0.55f, 0.55f);
  if (!emoWait(c, 1200)) return emoFinish(c);
  if (!emoHeadTo(c, 20 * side, 2, 300) || !emoWait(c, 400)) return emoFinish(c);
  if (!emoHeadTo(c, 45 * side, -4, 200) || !emoWait(c, 900)) return emoFinish(c);
  setFace("sad");
  if (!emoHeadTo(c, 0, 8, 700) || !emoWait(c, 400)) return emoFinish(c);
  setFace("happy");
  emoFinish(c);
}

// Kiss: leans in, eyes closed, a kiss - then shy about it
inline void runKissEmote() {
  const char* c = "kiss";
  runStandPose(0);
  setFaceWithMode("love", FACE_ANIM_ONCE);
  emoCorners(500, 0.35f, 0.35f, 0, 0);
  if (!emoHeadTo(c, 0, -12, 400)) return emoFinish(c);
  setFace("kiss");
  playSfx("w:kiss|1800~2400:60 0:40 2000~2600:60");
  if (!emoWait(c, 700)) return emoFinish(c);
  emoCorners(400, 0, 0, 0, 0);
  setFace("shy");
  if (!emoHeadTo(c, 15, 6, 400) || !emoWait(c, 700)) return emoFinish(c);
  setFace("love");
  emoFinish(c);
}
