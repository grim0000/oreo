// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
#pragma once
// Which PCA9685 channel drives which servo. Wire your servos to any channels, then match them up on the servo
// mapping page (http://<robot>/servomap): it saves the map in flash. The defaults are the reference build's wiring.
#include <Preferences.h>

// The firmware's leg order (index = the leg servo's id everywhere in the code):
//   0 R1 front-right hip   1 R2 back-right hip   2 L1 front-left hip   3 L2 back-left hip
//   4 R4 back-right knee   5 R3 front-right knee 6 L3 front-left knee  7 L4 back-left knee
int mappedPins[8] = {14, 4, 12, 13, 8, 9, 11, 10};
int headPanCh = 7, headTiltCh = 5;

const char* const SERVO_ROLE_ID[10] = {"R1", "R2", "L1", "L2", "R4", "R3", "L3", "L4", "PAN", "TILT"};
const char* const SERVO_ROLE_NAME[10] = {"Front-right hip", "Back-right hip", "Front-left hip", "Back-left hip",
                                         "Back-right knee", "Front-right knee", "Front-left knee", "Back-left knee",
                                         "Head pan (left/right)", "Head tilt (up/down)"};
const int SERVO_DEFAULT_CH[10] = {14, 4, 12, 13, 8, 9, 11, 10, 7, 5};

inline int servoChOf(int role) { return role < 8 ? mappedPins[role] : (role == 8 ? headPanCh : headTiltCh); }

inline void servoMapLoad() {
  Preferences p;
  p.begin("svmap", true);
  uint8_t b[10];
  if (p.getBytesLength("m") == 10 && p.getBytes("m", b, 10) == 10) {
    bool ok = true;
    for (int i = 0; i < 10; i++) ok = ok && b[i] < 16;
    if (ok) {
      for (int i = 0; i < 8; i++) mappedPins[i] = b[i];
      headPanCh = b[8];
      headTiltCh = b[9];
    }
  }
  p.end();
}

inline void servoMapSave() {
  uint8_t b[10];
  for (int i = 0; i < 10; i++) b[i] = (uint8_t)servoChOf(i);
  Preferences p;
  p.begin("svmap", false);
  p.putBytes("m", b, 10);
  p.end();
}

inline void servoMapSet(int role, int ch) {
  if (role < 0 || role > 9 || ch < 0 || ch > 15) return;
  if (role < 8) mappedPins[role] = ch;
  else if (role == 8) headPanCh = ch;
  else headTiltCh = ch;
}

inline void servoMapReset() {
  for (int i = 0; i < 10; i++) servoMapSet(i, SERVO_DEFAULT_CH[i]);
  servoMapSave();
}
