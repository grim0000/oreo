// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
// More emotes: body moves on the spot - never a step. Written with the emote helpers of
// movement-sequences.h (included after it).
// Per leg a hip "fwd" and a knee "bend" in degrees from the stand: + fwd swings that foot forward, + bend folds the knee
// (that corner lower - or the foot up, when the leg carries nothing).
#pragma once

// legs: 0 FR (R1 hip, R3 knee), 1 FL (L1, L3), 2 RR (R2, R4), 3 RL (L2, L4)
const uint8_t EM_HIP[4] = {R1, L1, R2, L2}, EM_KNEE[4] = {R3, L3, R4, L4};
const int8_t EM_FWD[4] = {+1, -1, +1, -1}, EM_BEND[4] = {-1, +1, +1, -1};

// four numbers, one per leg (FR FL RR RL)
struct EmV4 { float v[4]; };
#define EMV(...) EmV4{{__VA_ARGS__}}

// the stand + per-leg offsets (fwd, bend; NAN = that joint unchanged) -> eased move, scaled like every emote
inline void emoLegsA(int ms, EmV4 fwd, EmV4 bend) {
  int t[8];
  for (int i = 0; i < 8; i++) t[i] = -999;
  for (int l = 0; l < 4; l++) {
    if (fwd.v[l] == fwd.v[l]) t[EM_HIP[l]] = (int)lroundf(POSE_STAND[EM_HIP[l]] + EM_FWD[l] * fwd.v[l]);
    if (bend.v[l] == bend.v[l]) t[EM_KNEE[l]] = (int)lroundf(POSE_STAND[EM_KNEE[l]] + EM_BEND[l] * bend.v[l]);
  }
  emoLegs(ms, t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7]);
}
#define EMN NAN

// Stomp: impatient - the front paws stomp in turn, a nod with each
inline void runStompEmote() {
  const char* c = "stomp";
  Serial.println(F("STOMP"));
  runStandPose(0);
  setFaceWithMode("pout", FACE_ANIM_ONCE);
  emoLegsA(300, EMV(0, 0, 0, 0), EMV(-8, -8, 24, 24));  // the weight back a little
  for (int i = 0; i < 6; i++) {
    const int l = i & 1;  // FR, FL in turn
    EmV4 b = EMV(-8, -8, 24, 24);
    b.v[l] = 58;  // the paw up
    emoLegsA(180, EMV(0, 0, 0, 0), b);
    emoHead(0, -6);
    b.v[l] = -12;  // and down, hard
    emoLegsA(110, EMV(0, 0, 0, 0), b);
    emoHead(0, 6);
    if (!emoWait(c, 90)) return emoFinish(c);
  }
  setFace("angry");
  if (!emoWait(c, 400)) return emoFinish(c);
  emoFinish(c);
}

// Sway dance: the body rolls side to side, the head sways against it
inline void runSwayDanceEmote() {
  const char* c = "swaydance";
  Serial.println(F("SWAYDANCE"));
  runStandPose(0);
  setFaceWithMode("vibing", FACE_ANIM_ONCE);
  for (int i = 0; i < 8; i++) {
    const float s = (i & 1) ? 1 : -1;  // + = lean right
    emoLegsA(420, EMV(0, 0, 0, 0), EMV(40 * (s > 0), 40 * (s < 0), 40 * (s > 0), 40 * (s < 0)));
    emoHead((int)(-18 * s), -4);
    if (!emoWait(c, 40)) return emoFinish(c);
  }
  emoFinish(c);
}

// Bob: bounces to a beat, nodding with it
inline void runBobEmote() {
  const char* c = "bob";
  Serial.println(F("BOB"));
  runStandPose(0);
  setFaceWithMode("happy", FACE_ANIM_ONCE);
  for (int i = 0; i < 8; i++) {
    emoLegsA(170, EMV(0, 0, 0, 0), EMV(44, 44, 44, 44));
    emoHead(0, 10);
    emoLegsA(170, EMV(0, 0, 0, 0), EMV(0, 0, 0, 0));
    emoHead(0, -6);
    if (!emoWait(c, 20)) return emoFinish(c);
  }
  emoFinish(c);
}

