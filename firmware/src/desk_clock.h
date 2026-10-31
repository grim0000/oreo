// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
// Timers, alarms and the sleep timer: Oreo as a desk clock, in every mode.
//  - Timers: up to 4 at once, each with a name ("tea"). Done -> a chime, a face, an OLED card; a touch on
//    the head stops it (it gives up by itself after 45 s).
//  - Alarms: up to 6, hh:mm on chosen weekdays (or once), saved on the robot. Ringing: a touch = snooze
//    9 min, held 2 s = off; it gives up after 3 min. "Shut down" sleeps until the next alarm (the ESP's
//    timer wakes it ~2 min early, a normal start, then it rings on time - the RTC keeps the clock).
//  - Sleep timer: after N minutes the music fades out, Oreo yawns and (desk mode) goes to StandBy, or
//    shuts down (deep sleep) when asked.
// Sounds: /sesame/alarm.wav and /sesame/timer.wav on the card if they're there, else a short chime.
// Included by main.cpp after desk_mode.h; clk::tick() runs in loop(), GET /clock is the page's API.
#pragma once

extern volatile bool shutdownReq;  // main.cpp (below): the page's "Shut down", done by loop()

namespace clk {

struct Timer { uint32_t endMs, totalS; char label[20]; bool on; };
struct Alarm { uint8_t h, m, days, on; char label[20]; };  // days: bit 0 = Sunday .. bit 6 = Saturday; 0 = once
const int NT = 4, NA = 6;
Timer timers[NT];
Alarm alarms[NA];
int nAlarms = 0;

enum Ring { R_NONE, R_TIMER, R_ALARM };
Ring ring = R_NONE;
char ringLabel[20] = "";
int ringAlarm = -1;
unsigned long ringStart = 0, nextChime = 0, snoozeUntil = 0, touchDownMs = 0;
int snoozeAlarm = -1;
bool touchWas = false;
int lastFiredMin = -1;  // the minute of the day an alarm last went off (no double ring in that minute)

uint32_t sleepEndMs = 0;  // the sleep timer (0 = off)
bool sleepDeep = false;

// ---- Focus (Pomodoro): work blocks with breaks; desk mode stays quiet while one runs (desk_mode.h) ----
bool focusOn = false, focusWork = false, focusAwayNoted = false;
int fWorkMin = 25, fBreakMin = 5, fRounds = 4, fRound = 0;
uint32_t fPhaseEnd = 0, fPhaseStart = 0;
struct FocusStats { uint32_t day; uint16_t minToday, streak; uint32_t lastDay, totalMin; };
FocusStats fst = {0, 0, 0, 0, 0};
// ---- Break nudges (stretch / water / eyes) while you're at the desk; bedtime; the morning hello ----
int nudgeMin = 0;  // 0 = off
unsigned long lastNudgeMs = 0;
int nudgeIdx = 0;
int bedH = -1, bedM = 0, bedDoneDay = -1;
unsigned long morningCard2 = 0;

inline bool ringing() { return ring != R_NONE; }
inline bool clockOk(struct tm& t) { return getLocalTime(&t, 0) && t.tm_year > 120; }

void save() {
  if (!xl::lockFlash(500)) return;
  prefs.putBytes("alarms", alarms, sizeof(Alarm) * nAlarms);
  prefs.putUChar("alarmN", nAlarms);
  xl::unlockFlash();
}
void load() {
  nAlarms = constrain((int)prefs.getUChar("alarmN", 0), 0, NA);
  if (nAlarms && prefs.getBytesLength("alarms") == sizeof(Alarm) * nAlarms) prefs.getBytes("alarms", alarms, sizeof(Alarm) * nAlarms);
  else nAlarms = 0;
  for (auto& t : timers) t.on = false;
  if (prefs.getBytesLength("focusSt") == sizeof(fst)) prefs.getBytes("focusSt", &fst, sizeof(fst));
  nudgeMin = prefs.getInt("nudgeMin", 0);
  const int bed = prefs.getInt("bedTime", -1);  // minutes of the day, -1 = off
  bedH = bed >= 0 ? bed / 60 : -1;
  bedM = bed >= 0 ? bed % 60 : 0;
}
// today as a number (year * 400 + day of the year; 0 = no clock)
uint32_t dayKey() { struct tm t; return clockOk(t) ? (uint32_t)(t.tm_year * 400 + t.tm_yday) : 0; }
// whole minutes of focus done: today's total, the day streak, all-time
void focusCredit(int mins) {
  if (mins <= 0) return;
  const uint32_t d = dayKey();
  if (d && d != fst.day) {  // a new day: yesterday counted -> the streak goes on, else it starts again
    fst.streak = fst.lastDay && d - fst.lastDay == 1 ? fst.streak : 0;
    fst.day = d;
    fst.minToday = 0;
  }
  fst.minToday += mins;
  fst.totalMin += mins;
  if (d && fst.lastDay != d) { fst.streak++; fst.lastDay = d; }
  if (xl::lockFlash(500)) { prefs.putBytes("focusSt", &fst, sizeof(fst)); xl::unlockFlash(); }
}
uint16_t minToday() { return fst.day == dayKey() ? fst.minToday : 0; }

bool cardAnyMode = false;  // the OLED shows lifeMsg in every mode while it's the clock's (oled_ui.h)
void msg(const char* t, const char* l1, unsigned long ms) {
  strlcpy(lifeMsgTitle, t, sizeof(lifeMsgTitle));
  strlcpy(lifeMsgL1, l1, sizeof(lifeMsgL1));
  lifeMsgUntil = millis() + ms;
  cardAnyMode = true;
}
// the OLED's quiet card while a timer (or the sleep timer) runs: the one ending soonest
bool runningCard(char* title, size_t tn, char* l1, size_t ln) {
  const unsigned long now = millis();
  if (focusOn) {
    const long s = max(0L, (long)((int32_t)(fPhaseEnd - now)) / 1000);
    strlcpy(title, focusWork ? "Focus" : "Break", tn);
    snprintf(l1, ln, "%ld:%02ld  %d/%d", s / 60, s % 60, fRound, fRounds);
    return true;
  }
  int best = -1;
  long left = 0;
  for (int i = 0; i < NT; i++)
    if (timers[i].on) {
      const long s = (long)((int32_t)(timers[i].endMs - now)) / 1000;
      if (best < 0 || s < left) { best = i; left = s; }
    }
  if (best >= 0) {
    strlcpy(title, timers[best].label, tn);
    snprintf(l1, ln, "%ld:%02ld left", max(0L, left) / 60, max(0L, left) % 60);
    return true;
  }
  if (sleepEndMs) {
    strlcpy(title, "Sleep", tn);
    snprintf(l1, ln, "in %ld min", ((long)((int32_t)(sleepEndMs - now)) / 1000 + 59) / 60);
    return true;
  }
  return false;
}

// one chime: the card's WAV if it's there, else the synth (a song playing fades out first)
void chime(bool alarm, int n) {
  if (aud::playing) { aud::stopReq = true; return; }  // next chime: the music has gone
  const char* wav = alarm ? "/sesame/alarm.wav" : "/sesame/timer.wav";
  const float vol = alarm ? min(1.0f, 0.35f + 0.08f * n) : 0.5f;  // an alarm grows a little louder
  if (sdSizeMB >= 0 && SD_MMC.exists(wav) && aud::play(wav, vol)) return;
  aud::sfx(alarm ? "v0.55 784:110 988:110 1175:110 1568:320 0:160 1175:110 1568:420"
                 : "v0.5 1047:120 1319:120 1568:300 0:140 1568:120 2093:340");
}

void startRing(Ring r, const char* label, int alarmIdx) {
  ring = r;
  strlcpy(ringLabel, label && *label ? label : (r == R_ALARM ? "Alarm" : "Timer"), sizeof(ringLabel));
  ringAlarm = alarmIdx;
  ringStart = millis();
  nextChime = 0;
  touchDownMs = 0;
  touchWas = TOUCH_PIN >= 0 && digitalRead(TOUCH_PIN) == HIGH;  // a hand already there doesn't count
  if (activeMode == MODE_DESK) deskm::standbyExit(ringStart, r == R_ALARM ? "alarm" : "timer");
  setFaceWithMode(r == R_ALARM ? "excited" : "surprised", FACE_ANIM_ONCE);
  xlog("CLOCK %s ringing: %s", r == R_ALARM ? "alarm" : "timer", ringLabel);
}
void morningHello();
long nextAlarmSecs(int* which = nullptr);
void stopRing(const char* why) {
  if (!ring) return;
  xlog("CLOCK %s stopped (%s)", ring == R_ALARM ? "alarm" : "timer", why);
  if (ring == R_ALARM && strcmp(why, "nobody came") != 0) {
    struct tm t;
    if (clockOk(t) && t.tm_hour >= 4 && t.tm_hour < 12) { ring = R_NONE; ringAlarm = -1; morningHello(); return; }
  }
  ring = R_NONE;
  ringAlarm = -1;
  if (aud::playing) aud::stopReq = true;
  lifeMsgUntil = 0;
  setFaceWithMode("happy", FACE_ANIM_ONCE);
}
void snooze() {
  if (ring != R_ALARM) { stopRing("touch"); return; }
  snoozeAlarm = ringAlarm >= 0 ? ringAlarm : NA;  // (a snoozed snooze: NA = keep the label only)
  snoozeUntil = millis() + 9 * 60000UL;
  if (!snoozeUntil) snoozeUntil = 1;
  char l[22];
  snprintf(l, sizeof(l), "%s +9 min", ringLabel);
  stopRing("snooze");
  msg("Snooze", l, 4000);
}

int addTimer(uint32_t secs, const char* label) {
  for (int i = 0; i < NT; i++)
    if (!timers[i].on) {
      timers[i].endMs = millis() + secs * 1000;
      timers[i].totalS = secs;
      timers[i].on = true;
      strlcpy(timers[i].label, label && *label ? label : "Timer", sizeof(timers[i].label));
      char l[22];
      snprintf(l, sizeof(l), "%s %lu:%02lu", timers[i].label, (unsigned long)secs / 60, (unsigned long)secs % 60);
      msg("Timer", l, 2500);
      xlog("CLOCK timer %d: %lu s (%s)", i, (unsigned long)secs, timers[i].label);
      return i;
    }
  return -1;
}

// ---- focus blocks ----
void softChime(int kind) {  // 0 start work, 1 break, 2 all done
  if (aud::playing || aud::talkOn) return;
  static const char* const P[] = {"v0.3 523:110 784:220", "v0.35 784:120 659:120 523:260", "v0.45 523:100 659:100 784:100 1047:360"};
  aud::sfx(P[kind]);
}
void focusWorkStart(unsigned long now) {
  focusWork = true;
  focusAwayNoted = false;
  fPhaseStart = now;
  fPhaseEnd = now + fWorkMin * 60000UL;
  if (activeMode == MODE_DESK) deskm::standbyExit(now, "focus");
  setFaceWithMode("focused", FACE_ANIM_ONCE);
  char l[22];
  snprintf(l, sizeof(l), "%d min - go!", fWorkMin);
  msg(fRound == 1 ? "Focus" : "Back to it", l, 4000);
  softChime(0);
  xlog("CLOCK focus %d/%d: work %d min", fRound, fRounds, fWorkMin);
}
void focusStart(int work, int brk, int rounds) {
  fWorkMin = constrain(work, 1, 180);
  fBreakMin = constrain(brk, 1, 60);
  fRounds = constrain(rounds, 1, 12);
  fRound = 1;
  focusOn = true;
  focusWorkStart(millis());
}
void focusStop(const char* why) {
  if (!focusOn) return;
  if (focusWork) focusCredit((int)((millis() - fPhaseStart) / 60000));
  focusOn = focusWork = false;
  xlog("CLOCK focus stopped (%s)", why);
}
static const char* const NUDGE[][2] = {{"Stretch", "stand up a bit"}, {"Water", "have a sip"}, {"Eyes", "look far, 20 s"},
                                       {"Posture", "sit up tall"}, {"Breathe", "3 slow breaths"}};
void focusTick(unsigned long now) {
  if (!focusOn) return;
  if (focusWork && !focusAwayNoted && activeMode == MODE_DESK && camReady && now - fPhaseStart > 240000 &&
      now - faceSeenGlobalMs > 240000) {  // no face for 4 min mid-block: once, gently
    focusAwayNoted = true;
    setFaceWithMode("confused", FACE_ANIM_ONCE);
    msg("Focus", "still there?", 5000);
  }
  if ((int32_t)(now - fPhaseEnd) < 0) return;
  if (focusWork) {
    focusWork = false;
    focusCredit(min(fWorkMin, (int)((now - fPhaseStart + 30000) / 60000)));  // (skipped early: what was really done)
    if (fRound >= fRounds) {
      focusOn = false;
      setFaceWithMode("proud", FACE_ANIM_ONCE);
      char l[22];
      snprintf(l, sizeof(l), "%d min today", minToday());
      msg("All done!", l, 8000);
      softChime(2);
      xlog("CLOCK focus done: %d rounds, %u min today, streak %u", fRounds, minToday(), fst.streak);
      return;
    }
    fPhaseStart = now;
    fPhaseEnd = now + fBreakMin * 60000UL;
    setFaceWithMode("relaxed", FACE_ANIM_ONCE);
    const int k = nudgeIdx++ % 5;
    char l[22];
    snprintf(l, sizeof(l), "%s: %s", NUDGE[k][0], NUDGE[k][1]);
    msg("Break", l, 8000);
    softChime(1);
    xlog("CLOCK focus break %d min", fBreakMin);
  } else {
    fRound++;
    focusWorkStart(now);
  }
}
// ---- break nudges (desk mode, you at the desk, no focus running) ----
void nudgeTick(unsigned long now) {
  if (!nudgeMin || focusOn || ring || activeMode != MODE_DESK || deskm::standby) { if (!nudgeMin || focusOn) lastNudgeMs = now; return; }
  if (!lastNudgeMs) lastNudgeMs = now;
  if (now - lastNudgeMs < nudgeMin * 60000UL) return;
  if (camReady && now - faceSeenGlobalMs > 120000) return;  // nobody here: wait for you
  lastNudgeMs = now;
  const int k = nudgeIdx++ % 5;
  setFaceWithMode("cute", FACE_ANIM_ONCE);
  msg(NUDGE[k][0], NUDGE[k][1], 9000);
  softChime(1);
  xlog("CLOCK nudge: %s", NUDGE[k][0]);
}
// ---- bedtime and the morning hello ----
void morningHello() {
  setFaceWithMode("happy", FACE_ANIM_ONCE);
  char l[22] = "have a nice day";
  if (deskm::ctx.weather != desk::W_UNKNOWN) snprintf(l, sizeof(l), "%s, %d C", desk::weatherName(deskm::ctx.weather), (int)lroundf(deskm::ctx.tempC));
  msg("Morning!", l, 6000);
  morningCard2 = millis() + 6500;
  xlog("CLOCK morning hello");
}
void routineTick(unsigned long now) {
  if (morningCard2 && (int32_t)(now - morningCard2) >= 0) {
    morningCard2 = 0;
    char l[22];
    if (fst.streak > 1 && fst.lastDay + 1 >= dayKey()) snprintf(l, sizeof(l), "focus streak %u days", fst.streak);
    else strlcpy(l, "focus later?", sizeof(l));
    msg("Today", l, 5000);
  }
  struct tm t;
  if (bedH < 0 || !clockOk(t) || t.tm_hour != bedH || t.tm_min != bedM || bedDoneDay == t.tm_yday) return;
  bedDoneDay = t.tm_yday;
  setFaceWithMode("sleepy", FACE_ANIM_ONCE);
  char l[22] = "sleep well";
  int nx = -1;
  const long s = nextAlarmSecs(&nx);
  if (s > 0 && nx >= 0) snprintf(l, sizeof(l), "alarm %02d:%02d", alarms[nx].h, alarms[nx].m);
  msg("Bedtime", l, 15000);
  softChime(1);
  if (activeMode == MODE_DESK) deskm::standbyEnter(now);
  xlog("CLOCK bedtime");
}

// seconds from now to alarm a (or -1: off / no clock)
long secsTo(const Alarm& a, const struct tm& t) {
  if (!a.on) return -1;
  const long nowS = t.tm_hour * 3600L + t.tm_min * 60 + t.tm_sec;
  for (int d = 0; d < 8; d++) {
    const int wd = (t.tm_wday + d) % 7;
    if (a.days && !(a.days >> wd & 1)) continue;
    const long s = d * 86400L + a.h * 3600L + a.m * 60 - nowS;
    if (s > 0) return s;
  }
  return -1;
}
// the next alarm (seconds, -1 none) - for the page and for sleeping until it
long nextAlarmSecs(int* which) {
  struct tm t;
  if (!clockOk(t)) return -1;
  long best = -1;
  for (int i = 0; i < nAlarms; i++) {
    const long s = secsTo(alarms[i], t);
    if (s > 0 && (best < 0 || s < best)) { best = s; if (which) *which = i; }
  }
  return best;
}
// called by shutdownNow() just before deep sleep: wake ~2 min before the next alarm
void armWake() {
  const long s = nextAlarmSecs();
  if (s > 150) {
    esp_sleep_enable_timer_wakeup((uint64_t)(s - 120) * 1000000ULL);
    xlogSys("CLOCK sleeping until the next alarm (wake in %ld min)", (s - 120) / 60);
  }
}

void tick(unsigned long now) {
  focusTick(now);
  nudgeTick(now);
  routineTick(now);
  // timers
  for (int i = 0; i < NT; i++)
    if (timers[i].on && (int32_t)(now - timers[i].endMs) >= 0) {
      timers[i].on = false;
      if (!ring) startRing(R_TIMER, timers[i].label, -1);
    }
  // alarms: once a minute is enough, at its first second
  struct tm t;
  if (clockOk(t)) {
    const int minOfDay = t.tm_hour * 60 + t.tm_min;
    if (minOfDay != lastFiredMin)
      for (int i = 0; i < nAlarms; i++) {
        const Alarm& a = alarms[i];
        if (!a.on || a.h != t.tm_hour || a.m != t.tm_min) continue;
        if (a.days && !(a.days >> t.tm_wday & 1)) continue;
        lastFiredMin = minOfDay;
        if (!a.days) { alarms[i].on = 0; save(); }  // a one-off: done
        if (!ring) startRing(R_ALARM, a.label, i);
        break;
      }
  }
  if (snoozeUntil && (int32_t)(now - snoozeUntil) >= 0) {
    snoozeUntil = 0;
    if (!ring) startRing(R_ALARM, ringLabel, snoozeAlarm < NA ? snoozeAlarm : -1);
  }
  // sleep timer
  if (sleepEndMs && (int32_t)(now - sleepEndMs) >= 0) {
    sleepEndMs = 0;
    xlog("CLOCK sleep timer ended (%s)", sleepDeep ? "shut down" : "quiet");
    if (aud::playing) aud::stopReq = true;
    setFaceWithMode("sleepy", FACE_ANIM_ONCE);
    msg("Good night", sleepDeep ? "shutting down" : "sleep well", 4000);
    if (sleepDeep) shutdownReq = true;
    else if (activeMode == MODE_DESK) deskm::standbyEnter(now);
  }

  if (!ring) return;
  // ringing: the card, a chime every few seconds; a touch snoozes / stops
  const bool alarm = ring == R_ALARM;
  const unsigned long ringFor = now - ringStart;
  if (ringFor > (alarm ? 180000UL : 45000UL)) {
    char l[22];
    snprintf(l, sizeof(l), "missed: %s", ringLabel);
    stopRing("nobody came");
    msg(alarm ? "Alarm" : "Timer", l, 60000);
    return;
  }
  char l1[22];
  snprintf(l1, sizeof(l1), alarm ? "%s - touch me" : "%s done!", ringLabel);
  strlcpy(lifeMsgTitle, alarm ? "Alarm" : "Timer", sizeof(lifeMsgTitle));
  strlcpy(lifeMsgL1, l1, sizeof(lifeMsgL1));
  lifeMsgUntil = now + 1500;
  cardAnyMode = true;
  if (!nextChime || (int32_t)(now - nextChime) >= 0) {
    chime(alarm, (int)(ringFor / 4000));
    if (nextChime) setFaceWithMode(alarm ? "excited" : "surprised", FACE_ANIM_ONCE);
    nextChime = now + (alarm ? 4000 : 6000);
  }
  const bool touch = TOUCH_PIN >= 0 && digitalRead(TOUCH_PIN) == HIGH;
  if (touch && !touchWas) touchDownMs = now;
  if (touch && touchDownMs && alarm && now - touchDownMs >= 2000) { stopRing("touch held"); touchDownMs = 0; }
  else if (!touch && touchWas && touchDownMs) { touchDownMs = 0; snooze(); }
  touchWas = touch;
}

// ---- GET /clock: the page ----
// ?timer=SECS[&label=x]  ?tcancel=i  ?alarm=HH:MM[&days=0..127][&label=x][&id=i: change that one]
// ?adel=i  ?aon=i&v=0|1  ?stop=1  ?snooze=1  ?sleep=MIN[&deep=1] (0 = off)  -> the state as JSON
esp_err_t clock_handler(httpd_req_t* req) {
  char q[200] = {0}, v[24], lab[40] = "";
  httpd_req_get_url_query_str(req, q, sizeof(q));
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  if (httpd_query_key_value(q, "label", lab, sizeof(lab)) == ESP_OK) {
    for (char* p = lab; *p; p++) if (*p == '+') *p = ' ';  // (the page sends encodeURIComponent; '+' = space)
    char* o = lab;  // %xx -> the byte
    for (const char* p = lab; *p; p++) {
      if (*p == '%' && isxdigit((uint8_t)p[1]) && isxdigit((uint8_t)p[2])) { char h[3] = {p[1], p[2], 0}; *o++ = (char)strtol(h, nullptr, 16); p += 2; }
      else *o++ = *p;
    }
    *o = 0;
    for (char* p = lab; *p; p++) if (*p == '"' || *p == '\\' || (uint8_t)*p < 32) *p = ' ';  // JSON-safe
  }
  if (httpd_query_key_value(q, "timer", v, sizeof(v)) == ESP_OK) {
    const long s = atol(v);
    if (s > 0 && s <= 24 * 3600) addTimer((uint32_t)s, lab);
  }
  if (httpd_query_key_value(q, "tcancel", v, sizeof(v)) == ESP_OK) {
    const int i = atoi(v);
    if (i >= 0 && i < NT) timers[i].on = false;
  }
  if (httpd_query_key_value(q, "alarm", v, sizeof(v)) == ESP_OK) {
    int h = -1, m = -1;
    if (sscanf(v, "%d:%d", &h, &m) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60) {
      char d[8] = "", id[8] = "";
      int i = httpd_query_key_value(q, "id", id, sizeof(id)) == ESP_OK ? atoi(id) : -1;
      if (i < 0 || i >= nAlarms) i = nAlarms < NA ? nAlarms++ : -1;
      if (i >= 0) {
        alarms[i].h = h;
        alarms[i].m = m;
        alarms[i].days = httpd_query_key_value(q, "days", d, sizeof(d)) == ESP_OK ? (uint8_t)(atoi(d) & 127) : 0;
        alarms[i].on = 1;
        strlcpy(alarms[i].label, *lab ? lab : "Alarm", sizeof(alarms[i].label));
        save();
        xlog("CLOCK alarm %d set %02d:%02d days %d", i, h, m, alarms[i].days);
      }
    }
  }
  if (httpd_query_key_value(q, "adel", v, sizeof(v)) == ESP_OK) {
    const int i = atoi(v);
    if (i >= 0 && i < nAlarms) {
      for (int k = i; k < nAlarms - 1; k++) alarms[k] = alarms[k + 1];
      nAlarms--;
      save();
    }
  }
  if (httpd_query_key_value(q, "aon", v, sizeof(v)) == ESP_OK) {
    const int i = atoi(v);
    char on[4] = "1";
    httpd_query_key_value(q, "v", on, sizeof(on));
    if (i >= 0 && i < nAlarms) { alarms[i].on = on[0] == '1'; save(); }
  }
  if (httpd_query_key_value(q, "stop", v, sizeof(v)) == ESP_OK) { stopRing("page"); snoozeUntil = 0; }
  if (httpd_query_key_value(q, "snooze", v, sizeof(v)) == ESP_OK && ring) snooze();
  if (httpd_query_key_value(q, "sleep", v, sizeof(v)) == ESP_OK) {
    const int mins = constrain(atoi(v), 0, 600);
    char d[4] = "0";
    httpd_query_key_value(q, "deep", d, sizeof(d));
    sleepDeep = d[0] == '1';
    sleepEndMs = mins ? millis() + mins * 60000UL : 0;
    if (mins && !sleepEndMs) sleepEndMs = 1;
    xlog("CLOCK sleep timer %d min%s", mins, sleepDeep ? " then shut down" : "");
  }

  if (httpd_query_key_value(q, "focus", v, sizeof(v)) == ESP_OK) {  // ?focus=MIN&brk=MIN&rounds=N (0 = stop)
    const int w = atoi(v);
    char b[8] = "5", r[8] = "4";
    httpd_query_key_value(q, "brk", b, sizeof(b));
    httpd_query_key_value(q, "rounds", r, sizeof(r));
    if (w > 0) focusStart(w, atoi(b), atoi(r));
    else focusStop("page");
  }
  if (httpd_query_key_value(q, "skip", v, sizeof(v)) == ESP_OK && focusOn) fPhaseEnd = millis();  // end this block / break now
  if (httpd_query_key_value(q, "nudge", v, sizeof(v)) == ESP_OK) {
    nudgeMin = constrain(atoi(v), 0, 240);
    lastNudgeMs = millis();
    prefPut("nudgeMin", nudgeMin, false);
  }
  if (httpd_query_key_value(q, "bed", v, sizeof(v)) == ESP_OK) {  // ?bed=HH:MM or ?bed=off
    int h = -1, m = 0;
    if (sscanf(v, "%d:%d", &h, &m) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60) { bedH = h; bedM = m; }
    else bedH = -1;
    bedDoneDay = -1;
    prefPut("bedTime", bedH >= 0 ? bedH * 60 + bedM : -1, false);
  }

  // the state
  static char* out = (char*)heap_caps_malloc(1600, MALLOC_CAP_SPIRAM);
  if (!out) return httpd_resp_send_500(req);
  const unsigned long now = millis();
  struct tm t;
  const bool ok = clockOk(t);
  int nx = -1;
  const long nxs = nextAlarmSecs(&nx);
  int o = snprintf(out, 1600, "{\"clock\":%d,\"now\":\"%02d:%02d\",\"wday\":%d,\"timers\":[", ok ? 1 : 0, ok ? t.tm_hour : 0, ok ? t.tm_min : 0, ok ? t.tm_wday : 0);
  bool first = true;
  for (int i = 0; i < NT; i++)
    if (timers[i].on) {
      const long left = max(0L, (long)((int32_t)(timers[i].endMs - now)) / 1000);
      o += snprintf(out + o, 1600 - o, "%s{\"id\":%d,\"label\":\"%s\",\"left\":%ld,\"total\":%lu}", first ? "" : ",", i, timers[i].label, left,
                    (unsigned long)timers[i].totalS);
      first = false;
    }
  o += snprintf(out + o, 1600 - o, "],\"alarms\":[");
  for (int i = 0; i < nAlarms; i++)
    o += snprintf(out + o, 1600 - o, "%s{\"id\":%d,\"time\":\"%02d:%02d\",\"days\":%d,\"on\":%d,\"label\":\"%s\"}", i ? "," : "", i, alarms[i].h,
                  alarms[i].m, alarms[i].days, alarms[i].on, alarms[i].label);
  o += snprintf(out + o, 1600 - o, "],\"next\":%ld,\"nextId\":%d,\"ring\":\"%s\",\"ringLabel\":\"%s\",\"snooze\":%ld,\"sleep\":%ld,\"sleepDeep\":%d,",
                nxs, nx, ring == R_ALARM ? "alarm" : ring == R_TIMER ? "timer" : "", ring ? ringLabel : "",
                snoozeUntil ? (long)((int32_t)(snoozeUntil - now)) / 1000 : 0L, sleepEndMs ? (long)((int32_t)(sleepEndMs - now)) / 1000 : 0L,
                sleepDeep ? 1 : 0);
  char bed[8] = "";
  if (bedH >= 0) snprintf(bed, sizeof(bed), "%02d:%02d", bedH, bedM);
  o += snprintf(out + o, 1600 - o,
                "\"focus\":{\"on\":%d,\"work\":%d,\"left\":%ld,\"round\":%d,\"rounds\":%d,\"workMin\":%d,\"breakMin\":%d},"
                "\"stats\":{\"today\":%u,\"streak\":%u,\"total\":%lu},\"nudge\":%d,\"bed\":\"%s\"}",
                focusOn ? 1 : 0, focusWork ? 1 : 0, focusOn ? max(0L, (long)((int32_t)(fPhaseEnd - now)) / 1000) : 0L, fRound, fRounds, fWorkMin,
                fBreakMin, minToday(), fst.lastDay + 1 >= dayKey() ? fst.streak : 0, (unsigned long)fst.totalMin, nudgeMin, bed);
  return httpd_resp_send(req, out, o);
}

}  // namespace clk
