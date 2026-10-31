// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
#pragma once
// The chest OLED (128 x 64, blue): a status bar on top - what it's doing, WiFi, battery - and one
// card for the most important thing right now, each with a small animated icon. Drawn 5 times a
// second (once a second while driving or talking: the governor), inside the calibrated visible
// area (OLED screen panel).
// Included in main.cpp just before loop(): it uses main.cpp's state directly.
#include "oled_icons.h"

enum OledKind : uint8_t { OC_IDLE, OC_WALK, OC_RC, OC_EXPLORE, OC_HELP, OC_FACE, OC_TALK, OC_SCENE, OC_SLEEP,
                          OC_CHARGE, OC_UPDATE, OC_LOWBAT, OC_FALL, OC_THANKS, OC_EMOTE };
uint32_t oledFrame = 0;
OledKind oledKindNow = OC_IDLE;
const char* const OLED_KIND_NAMES[] = {"idle", "walk", "rc", "explore", "help", "face", "talk", "scene", "sleep",
                                       "charge", "update", "lowbat", "fall", "thanks", "emote"};

static void oledText(int x, int y, const char* s, int size = 1) {
  oled.setTextSize(size);
  oled.setCursor(x, y);
  oled.print(s);
}

// ---- icons (animated by the frame counter) ----
static void icHeart(int x, int y, uint32_t t) {
  oled.drawBitmap(x, y + 2, t % 4 < 2 ? ICO_HEART0 : ICO_HEART1, 22, 15, SSD1306_WHITE);
  const int pts[6][2] = {{0, 0}, {4, 0}, {6, -4}, {8, 4}, {10, 0}, {22, 0}};  // a heartbeat line
  for (int i = 0; i < 5; i++) oled.drawLine(x + pts[i][0], y + 22 + pts[i][1], x + pts[i + 1][0], y + 22 + pts[i + 1][1], SSD1306_WHITE);
}
static void icLegs(int x, int y, uint32_t t) {
  oled.fillRect(x + 2, y + 4, 20, 8, SSD1306_WHITE);
  oled.drawPixel(x + 18, y + 6, SSD1306_BLACK);
  oled.drawPixel(x + 19, y + 6, SSD1306_BLACK);
  const int sw[4] = {-2, 0, 2, 0};
  for (int i = 0; i < 4; i++) {
    const int lx = x + 4 + i * 5, s = sw[(t + i * 2) % 4];
    oled.drawLine(lx, y + 12, lx + s, y + 19, SSD1306_WHITE);
    oled.drawLine(lx + s, y + 19, lx + s + 2, y + 19, SSD1306_WHITE);
  }
  for (int i = 0; i < 3; i++) {
    const int gx = x + ((i * 9 - (int)t * 2) % 24 + 24) % 24;
    oled.drawLine(gx, y + 22, gx + 3, y + 22, SSD1306_WHITE);
  }
}
static void icRadar(int x, int y, uint32_t t, int pickDeg) {
  const int cx = x + 12, cy = y + 24, r = 12;
  for (int a = -75; a <= 75; a += 5) {
    const float ang = (-90 - a) * DEG_TO_RAD;
    oled.drawPixel(cx + cosf(ang) * r, cy + sinf(ang) * r, SSD1306_WHITE);
  }
  const float sw = (-90 - (-75 + (int)(t * 18) % 150)) * DEG_TO_RAD;  // the sweeping beam
  oled.drawLine(cx, cy, cx + cosf(sw) * r, cy + sinf(sw) * r, SSD1306_WHITE);
  const float pa = (-90 - constrain(pickDeg, -80, 80)) * DEG_TO_RAD;  // where it's going: a dotted line
  for (int k = 0; k < r; k += 3) oled.drawPixel(cx + cosf(pa) * k, cy + sinf(pa) * k, SSD1306_WHITE);
  oled.fillRect(cx - 2, cy - 2, 5, 5, SSD1306_WHITE);
}
static void icReticle(int x, int y, uint32_t t, bool seen) {
  const int k = t % 4 < 2 ? 2 : 0;
  for (int a = 0; a < 2; a++)
    for (int b = 0; b < 2; b++) {
      const int px = x + a * 22, py = y + b * 22, sx = a ? -1 : 1, sy = b ? -1 : 1;
      oled.drawLine(px + sx * k, py + sy * k, px + sx * (k + 6), py + sy * k, SSD1306_WHITE);
      oled.drawLine(px + sx * k, py + sy * k, px + sx * k, py + sy * (k + 6), SSD1306_WHITE);
    }
  if (seen) {  // a little face in the frame
    const int fx = x + 11 + (int)(t % 6) - 3, fy = y + 11;
    oled.drawCircle(fx, fy, 5, SSD1306_WHITE);
    oled.drawPixel(fx - 2, fy - 1, SSD1306_WHITE);
    oled.drawPixel(fx + 2, fy - 1, SSD1306_WHITE);
    oled.drawLine(fx - 1, fy + 2, fx + 1, fy + 2, SSD1306_WHITE);
  } else {
    oledText(x + 9, y + 8, "?");
  }
}
static void icWave(int x, int y, uint32_t t, float level) {
  oled.drawBitmap(x, y + 6, ICO_TALK, 8, 8, SSD1306_WHITE);
  for (int i = 0; i < 5; i++) {
    const int h = 3 + (int)(8 * fabsf(sinf(t * 0.9f + i * 1.3f)) * (0.4f + 0.6f * constrain(level * 4, 0.0f, 1.0f)));
    oled.fillRect(x + 11 + i * 3, y + 12 - h / 2, 2, h, SSD1306_WHITE);
  }
}
static void icNote(int x, int y, uint32_t t) {
  const int dy[4] = {0, -2, -3, -2};
  oled.drawBitmap(x + 2, y + 2 + dy[t % 4], ICO_NOTE, 14, 11, SSD1306_WHITE);
  if (t % 4 >= 2) { oled.drawPixel(x + 20, y + 3, SSD1306_WHITE); oled.drawPixel(x + 22, y + 1, SSD1306_WHITE); }
}
static void icSparkle(int x, int y, uint32_t t) {
  const int cx = x + 11, cy = y + 12, s = 5 + (t % 4 < 2 ? 3 : 0);
  oled.drawLine(cx - s, cy, cx + s, cy, SSD1306_WHITE);
  oled.drawLine(cx, cy - s, cx, cy + s, SSD1306_WHITE);
  oled.drawLine(cx - s / 2, cy - s / 2, cx + s / 2, cy + s / 2, SSD1306_WHITE);
  oled.drawLine(cx - s / 2, cy + s / 2, cx + s / 2, cy - s / 2, SSD1306_WHITE);
  oled.drawPixel(x + 2 + (t * 5) % 20, y + 2, SSD1306_WHITE);
}
static void icMoon(int x, int y, uint32_t t) {
  oled.fillCircle(x + 9, y + 12, 8, SSD1306_WHITE);
  oled.fillCircle(x + 14, y + 9, 8, SSD1306_BLACK);
  if (t % 4 < 3) oledText(x + 16, y + 8 - (t % 4), "z");
  if ((t + 1) % 4 < 3) oledText(x + 19, y + 2 - (t % 4), "z");
}
static void icBattery(int x, int y, uint32_t t, bool charging, int pct) {
  oled.drawRect(x, y + 4, 20, 14, SSD1306_WHITE);
  oled.fillRect(x + 20, y + 8, 3, 6, SSD1306_WHITE);
  const int fill = charging ? (int)(t % 5) * 4 : constrain(pct, 0, 100) * 16 / 100;
  if (fill) oled.fillRect(x + 2, y + 6, fill, 10, SSD1306_WHITE);
  if (charging) {
    const uint16_t c = fill > 6 ? SSD1306_BLACK : SSD1306_WHITE;
    oled.drawLine(x + 11, y + 6, x + 8, y + 11, c);
    oled.drawLine(x + 8, y + 11, x + 12, y + 11, c);
    oled.drawLine(x + 12, y + 11, x + 9, y + 16, c);
  } else if (t % 2 == 0) {
    oledText(x + 8, y + 7, "!");
  }
}
static void icDownload(int x, int y, uint32_t t, int pct) {
  const int dy = t % 4;
  oled.fillRect(x + 8, y + dy, 6, 5, SSD1306_WHITE);
  oled.fillTriangle(x + 4, y + 5 + dy, x + 17, y + 5 + dy, x + 11, y + 11 + dy, SSD1306_WHITE);
  oled.drawRect(x, y + 17, 22, 6, SSD1306_WHITE);
  oled.fillRect(x + 1, y + 18, constrain(pct, 0, 100) * 20 / 100, 4, SSD1306_WHITE);
}
static void icFallen(int x, int y, uint32_t t) {  // the robot on its back, legs waving
  oled.fillRoundRect(x + 2, y + 12, 20, 8, 2, SSD1306_WHITE);
  oled.drawPixel(x + 18, y + 17, SSD1306_BLACK);
  for (int i = 0; i < 4; i++) {
    const int lx = x + 4 + i * 5, w = ((t + i) % 2) ? 2 : -2;
    oled.drawLine(lx, y + 12, lx + w, y + 5, SSD1306_WHITE);
  }
  if (t % 2) oledText(x + 20, y, "!");
}