// Peek: leans to one side and peeks around a corner, then the other
inline void runPeekEmote() {
  const char* c = "peek";
  Serial.println(F("PEEK"));
  runStandPose(0);
  setFaceWithMode("curious", FACE_ANIM_ONCE);
  for (int k = 0; k < 2; k++) {
    const int left = k == 0;
    emoLegsA(500, EMV(0, 0, 0, 0), left ? EMV(-12, 52, -12, 52) : EMV(52, -12, 52, -12));
    if (!emoHeadTo(c, left ? 45 : -45, 6, 450) || !emoWait(c, 600)) return emoFinish(c);
    setFace(k ? "happy" : "sneaky");
    if (!emoHeadTo(c, left ? 30 : -30, -6, 250) || !emoWait(c, 300)) return emoFinish(c);
  }
  emoFinish(c);
}

// Doggy shake: a quick wet-dog shake, the whole body rolling, the head whipping
inline void runDoggyShakeEmote() {
  const char* c = "doggyshake";
  Serial.println(F("DOGGYSHAKE"));
  runStandPose(0);
  setFaceWithMode("silly", FACE_ANIM_ONCE);
  emoLegsA(250, EMV(0, 0, 0, 0), EMV(16, 16, 16, 16));
  for (int i = 0; i < 10; i++) {
    const float s = (i & 1) ? 1 : -1;
    emoLegsA(110, EMV(0, 0, 0, 0), EMV(16 + 28 * s, 16 - 28 * s, 16 + 28 * s, 16 - 28 * s));
    emoHead((int)(24 * s), 0);
  }
  setFace("happy");
  if (!emoWait(c, 200)) return emoFinish(c);
  emoFinish(c);
}

// Bow wiggle: a play bow - front down, behind up - and the behind wiggles
inline void runBowWiggleEmote() {
  const char* c = "bowwiggle";
  Serial.println(F("BOWWIGGLE"));
  runStandPose(0);
  setFaceWithMode("excited", FACE_ANIM_ONCE);
  emoLegsA(450, EMV(10, 10, 0, 0), EMV(60, 60, -20, -20));
  if (!emoHeadTo(c, 0, -18, 300)) return emoFinish(c);
  for (int i = 0; i < 6; i++) {
    const float s = (i & 1) ? 1 : -1;
    emoLegsA(200, EMV(10, 10, 18 * s, -18 * s), EMV(60, 60, -20, -20));
    if (!emoWait(c, 20)) return emoFinish(c);
  }
  emoFinish(c);
}

// High five: the weight over to the left and back, the right paw up high - held out for a high five
inline void runHighFiveEmote() {
  const char* c = "highfive";
  Serial.println(F("HIGHFIVE"));
  runStandPose(0);
  setFaceWithMode("excited", FACE_ANIM_ONCE);
  emoLegsA(400, EMV(0, -6, 0, 0), EMV(0, -8, 14, 14));
  emoLegsA(350, EMV(28, -6, 0, 0), EMV(72, -8, 14, 14));
  if (!emoHeadTo(c, -12, -16, 300) || !emoWait(c, 1100)) return emoFinish(c);
  setFace("starstruck");
  for (int i = 0; i < 2; i++) {
    emoLegsA(140, EMV(28, -6, 0, 0), EMV(56, -8, 14, 14));
    emoLegsA(140, EMV(28, -6, 0, 0), EMV(72, -8, 14, 14));
  }
  emoLegsA(350, EMV(0, -6, 0, 0), EMV(0, -8, 14, 14));
  emoFinish(c);
}

// March: marches on the spot, the diagonal pairs in turn, head proud
inline void runMarchEmote() {
  const char* c = "march";
  Serial.println(F("MARCH"));
  runStandPose(0);
  setFaceWithMode("proud", FACE_ANIM_ONCE);
  if (!emoHeadTo(c, 0, -10, 250)) return emoFinish(c);
  for (int i = 0; i < 10; i++) {
    const bool a = i & 1;  // FR + RL, or FL + RR
    emoLegsA(170, EMV(0, 0, 0, 0), a ? EMV(58, 0, 0, 58) : EMV(0, 58, 58, 0));
    emoLegsA(150, EMV(0, 0, 0, 0), EMV(0, 0, 0, 0));
    if (!emoWait(c, 20)) return emoFinish(c);
  }
  emoFinish(c);
}

