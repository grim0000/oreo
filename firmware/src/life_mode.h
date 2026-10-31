// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
// Life mode: Oreo lives on its own. Needs (social, curiosity, boredom, energy) pick what it does
// next: look around, look for a person, walk over and greet them by name, keep them company (the head
// follows their face), play, nap, ask for a charge. Head pats, being picked up and loud sounds get reactions.
// A separate mode: it only runs while Life is selected; leaving it stops everything it started.
// SAFETY: it walks only after "On the floor" is ticked on the page (reset every time Life starts) and only
// with the ToF working - otherwise head, face and on-the-spot emotes only (like desk mode).
// Included by main.cpp after its globals; lifeTick() runs in loop().
#pragma once

namespace lifem {

enum Act { A_IDLE, A_LOOK, A_WANDER, A_APPROACH, A_GREET, A_ENGAGE, A_PLAY, A_NAP, A_TIRED, A_HELD, A_N };
const char* const ACT_NAMES[A_N] = {"idle", "looking for you", "wandering", "coming to you", "saying hi",
                                    "keeping you company", "playing", "napping", "tired", "picked up"};

bool onFloor = false;           // the page's "On the floor" switch: walking allowed (reset at every start)
Act doing = A_IDLE;
int phase = 0;
unsigned long actStart = 0, actUntil = 0, phaseMs = 0, tickMs = 0, needMs = 0, lastGreetMs = 0, lastAskMs = 0;
unsigned long lastPersonMs = 0, lastTouchMs = 0, touchDownMs = 0, lastJoltMs = 0, walkUntil = 0;
float social = 0.5f, curiosity = 0.4f, boredom = 0.3f;
bool touchWas = false, wasFallen = false, petting = false, paused = false;
char userName[24] = "";
uint32_t seed = 1;
float micAvg = -1;

inline float rnd() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0f; }
inline float rr(float a, float b) { return a + (b - a) * rnd(); }
inline void msg(const char* t, const char* l1, unsigned long ms = 3500) {
  strlcpy(lifeMsgTitle, t, sizeof(lifeMsgTitle));
  strlcpy(lifeMsgL1, l1, sizeof(lifeMsgL1));
  lifeMsgUntil = millis() + ms;
}
inline void face(const char* e, bool hold = false) { setFaceWithMode(e, hold ? FACE_ANIM_LOOP : FACE_ANIM_ONCE); }
inline void head(float panOff, float tiltOff) {
  headSlewTo(constrain(PAN_MID + (panPlusIsLeft ? panOff : -panOff), PAN_MIN + 3, PAN_MAX - 3),
             constrain(TILT_MID + tiltOff, TILT_MIN + 3, TILT_MAX - 6));
}
inline bool isGait(const String& c) { return c.startsWith("smooth_") || c.startsWith("step_") || c.startsWith("sharp_"); }
inline void stopGait() { if (isGait(currentCommand)) currentCommand = ""; }
inline bool personSeen(unsigned long now, unsigned long within = 1600) { return camReady && now - faceSeenGlobalMs < within; }
// walking: ticked "On the floor", ToF running, upright, battery ok
inline bool canWalk() { return onFloor && tofOk && tofMm > 0 && !robotFallen && !batEmpty; }
inline int batPct() { return batPctShown >= 0 ? batPctShown : 80; }
inline void emote(const char* e) { currentCommand = e; }

void loadName() {
  userName[0] = 0;
  if (sdSizeMB < 0) return;
  File f = SD_MMC.open("/sesame/profile.txt", FILE_READ);
  if (!f) return;
  char line[140];
  int bestRank = 9;
  while (f.available()) {
    const int n = f.readBytesUntil('\n', line, sizeof(line) - 1);
    line[n] = 0;
    char* eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    const char* k = line;
    char* v = eq + 1;
    while (*v == ' ') v++;
    for (char* c = v; *c; c++) if (*c == '\r') *c = 0;
    // "name" first, then "user_name" / "person_name" / "my_name" (never a pet's or the robot's)
    int rank = !strcmp(k, "name") ? 0 : (!strcmp(k, "user_name") || !strcmp(k, "person_name") || !strcmp(k, "my_name")) ? 1
             : (strstr(k, "name") && !strstr(k, "pet") && !strstr(k, "robot") && !strstr(k, "dog") && !strstr(k, "cat")) ? 2 : 9;
    if (rank < bestRank && v[0]) {
      bestRank = rank;
      strlcpy(userName, v, sizeof(userName));
      char* sp = strchr(userName, ' ');  // the first name is enough
      if (sp) *sp = 0;
    }
  }
  f.close();
}