// ---- the picture ----
struct OledCardText { char title[16], l1[20], l2[20]; };

static void strCap(char* out, size_t n, const char* in) {
  strlcpy(out, in, n);
  if (out[0] >= 'a' && out[0] <= 'z') out[0] -= 32;
  for (char* c = out; *c; c++) if (*c == '_') *c = ' ';
}

void oledDraw(int pctShown, float volts) {
  const int x0 = constrain(OLED_ML, 0, 40), y0 = constrain(OLED_MT, 0, 30);
  const int x1 = 127 - constrain(OLED_MR, 0, 40), y1 = 63 - constrain(OLED_MB, 0, 30);
  const uint32_t t = oledFrame++;
  const unsigned long now = millis();
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  if (now < oledTestUntil) {  // calibration: a box around the visible area, corner marks, the numbers
    oled.drawRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, SSD1306_WHITE);
    oled.fillRect(x0, y0, 4, 4, SSD1306_WHITE);
    oled.fillRect(x1 - 3, y0, 4, 4, SSD1306_WHITE);
    oled.fillRect(x0, y1 - 3, 4, 4, SSD1306_WHITE);
    oled.fillRect(x1 - 3, y1 - 3, 4, 4, SSD1306_WHITE);
    char b[24];
    snprintf(b, sizeof(b), "L%d T%d", OLED_ML, OLED_MT);
    oledText(x0 + 7, y0 + 7, b);
    snprintf(b, sizeof(b), "R%d B%d", OLED_MR, OLED_MB);
    oledText(x0 + 7, y0 + 18, b);
    snprintf(b, sizeof(b), "%dx%d", x1 - x0 + 1, y1 - y0 + 1);
    oledText(x0 + 7, y0 + 29, b);
    oled.display();
    i2cSpeedRestore();  // (the OLED library drops the bus to 100 kHz after a push)
    return;
  }
  // --- what matters most right now ---
  OledCardText c = {"", "", ""};
  OledKind k = OC_IDLE;
  const String& cmd = currentCommand;
  const bool walking = cmd.startsWith("smooth_") || cmd == "forward" || cmd == "backward" || cmd == "left" || cmd == "right";
  if (otaActive) {
    k = OC_UPDATE;
    strlcpy(c.title, "Update", sizeof(c.title));
    snprintf(c.l1, sizeof(c.l1), "firmware %d%%", max(0, (int)otaPct));
  } else if (robotFallen) {
    k = OC_FALL;
    strlcpy(c.title, "Help!", sizeof(c.title));
    strlcpy(c.l1, "I fell over", sizeof(c.l1));
    strlcpy(c.l2, "stand me up", sizeof(c.l2));
  } else if (now < thanksUntilMs) {
    k = OC_THANKS;
    strlcpy(c.title, "Thanks", sizeof(c.title));
    strlcpy(c.l1, "back on my", sizeof(c.l1));
    strlcpy(c.l2, "feet again", sizeof(c.l2));
  } else if ((activeMode == MODE_LIFE || activeMode == MODE_DESK || clk::cardAnyMode) && now < lifeMsgUntil) {  // life mode's notes ("Hi!", "Charge me?"), the clock's (any mode)
    k = OC_THANKS;
    strlcpy(c.title, lifeMsgTitle, sizeof(c.title));
    strlcpy(c.l1, lifeMsgL1, sizeof(c.l1));
    c.l2[0] = 0;
  } else if (millis() < gem::voiceShowUntil && gem::state == gem::S_OFF) {  // a voice sample playing (Change voice)
    k = OC_TALK;
    strlcpy(c.title, "Voice", sizeof(c.title));
    strlcpy(c.l1, gem::voiceShowName, sizeof(c.l1));
    strlcpy(c.l2, gem::voiceInfo(gem::voiceShowName), sizeof(c.l2));
  } else if (gem::state != gem::S_OFF || millis() < gem::wakeAckUntil) {  // a conversation (gemini.h)
    k = OC_TALK;
    strlcpy(c.title, "Chat", sizeof(c.title));
    static const char* const L1[] = {"heard you!", "connecting...", "listening", "thinking...", "talking"};
    strlcpy(c.l1, L1[gem::state], sizeof(c.l1));
    if (gem::state == gem::S_LISTENING) {
      // live: is it hearing something now (above the room's own level)? and how loud - a 12-step bar
      const float lv = gem::inLvl, fl = gem::noise * 3 + 0.004f;
      if (lv > fl) strlcpy(c.l1, "hearing you", sizeof(c.l1));
      const int n = constrain((int)lroundf(12 * sqrtf(constrain((lv - gem::noise) / 0.15f, 0.0f, 1.0f))), 0, 12);
      for (int i = 0; i < 12; i++) c.l2[i] = i < n ? '#' : '.';
      c.l2[12] = 0;
    } else if (gem::latMs >= 0) snprintf(c.l2, sizeof(c.l2), "reply %.1f s", gem::latMs / 1000.0f);
  } else if (aud::talkOn) {
    k = OC_TALK;
    strlcpy(c.title, "Talk", sizeof(c.title));
    strlcpy(c.l1, "phone mic on", sizeof(c.l1));
  } else if (activeMode == MODE_EXPLORE) {
    const char* st = exploreStateName;
    if (exFails >= EX_MAX_FAILS) {
      k = OC_HELP;
      strlcpy(c.title, "Help!", sizeof(c.title));
      strlcpy(c.l1, "I'm stuck", sizeof(c.l1));
      strlcpy(c.l2, "move me please", sizeof(c.l2));
    } else {
      k = OC_EXPLORE;
      const char* w = !strncmp(st, "forward", 7) || !strncmp(st, "curve", 5) || !strncmp(st, "wander", 6) ? "Walk"
                    : !strncmp(st, "look", 4) ? "Look" : !strncmp(st, "turn L", 6) ? "Left" : !strncmp(st, "turn R", 6) ? "Right"
                    : !strncmp(st, "backup", 6) ? "Back" : !strncmp(st, "stuck", 5) ? "Oops" : !strncmp(st, "check", 5) ? "Floor?"
                    : !strncmp(st, "stop", 4) ? "Stop" : "Explore";
      strlcpy(c.title, w, sizeof(c.title));
      if (tofMm >= TOF_FAR_MM || tofMm < 0) strlcpy(c.l1, "ahead: clear", sizeof(c.l1));
      else snprintf(c.l1, sizeof(c.l1), "ahead %d cm", (int)tofMm / 10);
      if (exFails) snprintf(c.l2, sizeof(c.l2), "problems %d", exFails);
    }
  } else if (activeMode == MODE_FACE) {
    k = OC_FACE;
    const bool seen = now - faceSeenGlobalMs < 1500;
    strlcpy(c.title, seen ? "Hi!" : "Hmm", sizeof(c.title));
    strlcpy(c.l1, seen ? "I see you" : "where are", sizeof(c.l1));
    strlcpy(c.l2, seen ? "following" : "you?", sizeof(c.l2));
  } else if (rcActive()) {
    k = OC_RC;
    strlcpy(c.title, "RC", sizeof(c.title));
    snprintf(c.l1, sizeof(c.l1), "speed %d%%", (int)lroundf(rcSpeed * 100));
    if (streamFps > 0.5f) snprintf(c.l2, sizeof(c.l2), "video %.0f fps", (float)streamFps);
    else strlcpy(c.l2, "camera off", sizeof(c.l2));
  } else if (clk::runningCard(c.title, sizeof(c.title), c.l1, sizeof(c.l1))) {  // a timer counting down (desk_clock.h)
    k = OC_THANKS;
  } else if (mus::card(c.title, sizeof(c.title), c.l1, sizeof(c.l1))) {  // a song / ambient sound (music.h)
    k = OC_TALK;
  } else if (batCharging) {
    k = OC_CHARGE;
    snprintf(c.title, sizeof(c.title), "%.1fV", volts);
    if (batFull) strlcpy(c.l1, "full: unplug", sizeof(c.l1));
    else snprintf(c.l1, sizeof(c.l1), "charging %d%%", pctShown);
  } else if (cmd.length() && !walking && cmd != "rest" && cmd != "stand") {
    k = OC_EMOTE;
    strCap(c.title, sizeof(c.title), cmd.c_str());
    strlcpy(c.l1, "emote", sizeof(c.l1));
  } else if (walking) {
    k = OC_WALK;
    const bool back = cmd == "backward" || cmd == "smooth_walk_back";
    const bool left = cmd == "left" || cmd.endsWith("left"), right = cmd == "right" || cmd.endsWith("right");
    strlcpy(c.title, back ? "Back" : left ? "Left" : right ? "Right" : "Walk", sizeof(c.title));
    snprintf(c.l1, sizeof(c.l1), "speed %d%%", (int)lroundf(cpgSpeed * 100));
  } else if (faceScenePlaying) {
    k = OC_SCENE;
    char nm[16];
    portENTER_CRITICAL(&faceMux);
    strlcpy(nm, faceSceneName, sizeof(nm));
    portEXIT_CRITICAL(&faceMux);
    strCap(c.title, sizeof(c.title), nm);
    strlcpy(c.l1, "playing", sizeof(c.l1));
  } else if (faceEng.dozing()) {
    k = OC_SLEEP;
    strlcpy(c.title, "Zzz", sizeof(c.title));
    strlcpy(c.l1, "pat me to", sizeof(c.l1));
    strlcpy(c.l2, "wake me up", sizeof(c.l2));
  } else if (pctShown < 15 && batState >= 0 && (now / 5000) % 2) {
    k = OC_LOWBAT;
    snprintf(c.title, sizeof(c.title), "%d%%", pctShown);
    strlcpy(c.l1, "charge me", sizeof(c.l1));
    strlcpy(c.l2, "soon", sizeof(c.l2));
  } else {
    k = OC_IDLE;
    const char* f = currentFaceName;
    if (!strncmp(f, "talk_", 5)) f += 5;
    char mood[16];
    strlcpy(mood, f, sizeof(mood));
    char* us = strstr(mood, "_m");
    if (us && (!strcmp(us, "_mild") || !strcmp(us, "_max"))) *us = 0;
    strlcpy(c.title, !strcmp(mood, "idle") ? "chill" : mood, sizeof(c.title));
    struct tm tmv;
    if (getLocalTime(&tmv, 5)) snprintf(c.l1, sizeof(c.l1), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    else strlcpy(c.l1, "hello!", sizeof(c.l1));
    const int alt = (now / 4000) % 2;
    if (alt == 0 && WiFi.status() == WL_CONNECTED) snprintf(c.l2, sizeof(c.l2), "%s", WiFi.localIP().toString().c_str());
    else if (alt == 0) strlcpy(c.l2, "AP 192.168.4.1", sizeof(c.l2));
    else if (playInS >= 0) snprintf(c.l2, sizeof(c.l2), playInS >= 60 ? "play in %ld min" : "play in %ld s", playInS >= 60 ? playInS / 60 : playInS);
    else strlcpy(c.l2, "bored? pat me", sizeof(c.l2));
  }
  oledKindNow = k;
  // --- status bar: what it's doing, WiFi, battery ---
  const uint8_t* mi = k == OC_TALK ? ICO_TALK : k == OC_EXPLORE || k == OC_HELP ? ICO_EXPLORE : k == OC_FACE ? ICO_FACE
                    : k == OC_RC ? ICO_RC : k == OC_SCENE ? ICO_PLAY : k == OC_SLEEP ? ICO_SLEEP : k == OC_CHARGE ? ICO_CHARGE
                    : k == OC_FALL ? ICO_FALL : k == OC_EMOTE ? ICO_EMOTE : ICO_WALK;
  oled.drawBitmap(x0 + 1, y0, mi, 8, 8, SSD1306_WHITE);
  if (WiFi.status() == WL_CONNECTED) {
    const int rssi = WiFi.RSSI(), bars = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
    for (int i = 0; i < 4; i++) {
      const int h = 2 + i * 2;
      if (i < bars) oled.fillRect(x0 + 12 + i * 3, y0 + 8 - h, 2, h, SSD1306_WHITE);
      else oled.drawPixel(x0 + 12 + i * 3, y0 + 7, SSD1306_WHITE);
    }
    char nm[9];
    strlcpy(nm, WiFi.SSID().c_str(), sizeof(nm));
    oledText(x0 + 25, y0, nm);
  } else {
    oled.drawBitmap(x0 + 12, y0, ICO_AP, 8, 8, SSD1306_WHITE);
    oledText(x0 + 23, y0, staSearch ? "looking" : "hotspot");
  }
  char pt[8];
  snprintf(pt, sizeof(pt), "%d%%", pctShown);
  const int bx = x1 - 15;
  oledText(bx - (int)strlen(pt) * 6 - 2, y0, pt);
  oled.drawRect(bx, y0, 13, 7, SSD1306_WHITE);
  oled.fillRect(bx + 13, y0 + 2, 2, 3, SSD1306_WHITE);
  const int bf = batCharging ? (int)(t % 6) * 2 + 1 : (int)lroundf(11.0f * constrain(pctShown, 0, 100) / 100);
  if (bf > 0) oled.fillRect(bx + 1, y0 + 1, min(11, bf), 5, SSD1306_WHITE);
  oled.drawLine(x0, y0 + 10, x1, y0 + 10, SSD1306_WHITE);
  // --- the card: an icon, a title, two lines ---
  const int ix = x0 + 3, iy = y0 + 16;
  switch (k) {
    case OC_IDLE: icHeart(ix, iy, t); break;
    case OC_THANKS: icHeart(ix, iy, t); break;
    case OC_WALK: case OC_RC: icLegs(ix, iy, t); break;
    case OC_EXPLORE: case OC_HELP: icRadar(ix, iy, t, exScanPick); break;
    case OC_FACE: icReticle(ix, iy, t, now - faceSeenGlobalMs < 1500); break;
    case OC_TALK: icWave(ix, iy, t, gem::state == gem::S_LISTENING ? gem::inLvl * 3 : aud::talkLevel); break;
    case OC_SCENE: icNote(ix, iy, t); break;
    case OC_EMOTE: icSparkle(ix, iy, t); break;
    case OC_SLEEP: icMoon(ix, iy, t); break;
    case OC_CHARGE: icBattery(ix, iy, t, true, pctShown); break;
    case OC_LOWBAT: icBattery(ix, iy, t, false, pctShown); break;
    case OC_UPDATE: icDownload(ix, iy, t, otaPct); break;
    case OC_FALL: icFallen(ix, iy, t); break;
  }
  const int tx = x0 + 32, room = (x1 - tx) / 6;  // characters that fit
  const bool bigT = (int)strlen(c.title) * 12 <= x1 - tx;
  c.title[bigT ? 15 : min(15, room)] = 0;
  oledText(tx, y0 + 15, c.title, bigT ? 2 : 1);
  c.l1[min(19, room)] = 0;
  c.l2[min(19, room)] = 0;
  oledText(tx, y0 + (bigT ? 34 : 26), c.l1);
  oledText(tx, y0 + (bigT ? 44 : 36), c.l2);
  oled.display();
    i2cSpeedRestore();  // (the OLED library drops the bus to 100 kHz after a push)
}
