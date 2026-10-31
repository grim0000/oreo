// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
// Desk mode: Oreo sits on the desk and its
// inner life (desk_brain.h) - fed by the senses (desk_senses.h) and the context (time, weather, light,
// noise, the user, battery) - picks what to do: face, head, small body moves (desk_body.h), sounds.
// Never walks. Included by main.cpp after its globals; deskTick() runs in loop().
#pragma once
#include "desk_brain.h"
#include "desk_senses.h"
#include "desk_body.h"
#include "desk_persona.h"  // the long-term self: traits, bond, your hours, life events (saved)

namespace clk { extern bool focusWork; }  // desk_clock.h (below): a focus block running - keep quiet
namespace mus { extern bool songOn; }     // music.h (below): a song of its own playing - bob along

namespace deskm {
using namespace desk;
using deskbody::Move;

Brain brain;
Persona per;
bool walkAllowed = false;              // the page's "may walk on the desk" (off at every start: a desk has edges)
int wanderSteps = 0;
unsigned long wanderMs = 0;
bool wanderStood = false;
// the desk surface where the head looks (25 deg down - the tilt limit allows no steeper): Explore's calibrated floor distance there
inline int surfaceMm() { return exFloorSaved[0] > 0 ? exFloorSaved[0] : 455; }
inline void wanderHalt() { if (deskWalkActive) { deskWalkActive = false; if (currentCommand.startsWith("smooth_")) currentCommand = ""; } }  // between steps
inline void wanderStop() {
  if (deskWalkActive) { deskWalkActive = false; if (currentCommand.startsWith("smooth_")) currentCommand = ""; }
  if (wanderStood) { wanderStood = false; if (!currentCommand.length()) currentCommand = "sit"; }  // back to sitting (desk mode's pose)
}
unsigned long perSaveMs = 0, lifeEvMs = 0;
inline uint32_t epochNow() { const time_t t = time(nullptr); return t > 1700000000 ? (uint32_t)t : 0; }  // 0: no clock yet
TapDetector tap;
Presence pres;
Sound snd;
Light lit;
Context ctx;
Live live = CALM;
int latE4 = 0, lonE4 = 0;              // the city for the weather (deg x 10000; 0,0 = not set)
volatile bool weatherBusy = false;
char cityReq[40] = "";                  // a city name to look up (the robot does it: the phone may have no internet)
char cityCountry[4] = "";  // (optional) prefer this country when looking up the city
char cityName[40] = "";                 // the place found (saved as "deskCity")
volatile bool citySaveReq = false;      // the name to save (loop: deskTick, with the camera paused)
volatile bool forceScene = false;       // "Surprise me": a scene next, now
// StandBy (Apple-style): idle a while -> a clock / calendar / weather / dozing-Oreo screen; a touch wakes it
bool sbOn = true;                       // the page's switch (saved)
int sbMin = 5;                          // minutes without a touch / tap / voice before it (saved)
bool standby = false;
unsigned long lastInteractMs = 0, touchDownMs = 0, relaxMs = 0;
const char* idleWhy = "start";          // what last counted as "not idle" (the page shows it)
int sbStyle = 0;                        // the StandBy style (saved; a short touch in StandBy changes it)
unsigned long weatherTryMs = 0;
volatile int wxKind = W_UNKNOWN;
volatile float wxTemp = 25;

// what it's doing now
Beh doing = B_IDLE;
char doingName[24] = "idle";
unsigned long actUntil = 0, nextChoiceAt = 0, lastAskMs = 0, pausedAt = 0, ctxMs = 0, updMs = 0, senseMs = 0;
int phase = 0;
bool resting = false, paused = false, touchWas = false, entered = false, sceneLogPending = false;

inline float ampNow() { return live == PLAYFUL ? 1.0f : live == NORMAL ? 0.8f : 0.6f; }
// an expression plays once and the face goes back to its living idle (held, it looks like a static face);
// only sleep / nap hold theirs
inline void face(const char* e, bool hold = false) { setFaceWithMode(e, hold ? FACE_ANIM_LOOP : FACE_ANIM_ONCE); }
inline void head(float panOff, float tiltOff) {
  headSlewTo(constrain(PAN_MID + (panPlusIsLeft ? panOff : -panOff), PAN_MIN + 3, PAN_MAX - 3),
             constrain(TILT_MID + tiltOff, TILT_MIN + 3, TILT_MAX - 6));
}
inline float rr(float a, float b) { return a + (b - a) * rnd01(brain.seed); }
inline const char* moodFace() {
  // the weather shows in the mood now and then
  const float r = rnd01(brain.seed);
  if (brain.mood() == M_CONTENT && (ctx.weather == W_RAIN || ctx.weather == W_STORM) && r < 0.4f) return "rainy_day";
  if (ctx.weather != W_UNKNOWN && ctx.tempC > 32 && r < 0.3f) return "overheated";
  if (ctx.weather == W_CLEAR && ctx.hour >= 8 && ctx.hour < 17 && brain.mood() == M_CONTENT && r < 0.25f) return "sunny";
  switch (brain.mood()) {
    case M_SLEEPY: return "tired";
    case M_CURIOUS: return "curious";
    case M_LONELY: return "lonely";
    case M_EXCITED: return "happy";
    case M_GRUMPY: return "grumpy";
    default: return "content";
  }
}
inline void setDoing(const char* n) { strlcpy(doingName, n, sizeof(doingName)); }

// ---- the weather: Open-Meteo over plain HTTP (public data; the CA list is Google's), every 30 min ----
// one plain-HTTP GET into buf (Open-Meteo answers on http too); bytes read
int httpGet(const char* url, char* buf, int max) {
  using namespace gem;  // (gemini.h included esp_http_client.h inside its namespace)
  esp_http_client_config_t c = {};
  c.url = url;
  c.timeout_ms = 8000;
  c.buffer_size = 1024;
  esp_http_client_handle_t h = esp_http_client_init(&c);
  int n = 0;
  if (h) {
    if (esp_http_client_open(h, 0) == ESP_OK) {
      esp_http_client_fetch_headers(h);
      int r;
      while (n < max - 1 && (r = esp_http_client_read(h, buf + n, max - 1 - n)) > 0) n += r;
    }
    esp_http_client_close(h);
    esp_http_client_cleanup(h);
  }
  buf[n] = 0;
  return n;
}
void weatherTask(void*) {
  using namespace gem;  // (gemini.h included esp_http_client.h inside its namespace)
  if (cityReq[0]) {  // a city name first: where is it?
    static char gb[1536];
    char q[48], url[180];
    int k = 0;
    for (const char* c2 = cityReq; *c2 && k < (int)sizeof(q) - 4; c2++) {  // spaces -> %20
      if (*c2 == ' ') { q[k++] = '%'; q[k++] = '2'; q[k++] = '0'; }
      else q[k++] = *c2;
    }
    q[k] = 0;
    // (a name can be ambiguous: set a 2-letter country on the page (/desk?country=FR) to prefer one country)
    if (cityCountry[0]) {
      snprintf(url, sizeof(url), "http://geocoding-api.open-meteo.com/v1/search?count=1&countryCode=%s&name=%s", cityCountry, q);
      httpGet(url, gb, sizeof(gb));
    } else gb[0] = 0;
    if (!strstr(gb, "\"latitude\":")) {
      snprintf(url, sizeof(url), "http://geocoding-api.open-meteo.com/v1/search?count=1&name=%s", q);
      httpGet(url, gb, sizeof(gb));
    }
    const char* la = strstr(gb, "\"latitude\":");
    const char* lo = strstr(gb, "\"longitude\":");
    const char* nm = strstr(gb, "\"name\":\"");
    if (la && lo) {
      latE4 = (int)lroundf(atof(la + 11) * 10000);
      lonE4 = (int)lroundf(atof(lo + 12) * 10000);
      if (nm) { nm += 8; int i = 0; while (nm[i] && nm[i] != '"' && i < (int)sizeof(cityName) - 1) { cityName[i] = nm[i]; i++; } cityName[i] = 0; }
      prefPut("deskLat", latE4, false);
      prefPut("deskLon", lonE4, false);
      citySaveReq = true;  // (the name too, so it survives a restart)
      xlog("DESK city %s -> %s %.4f %.4f", cityReq, cityName, latE4 / 10000.0, lonE4 / 10000.0);
    } else {
      xlog("DESK city %s: not found", cityReq);
      strlcpy(cityName, "not found", sizeof(cityName));
    }
    cityReq[0] = 0;
  }
  if (!latE4 && !lonE4) { weatherBusy = false; vTaskDelete(nullptr); return; }
  char url[160];
  snprintf(url, sizeof(url), "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code",
           latE4 / 10000.0, lonE4 / 10000.0);
  esp_http_client_config_t c = {};
  c.url = url;
  c.timeout_ms = 8000;
  c.buffer_size = 1024;
  esp_http_client_handle_t h = esp_http_client_init(&c);
  if (h) {
    static char body[1536];
    int n = 0;
    if (esp_http_client_open(h, 0) == ESP_OK) {
      esp_http_client_fetch_headers(h);
      int r;
      while (n < (int)sizeof(body) - 1 && (r = esp_http_client_read(h, body + n, sizeof(body) - 1 - n)) > 0) n += r;
    }
    body[n] = 0;
    Weather w;
    float t;
    if (const char* tz = strstr(body, "\"utc_offset_seconds\":")) {  // the city's timezone -> the robot's clock
      const int off = atoi(tz + 21);
      if (off != tzOffsetSec && prefs.getBool("tzAuto", true)) {
        applyUtcOffset(off, true);
        xlog("DESK timezone UTC%+.1f h (from the city)", off / 3600.0);
      }
    }
    if (parseOpenMeteo(body, w, t)) {
      wxKind = w;
      wxTemp = t;
      xlog("DESK weather %s %.1f C", weatherName(w), t);
    } else {
      xlog("DESK weather: no answer (%d bytes)", n);
    }
    esp_http_client_close(h);
    esp_http_client_cleanup(h);
  }
  weatherBusy = false;
  vTaskDelete(nullptr);
}
inline void weatherMaybe(unsigned long now) {
  if (weatherBusy || (!latE4 && !lonE4 && !cityReq[0]) || aud::talkOn || WiFi.status() != WL_CONNECTED) return;
  if (weatherTryMs && now - weatherTryMs < 30UL * 60000 && !cityReq[0]) return;
  weatherTryMs = now;
  weatherBusy = true;
  if (xTaskCreatePinnedToCore(weatherTask, "deskWx", 8192, nullptr, 1, nullptr, 1) != pdPASS) weatherBusy = false;
}

// ---- the context snapshot (every minute) ----
inline void contextUpdate(unsigned long now) {
  struct tm tmv;
  if (getLocalTime(&tmv, 0)) { ctx.hour = tmv.tm_hour; ctx.minute = tmv.tm_min; ctx.weekday = tmv.tm_wday; }
  else ctx.hour = -1;  // no clock yet (no internet): no clock scene
  ctx.weather = (Weather)wxKind;
  ctx.tempC = wxTemp;
  if (camReady && camLumaAll >= 0) ctx.light = constrain(camLumaAll / 180.0f, 0.0f, 1.0f);
  ctx.noise = snd.noise01();
  ctx.userPresent = pres.present;
  ctx.userAwayMin = pres.awayMin(now);
  ctx.batteryPct = batPctShown >= 0 ? batPctShown : 80;
  ctx.charging = batCharging;
  ctx.canWalk = walkAllowed && tofOk && !robotFallen && !deskbody::folded;
  faceWeather = ctx.weather;      // the scenes know the weather (rain: the umbrella, a storm: thunder)
  faceEng.tempNow = ctx.weather != W_UNKNOWN ? ctx.tempC : -99;  // StandBy's weather page
  strlcpy(faceEng.cityNow, cityName[0] && strcmp(cityName, "looking up...") && strcmp(cityName, "not found") ? cityName : "", sizeof(faceEng.cityNow));
  faceSbNight = ctx.hour >= 22 || (ctx.hour >= 0 && ctx.hour < 6) || ctx.light < 0.15f;
  faceUserHere = pres.present;    // and whether someone is there (the hello scene)
}
inline bool restTime() { return ctx.hour >= 22 || (ctx.hour >= 0 && ctx.hour < 6) || ctx.charging; }  // (hour -1 = no clock yet: not night)

// ---- Being alive. What makes a desk robot feel alive (Anki's Cozmo / Vector, EMO, the HRI
// "lively" pattern): it's never frozen, the eyes lead the head, it holds a look a while and then moves on,
// its mood shows in the neck, and it has many small moments of its own - daydreaming, nodding off, a sigh,
// humming, watching a fly that isn't there, a double take, checking on you - picked by mood, never the
// same one twice running, each with its own random size and timing. Head, eyes, face and sound only:
// it works with the legs folded too.
namespace alive {
enum Act : uint8_t { A_SHIFT, A_LOOK_USER, A_DOUBLE_TAKE, A_SIGH, A_NOD_OFF, A_DAYDREAM, A_THINK, A_TRACK_FLY, A_HUM,
                     A_PEEK_DESK, A_SHAKE, A_NECK_STRETCH, A_LISTEN, A_DANCE, A_SNEAKY, A_N };
const char* const ACT_NAME[A_N] = {"shift", "look_user", "double_take", "sigh", "nod_off", "daydream", "think", "track_fly",
                                   "hum", "peek_desk", "shake", "neck_stretch", "listen", "head_dance", "sneaky"};
const int KEEP = -128;
struct Key {
  uint16_t ms;        // from the act's start
  int8_t pan, tilt;   // head offsets (deg, as head()); KEEP = the head stays
  int8_t ex, ey;      // the eyes -100..100; KEEP = leave them
  uint8_t glide;      // x100 ms: the head glides there (0: the spring's quick move)
  uint8_t hold;       // the eyes' hold, x100 ms
  const char* face;
  const char* sfx;
};
Key keys[20];
int nk = 0, ki = 0;
Act cur = A_N;
unsigned long t0 = 0, nextAt = 0, glS = 0, glE = 0, glLast = 0;
float glFP = 0, glFT = 0, glTP = 0, glTT = 0, bp = 0, bt = 0;
bool gl = false, sndOn = true;
uint8_t hist[4] = {A_N, A_N, A_N, A_N};

inline bool running() { return cur != A_N; }
inline void stop() { cur = A_N; nk = 0; gl = false; }
inline int8_t c8(float v) { return v == KEEP ? (int8_t)KEEP : (int8_t)constrain((int)lroundf(v), -127, 127); }
inline void add(int ms, float p, float t, float ex = KEEP, float ey = KEEP, const char* f = nullptr, const char* sfx = nullptr,
                int glide = 0, int hold = 5) {
  if (nk >= 20 || ms < 0) return;
  keys[nk++] = {(uint16_t)ms, c8(p), c8(t), c8(ex), c8(ey), (uint8_t)glide, (uint8_t)hold, f, sfx};
  if (p != KEEP) { bp = p; bt = t; }
}
// a look somewhere: the eyes go first, the head follows ~140 ms later (a glide: the eyes stay with the head)
inline void look(int ms, float p, float t, const char* f = nullptr, const char* sfx = nullptr, int glide = 0) {
  if (!glide && (fabsf(p - bp) > 4 || fabsf(t - bt) > 3)) {
    add(ms, KEEP, KEEP, constrain((p - bp) / 30.0f, -1.0f, 1.0f) * 100, constrain((t - bt) / 14.0f, -1.0f, 1.0f) * 100, f, sfx, 0, 5);
    add(ms + 140, p, t);
  } else add(ms, p, t, KEEP, KEEP, f, sfx, glide);
}
inline void eyes(int ms, float x, float y, int hold, const char* f = nullptr, const char* sfx = nullptr) { add(ms, KEEP, KEEP, x * 100, y * 100, f, sfx, 0, hold); }
inline void say(int ms, const char* f, const char* sfx = nullptr) { add(ms, KEEP, KEEP, KEEP, KEEP, f, sfx); }
inline void endAt(int ms) { add(ms, KEEP, KEEP); }

// the mood in the neck: sleepy hangs low, lonely a little down, excited looks up (+ = down)
inline float posture() {
  switch (brain.mood()) {
    case M_SLEEPY: return 7;
    case M_LONELY: return 4;
    case M_GRUMPY: return 3;
    case M_EXCITED: return -4;
    case M_CURIOUS: return -2;
    default: return 0;
  }
}
inline bool userKnown(unsigned long now) { return deskUserMs && now - deskUserMs < 90000 && pres.present; }
inline float userP() { return constrain((deskUserPan - PAN_MID) * (panPlusIsLeft ? 1 : -1), -45.0f, 45.0f); }
inline float userT() { return constrain(deskUserTilt - TILT_MID, -15.0f, 12.0f); }
inline bool night() { return ctx.hour >= 22 || (ctx.hour >= 0 && ctx.hour < 7); }
inline float side() { return rnd01(brain.seed) < 0.5f ? -1 : 1; }
inline bool chance(float p) { return rnd01(brain.seed) < p; }

// how much it wants each moment now (A_SHIFT, the plain look-around, is the filler: not here)
Act choose(unsigned long now) {
  float w[A_N] = {0};
  const Mood m = brain.mood();
  const bool sleepy = m == M_SLEEPY || brain.need[ENERGY] < 0.4f;
  w[A_LOOK_USER] = userKnown(now) ? 2.5f : 0;
  w[A_DOUBLE_TAKE] = 0.8f + (m == M_CURIOUS ? 1.2f : 0);
  w[A_SIGH] = (m == M_LONELY || m == M_GRUMPY ? 1.6f : 0.25f) + 0.8f * brain.need[BOREDOM];
  w[A_NOD_OFF] = sleepy ? 2.5f : 0;
  w[A_DAYDREAM] = 1.0f + (sleepy ? 0.5f : 0) + (m == M_LONELY ? 0.6f : 0);
  w[A_THINK] = 0.9f + (m == M_CURIOUS ? 0.6f : 0);
  w[A_TRACK_FLY] = sleepy ? 0.2f : 1.0f + (m == M_CURIOUS ? 1.5f : 0) + (m == M_EXCITED ? 0.8f : 0);
  w[A_HUM] = sleepy ? 0.2f : 0.8f + (m == M_EXCITED ? 1.0f : 0) + (m == M_CONTENT ? 0.5f : 0);
  w[A_PEEK_DESK] = 0.9f + (m == M_CURIOUS ? 0.8f : 0);
  w[A_SHAKE] = 0.5f + (m == M_GRUMPY ? 0.6f : 0);
  w[A_NECK_STRETCH] = 0.5f + (sleepy ? 1.0f : 0);
  w[A_LISTEN] = 0.8f + 1.5f * ctx.noise + (m == M_LONELY ? 0.6f : 0);
  w[A_DANCE] = sleepy || m == M_GRUMPY ? 0 : 0.4f + (m == M_EXCITED ? 1.2f : 0) + (live == PLAYFUL ? 0.6f : 0);
  w[A_SNEAKY] = 0.6f + (m == M_GRUMPY ? 0.6f : 0);
  if (night()) { w[A_HUM] *= 0.3f; w[A_DANCE] *= 0.2f; w[A_TRACK_FLY] *= 0.4f; }
  if (clk::focusWork) for (int i = 0; i < A_N; i++) if (i != A_DAYDREAM && i != A_THINK && i != A_SIGH && i != A_LOOK_USER) w[i] *= 0.1f;
  if (hist[0] < A_N) w[hist[0]] = 0;
  for (int k = 1; k < 4; k++) if (hist[k] < A_N) w[hist[k]] *= 0.3f;
  float sum = 0;
  for (int i = 1; i < A_N; i++) sum += w[i];
  if (sum <= 0) return A_DAYDREAM;
  float r = rnd01(brain.seed) * sum;
  for (int i = 1; i < A_N; i++) { if (w[i] <= 0) continue; if ((r -= w[i]) <= 0) return (Act)i; }
  return A_DAYDREAM;
}

// a moment: its keys (random size and timing every time); ms until it ends
int startAct(Act a, unsigned long now) {
  stop();
  nk = 0; ki = 0;
  bp = (emoPanNow - PAN_MID) * (panPlusIsLeft ? 1 : -1);
  bt = emoTiltNow - TILT_MID;
  // where "home" is: often you (when it knows where you are), else straight ahead; and the mood's posture
  float hp = 0, ht = posture();
  if (userKnown(now) && chance(0.45f)) { hp = userP(); ht = userT() + 0.5f * posture(); }
  const float s = side();
  const Mood m = brain.mood();
  sndOn = !clk::focusWork && !night() && chance(live == PLAYFUL ? 0.85f : live == NORMAL ? 0.6f : 0.35f);
  switch (a) {
    case A_SHIFT: {  // hold a look a while, then somewhere else (now and then the eyes alone)
      const int hold = (int)rr(1500, 4500);
      if (chance(0.2f)) eyes(0, rr(-0.8f, 0.8f), rr(-0.5f, 0.4f), hold / 100);
      else {
        const bool near = chance(0.55f);
        look(0, near ? hp + rr(-14, 14) : rr(-38, 38), ht + (near ? rr(-5, 4) : rr(-10, 8)));
        if (chance(0.25f)) look(hold * 6 / 10, bp + rr(-4, 4), bt + rr(-2, 2));  // a small correction, as eyes do
      }
      endAt(hold);
      break;
    }
    case A_LOOK_USER: {  // looks at you; a smile, sometimes a little chirp
      const float up = userP(), ut = userT();
      look(0, up, ut, chance(0.5f) ? "happy_blink" : "content");
      if (chance(0.3f)) say(700, "happy", "w:v_chat1|700~900:120");
      const int hold = (int)rr(2000, 4000);
      if (chance(0.35f)) say(hold - 600, "cozy");
      endAt(hold);
      break;
    }
    case A_DOUBLE_TAKE:
      look(0, hp + s * rr(22, 35), ht + rr(-4, 2), "curious");
      look(700, hp, ht);
      look(1300, hp + s * rr(28, 38), ht - 2, "surprised", "w:v_huh|500~700:200");
      look((int)rr(2800, 3400), hp, ht, chance(0.5f) ? "sheepish" : "content");
      endAt(3900);
      break;
    case A_SIGH: {
      const float p = hp + rr(-8, 8);
      look(0, p, ht, "gloomy");
      add(400, p, ht + rr(8, 12), KEEP, KEEP, nullptr, nullptr, 12);
      add((int)rr(2300, 2900), p, ht - 2, KEEP, KEEP, "content", nullptr, 10);
      endAt(4000);
      break;
    }
    case A_NOD_OFF: {  // the head sinks... and jerks awake, looks around: did anyone see?
      say(0, "sleepy");
      const int sink = (int)rr(2400, 3400);
      add(200, hp + rr(-5, 5), ht + rr(14, 18), KEEP, KEEP, nullptr, nullptr, sink / 100);
      eyes(400, 0, 0.6f, sink / 100);
      add(200 + sink + 300, hp, ht - 6, KEEP, KEEP, "startled", "w:v_eep|1400~900:180");
      look(200 + sink + 900, hp + s * 25, ht - 2);
      look(200 + sink + 1600, hp - s * 20, ht);
      look(200 + sink + 2300, hp, ht, "sheepish");
      endAt(200 + sink + 3300);
      break;
    }
    case A_DAYDREAM: {  // gazes up and away, the eyes wander, then back
      add(0, hp + s * rr(15, 28), ht - rr(8, 13), KEEP, KEEP, "wistful", nullptr, 15);
      eyes(1800, s * 0.3f, -0.6f, 15);
      eyes(3200, -s * 0.2f, -0.7f, 15);
      if (chance(0.3f)) say(2500, nullptr, "w:v_hmm|330~360:300");
      const int back = (int)rr(4500, 6000);
      add(back, hp, ht, KEEP, KEEP, "content", nullptr, 12);
      endAt(back + 1400);
      break;
    }
    case A_THINK:  // looks up to one side, hmm... an idea (or not)
      look(0, hp + s * rr(12, 20), ht - rr(6, 10), "thinking", "w:v_hmm|440~470:300");
      eyes(900, s * 0.6f, -0.7f, 12);
      if (chance(0.55f)) {
        look(2300, hp, ht - 3, "happy", "w:v_ooh|600~900:220");
        add(2700, hp, ht + 3);
        add(2950, hp, ht - 1);
      } else look(2300, hp, ht, chance(0.5f) ? "unimpressed" : "content");
      endAt(3800);
      break;
    case A_TRACK_FLY: {  // follows something only it can see, flitting about
      float p = hp + rr(-25, 25), t = ht + rr(-10, 4);
      look(0, p, t, "curious");
      int ms = 0;
      const int n = 5 + (int)(rnd01(brain.seed) * 3);
      for (int i = 0; i < n; i++) {
        ms += (int)rr(300, 650);
        p = constrain(p + rr(-18, 18), -42.0f, 42.0f);
        t = constrain(t + rr(-6, 6), ht - 12, ht + 10);
        look(ms, p, t, i == 2 ? "focus" : nullptr);
      }
      ms += 500;
      const float r = rnd01(brain.seed);
      if (r < 0.4f) { look(ms, p, t + 8, "giggle", "w:v_hehe|900~1200:90 0:50 1000~1300:90"); look(ms + 900, hp, ht, "happy"); }
      else if (r < 0.7f) { look(ms, p + s * 25, t - 4, "surprised", "w:v_huh|500~750:200"); look(ms + 1100, hp, ht, "content"); }
      else look(ms, hp, ht, "content");
      endAt(ms + 1800);
      break;
    }
    case A_HUM: {  // sways to a little tune of its own
      say(0, chance(0.5f) ? "vibing" : "content");
      const int per = (int)rr(700, 1100), n = 4 + (int)(rnd01(brain.seed) * 4);
      for (int i = 0; i < n; i++)
        add(i * per, hp + (i % 2 ? 1 : -1) * rr(6, 10), ht + (i % 2 ? 2 : -2), KEEP, KEEP, nullptr,
            i == 1 ? "w:v_chat2|500~800:140" : i == 4 ? "w:v_chat1|700~900:120" : nullptr, per / 100);
      add(n * per, hp, ht, KEEP, KEEP, "content", nullptr, 6);
      endAt(n * per + 800);
      break;
    }
    case A_PEEK_DESK:  // something on the desk?
      look(0, hp + rr(-12, 12), ht + rr(16, 22), "curious");
      eyes(700, rr(-0.4f, 0.4f), 0.6f, 10);
      if (chance(0.35f)) say(1400, "surprised", "w:v_ooh|500~700:300");
      look((int)rr(2200, 3000), hp, ht, "content");
      endAt(3600);
      break;
    case A_SHAKE:  // a quick shake of the head, shaking a thought off
      say(0, "dizzy");
      for (int i = 0; i < 4; i++) add(i * 180, hp + (i % 2 ? -1 : 1) * (12 - 2 * i), ht);
      add(720, hp, ht, KEEP, KEEP, chance(0.5f) ? "giddy" : "content");
      endAt(1600);
      break;
    case A_NECK_STRETCH: {  // up, one side, the other, slowly (a yawn when sleepy)
      const bool y = m == M_SLEEPY || brain.need[ENERGY] < 0.4f;
      add(0, hp, ht - 14, KEEP, KEEP, y ? "yawn" : "content", y ? "w:v_yawn|" : nullptr, 12);
      add(1600, hp + 22, ht - 8, KEEP, KEEP, nullptr, nullptr, 12);
      add(3000, hp - 22, ht - 8, KEEP, KEEP, nullptr, nullptr, 14);
      add(4600, hp, ht, KEEP, KEEP, "content", nullptr, 10);
      endAt(5800);
      break;
    }
    case A_LISTEN: {  // turns an ear to something, very still
      look(0, hp + s * rr(28, 40), ht + rr(-2, 3), "focus");
      eyes(400, s * 0.7f, 0, 20);
      if (chance(0.3f)) say(1600, nullptr, "w:v_hmm|440~520:220");
      const int back = (int)rr(2500, 3500);
      look(back, hp, ht, "curious");
      endAt(back + 700);
      break;
    }
    case A_DANCE: {  // a little head dance
      say(0, "giddy", "w:v_chat2|500~800:140");
      const int beat = (int)rr(380, 480), n = 8 + (int)(rnd01(brain.seed) * 5);
      for (int i = 0; i < n; i++) add(i * beat, hp + ((i / 2) % 2 ? 10 : -10), ht + (i % 2 ? 5 : -4));
      add(n * beat, hp, ht, KEEP, KEEP, "happy", "w:v_yay|660~990:150 0:60 880~1320:200");
      endAt(n * beat + 900);
      break;
    }
    case A_SNEAKY:  // eyes this way, that way... then the head
      eyes(0, s * 0.9f, 0, 12, "sneaky");
      eyes(1200, -s * 0.9f, 0, 10);
      look(2300, hp + s * rr(25, 35), ht, "suspicious");
      look(3400, hp, ht, "content");
      endAt(4000);
      break;
    default:
      endAt(1000);
      break;
  }
  cur = a;
  t0 = now;
  if (a != A_SHIFT) {
    for (int k = 3; k > 0; k--) hist[k] = hist[k - 1];
    hist[0] = a;
    xlog("DESK alive %s", ACT_NAME[a]);
  }
  return nk ? keys[nk - 1].ms + 200 : 0;
}

void tick(unsigned long now) {
  if (cur == A_N) return;
  const unsigned long t = now - t0;
  while (ki < nk && t >= keys[ki].ms) {
    const Key& k = keys[ki++];
    if (k.face) face(k.face);
    if (k.sfx && sndOn && !aud::busy && !aud::playing && !aud::talkOn) playSfx(k.sfx);
    if (k.ex != KEEP) faceLookAt(k.ex / 100.0f, k.ey / 100.0f, k.hold / 10.0f);
    if (k.pan != KEEP) {
      if (k.glide) {
        glFP = (emoPanNow - PAN_MID) * (panPlusIsLeft ? 1 : -1);
        glFT = emoTiltNow - TILT_MID;
        glTP = k.pan; glTT = k.tilt;
        glS = now; glE = now + k.glide * 100UL; gl = true; glLast = 0;
      } else { gl = false; head(k.pan, k.tilt); }
    }
  }
  if (gl && now - glLast >= 60) {  // a glide: the target walks along, eased
    glLast = now;
    float u = (float)(now - glS) / (float)max(1UL, glE - glS);
    if (u >= 1) { u = 1; gl = false; }
    const float e = u * u * (3 - 2 * u);
    head(glFP + (glTP - glFP) * e, glFT + (glTT - glFT) * e);
  }
  if (ki >= nk && !gl) stop();
}

// idle: always something - a look held, then another; now and then one of the moments
void idleTick(unsigned long now) {
  if (deskFaceTrack && now - faceSeenGlobalMs < 1500) { stop(); return; }  // the head is the tracking's
  if (running()) {
    tick(now);
    if (!running()) nextAt = now + (unsigned long)(rr(300, 1500) * (live == CALM ? 1.5f : 1.0f));
    return;
  }
  if ((long)(now - nextAt) < 0) return;
  const float pBig = live == PLAYFUL ? 0.55f : live == NORMAL ? 0.4f : 0.28f;
  startAct(rnd01(brain.seed) < pBig ? choose(now) : A_SHIFT, now);
}
}  // namespace alive

// ---- the page's "Fold legs": folded up like the shutdown (knees to their min, then the hips), legs off;
// desk mode goes on with the head, face, sound and the internet only ----
volatile int foldReq = -1;  // the page: 1 fold, 0 unfold, 2 fold again (a start); loop does it (I2C and the legs are its)
inline void relaxLegs() {
  if (deskbody::folded && !legsPowered) { relaxMs = millis(); return; }  // (already off, folded)
  float keep[8];
  memcpy(keep, currentAngle, sizeof(keep));
  legsRelax();
  if (deskbody::folded) memcpy(currentAngle, keep, sizeof(keep));  // (folded: they stay there, not at "lying")
  relaxMs = millis();
}
void foldLegs() {
  alive::stop();
  wanderStop();
  deskbody::now = deskbody::MV_NONE;
  resting = false;
  currentCommand = "";
  face("cozy");
  if (!robotFallen) {  // (on its side: the legs would push against the desk)
    if (legsPowered)  // sitting first (the shutdown's start)
      moveSmoothHelper(500, (int)POSE_STAND[0], (int)POSE_STAND[1], (int)POSE_STAND[2], (int)POSE_STAND[3], REST_BACK[0], REST_BACK[1], REST_BACK[2], REST_BACK[3]);
    moveSmoothHelper(700, -999, -999, -999, -999, (int)LEG_MIN0[4], (int)LEG_MIN0[5], (int)LEG_MIN0[6], (int)LEG_MIN0[7]);
    delay(200);
    moveSmoothHelper(700, (int)LEG_MIN0[0], (int)LEG_MIN0[1], (int)LEG_MIN0[2], (int)LEG_MIN0[3], -999, -999, -999, -999);
    delay(250);
  }
  deskbody::folded = true;
  relaxLegs();
  prefPut("deskFold", 1, false);
  xlog("DESK legs folded: head, face, sound only");
}
void unfoldLegs() {
  deskbody::folded = false;
  prefPut("deskFold", 0, false);
  if (!robotFallen) {  // the fold backwards: hips out, then the knees to the sit
    moveSmoothHelper(700, (int)POSE_STAND[0], (int)POSE_STAND[1], (int)POSE_STAND[2], (int)POSE_STAND[3], -999, -999, -999, -999);
    delay(150);
    moveSmoothHelper(700, -999, -999, -999, -999, REST_BACK[0], REST_BACK[1], REST_BACK[2], REST_BACK[3]);
  }
  relaxMs = millis();
  deskbody::lastMoveMs = millis();
  face("happy");
  xlog("DESK legs unfolded: sitting");
}

// ---- behaviours ----
inline void lieDown() {
  deskbody::bodyStart(deskbody::MV_LIE_DOWN, 1.0f);
  resting = true;
}
inline void wakeUp() {
  if (!resting) return;
  resting = false;
  deskbody::bodyStop();  // back to sitting
}
void startBehaviour(Beh b, unsigned long now) {
  if (b == B_SCENE || b == B_SHOWOFF) per.onPlay();
  else if (b == B_NAP || b == B_SLEEP) per.onNap();
  doing = b;
  phase = 0;
  setDoing(behName(b));
  const float amp = ampNow();
  if (b != B_NAP && b != B_SLEEP) wakeUp();
  if (b != B_WANDER) wanderStop();
  alive::stop();
  switch (b) {
    case B_WANDER:  // a stroll on the desk: look down, a short step if the desk is there, else back off and turn
      face("curious");
      wanderSteps = 3 + (int)(rnd01(brain.seed) * 4);
      phase = 0;
      wanderMs = now;
      actUntil = now + 60000;
      break;
    case B_IDLE:  // (the head: alive::idleTick)
      if (rnd01(brain.seed) < 0.6f) face(moodFace());
      // cold out: a shiver now and then
      if (ctx.weather != W_UNKNOWN && ctx.tempC < 12 && rnd01(brain.seed) < 0.3f) { deskbody::bodyStart(deskbody::MV_SHIVER, amp); face("freezing"); }
      else deskbody::bodyStart(deskbody::MV_BREATHE, amp);
      actUntil = now + 4000;
      break;
    case B_GLANCE:
      face("curious");
      head(rr(-35, 35), rr(-12, 8));
      actUntil = now + (unsigned long)rr(1500, 3000);
      break;
    case B_FIDGET: {
      const Move ms[] = {deskbody::MV_LEAN_L, deskbody::MV_LEAN_R, deskbody::MV_PAW_TAP, deskbody::MV_SIT_UP, deskbody::MV_WIGGLE,
                         deskbody::MV_TWIST, deskbody::MV_PAW_L, deskbody::MV_PAW_R, deskbody::MV_BOUNCE, deskbody::MV_HAPPY_HOP};
      face(moodFace());
      if (deskbody::folded || rnd01(brain.seed) < 0.45f) {  // one of the head's moments (folded: always)
        actUntil = now + alive::startAct(alive::choose(now), now);
        break;
      }
      if (rnd01(brain.seed) < 0.3f) {  // now and then a little emote
        static const char* const fe2[] = {"sniff", "huh", "lookaround", "bored", "sneaky", "wiggle"};
        currentCommand = fe2[rnd(brain.seed) % 6];
      } else deskbody::bodyStart(ms[rnd(brain.seed) % (sizeof(ms) / sizeof(ms[0]))], amp);
      actUntil = now + 2500;
      break;
    }
    case B_STRETCH:
      if (deskbody::folded) { actUntil = now + alive::startAct(alive::A_NECK_STRETCH, now); break; }
      face("yawn", true);
      head(0, -10);
      deskbody::bodyStart(deskbody::MV_STRETCH, amp);
      actUntil = now + 3300;
      break;
    case B_SCENE:
      // someone's there: sometimes a hello for them; otherwise any scene (it picks by time, weather, history)
      faceScene(pres.present && rnd01(brain.seed) < 0.3f ? "hello" : "random");
      sceneLogPending = true;
      deskbody::bodyStart(deskbody::MV_BREATHE, amp);
      actUntil = now + 6000;  // (then waits while the scene plays)
      break;
    case B_SEEK_USER:
      face("curious");
      deskbody::bodyStart(deskbody::MV_SIT_UP, amp);
      head(-40, -5);
      actUntil = now + 6000;
      break;
    case B_NAP:
    case B_SLEEP:
      face(b == B_SLEEP ? "sleeping" : "sleepy", true);
      head(0, b == B_SLEEP ? 8 : 6);  // (only a little: the camera must still see the user arrive to wake it)
      lieDown();
      actUntil = now + (b == B_SLEEP ? 30UL * 60000 : (unsigned long)rr(60000, 300000));
      break;
    case B_SHOWOFF: {
      static const char* const em[] = {"wave", "nodyes", "cute", "happy", "proud", "jam", "shy", "kiss", "peekaboo", "wiggle",
                                       // (the emote library's gentle ones)
                                       "givepaw", "givepaw_l", "slowblink", "kneadpaws", "hellowave", "yesyes", "tailwag", "bashful",
                                       "thankyou", "lovestruck", "peekshy", "cheerup", "headbob2", "curtsy", "blowkiss", "smug"};
      if (deskbody::folded || rnd01(brain.seed) < 0.3f) {  // a head dance / a hum (folded: no emotes)
        actUntil = now + alive::startAct(rnd01(brain.seed) < 0.6f ? alive::A_DANCE : alive::A_HUM, now);
        break;
      }
      face("proud", true);
      currentCommand = em[rnd(brain.seed) % (sizeof(em) / sizeof(em[0]))];  // in-place emotes only
      actUntil = now + 4000;
      break;
    }
    case B_INVESTIGATE:
      face("curious");
      head(30, 0);
      deskbody::bodyStart(deskbody::MV_LEAN_F, amp);
      actUntil = now + 4000;
      break;
    case B_CLOCK:  // it plays with the time (Crumb often joins)
      faceScene("clock");
      sceneLogPending = true;
      doing = B_SCENE;  // (runs like a scene: events, logging)
      actUntil = now + 6000;
      break;
    case B_FOLLOW_USER:
      face("content");
      actUntil = now + (unsigned long)rr(10000, 20000);
      break;
    case B_ASK_CHARGE:
      if (now - lastAskMs < 10UL * 60000) { startBehaviour(B_IDLE, now); return; }
      lastAskMs = now;
      face("low_battery");
      head(0, 10);
      playSfx("w:v_chat_sad|520~380:250 0:80 440~330:300");
      actUntil = now + 4000;
      break;
    default:
      actUntil = now + 2000;
      break;
  }
  brain.didBehaviour(b);
}

// a behaviour's moments after its start
void behaviourStep(unsigned long now) {
  if (doing == B_WANDER) {
    tofNeedMs = now;  // the ToF runs while it strolls
    const bool ownCmd = !currentCommand.length() || currentCommand.startsWith("smooth_") || currentCommand == "stand";
    if (!walkAllowed || !tofOk || robotFallen || !ownCmd) {
      wanderStop(); actUntil = now; return;
    }
    const int exp = surfaceMm();
    const int d = tofMm;
    const bool noDesk = d < 0 || d > exp * 1.3f;   // much farther than the desk (or no echo): an edge
    const bool thing = d > 0 && d < exp * 0.7f;    // something on the desk, close
    switch (phase) {
      case 0:  // look down at the desk in front, let the ToF settle
        if (!wanderStood) { wanderStood = true; currentCommand = "stand"; wanderMs = now; }  // up first (desk mode sits)
        head(0, EX_FLOOR_ANGLES[0]);
        if (currentCommand.length() == 0 && now - wanderMs > 700) { phase = 1; wanderMs = now; }
        break;
      case 1:  // decide
        if (wanderSteps <= 0) { wanderStop(); head(0, 0); face("proud"); actUntil = now + 1500; phase = 9; break; }
        if (noDesk) {  // an edge: startled, back up, turn away
          face("startled", true);
          xlog("DESK wander: edge (%d mm, desk %d)", d, exp);
          deskWalkActive = true; currentCommand = "smooth_walk_back"; phase = 3; wanderMs = now;
        } else if (thing) {
          xlog("DESK wander: something close (%d mm)", d);
          deskWalkActive = true; currentCommand = rnd01(brain.seed) < 0.5f ? "smooth_sharp_left" : "smooth_sharp_right"; phase = 4; wanderMs = now;
        } else {
          deskWalkActive = true; currentCommand = "smooth_walk"; phase = 2; wanderMs = now; wanderSteps--;
        }
        break;
      case 2:  // a short step forward, watching the desk the whole time
        if (noDesk) { wanderHalt(); phase = 1; wanderMs = now; break; }  // (the next decision backs off)
        if (now - wanderMs > 900) { wanderHalt(); phase = 0; wanderMs = now; }
        break;
      case 3:  // backing off an edge, then a turn
        if (now - wanderMs > 700) { deskWalkActive = true; currentCommand = rnd01(brain.seed) < 0.5f ? "smooth_sharp_left" : "smooth_sharp_right"; phase = 4; wanderMs = now; }
        break;
      case 4:  // turning
        if (now - wanderMs > 1400) { wanderHalt(); phase = 0; wanderMs = now; }
        break;
      default: break;
    }
    return;
  }
  if (doing != B_IDLE && alive::running()) alive::tick(now);  // a moment started by a behaviour (fidget, show-off...)
  switch (doing) {
    case B_IDLE:  // never frozen: looks held and moved on, and now and then one of its moments (alive)
      alive::idleTick(now);
      break;
    case B_GLANCE:
      if (phase == 0 && now + 600 > actUntil) { head(0, 0); phase = 1; }
      break;
    case B_SEEK_USER:  // a slow sweep across
      if (phase == 0 && now + 3000 > actUntil) { head(40, -5); phase = 1; }
      if (phase == 1 && now + 600 > actUntil) { head(0, 0); phase = 2; }
      break;
    case B_INVESTIGATE:
      if (phase == 0 && now + 2000 > actUntil) { head(-30, 0); phase = 1; }
      if (phase == 1 && now + 500 > actUntil) { head(0, 0); phase = 2; }
      break;
    case B_FOLLOW_USER:  // the face on camera: turn a little toward it
      if (now - faceSeenGlobalMs < 1500 && headTargetDirty) { headTargetDirty = false; headSlewTo(headPanTarget, headTiltTarget); }
      break;
    case B_NAP:
    case B_SLEEP:  // lying: legs off once down
      if (phase == 0 && deskbody::bodyNow() == deskbody::MV_NONE) { relaxLegs(); phase = 1; }
      if (doing == B_SLEEP && phase == 1 && !restTime() && ctx.light > 0.3f) actUntil = now;  // morning, lights on
      break;
    case B_SCENE:
      if (sceneLogPending && faceScenePlaying) {
        sceneLogPending = false;
        xlog("DESK scene %s beats=%d/%d/%d", faceSceneName, (int)faceBeatMid, (int)faceBeatTwist, (int)faceBeatEnd);
      }
      if (faceEng.playing()) actUntil = max(actUntil, now + 500);
      break;
    default:
      break;
  }
}

// ---- reactions to events ----
// which scenes do something with which event (face_scenes.h: sceneEvent)
inline bool sceneTakes(const char* sc, Event e) {
  // the newer scenes keep their own list (face_scenes_more.h): 1 tap, 2 loud, 3 pat, 4 someone arrived
  if (fe::Face::sceneTakes2(sc, e == E_TAP ? 1 : e == E_LOUD ? 2 : e == E_PAT ? 3 : e == E_USER_ARRIVED ? 4 : 0)) return true;
  if (e == E_TAP) return !strcmp(sc, "fly") || !strcmp(sc, "bubbles") || !strcmp(sc, "ant") || !strcmp(sc, "crumb");
  if (e == E_LOUD) return !strcmp(sc, "fly") || !strcmp(sc, "bee") || !strcmp(sc, "pillow");
  if (e == E_PAT) return !strcmp(sc, "butterfly") || !strcmp(sc, "snack") || !strcmp(sc, "crumb") || !strcmp(sc, "clock");
  if (e == E_USER_ARRIVED) return !strcmp(sc, "book");
  return false;
}
void react(Event e, unsigned long now) {
  if (e == E_NONE) return;
  wanderStop();
  alive::stop();
  if ((doing == B_NAP || doing == B_SLEEP) && !(e == E_TAP || e == E_LOUD || e == E_USER_ARRIVED || e == E_LIGHT || e == E_PAT)) {
    brain.onEvent(e, ctx, now / 1000);
    return;
  }
  // a scene is playing: it takes the tap / bang / pat / arrival itself (the fly flees, the bubbles
  // pop, it looks up from the book...) - no reaction over the top of it
  const char* sev = e == E_TAP ? "tap" : e == E_LOUD ? "loud" : e == E_PAT ? "pat" : e == E_USER_ARRIVED ? "arrived" : nullptr;
  // (only to a scene that does something with it)
  if (sev && faceScenePlaying && doing == B_SCENE && sceneTakes(faceSceneName, e)) {
    brain.onEvent(e, ctx, now / 1000);
    faceSceneEvent(sev);
    xlog("DESK event=%s -> the scene (%s)", eventName(e), faceSceneName);
    return;
  }
  const uint8_t v = brain.onEvent(e, ctx, now / 1000);
  const char* rn = reactionName(e, v);
  // a tap while it was busy with something fun: it may pout (less often the fonder it is of you) - EMO-style
  if (e == E_TAP && (doing == B_SCENE || doing == B_SHOWOFF || doing == B_WANDER) &&
      rnd01(brain.seed) < 0.75f - 0.45f * per.t(T_AFFECTION))
    rn = "interrupted";
  if (e == E_PAT) per.onPat();
  else if (e == E_TAP) per.onTap();
  else if (e == E_LOUD) per.onLoud();
  else if (e == E_USER_ARRIVED) {
    // the greeting by the bond: shy with a stranger, overjoyed after a long time apart
    const uint8_t gs = per.greetStyle(epochNow());
    if (gs == 2) rn = "missed_you";
    else if (gs == 0 && strcmp(rn, "glance_smile")) rn = "shy_greet";
    per.onGreet();
  }
  xlog("DESK event=%s -> %s", eventName(e), rn);
  if (faceEng.playing()) faceScene("stop");
  doing = B_REACT;
  phase = 0;
  setDoing(rn);
  const float amp = ampNow();
  wakeUp();
  unsigned long dur = 2500;
  if (!strcmp(rn, "startle")) { face("startled", true); deskbody::bodyStart(deskbody::MV_CROUCH, amp); }
  else if (!strcmp(rn, "giggle")) { face("giggle", true); playSfx("w:v_hehe|900~1200:90 0:50 1000~1300:90"); }
  else if (!strcmp(rn, "annoyed")) { face("annoyed"); head(rr(-25, 25), 0); dur = 3500; }
  else if (!strcmp(rn, "purr")) { face("blissful"); playSfx("w:v_chat3|180:600"); deskbody::bodyStart(deskbody::MV_LEAN_F, amp); dur = 3000; }
  else if (!strcmp(rn, "happy_squint")) { face("happy_blink", true); }
  else if (!strcmp(rn, "lean_in")) { face("adoring"); deskbody::bodyStart(deskbody::MV_LEAN_F, amp); }
  else if (!strcmp(rn, "big_greet")) { face("excited", true); head(0, -10); deskbody::bodyStart(deskbody::MV_HOP, amp); playSfx("w:v_yay|660~990:150 0:60 880~1320:200"); dur = 3000; }
  else if (!strcmp(rn, "small_greet")) { face("happy", true); head(0, -8); playSfx("w:v_chat1|700~900:120"); }
  else if (!strcmp(rn, "glance_smile")) { face("happy_blink", true); head(0, -5); dur = 1500; }
  else if (!strcmp(rn, "interrupted")) { face("annoyed", true); head(rnd01(brain.seed) < 0.5f ? -32 : 32, 6); playSfx("w:v_hmph|320~170:220"); dur = 3200; }
  else if (!strcmp(rn, "missed_you")) {  // a long time apart and a strong bond: overjoyed
    face("adoring", true); head(0, -12); deskbody::bodyStart(deskbody::MV_HOP, amp);
    playSfx("w:v_yay|660~990:150 0:50 880~1320:150 0:50 990~1480:260"); dur = 4500;
  }
  else if (!strcmp(rn, "shy_greet")) { face("happy_blink", true); head(rr(-15, 15), 6); playSfx("w:v_chat1|600~700:90"); dur = 2500; }
  else if (!strcmp(rn, "watch_spot")) { face("wistful"); dur = 5000; }
  else if (!strcmp(rn, "flinch") || !strcmp(rn, "flinch_look")) {
    face("flinch", true);
    deskbody::bodyStart(deskbody::MV_SHIVER, amp);
    if (rn[6]) head(rr(-35, 35), -5);
  }
  else if (!strcmp(rn, "head_bob")) { face("vibing"); dur = 15000; }
  else if (!strcmp(rn, "doze")) { startBehaviour(B_NAP, now); return; }
  else if (!strcmp(rn, "go_to_bed")) { startBehaviour(B_SLEEP, now); return; }
  else if (!strcmp(rn, "wake_stretch")) { startBehaviour(B_STRETCH, now); return; }
  else if (!strcmp(rn, "ask_charge")) { startBehaviour(B_ASK_CHARGE, now); return; }
  actUntil = now + dur;
}
inline void reactStep(unsigned long now) {
  if (!strcmp(doingName, "head_bob")) {  // nod along: on the beat when it has one (its own song: music.h), else ~2 a second
    float s = sinf((now % 1000) / 1000.0f * 2 * PI);
    if (beat::periodMs > 250 && beat::conf > 0.3f && beat::lastBeatMs) {
      const float ph = fmodf((float)(long)(now - beat::lastBeatMs) / beat::periodMs, 1.0f);
      s = cosf(ph * 2 * PI);  // down on the beat
    }
    static float sent = 0;
    if (fabsf(s - sent) > 0.5f) { head(0, 6 * s); sent = s; }
    if (mus::songOn) actUntil = max(actUntil, now + 2000);  // as long as its song plays
  }
}

// ---- senses -> events (every 100 ms) ----
Event senseEvents(unsigned long now) {
  Event e = E_NONE;
  const bool moving = deskbody::bodyNow() != deskbody::MV_NONE && deskbody::bodyNow() != deskbody::MV_BREATHE;
  // (not for 4 s after its own move or the legs going limp: the body settling reads as a tap)
  // (and 1.5 s after a head move: a glance jolts the body too)
  const bool settled = now - deskbody::lastMoveMs > 4000 && now - relaxMs > 4000 && now - exHeadMovedMs > 1500;
  // resting (legs off): a firmer knock (a slow sag of the body can read as a tap)
  if (imuOk && !moving && settled && currentCommand.length() == 0 && tap.feed(imuJolt, now, resting ? 0.5f : 0.45f)) e = E_TAP;
  const bool touch = TOUCH_PIN >= 0 && digitalRead(TOUCH_PIN) == HIGH;
  if (touch && !touchWas) e = E_PAT;
  touchWas = touch;
  const Event p = pres.feed(camReady && now - faceSeenGlobalMs < 1600, tofMm, now);
  if (!e) e = p;
  // its own voice and sounds aren't the room's (they would read as "loud")
  static unsigned long spkMs = 0;
  if (aud::busy || aud::playing || aud::talkOn) spkMs = now;
  if (now - spkMs > 1000) {
    const Event s = snd.feed(ww::micLvl, now);
    if (!e) e = s;
  }
  if (camReady && camLumaAll >= 0) {
    const Event l = lit.feed(constrain(camLumaAll / 180.0f, 0.0f, 1.0f), now);
    if (!e) e = l;
  }
  static bool lowSaid = false;
  if (ctx.batteryPct < 20 && !ctx.charging && !lowSaid) { lowSaid = true; if (!e) e = E_BATTERY_LOW; }
  if (ctx.batteryPct > 30 || ctx.charging) lowSaid = false;
  return e;
}

// ---- mode in / out ----
void deskEnter() {
  if (!entered) { brain.reset(esp_random()); entered = true; }
  pres = Presence();
  pres.lastSeenMs = millis();
  paused = false;
  resting = false;
  contextUpdate(millis());
  if (ctx.charging) {  // charging (e.g. a restart then): straight back to sleep, legs left limp
    doing = B_SLEEP;
    phase = 1;
    resting = true;
    face("sleeping", true);
    setDoing("sleep");
    if (legsPowered) relaxLegs();
    actUntil = millis() + 30UL * 60000;
  } else {
    if (deskbody::folded) { if (legsPowered) foldReq = 2; }  // folded before: fold again (after a restart the board still drives them)
    else currentCommand = "sit";
    doing = B_IDLE;
    setDoing("settling");
    actUntil = millis() + 2000;
  }
  nextChoiceAt = actUntil;
  faceAmbient = true;
  xlog("DESK on: live=%d lat=%d lon=%d", (int)live, latE4, lonE4);
}
void deskLeave() {
  wanderStop();
  if (per.dirty && xl::lockFlash(300)) { prefs.putBytes("deskPer", &per.d, sizeof(per.d)); xl::unlockFlash(); per.dirty = false; }
  if (standby) { standby = false; faceStandby = false; deskCamOff = false; }
  if (deskbody::bodyNow() != deskbody::MV_NONE || resting) deskbody::bodyStop();
  resting = false;
  if (faceEng.playing()) faceScene("stop");
  headSlewTo(PAN_MID, TILT_MID);
  face("idle");
  setDoing("off");
  faceAmbient = false;
  xlog("DESK off");
}

// ---- StandBy in / out ----
void standbyEnter(unsigned long now) {
  standby = true;
  if (faceEng.playing()) faceScene("stop");
  faceStandby = true;
  deskCamOff = true;  // the camera rests
  lieDown();          // and the legs (off once down)
  doing = B_SLEEP;
  phase = 0;
  setDoing("standby");
  xlog("DESK standby on (idle %lu min)", (now - lastInteractMs) / 60000);
}
void standbyExit(unsigned long now, const char* why) {
  if (!standby) return;
  standby = false;
  faceStandby = false;
  deskCamOff = false;
  lastInteractMs = now;
  xlog("DESK standby off: %s", why);
  startBehaviour(B_STRETCH, now);  // a stretch and a smile
  face("happy");
  nextChoiceAt = actUntil;
}
// ---- every loop pass in desk mode ----
void deskTick(unsigned long now) {
  if (citySaveReq && xl::lockFlash(300)) {  // the city's name kept over a restart
    citySaveReq = false;
    prefs.putString("deskCity", cityName);
    xl::unlockFlash();
  }
  // a chat, RC or an update has the robot: no new behaviours; afterwards no "welcome back"
  const bool hold = aud::talkOn || rcActive() || otaActive;
  if (hold) {
    if (standby) standbyExit(now, "chat / RC / update");
    lastInteractMs = now;
    idleWhy = aud::talkOn ? "chat" : rcActive() ? "rc" : "update";
    if (!paused) { wanderStop(); paused = true; pausedAt = now; deskbody::bodyNow() != deskbody::MV_NONE ? deskbody::bodyStop() : (void)0;  /* (stop any move: a frozen half-done one would jump ahead after the pause) */ setDoing("paused"); }
    return;
  }
  // Face tracking (the page's switch): the head follows the face it sees - not while it naps / sleeps,
  // plays a scene or is in StandBy
  if (deskFaceTrack && headTargetDirty) {
    headTargetDirty = false;
    if (now - faceSeenGlobalMs < 1500 && !standby && !faceScenePlaying && doing != B_NAP && doing != B_SLEEP && !paused)
      headSlewTo(headPanTarget, headTiltTarget);
  }
  if (paused) {
    paused = false;
    pres.present = true;  // they were just talking / driving: here, no greeting
    pres.lastSeenMs = now;
    actUntil = now;
    nextChoiceAt = now + 1500;
  }
  deskbody::bodyUpdate(now);
  if (now - ctxMs >= 60000 || !ctxMs) {
    const bool first = !ctxMs;
    ctxMs = now; contextUpdate(now); weatherMaybe(now);
    const uint32_t ep = epochNow();
    if (!first) per.minuteTick(pres.present, ctx.hour, ep);
    if (per.dailyMaybe(ep, rnd01(brain.seed), rnd01(brain.seed), ctx.weather != W_UNKNOWN && ctx.tempC < 15)) {
      char dsc[96]; per.describe(dsc, sizeof(dsc), ep);
      xlog("DESK persona new day: %s", dsc);
    }
  }
  if (per.dirty && (!perSaveMs || now - perSaveMs > 600000) && xl::lockFlash(300)) {  // kept over a restart (every 10 min at most)
    perSaveMs = now;
    prefs.putBytes("deskPer", &per.d, sizeof(per.d));
    xl::unlockFlash();
    per.dirty = false;
  }
  if (!lastInteractMs) lastInteractMs = now;
  if (foldReq >= 0 && currentCommand.length() == 0) {  // the page's "Fold legs"
    const int r = foldReq;
    foldReq = -1;
    if (r && (r == 2 || !deskbody::folded)) foldLegs();
    else if (!r && deskbody::folded) { if (standby) standbyExit(now, "unfold"); unfoldLegs(); }
    if (!standby) { doing = B_IDLE; setDoing(deskbody::folded ? "folded" : "settling"); actUntil = millis() + 1500; nextChoiceAt = actUntil; }
    return;
  }
  if (standby) {  // a short touch: the next style; held 2 s: wake up. The needs still drift; legs off once lying
    const bool touch = TOUCH_PIN >= 0 && digitalRead(TOUCH_PIN) == HIGH;
    if (touch && !touchWas) touchDownMs = now;
    if (touch && touchDownMs && now - touchDownMs >= 2000) { touchDownMs = 0; touchWas = touch; standbyExit(now, "touch held"); return; }
    if (!touch && touchWas && touchDownMs && now - touchDownMs > 40) {
      sbStyle = (sbStyle + 1) % fe::Face::SB_STYLES;
      faceSbStyle = sbStyle;
      prefPut("deskSbStyle", sbStyle, false);
      playSfx("w:tick|v0.2 1400:25");
      xlog("DESK standby style %d", sbStyle);
    }
    if (!touch) touchDownMs = 0;
    touchWas = touch;
    if (phase == 0 && deskbody::bodyNow() == deskbody::MV_NONE) { relaxLegs(); phase = 1; }
    if (now - updMs >= 500) { updMs = now; brain.update(0.5f, ctx, true); }
    return;
  }
  if (sbOn && now - lastInteractMs > (unsigned long)sbMin * 60000UL && !faceScenePlaying && currentCommand.length() == 0) {
    standbyEnter(now);
    return;
  }
  if (now - updMs >= 500) {
    const float dt = updMs ? (now - updMs) / 1000.0f : 0.5f;
    updMs = now;
    ctx.userPresent = pres.present;
    ctx.userAwayMin = pres.awayMin(now);
    brain.update(dt, ctx, resting);
  }
  if (now - senseMs >= 100) {
    senseMs = now;
    const Event e = senseEvents(now);
    // only a touch counts (a chat too, above): desk "taps" still get their reaction, but stray jolts (glances,
    // the body settling) would keep resetting the timer
    if (e == E_PAT) { lastInteractMs = now; idleWhy = eventName(e); }
    if (e) react(e, now);
  }
  if (doing == B_WANDER && (long)(now - actUntil) < 0) { behaviourStep(now); return; }  // the stroll watches the edge every pass
  {  // someone it knows was just recognized: "Hi <name>" (once, again after 10 min away)
    static unsigned long seenWho = 0, namedMs = 0;
    static char lastNamed[24] = "";
    if (fid::whoMs != seenWho) {
      seenWho = fid::whoMs;
      if (strcmp(lastNamed, fid::whoName) || now - namedMs > 600000) {
        strlcpy(lastNamed, fid::whoName, sizeof(lastNamed));
        strlcpy(lifeMsgTitle, "Hi", sizeof(lifeMsgTitle));
        snprintf(lifeMsgL1, sizeof(lifeMsgL1), "%s!", fid::whoName);
        lifeMsgUntil = now + 4000;
        if (doing == B_IDLE || doing == B_GLANCE) { face("happy_blink"); playSfx("w:v_chat1|700~900:120"); }
        xlog("DESK recognized %s", fid::whoName);
      }
      namedMs = now;
    }
  }
  if (currentCommand.length()) return;  // an emote / the sit pose running
  if ((per.hasCold() || per.hasHiccups()) && (doing == B_IDLE || doing == B_GLANCE) && !resting && !faceScenePlaying && !clk::focusWork &&
      (long)(now - lifeEvMs) >= 0) {
    // (the face engine's own animated scenes: the sneeze, the hiccups - props, face, sounds)
    if (per.hasCold()) {  // a cold: a sneeze every few minutes
      faceScene("sneeze");
      deskbody::bodyStart(deskbody::MV_CROUCH, ampNow());
      lifeEvMs = now + (unsigned long)rr(240000, 540000);
      xlog("DESK achoo (a cold)");
    } else {  // hiccups: a bout every few minutes
      faceScene("hiccups");
      lifeEvMs = now + (unsigned long)rr(150000, 360000);
      xlog("DESK hiccups");
    }
    actUntil = now + 9000;
  }
  if (doing == B_REACT) reactStep(now);
  else behaviourStep(now);
  if ((long)(now - actUntil) < 0) return;
  if (doing != B_IDLE && doing != B_NAP && doing != B_SLEEP && nextChoiceAt < now) {
    // a short pause between behaviours: idle life
    nextChoiceAt = now + (unsigned long)(brain.nextChoiceInS(live) * 1000);
    startBehaviour(B_IDLE, now);
    actUntil = nextChoiceAt;
    return;
  }
  if ((long)(now - nextChoiceAt) < 0 && doing == B_IDLE) return;
  contextUpdate(now);
  for (int i = 0; i < B_N; i++) brain.extMul[i] = per.behMul((Beh)i);
  if (!pres.present) brain.extMul[B_SEEK_USER] *= 1 + 2 * per.expectUser(ctx.hour);  // it expects you about now
  if (clk::focusWork) {  // you're focusing (desk_clock.h): no shows, no calling for you - still, small and calm
    for (Beh q : {B_SCENE, B_SHOWOFF, B_SEEK_USER, B_GREET, B_WANDER, B_INVESTIGATE, B_FIDGET, B_STRETCH}) brain.extMul[q] *= 0.02f;
    brain.extMul[B_GLANCE] *= 0.4f;
  }
  Beh b = brain.choose(ctx, live);
  if (forceScene) { forceScene = false; b = B_SCENE; }  // "Surprise me"
  char who[16];
  if (pres.present) strcpy(who, "here");
  else snprintf(who, sizeof(who), "away %dm", (int)ctx.userAwayMin);
  xlog("DESK want=%s mood=%s e=%.2f b=%.2f c=%.2f co=%.2f cf=%.2f ctx=%dh,%s,light=%.2f,noise=%.2f,user=%s", behName(b),
       moodName(brain.mood()), brain.need[ENERGY], brain.need[BOREDOM], brain.need[CURIOSITY], brain.need[COMPANY],
       brain.need[COMFORT], ctx.hour, weatherName(ctx.weather), ctx.light, ctx.noise, who);
  startBehaviour(b, now);
  nextChoiceAt = actUntil;
}

// ---- GET /desk: state; ?live=0|1|2 ?lat=&lon= ?surprise=1 ?move=<name> ----
esp_err_t desk_handler(httpd_req_t* req) {
  char q[160] = "", v[24];
  httpd_req_get_url_query_str(req, q, sizeof(q));
  if (httpd_query_key_value(q, "sb", v, sizeof(v)) == ESP_OK) {
    sbOn = v[0] == '1';
    prefPut("deskSb", sbOn ? 1 : 0, false);
    lastInteractMs = millis();
    if (!sbOn && standby) standbyExit(millis(), "switched off");
  }
  if (httpd_query_key_value(q, "ft", v, sizeof(v)) == ESP_OK) {
    deskFaceTrack = v[0] == '1';
    prefPut("deskFt", deskFaceTrack ? 1 : 0, false);
  }
  if (httpd_query_key_value(q, "sbmin", v, sizeof(v)) == ESP_OK) {
    sbMin = constrain(atoi(v), 1, 60);
    prefPut("deskSbMin", sbMin, false);
    lastInteractMs = millis();
  }
  if (httpd_query_key_value(q, "wake", v, sizeof(v)) == ESP_OK && standby) standbyExit(millis(), "page");
  if (httpd_query_key_value(q, "sbnight", v, sizeof(v)) == ESP_OK) {
    faceSbNightMode = constrain(atoi(v), 0, 2);
    prefPut("deskSbNight", faceSbNightMode, false);
  }
  if (httpd_query_key_value(q, "sbstyle", v, sizeof(v)) == ESP_OK) {
    sbStyle = constrain(atoi(v), 0, fe::Face::SB_STYLES - 1);
    faceSbStyle = sbStyle;
    prefPut("deskSbStyle", sbStyle, false);
  }
  if (httpd_query_key_value(q, "standby", v, sizeof(v)) == ESP_OK && activeMode == MODE_DESK && !standby) standbyEnter(millis());  // (try it now)
  if (httpd_query_key_value(q, "live", v, sizeof(v)) == ESP_OK) {
    live = (Live)constrain(atoi(v), 0, 2);
    prefPut("deskLive", (int)live, false);
  }
  if (httpd_query_key_value(q, "lat", v, sizeof(v)) == ESP_OK) {
    latE4 = (int)lroundf(atof(v) * 10000);
    if (httpd_query_key_value(q, "lon", v, sizeof(v)) == ESP_OK) lonE4 = (int)lroundf(atof(v) * 10000);
    prefPut("deskLat", latE4, false);
    prefPut("deskLon", lonE4, false);
    weatherTryMs = 0;  // fetch soon
    ctxMs = 0;
  }
  // (not while a scene plays: each press would cut the last one short)
  if (httpd_query_key_value(q, "surprise", v, sizeof(v)) == ESP_OK && activeMode == MODE_DESK && !faceScenePlaying) {
    actUntil = 0; nextChoiceAt = 0; doing = B_IDLE; forceScene = true;
  }
  char sn[16];
  if (httpd_query_key_value(q, "scene", sn, sizeof(sn)) == ESP_OK && activeMode == MODE_DESK && !faceScenePlaying) {
    // a given scene now
    faceScene(sn);
    doing = B_SCENE;
    sceneLogPending = true;
    actUntil = millis() + 6000;
  }
  char cv2[40];
  if (httpd_query_key_value(q, "tzoff", cv2, sizeof(cv2)) == ESP_OK) {  // timezone by hand (seconds from UTC), or "auto" (from the city)
    if (!strcmp(cv2, "auto")) { prefs.putBool("tzAuto", true); weatherTryMs = 0; }
    else { prefs.putBool("tzAuto", false); applyUtcOffset(atoi(cv2), true); }
  }
  if (httpd_query_key_value(q, "country", cv2, sizeof(cv2)) == ESP_OK) {  // prefer this country in city search (2 letters)
    strlcpy(cityCountry, cv2, sizeof(cityCountry));
    prefs.putString("cityCC", cityCountry);
  }
  if (httpd_query_key_value(q, "city", cv2, sizeof(cv2)) == ESP_OK && cv2[0]) {
    {  // %XX and + decoded
      char* w = cv2;
      for (const char* r = cv2; *r; r++) {
        if (*r == '+') *w++ = ' ';
        else if (*r == '%' && r[1] && r[2]) { char hx[3] = {r[1], r[2], 0}; *w++ = (char)strtol(hx, nullptr, 16); r += 2; }
        else *w++ = *r;
      }
      *w = 0;
    }
    strlcpy(cityReq, cv2, sizeof(cityReq));
    strlcpy(cityName, "looking up...", sizeof(cityName));
    weatherTryMs = 0;
    ctxMs = 0;  // (the next tick starts the lookup)
  }
  // The phone's clock and weather (the page sends them): on the robot's own hotspot there's no internet,
  // so no NTP and no Open-Meteo.
  if (httpd_query_key_value(q, "t", v, sizeof(v)) == ESP_OK) {
    const time_t t = (time_t)atoll(v);
    if (t > 1700000000) {
      const time_t mine = time(nullptr);
      if (mine < 1700000000 || llabs((long long)(mine - t)) > 60) {  // not set yet (no NTP), or off
        struct timeval tv = {t, 0};
        settimeofday(&tv, nullptr);
        xlogSys("DESK clock set from the phone");
        ctxMs = 0;
      }
    }
  }
  char wc[12];
  if (httpd_query_key_value(q, "wxcode", wc, sizeof(wc)) == ESP_OK && httpd_query_key_value(q, "wxtemp", v, sizeof(v)) == ESP_OK) {
    char js[96];
    snprintf(js, sizeof(js), "{\"current\":{\"temperature_2m\":%s,\"weather_code\":%s}}", v, wc);
    Weather w;
    float t;
    if (parseOpenMeteo(js, w, t)) {
      wxKind = w;
      wxTemp = t;
      weatherTryMs = millis();
      ctxMs = 0;
    }
  }
  if (httpd_query_key_value(q, "walk", v, sizeof(v)) == ESP_OK) {
    walkAllowed = atoi(v) == 1;
    if (!walkAllowed) wanderStop();
    xlog("DESK may walk on the desk: %s", walkAllowed ? "yes (edge-checked)" : "no");
  }
  if (httpd_query_key_value(q, "fold", v, sizeof(v)) == ESP_OK) foldReq = v[0] == '1' ? 1 : 0;
  if (httpd_query_key_value(q, "move", v, sizeof(v)) == ESP_OK) {
    const Move m = deskbody::moveByName(v);
    if (m != deskbody::MV_NONE) deskbody::bodyStart(m, 0.8f);
  }
  char o[800];
  char dsc[96];
  per.describe(dsc, sizeof(dsc), epochNow());
  snprintf(o, sizeof(o),
           "{\"persona\":\"%s\",\"traits\":[%.2f,%.2f,%.2f,%.2f,%.2f],\"bond\":%.2f,\"pats\":%u,\"walk\":%d,\"on\":%d,\"mood\":\"%s\",\"needs\":[%.2f,%.2f,%.2f,%.2f,%.2f],\"doing\":\"%s\",\"live\":%d,\"weather\":\"%s\",\"temp\":%.1f,"
           "\"lat\":%.4f,\"lon\":%.4f,\"user\":\"%s\",\"move\":\"%s\",\"city\":\"%s\",\"sb\":%d,\"sbmin\":%d,\"standby\":%d,\"sbstyle\":%d,\"sbnight\":%d,\"idle\":%lu,\"idleWhy\":\"%s\",\"net\":%d,\"ft\":%d,\"fold\":%d}",
           dsc, per.d.trait[0], per.d.trait[1], per.d.trait[2], per.d.trait[3], per.d.trait[4], per.d.bond, (unsigned)per.d.pats, walkAllowed ? 1 : 0,
           activeMode == MODE_DESK, moodName(brain.mood()), brain.need[ENERGY], brain.need[BOREDOM], brain.need[CURIOSITY],
           brain.need[COMPANY], brain.need[COMFORT], doingName, (int)live, weatherName((Weather)(int)wxKind), (float)wxTemp,
           latE4 / 10000.0, lonE4 / 10000.0, pres.present ? "here" : "away", deskbody::moveName(deskbody::bodyNow()), cityName,
           sbOn ? 1 : 0, sbMin, standby ? 1 : 0, sbStyle, (int)faceSbNightMode, (millis() - lastInteractMs) / 1000, idleWhy, WiFi.status() == WL_CONNECTED ? 1 : 0, deskFaceTrack ? 1 : 0, deskbody::folded ? 1 : 0);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, o, HTTPD_RESP_USE_STRLEN);
}