void start(Act a, unsigned long now) {
  if (doing == A_WANDER || doing == A_APPROACH || a == A_NAP || a == A_HELD) stopGait();
  if (doing == A_NAP && a != A_NAP && !legsPowered) { currentCommand = "stand"; }
  doing = a;
  phase = 0;
  actStart = phaseMs = now;
  switch (a) {
    case A_IDLE:     actUntil = now + (unsigned long)rr(4000, 9000); face("idle"); break;
    case A_LOOK:     actUntil = now + 9000; face("curious"); break;
    case A_WANDER:   actUntil = now + (unsigned long)rr(15000, 25000); face("happy"); head(0, 0); break;
    case A_APPROACH: actUntil = now + 14000; face("excited"); break;
    case A_GREET:    actUntil = now + 4000; break;
    case A_ENGAGE:   actUntil = now + (unsigned long)rr(20000, 40000); break;
    case A_PLAY:     actUntil = now + 6000; break;
    case A_NAP:      actUntil = now + (unsigned long)rr(120000, 300000); break;
    case A_TIRED:    actUntil = now + 8000; break;
    case A_HELD:     actUntil = now + 600000; break;
    default: break;
  }
  xlog("LIFE do %s | social %.2f curiosity %.2f boredom %.2f bat %d%% floor=%d person=%d", ACT_NAMES[a], social, curiosity,
       boredom, batPct(), onFloor ? 1 : 0, personSeen(now) ? 1 : 0);
}

// what next: the needs, weighted
Act choose(unsigned long now) {
  if (batPct() < 20 && !batCharging && now - lastAskMs > 300000) return A_TIRED;
  if (personSeen(now)) {
    if (now - lastGreetMs > 600000) return canWalk() ? A_APPROACH : A_GREET;
    return rnd() < 0.65f ? A_ENGAGE : A_PLAY;
  }
  struct tm tmv;
  const bool night = getLocalTime(&tmv, 0) && (tmv.tm_hour >= 23 || tmv.tm_hour < 6);
  const float energyLow = batPct() < 35 ? 0.6f : 0.0f;
  float w[A_N] = {0};
  w[A_IDLE] = 0.35f;
  w[A_LOOK] = social * 1.3f;
  w[A_WANDER] = canWalk() ? curiosity * 1.2f : 0;
  w[A_PLAY] = boredom * 1.1f;
  w[A_NAP] = (night ? 0.9f : 0.08f) + energyLow;
  float sum = 0;
  for (float x : w) sum += x;
  float r = rnd() * sum;
  for (int i = 0; i < A_N; i++) { r -= w[i]; if (r <= 0) return (Act)i; }
  return A_IDLE;
}

void lifeEnter() {
  seed = esp_random() | 1;
  onFloor = false;  // asked again every time
  social = 0.5f; curiosity = 0.4f; boredom = 0.3f;
  loadName();
  paused = false;
  wasFallen = robotFallen;
  lastPersonMs = lastTouchMs = 0;
  micAvg = -1;
  faceAmbient = true;
  if (!robotFallen) currentCommand = "stand";
  start(A_IDLE, millis());
  msg("Life", userName[0] ? userName : "on my own");
  xlog("LIFE on (name: %s)", userName[0] ? userName : "-");
}
void lifeLeave() {
  stopGait();
  if (currentCommand.length() && !isGait(currentCommand)) currentCommand = "";
  headSlewTo(PAN_MID, TILT_MID);
  face("idle");
  faceAmbient = false;
  lifeMsgUntil = 0;
  xlog("LIFE off");
}

