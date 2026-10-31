// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
// Desk mode's long-term self (EMO-style): who Oreo has become over days and weeks - kept across
// restarts. Traits drift slowly with how it's treated, a bond with you grows with pats and time together (and
// fades a little when you're away for days), it learns the hours you're usually at the desk, and now and then
// a life event happens (a cold for a day, hiccups). The brain (desk_brain.h) reads it to weigh its choices.
// Plain C++ (no Arduino), so it can also be tested on a PC. Times in seconds (epoch or uptime).
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "desk_brain.h"

namespace desk {

enum Trait { T_PLAYFUL, T_AFFECTION, T_ENERGETIC, T_BRAVE, T_CURIOUS, T_N };
inline const char* traitName(int t) {
  static const char* const N[] = {"playful", "affectionate", "energetic", "brave", "curious"};
  return t >= 0 && t < T_N ? N[t] : "?";
}
enum LifeEvent : uint8_t { LE_NONE, LE_COLD, LE_HICCUPS, LE_N };
inline const char* lifeEventName(int e) {
  static const char* const N[] = {"none", "a cold", "hiccups"};
  return e >= 0 && e < LE_N ? N[e] : "?";
}

// What's saved (flash): plain bytes, versioned
struct PersonaData {
  uint16_t version = 2;
  float trait[T_N] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
  float bond = 0.2f;               // 0 a stranger .. 1 best friends
  float seenAtHour[24] = {0};      // how often you're here at each hour (0..1, slow average)
  uint32_t bornEpoch = 0;          // the first day it knew the time
  uint32_t lastDayEpoch = 0;       // the last daily update (local midnight-ish)
  uint32_t lastUserEpoch = 0;      // when you were last here
  uint32_t pats = 0, taps = 0, greetings = 0, scenes = 0;
  uint8_t event = LE_NONE;         // a life event now
  uint32_t eventUntilEpoch = 0;
  // what happened today (the daily drift reads it, then it's cleared)
  uint16_t todayPats = 0, todayTaps = 0, todayLoud = 0, todayPlay = 0, todayNaps = 0;
  uint16_t todayUserMin = 0;
};

class Persona {
 public:
  PersonaData d;
  bool dirty = false;

  // ---- the day's tally (events from desk mode) ----
  void onPat()   { d.pats++; d.todayPats++; d.bond = c01(d.bond + 0.001f); dirty = true; }
  void onTap()   { d.taps++; d.todayTaps++; dirty = true; }
  void onLoud()  { d.todayLoud++; dirty = true; }
  void onPlay()  { d.scenes++; d.todayPlay++; dirty = true; }
  void onNap()   { d.todayNaps++; dirty = true; }
  void onGreet() { d.greetings++; d.bond = c01(d.bond + 0.002f); dirty = true; }

  // ---- every minute: you here or not (learns your hours), the life event's end ----
  // hour -1: no clock yet (nothing learned)
  void minuteTick(bool userHere, int hour, uint32_t epoch) {
    if (userHere) { d.todayUserMin++; if (epoch) d.lastUserEpoch = epoch; d.bond = c01(d.bond + 0.00003f); }
    if (hour >= 0 && hour < 24) {
      // a slow average per hour (~2 weeks to learn a habit): each minute of that hour counts a little
      float& s = d.seenAtHour[hour];
      s += ((userHere ? 1.0f : 0.0f) - s) * 0.0025f;
    }
    if (d.event != LE_NONE && epoch && epoch > d.eventUntilEpoch) d.event = LE_NONE;
    dirty = true;
  }

