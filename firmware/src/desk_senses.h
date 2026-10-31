// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
// Desk mode's senses: raw readings -> events for the brain (desk_brain.h). Plain C++ (no Arduino),
// so it can also be tested on a PC. Times in ms.
#pragma once
#include <stdint.h>
#include "desk_brain.h"

namespace desk {

// A bump on the desk: the gyro's jolt over 0.35 g, at most one per 600 ms
struct TapDetector {
  uint32_t lastMs = 0;
  bool any = false;
  bool feed(float joltG, uint32_t ms, float threshold = 0.35f) {
    if (joltG <= threshold || (any && ms - lastMs < 600)) return false;
    any = true;
    lastMs = ms;
    return true;
  }
};

// The user: a face on camera, or something 4-25 cm in front of the ToF for 1 s (works without the
// camera too); gone after 20 s with neither
struct Presence {
  bool present = false;
  uint32_t lastSeenMs = 0, seenSinceMs = 0;
  bool seeing = false;
  Event feed(bool faceSeen, int tofMm, uint32_t ms) {
    const bool near = tofMm > 40 && tofMm < 250;
    if (faceSeen || near) {
      if (!seeing) { seeing = true; seenSinceMs = ms; }
      lastSeenMs = ms;
      if (!present && (faceSeen || ms - seenSinceMs >= 1000)) { present = true; return E_USER_ARRIVED; }
      return E_NONE;
    }
    seeing = false;
    if (present && ms - lastSeenMs > 20000) { present = false; return E_USER_LEFT; }
    return E_NONE;
  }
  float awayMin(uint32_t ms) const { return present ? 0 : (ms - lastSeenMs) / 60000.0f; }
};

// Sound from the mic's level (0..32767): a 10 s average and a 10 min floor
struct Sound {
  float avg10s = -1, floor10m = -1;
  uint32_t startMs = 0, lastMs = 0, lastLoudMs = 0, lastMusicMs = 0, aboveSinceMs = 0, quietSinceMs = 0;
  bool loudAny = false, musicAny = false, above = false, quiet = false, silenceSaid = false;
  Event feed(int level, uint32_t ms) {
    if (avg10s < 0) { avg10s = floor10m = (float)level; lastMs = ms; quietSinceMs = ms; quiet = true; startMs = ms; }
    const float dt = (ms - lastMs) / 1000.0f;
    lastMs = ms;
    Event e = E_NONE;
    const bool warm = ms - startMs >= 10000;  // (the averages settle first: the mic starts near 0)
    if (warm && level > 4 * avg10s && level > 3000 && (!loudAny || ms - lastLoudMs >= 5000)) {
      loudAny = true;
      lastLoudMs = ms;
      e = E_LOUD;
    }
    // steady sound well over the room's floor for 8 s: music (or a talk): bob along
    if (level > 2 * floor10m) {
      if (!above) { above = true; aboveSinceMs = ms; }
      if (warm && ms - aboveSinceMs >= 8000 && (!musicAny || ms - lastMusicMs >= 120000) && e == E_NONE) {
        musicAny = true;
        lastMusicMs = ms;
        e = E_MUSIC;
      }
    } else above = false;
    // quiet for 10 minutes: doze
    if (level < 1.3f * floor10m + 1) {
      if (!quiet) { quiet = true; quietSinceMs = ms; silenceSaid = false; }
      if (!silenceSaid && ms - quietSinceMs >= 600000 && e == E_NONE) { silenceSaid = true; e = E_SILENCE; }
    } else quiet = false;
    avg10s += (level - avg10s) * (dt / (10 + dt));
    floor10m += (level - floor10m) * (dt / (600 + dt));
    // the quietest the room has been (the mic's own resting level, ~7500 on the robot): noise is
    // relative to it; it creeps up 1%/min so a moved robot re-learns
    if (warm) {
      if (quietRef < 0 || avg10s < quietRef) quietRef = avg10s;
      else quietRef *= 1 + 0.01f * dt / 60;
    }
    return e;
  }
  float quietRef = -1;
  float noise01() const {
    if (quietRef <= 0) return 0;
    const float x = (floor10m / quietRef - 1) / 2;  // 3x the quiet level = 1
    return x < 0 ? 0 : x > 1 ? 1 : x;
  }
};

// Room light 0..1 (camera): dark after 30 s under 0.15, light again after 10 s over 0.3
struct Light {
  int state = 0;  // 0 unknown, 1 dark, 2 light
  uint32_t darkSince = 0, lightSince = 0;
  bool inDark = false, inLight = false;
  Event feed(float light01, uint32_t ms) {
    if (light01 < 0.15f) {
      if (!inDark) { inDark = true; darkSince = ms; }
      inLight = false;
      if (state != 1 && ms - darkSince >= 30000) { state = 1; return E_DARK; }
    } else if (light01 > 0.3f) {
      if (!inLight) { inLight = true; lightSince = ms; }
      inDark = false;
      if (state == 1 && ms - lightSince >= 10000) { state = 2; return E_LIGHT; }
      if (state == 0) state = 2;
    } else inDark = inLight = false;
    return E_NONE;
  }
};

}  // namespace desk