// a touch on the head: a tap, or a hand held there (petting)
void touchStep(unsigned long now) {
  const bool t = TOUCH_PIN >= 0 && digitalRead(TOUCH_PIN) == HIGH;
  if (t && !touchWas) touchDownMs = now;
  if (t && touchDownMs && !petting && now - touchDownMs > 1200) {  // held: petting
    petting = true;
    if (doing == A_NAP) start(A_IDLE, now);
    stopGait();
    emote("petting");
    face("love");
    social = max(0.0f, social - 0.4f);
    boredom = max(0.0f, boredom - 0.2f);
    lastTouchMs = now;
    xlog("LIFE petting");
  }
  if (!t && touchWas) {
    if (!petting && now - touchDownMs > 40) {  // a tap
      if (doing == A_NAP) { start(A_IDLE, now); emote("pat_wake"); }
      else {
        static int k = 0;
        const char* const R[] = {"pat_boop", "cute", "pat_nuzzle", "wiggle", "shy"};
        stopGait();
        emote(R[k++ % 5]);
      }
      social = max(0.0f, social - 0.25f);
      boredom = max(0.0f, boredom - 0.15f);
      lastTouchMs = now;
      xlog("LIFE pat");
    }
    petting = false;
    touchDownMs = 0;
  }
  touchWas = t;
}

// picked up / tipped over (fallUpdate: legs off, "Help!"), lifted (a big jolt while standing still), loud bangs
void bodyStep(unsigned long now) {
  if (robotFallen && !wasFallen) { start(A_HELD, now); face("dizzy", true); msg("Wheee!", "put me down"); }
  if (!robotFallen && wasFallen) {
    msg("Phew", "back down");
    start(A_IDLE, now);
    currentCommand = "stand";
    lastJoltMs = now;
  }
  wasFallen = robotFallen;
  if (robotFallen) return;
  const bool still = currentCommand.length() == 0 && now - exHeadMovedMs > 1500;
  if (imuOk && still && imuJolt > 0.9f && now - lastJoltMs > 5000) {
    lastJoltMs = now;
    if (doing == A_NAP) start(A_IDLE, now);
    emote("startle");
    msg("Whoa!", "what was that");
    xlog("LIFE jolt %.2f g", imuJolt);
  }
  // a loud sound (the mic level well over its running average; not its own sounds)
  static unsigned long spkMs = 0;
  if (aud::busy || aud::playing || aud::talkOn) spkMs = now;
  const float lvl = ww::micLvl;
  if (micAvg < 0) micAvg = lvl;
  micAvg += (lvl - micAvg) * 0.02f;
  if (now - spkMs > 1500 && lvl > 4 * micAvg && lvl > 3500 && now - lastJoltMs > 8000 && doing != A_HELD) {
    lastJoltMs = now;
    if (doing == A_NAP) start(A_IDLE, now);
    stopGait();
    emote("huh");
    start(A_LOOK, now);
    xlog("LIFE loud sound %d (avg %.0f)", (int)lvl, micAvg);
  }
}