unsigned long bodyQuietMs() {
  const unsigned long now = millis();
  if (currentCommand.length() || (deskbody::bodyNow() != deskbody::MV_NONE && deskbody::bodyNow() != deskbody::MV_BREATHE)) return 0;
  return now - deskbody::lastMoveMs;
}
inline void deskLoadSettings() {
  prefs.getString("cityCC", cityCountry, sizeof(cityCountry));
  live = (Live)constrain((int)prefs.getInt("deskLive", 0), 0, 2);
  prefs.getString("deskCity", cityName, sizeof(cityName));
  latE4 = prefs.getInt("deskLat", 0);
  sbOn = prefs.getInt("deskSb", 1) != 0;
  deskFaceTrack = prefs.getInt("deskFt", 0) != 0;
  deskbody::folded = prefs.getInt("deskFold", 0) != 0;
  sbMin = constrain((int)prefs.getInt("deskSbMin", 5), 1, 60);
  sbStyle = constrain((int)prefs.getInt("deskSbStyle", 0), 0, fe::Face::SB_STYLES - 1);
  faceSbStyle = sbStyle;
  lonE4 = prefs.getInt("deskLon", 0);
  faceSbNightMode = constrain((int)prefs.getInt("deskSbNight", 0), 0, 2);
  if (prefs.getBytesLength("deskPer") == sizeof(PersonaData)) {
    PersonaData pd;
    prefs.getBytes("deskPer", &pd, sizeof(pd));
    if (pd.version == PersonaData().version) per.d = pd;
  }
}

}  // namespace deskm
