// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
// Oreo's walk with the servos on a PCA9685 board (Oreo's wiring). Serial monitor at 115200, send one letter:
//   w forward   s backward   a curve left   d curve right   q turn left   e turn right
//   t balanced trot   x stand   + / - speed
// Copy ../../oreo_gait.h next to this sketch (Arduino IDE) or add the gait folder to the include path.
#define OREO_GAIT_PCA9685
#include "oreo_gait.h"

oreo::GaitEngine gait;

void setup() {
  Serial.begin(115200);
  oreo::outputBegin(1, 2);  // I2C SDA, SCL
  // your calibration page values (leg-id order R1 R2 L1 L2 R4 R3 L3 L4); the built-in ones until you set them
  // gait.cal.mid[oreo::R1] = 25; ...
  gait.stand(millis());
  Serial.println("w s a d q e t x + -");
}

void loop() {
  const uint32_t now = millis();
  if (Serial.available()) {
    switch (Serial.read()) {
      case 'w': gait.start(oreo::WALK_FORWARD, now); break;
      case 's': gait.start(oreo::WALK_BACKWARD, now); break;
      case 'a': gait.start(oreo::WALK_LEFT, now); break;
      case 'd': gait.start(oreo::WALK_RIGHT, now); break;
      case 'q': gait.start(oreo::TURN_LEFT, now); break;
      case 'e': gait.start(oreo::TURN_RIGHT, now); break;
      case 't': gait.start(oreo::balancedTrot(), now); break;
      case 'x': gait.stop(now); break;
      case '+': gait.speed = fminf(1.5f, gait.speed + 0.1f); break;
      case '-': gait.speed = fmaxf(0.5f, gait.speed - 0.1f); break;
    }
  }
  static uint32_t last = 0;
  if (now - last >= 20) { last = now; gait.update(now); }
}