// the current activity, every pass
void actStep(unsigned long now) {
  const bool seen = personSeen(now);
  if (seen) lastPersonMs = now;
  // an emote / pose is playing (a pat's reaction): the walking ones wait for it
  if ((doing == A_WANDER || doing == A_APPROACH) && currentCommand.length() && !isGait(currentCommand)) return;
  switch (doing) {
    case A_IDLE:  // small glances
      if (now - phaseMs > (unsigned long)rr(2500, 5000)) { phaseMs = now; head(rr(-28, 28), rr(-6, 8)); }
      if (seen && now - lastGreetMs > 600000) actUntil = now;  // someone's here: decide again
      break;
    case A_LOOK: {  // a slow sweep, a little upward (faces)
      const float u = (now - actStart) / 9000.0f;
      if (now - phaseMs > 120) { phaseMs = now; head(-55 + 110 * (0.5f - 0.5f * cosf(u * 2 * PI)), 10); }
      if (seen) { msg("Oh!", "there you are"); actUntil = now; }
      break;
    }
    case A_WANDER:
      if (!canWalk()) { actUntil = now; break; }
      curiosity = max(0.0f, curiosity - 0.0004f * 20);
      if (phase == 0) {  // forward until something's close
        head(0, 0);
        if (tofMm < 300 || now > walkUntil) {
          stopGait();
          phase = 1; phaseMs = now;
          walkUntil = now + (unsigned long)rr(1200, 2600);
          emote(rnd() < 0.5f ? "smooth_sharp_left" : "smooth_sharp_right");
        } else if (!isGait(currentCommand)) { emote("smooth_walk"); walkUntil = now + (unsigned long)rr(2500, 5000); }
      } else if (now > walkUntil) {  // turned: look, then go again
        stopGait();
        phase = 0;
        walkUntil = now + (unsigned long)rr(2500, 5000);
        if (tofMm > 450) emote("smooth_walk");
        else { phase = 1; walkUntil = now + 1500; emote("smooth_sharp_left"); }
      }
      if (seen && now - lastGreetMs > 600000) { stopGait(); start(A_APPROACH, now); }
      break;
    case A_APPROACH: {  // turn toward the face, walk until close
      if (!canWalk()) { stopGait(); start(A_GREET, now); break; }
      if (headTargetDirty && seen) { headTargetDirty = false; headSlewTo(headPanTarget, headTiltTarget); }
      if (now - lastPersonMs > 2500) { stopGait(); start(A_LOOK, now); break; }
      const float off = (float)headPanTarget - PAN_MID;
      const bool toLeft = panPlusIsLeft ? off > 0 : off < 0;
      const bool close = (tofMm > 0 && tofMm < 380) || lifeFaceW > 95;
      if (close) { stopGait(); start(A_GREET, now); break; }
      const char* want = fabsf(off) > 20 ? (toLeft ? "smooth_sharp_left" : "smooth_sharp_right") : "smooth_walk";
      if (currentCommand != want) emote(want);
      break;
    }
    case A_GREET:
      if (phase == 0) {
        stopGait();
        const bool longAgo = now - lastGreetMs > 600000;
        char l1[24];
        const char* who = fid::recent(8000);
        snprintf(l1, sizeof(l1), "%s%s", who[0] ? who : userName[0] ? userName : "friend", longAgo ? "!" : " :)");
        msg("Hi", l1, 4000);
        face("happy");
        emote(longAgo ? "wave" : "cute");
        if (playSound && !aud::talkOn) playSfx("w:v_yay|v0.3 523:90 659:90 784:200");
        lastGreetMs = now;
        social = max(0.0f, social - 0.5f);
        phase = 1;
      } else if (currentCommand.length() == 0) start(A_ENGAGE, now);
      break;
    case A_ENGAGE:  // keep them company: the head follows their face, a little something now and then
      if (headTargetDirty && seen) { headTargetDirty = false; headSlewTo(headPanTarget, headTiltTarget); }
      social = max(0.0f, social - 0.001f);
      if (now - phaseMs > (unsigned long)rr(7000, 12000) && currentCommand.length() == 0) {
        phaseMs = now;
        const char* const E[] = {"nodyes", "curious", "cute", "wiggle", "happy", "sniff"};
        emote(E[(int)rr(0, 5.99f)]);
      }
      // they stepped back: follow (the floor allows it)
      if (canWalk() && seen && tofMm > 700 && lifeFaceW > 0 && lifeFaceW < 45) start(A_APPROACH, now);
      if (now - lastPersonMs > 8000) { msg("Bye?", ""); start(A_IDLE, now); }
      break;
    case A_PLAY:
      if (phase == 0) {
        const char* const P[] = {"dance", "jam", "wiggle", "bounce", "playbow", "twirl", "sneaky", "proud", "laugh"};
        emote(P[(int)rr(0, 8.99f)]);
        boredom = max(0.0f, boredom - 0.6f);
        face("excited");
        phase = 1;
      } else if (currentCommand.length() == 0) actUntil = now;
      break;
    case A_NAP:
      if (phase == 0) { currentCommand = "rest"; face("sleeping", true); msg("Zzz", "nap time", 3000); phase = 1; }
      if (seen && now - actStart > 20000) { face("happy"); start(A_IDLE, now); }  // someone came: up
      break;
    case A_TIRED:
      if (phase == 0) {
        stopGait();
        lastAskMs = now;
        face("sleepy");
        emote("sad");
        char l1[24];
        snprintf(l1, sizeof(l1), "battery %d%%", batPct());
        msg("Charge me?", l1, 6000);
        phase = 1;
      } else if (currentCommand.length() == 0 && now - actStart > 5000) start(A_NAP, now);
      break;
    case A_HELD:
      break;  // bodyStep ends it once it's upright again
    default: break;
  }
}

