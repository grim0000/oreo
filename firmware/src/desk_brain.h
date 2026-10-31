// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
// Desk mode's inner life: five needs that
// drift with time and the senses, a mood from them, and (below) the choice of what to do next.
// Plain C++ (no Arduino), so it can also be tested on a PC.
#pragma once
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace desk {

enum Need { ENERGY, BOREDOM, CURIOSITY, COMPANY, COMFORT, NEED_N };
enum Mood : uint8_t { M_SLEEPY, M_CONTENT, M_CURIOUS, M_LONELY, M_EXCITED, M_GRUMPY };
enum Weather : uint8_t { W_UNKNOWN, W_CLEAR, W_CLOUDY, W_RAIN, W_STORM, W_FOG, W_SNOW };

enum Beh : uint8_t { B_IDLE, B_GLANCE, B_FIDGET, B_STRETCH, B_SCENE, B_SEEK_USER, B_GREET, B_NAP, B_SLEEP, B_SHOWOFF,
                     B_INVESTIGATE, B_FOLLOW_USER, B_ASK_CHARGE, B_REACT, B_CLOCK, B_WANDER, B_N };
enum Live : uint8_t { CALM, NORMAL, PLAYFUL };

inline const char* behName(Beh b) {
  static const char* const N[] = {"idle", "glance", "fidget", "stretch", "scene", "seek_user", "greet", "nap", "sleep",
                                  "showoff", "investigate", "follow_user", "ask_charge", "react", "clock", "wander"};
  return b < B_N ? N[b] : "?";
}

enum Event : uint8_t { E_NONE, E_USER_ARRIVED, E_USER_LEFT, E_TAP, E_PAT, E_LOUD, E_MUSIC, E_SILENCE, E_DARK, E_LIGHT,
                       E_BATTERY_LOW, E_N };
inline const char* eventName(Event e) {
  static const char* const N[] = {"none", "user_arrived", "user_left", "tap", "pat", "loud", "music", "silence", "dark",
                                  "light", "battery_low"};
  return e < E_N ? N[e] : "?";
}
// The reactions to each event, by variant (Brain::onEvent picks the variant)
inline const char* reactionName(Event e, uint8_t v) {
  switch (e) {
    case E_TAP: return v == 0 ? "startle" : v == 1 ? "giggle" : "annoyed";
    case E_PAT: return v == 0 ? "purr" : v == 1 ? "happy_squint" : "lean_in";
    case E_USER_ARRIVED: return v == 0 ? "big_greet" : v == 1 ? "small_greet" : "glance_smile";
    case E_USER_LEFT: return "watch_spot";
    case E_LOUD: return v == 0 ? "flinch" : "flinch_look";
    case E_MUSIC: return "head_bob";
    case E_SILENCE: return "doze";
    case E_DARK: return "go_to_bed";
    case E_LIGHT: return "wake_stretch";
    case E_BATTERY_LOW: return "ask_charge";
    default: return "none";
  }
}

inline const char* moodName(Mood m) {
  static const char* const N[] = {"sleepy", "content", "curious", "lonely", "excited", "grumpy"};
  return m <= M_GRUMPY ? N[m] : "?";
}
inline const char* weatherName(Weather w) {
  static const char* const N[] = {"unknown", "clear", "cloudy", "rain", "storm", "fog", "snow"};
  return w <= W_SNOW ? N[w] : "?";
}

// What the brain knows about the world (refreshed every minute; light and noise 0..1)
struct Context {
  int hour = 12, minute = 0, weekday = 1;  // weekday 0 = Sunday
  Weather weather = W_UNKNOWN;
  float tempC = 25, light = 0.5f, noise = 0.2f;
  bool userPresent = false;
  float userAwayMin = 0;
  int batteryPct = 80;
  bool charging = false;
  bool canWalk = false;  // the page allowed walking on the desk (edge-safe wander)
};

// xorshift32: small, and the same sequence on the robot and the PC
inline uint32_t rnd(uint32_t& s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}
inline float rnd01(uint32_t& s) { return (rnd(s) >> 8) * (1.0f / 16777216.0f); }

inline float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }


// Open-Meteo's "current" block: temperature_2m and the WMO weather_code -> kind (false if missing)
inline bool parseOpenMeteo(const char* json, Weather& w, float& tempC) {
  const char* cur = strstr(json, "\"current\":");  // (not "current_units")
  if (!cur) return false;
  auto num = [&](const char* key, float& out) {
    const char* k = strstr(cur, key);
    if (!k) return false;
    k += strlen(key);
    char* end = nullptr;
    out = strtof(k, &end);
    return end != k;
  };
  float t, code;
  if (!num("\"temperature_2m\":", t) || !num("\"weather_code\":", code)) return false;
  const int c = (int)code;
  w = c <= 1 ? W_CLEAR : c <= 3 ? W_CLOUDY : (c == 45 || c == 48) ? W_FOG : (c >= 51 && c <= 67) || (c >= 80 && c <= 82) ? W_RAIN
    : (c >= 71 && c <= 77) || c == 85 || c == 86 ? W_SNOW : c >= 95 ? W_STORM : W_CLOUDY;
  tempC = t;
  return true;
}