  // ---- once a day: the traits drift toward how the day went; maybe a life event ----
  // epoch: now (0 = no clock: nothing happens); r01: a random 0..1; coldDay: the weather was cold (< 15 C)
  bool dailyMaybe(uint32_t epoch, float r01, float r02, bool coldDay) {
    if (!epoch) return false;
    if (!d.bornEpoch) d.bornEpoch = epoch;
    if (!d.lastDayEpoch) { d.lastDayEpoch = epoch; dirty = true; return false; }
    if (epoch - d.lastDayEpoch < 20 * 3600) return false;
    const int days = (int)((epoch - d.lastDayEpoch) / 86400) + 1;
    d.lastDayEpoch = epoch;
    // played with a lot -> more playful; lots of pats -> more affectionate; many loud days -> more timid;
    // napping all day -> less energetic.
    // (a day moves a trait ~1%, and every day pulls it 4% back toward the middle: a strong habit settles
    // near 0.8 / 0.2 after a couple of months - a personality, never stuck at an end)
    nudge(T_PLAYFUL, d.todayPlay > 3 ? 0.012f : d.todayPlay == 0 ? -0.008f : 0);
    nudge(T_AFFECTION, d.todayPats > 5 ? 0.012f : d.todayPats == 0 ? -0.006f : 0);
    nudge(T_BRAVE, d.todayLoud > 4 ? -0.01f : 0.004f);
    nudge(T_ENERGETIC, d.todayNaps > 4 ? -0.01f : d.todayNaps <= 1 ? 0.006f : 0);
    nudge(T_CURIOUS, d.todayUserMin > 120 ? 0.006f : 0.002f);
    for (int i = 0; i < T_N; i++) d.trait[i] += (0.5f - d.trait[i]) * 0.04f;
    // away for days: the bond fades a little (never below 0.1 once you've met)
    const float awayDays = d.lastUserEpoch ? (epoch - d.lastUserEpoch) / 86400.0f : 0;
    if (awayDays > 1.5f) d.bond = fmaxf(0.1f, d.bond - 0.02f * (awayDays - 1));
    // a life event: a cold ~1 in 25 days (1 in 8 on cold days), hiccups ~1 in 6 days
    if (d.event == LE_NONE) {
      if (r01 < (coldDay ? 0.12f : 0.04f)) { d.event = LE_COLD; d.eventUntilEpoch = epoch + 20 * 3600; }
      else if (r02 < 0.16f) { d.event = LE_HICCUPS; d.eventUntilEpoch = epoch + 2 * 3600; }
    }
    d.todayPats = d.todayTaps = d.todayLoud = d.todayPlay = d.todayNaps = d.todayUserMin = 0;
    (void)days;
    dirty = true;
    return true;
  }

  // ---- what the brain reads ----
  float t(Trait x) const { return d.trait[x]; }
  // you're usually here at this hour (learned): 0..1
  float expectUser(int hour) const { return hour >= 0 && hour < 24 ? d.seenAtHour[hour] : 0; }
  // how much it missed you (away long, a strong bond): 0..1
  float missed(uint32_t epoch) const {
    if (!epoch || !d.lastUserEpoch) return 0;
    const float h = (epoch - d.lastUserEpoch) / 3600.0f;
    return c01((h - 2) / 22) * (0.4f + 0.6f * d.bond);
  }
  int ageDays(uint32_t epoch) const { return d.bornEpoch && epoch > d.bornEpoch ? (int)((epoch - d.bornEpoch) / 86400) : 0; }
  bool hasCold() const { return d.event == LE_COLD; }
  bool hasHiccups() const { return d.event == LE_HICCUPS; }

  // the brain's weights, per behaviour (1 = no change)
  float behMul(Beh b) const {
    const float pl = d.trait[T_PLAYFUL], af = d.trait[T_AFFECTION], en = d.trait[T_ENERGETIC], cu = d.trait[T_CURIOUS];
    float m = 1;
    switch (b) {
      case B_SCENE: case B_SHOWOFF: m = 0.5f + pl; break;
      case B_FIDGET: m = 0.6f + 0.8f * pl; break;
      case B_SEEK_USER: case B_FOLLOW_USER: m = 0.5f + af + 0.6f * d.bond; break;
      case B_INVESTIGATE: case B_GLANCE: m = 0.5f + cu; break;
      case B_NAP: case B_SLEEP: m = 1.5f - en; break;
      case B_STRETCH: m = 0.6f + 0.8f * en; break;
      case B_WANDER: m = 0.4f + cu + 0.4f * en; break;
      default: break;
    }
    if (hasCold()) m *= b == B_NAP || b == B_SLEEP ? 1.8f : b == B_SCENE || b == B_SHOWOFF ? 0.5f : 1;
    return m;
  }
  // the greeting fits the bond and how long you were gone: 0 shy, 1 friendly, 2 overjoyed ("missed you!")
  uint8_t greetStyle(uint32_t epoch) const {
    const float mi = missed(epoch);
    if (d.bond < 0.25f) return 0;
    return mi > 0.5f ? 2 : 1;
  }

  // a short line for the page / log
  void describe(char* out, int n, uint32_t epoch) const {
    int best = 0;
    for (int i = 1; i < T_N; i++) if (fabsf(d.trait[i] - 0.5f) > fabsf(d.trait[best] - 0.5f)) best = i;
    snprintf(out, n, "%s%s, bond %.0f%%, %d days old%s%s", d.trait[best] >= 0.5f ? "" : "not very ", traitName(best),
             d.bond * 100, ageDays(epoch), d.event ? ", has " : "", d.event ? lifeEventName(d.event) : "");
  }

 private:
  static float c01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
  void nudge(int tr, float by) { d.trait[tr] = c01(d.trait[tr] + by); }
};

}  // namespace desk