void lifeTick(unsigned long now) {
  tofNeedMs = now;  // the ToF keeps running (obstacles, the person's distance)
  // a chat, RC or an update has the robot: nothing new, stop walking
  if (aud::talkOn || rcActive() || otaActive) {
    if (!paused) { paused = true; stopGait(); xlog("LIFE paused (chat / RC / update)"); }
    return;
  }
  if (paused) { paused = false; lastPersonMs = now; start(A_IDLE, now); }
  // the needs drift (every 0.5 s)
  if (now - needMs >= 500) {
    const float dt = needMs ? (now - needMs) / 1000.0f : 0.5f;
    needMs = now;
    const bool here = personSeen(now, 3000);
    social = constrain(social + (here ? -dt / 25 : dt / 240), 0.0f, 1.0f);
    curiosity = constrain(curiosity + dt / 150, 0.0f, 1.0f);
    boredom = constrain(boredom + (here ? dt / 400 : dt / 180), 0.0f, 1.0f);
  }
  touchStep(now);
  bodyStep(now);
  // walking only with the floor ticked; never without the ToF
  if (isGait(currentCommand) && !canWalk()) { stopGait(); xlog("LIFE walk stopped: %s", !onFloor ? "not on the floor" : "no ToF / fallen"); }
  if (doing == A_HELD) return;
  actStep(now);
  if (currentCommand.length() && !isGait(currentCommand)) return;  // an emote / pose is running
  if ((long)(now - actUntil) >= 0) {
    Act next = choose(now);
    if (next == doing && doing != A_IDLE && doing != A_ENGAGE) next = A_IDLE;  // a little pause between the same thing
    start(next, now);
  }
}

// ---- GET /life: state; ?floor=0|1  ?go=<idle|look|wander|play|nap|greet> ----
esp_err_t life_handler(httpd_req_t* req) {
  char q[96] = "", v[24];
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
    if (httpd_query_key_value(q, "floor", v, sizeof(v)) == ESP_OK) {
      onFloor = atoi(v) == 1;
      xlog("LIFE on the floor: %s", onFloor ? "yes (may walk)" : "no");
    }
    if (httpd_query_key_value(q, "go", v, sizeof(v)) == ESP_OK && activeMode == MODE_LIFE) {
      const char* const K[] = {"idle", "look", "wander", "approach", "greet", "engage", "play", "nap"};
      for (int i = 0; i < 8; i++) if (!strcmp(v, K[i])) { actUntil = millis(); doing = (Act)i; start((Act)i, millis()); }
    }
  }
  char out[400];
  const unsigned long now = millis();
  snprintf(out, sizeof(out),
           "{\"on\":%d,\"doing\":\"%s\",\"floor\":%d,\"canWalk\":%d,\"person\":%d,\"name\":\"%s\",\"social\":%.2f,"
           "\"curiosity\":%.2f,\"boredom\":%.2f,\"bat\":%d,\"tof\":%d,\"faceW\":%d}",
           activeMode == MODE_LIFE ? 1 : 0, ACT_NAMES[doing], onFloor ? 1 : 0, canWalk() ? 1 : 0, personSeen(now) ? 1 : 0,
           userName, social, curiosity, boredom, batPct(), (int)tofMm, (int)lifeFaceW);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_sendstr(req, out);
}

}  // namespace lifem