// Hula: the body circles - each corner dips in turn, round and round
inline void runHulaEmote() {
  const char* c = "hula";
  Serial.println(F("HULA"));
  runStandPose(0);
  setFaceWithMode("vibing", FACE_ANIM_ONCE);
  const int steps = 12;
  for (int k = 0; k < steps * 3; k++) {
    const float ph = 6.2831853f * k / steps;
    // FR, FL, RL, RR around the body
    const float b0 = 24 + 24 * sinf(ph), b1 = 24 + 24 * sinf(ph + 1.5708f), b3 = 24 + 24 * sinf(ph + 3.1416f), b2 = 24 + 24 * sinf(ph + 4.7124f);
    emoLegsA(110, EMV(0, 0, 0, 0), EMV(b0, b1, b2, b3));
    emoHead((int)(14 * cosf(ph)), (int)(6 * sinf(ph)));
    if (currentCommand != c) return emoFinish(c);
  }
  emoFinish(c);
}

// Body nod: the whole body nods yes - front down, back down - with the head
inline void runBodyNodEmote() {
  const char* c = "bodynod";
  Serial.println(F("BODYNOD"));
  runStandPose(0);
  setFaceWithMode("happy", FACE_ANIM_ONCE);
  for (int i = 0; i < 3; i++) {
    emoLegsA(300, EMV(0, 0, 0, 0), EMV(60, 60, -12, -12));
    emoHead(0, 14);
    emoLegsA(300, EMV(0, 0, 0, 0), EMV(-12, -12, 52, 52));
    emoHead(0, -14);
    if (!emoWait(c, 40)) return emoFinish(c);
  }
  emoFinish(c);
}

// Twist look: the body twists left and right on its feet, the head looking further round
inline void runTwistLookEmote() {
  const char* c = "twistlook";
  Serial.println(F("TWISTLOOK"));
  runStandPose(0);
  setFaceWithMode("curious", FACE_ANIM_ONCE);
  for (int k = 0; k < 2; k++) {
    const float s = k ? -1 : 1;  // + = turn left
    emoLegsA(500, EMV(-20 * s, 20 * s, -20 * s, 20 * s), EMV(0, 0, 0, 0));
    if (!emoHeadTo(c, (int)(50 * s), -4, 400) || !emoWait(c, 500)) return emoFinish(c);
  }
  emoLegsA(400, EMV(0, 0, 0, 0), EMV(0, 0, 0, 0));
  emoFinish(c);
}

// Squats: deep crouch and up again, four reps - working out
inline void runSquatsEmote() {
  const char* c = "squats";
  Serial.println(F("SQUATS"));
  runStandPose(0);
  setFaceWithMode("proud", FACE_ANIM_ONCE);
  for (int i = 0; i < 4; i++) {
    emoCrouch(0.55f, 450);
    emoHead(0, 8);
    if (!emoWait(c, 120)) return emoFinish(c);
    emoCrouch(0.0f, 380);
    emoHead(0, -8);
    if (!emoWait(c, 120)) return emoFinish(c);
  }
  setFace("happy");
  emoFinish(c);
}

// Shimmy: front and back twist against each other, fast
inline void runShimmyEmote() {
  const char* c = "shimmy";
  Serial.println(F("SHIMMY"));
  runStandPose(0);
  setFaceWithMode("cheeky", FACE_ANIM_ONCE);
  emoLegsA(200, EMV(0, 0, 0, 0), EMV(20, 20, 20, 20));
  for (int i = 0; i < 12; i++) {
    const float s = (i & 1) ? 1 : -1;
    emoLegsA(120, EMV(16 * s, 16 * s, -16 * s, -16 * s), EMV(20, 20, 20, 20));
    emoHead((int)(10 * s), -4);
  }
  emoFinish(c);
}

// Itch kick: the weight forward and left, the back right foot up - a quick scratching kick
inline void runItchKickEmote() {
  const char* c = "itchkick";
  Serial.println(F("ITCHKICK"));
  runStandPose(0);
  setFaceWithMode("silly", FACE_ANIM_ONCE);
  emoLegsA(400, EMV(0, 0, 0, 0), EMV(-6, 14, -6, 18));
  emoLegsA(300, EMV(0, 0, 0, 0), EMV(-6, 14, 56, 18));
  if (!emoHeadTo(c, -20, 10, 250)) return emoFinish(c);
  for (int i = 0; i < 7; i++) {
    emoLegsA(90, EMV(0, 0, 16, 0), EMV(-6, 14, 56, 18));
    emoLegsA(90, EMV(0, 0, -12, 0), EMV(-6, 14, 56, 18));
  }
  emoLegsA(300, EMV(0, 0, 0, 0), EMV(-6, 14, -6, 18));
  setFace("happy");
  emoFinish(c);
}