class Brain {
 public:
  float need[NEED_N];
  uint32_t seed = 1;
  float extMul[B_N] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};  // the long-term persona's weights (desk_persona.h)

  void reset(uint32_t s) {
    seed = s ? s : 1;
    need[ENERGY] = 0.7f;
    need[BOREDOM] = 0.3f;
    need[CURIOSITY] = 0.3f;
    need[COMPANY] = 0.3f;
    need[COMFORT] = 0.8f;
  }

  // dtS seconds passed; resting = napping / sleeping (energy comes back)
  void update(float dtS, const Context& c, bool resting) {
    const float m = dtS / 60.0f;  // minutes
    const bool night = c.hour >= 22 || (c.hour >= 0 && c.hour < 6);  // (-1: no clock yet - not night)
    if (resting) need[ENERGY] += 0.15f * m;  // (a nap must really help, or it stays sleepy and naps all day)
    else need[ENERGY] -= 0.006f * m * (night ? 1.6f : 1.0f);
    need[BOREDOM] += 0.02f * m;
    need[CURIOSITY] += (need[CURIOSITY] > 0.2f ? -0.05f : 0.05f) * m;
    if (fabsf(need[CURIOSITY] - 0.2f) < 0.05f * m) need[CURIOSITY] = 0.2f;
    if (c.userPresent) need[COMPANY] *= powf(0.9f, dtS);  // seeing the user: gone within a minute
    else need[COMPANY] += 0.01f * m;
    need[COMFORT] += 0.02f * m;
    for (int i = 0; i < NEED_N; i++) need[i] = clamp01(need[i]);
  }

  Mood mood() const {
    if (need[ENERGY] < 0.25f) return M_SLEEPY;
    if (need[COMFORT] < 0.3f) return M_GRUMPY;
    if (need[COMPANY] > 0.7f) return M_LONELY;
    if (need[CURIOSITY] > 0.6f) return M_CURIOUS;
    if (need[ENERGY] > 0.7f && need[BOREDOM] < 0.4f) return M_EXCITED;
    return M_CONTENT;
  }

  // ---- what to do next ----
  // How well each behaviour fits the needs, times the context, the liveliness and novelty (never the
  // one just done; less of the last few); then a weighted random pick - not the top score.
  Beh choose(const Context& c, Live l) {
    float w[B_N], sum = 0;
    for (int i = 0; i < B_N; i++) {
      w[i] = fit((Beh)i, c) * contextMul((Beh)i, c) * liveMul((Beh)i, l) * extMul[i];
      if (i == hist[0]) w[i] = 0;
      else
        for (int k = 1; k < HIST; k++) if (hist[k] == i && i != B_NAP && i != B_SLEEP) { w[i] *= 0.4f; break; }  // (resting again isn't a rerun)
      sum += w[i];
    }
    Beh pick = B_IDLE;
    if (sum > 0) {
      float r = rnd01(seed) * sum;
      for (int i = 0; i < B_N; i++) {
        if (w[i] <= 0) continue;
        pick = (Beh)i;
        if ((r -= w[i]) <= 0) break;
      }
    } else pick = hist[0] == B_IDLE ? B_GLANCE : B_IDLE;
    for (int k = HIST - 1; k > 0; k--) hist[k] = hist[k - 1];
    hist[0] = pick;
    return pick;
  }

  // ---- something happened: nudge the needs, and pick how to react (the variant) ----
  uint8_t onEvent(Event e, const Context& c, uint32_t nowS) {
    uint8_t v = 0;
    switch (e) {
      case E_TAP: {  // startle, then giggles, then annoyed - the taps of the last minute
        tapAt[tapN++ % 8] = nowS;
        int n = 0;
        for (int i = 0; i < 8 && i < tapN; i++) n += nowS - tapAt[i] <= 60;
        v = n <= 1 ? 0 : n <= 3 ? 1 : 2;
        need[COMFORT] -= 0.1f;
        need[CURIOSITY] += 0.2f;
        break;
      }
      case E_PAT:
        do v = (uint8_t)(rnd(seed) % 3); while (v == lastPat);
        lastPat = v;
        need[COMFORT] += 0.2f;
        need[COMPANY] -= 0.2f;
        break;
      case E_USER_ARRIVED:
        v = c.userAwayMin >= 30 ? 0 : c.userAwayMin >= 2 ? 1 : 2;
        need[COMPANY] = 0;
        need[COMFORT] += 0.1f;
        break;
      case E_LOUD:
        v = (uint8_t)(rnd(seed) % 2);
        need[COMFORT] -= 0.3f;
        need[CURIOSITY] += 0.3f;
        break;
      case E_MUSIC: need[BOREDOM] -= 0.2f; break;
      case E_DARK: need[ENERGY] -= 0.2f; break;
      default: break;
    }
    for (int i = 0; i < NEED_N; i++) need[i] = clamp01(need[i]);
    return v;
  }
  Beh last() const { return (Beh)hist[0]; }
  // seconds until the next choice: 8 s calm, 1.6x / 2.5x as often, +-40%
  float nextChoiceInS(Live l) {
    const float base = l == PLAYFUL ? 8.0f / 2.5f : l == NORMAL ? 8.0f / 1.6f : 8.0f;
    return base * (0.6f + 0.8f * rnd01(seed));
  }
  // a behaviour done: its effect on the needs
  void didBehaviour(Beh b) {
    if (b == B_SCENE) need[BOREDOM] -= 0.3f;
    if (b == B_FIDGET || b == B_GLANCE) need[BOREDOM] -= 0.08f;
    if (b == B_INVESTIGATE) need[CURIOSITY] -= 0.3f;
    if (b == B_GREET || b == B_FOLLOW_USER) need[COMPANY] -= 0.3f;
    for (int i = 0; i < NEED_N; i++) need[i] = clamp01(need[i]);
  }

 private:
  static const int HIST = 6;
  uint8_t hist[HIST] = {B_N, B_N, B_N, B_N, B_N, B_N};
  uint32_t tapAt[8] = {0};
  int tapN = 0;
  uint8_t lastPat = 255;

  float fit(Beh b, const Context& c) const {
    const float e = need[ENERGY], bo = need[BOREDOM], cu = need[CURIOSITY], co = need[COMPANY], cf = need[COMFORT];
    switch (b) {
      case B_IDLE: return 0.4f + 0.3f * cf;
      case B_GLANCE: return 0.3f + 0.5f * cu;
      case B_FIDGET: return 0.6f * bo;
      case B_STRETCH: return 0.5f * e;
      case B_SCENE: return bo;
      case B_SEEK_USER: return c.userPresent ? 0 : co;
      case B_NAP: return 1 - e;
      case B_SLEEP: return (1 - e) * (1 - e);
      case B_SHOWOFF: return c.userPresent ? 0.5f * e : 0;
      case B_INVESTIGATE: return cu;
      case B_FOLLOW_USER: return c.userPresent ? 0.5f * co + 0.3f : 0;
      case B_ASK_CHARGE: return c.batteryPct < 20 && !c.charging ? 8.0f : 0;  // (it should win)
      case B_CLOCK: return c.hour < 0 ? 0 : 0.35f;  // (no clock without the time)
      case B_WANDER: return c.canWalk ? 0.2f + 0.5f * cu + 0.3f * bo : 0;  // a little stroll on the desk (edge-checked)
      default: return 0;  // greet, react: events only
    }
  }
  static float contextMul(Beh b, const Context& c) {
    float m = 1;
    const bool night = c.hour >= 22 || (c.hour >= 0 && c.hour < 6);  // (-1: no clock yet - not night)
    if (night) m *= b == B_SLEEP ? 4 : b == B_NAP ? 2 : b == B_SCENE ? 0.3f : b == B_SHOWOFF ? 0.2f : b == B_IDLE || b == B_GLANCE ? 0.5f : 1;
    if (night && c.userPresent) m *= b == B_SLEEP ? 0.1f : b == B_NAP ? 0.5f : 1;  // you're up working: it keeps you company
    if (c.hour >= 6 && c.hour < 10 && b == B_STRETCH) m *= 3;
    if (b == B_CLOCK && (c.minute < 3 || c.minute > 57)) m *= 2.5f;  // on the hour: it checks the time
    if (c.hour >= 18 && c.hour < 22 && b == B_NAP) m *= 1.5f;
    if ((c.weekday == 0 || c.weekday == 6) && (b == B_SCENE || b == B_SHOWOFF)) m *= 1.5f;
    if (c.weather == W_RAIN) m *= b == B_NAP ? 1.5f : b == B_SCENE ? 1.2f : 1;
    if (c.weather == W_STORM && b == B_INVESTIGATE) m *= 2;
    if (c.tempC > 32 && b == B_NAP) m *= 1.3f;
    if (c.light < 0.15f && b == B_SLEEP) m *= 3;
    if (c.noise > 0.6f) m *= b == B_INVESTIGATE ? 1.5f : b == B_NAP ? 0.5f : 1;
    if (c.userAwayMin > 10 && b == B_SEEK_USER) m *= 2;
    if (c.charging && (b == B_NAP || b == B_SLEEP)) m *= 2;
    // a low battery: lie down (legs off, less drain) - awake on the charger it can use all the
    // charger gives and the battery stays flat
    if (night && b == B_WANDER) m *= 0.2f;
    if (c.batteryPct < 30) m *= b == B_WANDER ? 0 : b == B_NAP ? 10 : b == B_SLEEP ? 4 : b == B_CLOCK ? 0.2f : b == B_SCENE || b == B_SHOWOFF || b == B_FIDGET ? 0.5f : 1;
    return m;
  }
  static float liveMul(Beh b, Live l) {
    if (l == CALM) return b == B_IDLE ? 1.5f : b == B_SCENE ? 0.7f : 1;
    if (l == PLAYFUL) return b == B_SCENE || b == B_SHOWOFF ? 1.5f : b == B_FIDGET ? 1.3f : b == B_IDLE ? 0.6f : 1;
    return 1;
  }
};

}  // namespace desk
