// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
// Desk mode's body: moves in place from the sit pose - never a step. The moves are keyframes
// (desk_moves_table.h). Per leg a hip "fwd" and a knee "bend" in degrees,
// so each leg is its own hip + knee. The sit is belly-down, so the lively moves first lift the body onto a half-stand, move there, and
// settle back. amp (liveliness) scales the offsets (0.8 .. 1.0 of the design).
// Included by desk_mode.h (main.cpp: after the IMU and servo globals).
#pragma once
#include "desk_moves_table.h"

namespace deskbody {

enum Move : uint8_t { MV_NONE, MV_BREATHE, MV_LEAN_L, MV_LEAN_R, MV_LEAN_F, MV_PAW_L, MV_PAW_R, MV_PAW_TAP, MV_STRETCH,
                      MV_SIT_UP, MV_LIE_DOWN, MV_HOP, MV_SHIVER, MV_CROUCH, MV_WIGGLE, MV_TWIST, MV_HAPPY_HOP, MV_BOUNCE, MV_N };
inline const char* moveName(Move m) {
  static const char* const N[] = {"none", "breathe", "lean_l", "lean_r", "lean_f", "paw_l", "paw_r", "paw_tap", "stretch",
                                  "sit_up", "lie_down", "hop", "shiver", "crouch", "wiggle", "twist", "happy_hop", "bounce"};
  return m < MV_N ? N[m] : "?";
}
inline Move moveByName(const char* s) {
  for (int i = 1; i < MV_N; i++) if (!strcmp(s, moveName((Move)i))) return (Move)i;
  return MV_NONE;
}

#define DK(name) n = sizeof(K_##name) / sizeof(LKey); return K_##name
inline const LKey* lkeys(Move m, int& n) {
  switch (m) {
    case MV_LEAN_L: DK(lean_l);
    case MV_LEAN_R: DK(lean_r);
    case MV_LEAN_F: DK(lean_f);
    case MV_PAW_L: DK(paw_l);
    case MV_PAW_R: DK(paw_r);
    case MV_PAW_TAP: DK(paw_tap);
    case MV_STRETCH: DK(stretch);
    case MV_SIT_UP: DK(sit_up);
    case MV_HOP: DK(hop);
    case MV_CROUCH: DK(crouch);
    case MV_WIGGLE: DK(wiggle);
    case MV_TWIST: DK(twist);
    case MV_HAPPY_HOP: DK(happy_hop);
    case MV_BOUNCE: DK(bounce);
    default: n = 0; return nullptr;
  }
}
#undef DK

// each leg's servos (R1 R2 L1 L2 R4 R3 L3 L4 = 0..7) and the signs that make + mean "foot forward" / "fold further"
// legs: FR (R1 hip, R3 knee), FL (L1, L3), RR (R2, R4), RL (L2, L4) - as the wave and the walk use them
const uint8_t LEG_HIP[4] = {0, 2, 1, 3}, LEG_KNEE[4] = {5, 6, 4, 7};
const int8_t HIP_FWD[4] = {+1, -1, +1, -1}, KNEE_BEND[4] = {-1, +1, +1, -1};

Move now = MV_NONE;
bool folded = false;         // the page's "Fold legs": legs folded up and off - no body moves at all (head, face, sound only)
float amp = 0.6f;
unsigned long t0 = 0;
bool lying = false;          // lie_down done: stays down until bodyStop / sit
unsigned long lastMoveMs = 0;  // the last move other than breathing (for the charge detection)
float tilt0 = 0;             // the body's tilt when the move started: sitting is already ~43 deg off standing level
float base[8], half[8], span[8];  // the sit pose, the half-stand (body lifted off the desk), stand -> lying per servo
int sent[8];

inline void calc() {
  for (int i = 0; i < 8; i++) {
    const float stand = POSE_STAND[i], rest = i < 4 ? POSE_REST[i] : REST_BACK[i - 4];
    span[i] = rest - stand;
    base[i] = i < 4 ? stand : rest;  // sitting: hips as standing, knees folded (belly on the desk)
    half[i] = base[i] + 0.6f * (stand - base[i]);
  }
}
inline void send(int i, float v) {
  const int a = (int)lroundf(v);
  if (a != sent[i]) { setServoAngle(i, a); sent[i] = a; }
}
// fraction moves, for breathe / shiver / lie_down: f per leg (0 = sit, + = toward lying)
inline void apply(float fr, float fl, float br, float bl, bool full) {
  const float f[4] = {fr, fl, br, bl};
  const float cap = full ? 999.0f : 20.0f * amp;
  for (int l = 0; l < 4; l++) {
    for (int s : {LEG_HIP[l], LEG_KNEE[l]}) {
      float off = f[l] * span[s] * (full ? 1.0f : amp);
      if (off > cap) off = cap;
      if (off < -cap) off = -cap;
      send(s, base[s] + off);
    }
  }
}
// a key, blended: the pose of key a -> b at u (0..1)
inline void applyKeys(const LKey& a, const LKey& b, float u) {
  const float k = constrain(0.5f + 0.5f * amp, 0.8f, 1.0f);
  for (int l = 0; l < 4; l++) {
    const int h = LEG_HIP[l], kn = LEG_KNEE[l];
    const float hA = (a.half ? half[h] : base[h]) + HIP_FWD[l] * a.fwd[l] * k, hB = (b.half ? half[h] : base[h]) + HIP_FWD[l] * b.fwd[l] * k;
    const float kA = (a.half ? half[kn] : base[kn]) + KNEE_BEND[l] * a.bend[l] * k, kB = (b.half ? half[kn] : base[kn]) + KNEE_BEND[l] * b.bend[l] * k;
    send(h, hA + (hB - hA) * u);
    send(kn, kA + (kB - kA) * u);
  }
}

inline void bodyStart(Move m, float a) {
  if (folded) { now = MV_NONE; return; }
  calc();
  if (now == MV_NONE || lying) for (int i = 0; i < 8; i++) sent[i] = currentAngle[i];
  now = m;
  amp = a;
  t0 = millis();
  tilt0 = imuOk ? imuTiltFastDeg : 0;
  if (m != MV_BREATHE) lastMoveMs = millis();
  if (m != MV_LIE_DOWN) lying = false;
}
inline Move bodyNow() { return now; }

// Ease back to the sit pose (also after a tilt)
inline void bodyStop() {
  calc();
  now = MV_NONE;
  lying = false;
  if (folded) return;  // (the legs stay folded)
  moveSmoothHelper(500, (int)base[0], (int)base[1], (int)base[2], (int)base[3], (int)base[4], (int)base[5], (int)base[6], (int)base[7]);
  for (int i = 0; i < 8; i++) sent[i] = (int)base[i];
}

// Each loop pass; true while a move runs (breathe runs until another move starts)
inline bool bodyUpdate(unsigned long ms) {
  if (now == MV_NONE) return false;
  // more than 25 deg beyond where the move started (the sit pose itself is ~43 deg from standing level; the paw waves
  // lean ~13 deg by design)
  if (imuOk && imuTiltFastDeg - tilt0 > 25 && now != MV_LIE_DOWN && now != MV_STRETCH) {
    xlog("DESK tilt -> sit (%s, %.0f deg, started at %.0f)", moveName(now), imuTiltFastDeg, tilt0);
    bodyStop();
    return false;
  }
  const float t = (ms - t0) / 1000.0f;
  if (now == MV_BREATHE) {  // the back legs, a slow breath
    const float s = 0.08f * sinf(t * 2 * PI * 0.25f);
    apply(0, 0, s, s, false);
    return true;
  }
  if (now == MV_SHIVER) {  // a quick tremble of all four for 1.2 s
    if (t > 1.2f) { apply(0, 0, 0, 0, false); now = MV_NONE; return false; }
    const float s = 0.10f * sinf(t * 2 * PI * 8);
    apply(s, -s, s, -s, false);
    return true;
  }
  if (now == MV_LIE_DOWN) {  // lies all the way down (stays down)
    const float u = min(1.0f, t / 1.5f);
    const float e = u * u * (3 - 2 * u);
    apply(e, e, e, e, true);
    if (u >= 1) { lying = true; now = MV_NONE; return false; }
    return true;
  }
  int n = 0;
  const LKey* k = lkeys(now, n);
  if (!k || n < 2) { now = MV_NONE; return false; }
  if (t >= k[n - 1].t) {
    applyKeys(k[n - 1], k[n - 1], 1);
    now = MV_NONE;
    return false;
  }
  int i = 0;
  while (i < n - 2 && t >= k[i + 1].t) i++;
  float u = (t - k[i].t) / (k[i + 1].t - k[i].t);
  u = u * u * (3 - 2 * u);  // eased
  applyKeys(k[i], k[i + 1], u);
  return true;
}

}  // namespace deskbody
