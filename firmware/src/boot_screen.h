// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
// ---- The boot screen (drawn by the face task until setup() is done) ----
// While it boots, a bar of six segments (legs, SD card, motion sensor, distance sensor, WiFi, wake word)
// fills as the parts start - red = failed.
// Only the face engine's Canvas, so the same code can be previewed on a PC.
#pragma once
#include "face_engine_v2.h"

namespace boot {

enum { B_LEGS, B_SD, B_IMU, B_TOF, B_WIFI, B_WAKE, B_N };
struct Info {
  uint8_t ok = 0, fail = 0;  // bits by B_*
  bool finished = false;      // setup() done
};

inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
inline float seg(float t, float a, float b) { return clamp01((t - a) / (b - a)); }
inline float easeOut(float x) { return 1 - (1 - x) * (1 - x) * (1 - x); }
inline float easeInOut(float x) { return x < 0.5f ? 4 * x * x * x : 1 - powf(-2 * x + 2, 3) / 2; }
inline float backOut(float x) {
  const float c1 = 1.4f, c3 = c1 + 1;
  return 1 + c3 * powf(x - 1, 3) + c1 * powf(x - 1, 2);
}

// the robot's colours
inline fe::RGB BG() { return fe::C(0, 0, 0); }          // pitch black
inline fe::RGB IVORY() { return fe::C(238, 238, 232); }  // the printed body
inline fe::RGB ORANGE() { return fe::C(242, 140, 40); }  // (not used on the boot screen)
inline fe::RGB MINTC() { return fe::C(111, 227, 193); }   // the accent: the chest display's mint
inline fe::RGB DIM() { return fe::C(40, 42, 46); }
inline fe::RGB GRID() { return fe::C(30, 32, 36); }
inline fe::RGB RED() { return fe::C(214, 72, 58); }

// a thick polyline drawn up to `p` (0..1) of its length: lines that draw themselves
inline void stroke(fe::Canvas& cv, const float* xy, int n, float p, float r, uint16_t c) {
  if (p <= 0 || n < 2) return;
  float total = 0;
  for (int i = 1; i < n; i++) total += hypotf(xy[2 * i] - xy[2 * i - 2], xy[2 * i + 1] - xy[2 * i - 1]);
  float left = total * clamp01(p);
  for (int i = 1; i < n && left > 0; i++) {
    const float x0 = xy[2 * i - 2], y0 = xy[2 * i - 1], x1 = xy[2 * i], y1 = xy[2 * i + 1];
    const float L = hypotf(x1 - x0, y1 - y0);
    const float k = L > 0 ? fminf(1, left / L) : 1;
    cv.line(x0, y0, x0 + (x1 - x0) * k, y0 + (y1 - y0) * k, r, c);
    left -= L;
  }
}
// a ring (outline circle): a filled circle, then the inside in `inner`
inline void ring(fe::Canvas& cv, float x, float y, float rad, float w, uint16_t c, uint16_t inner) {
  cv.circle(x, y, rad, c);
  if (rad - w > 0.5f) cv.circle(x, y, rad - w, inner);
}

// ---- the wordmark: letters in clean rounded strokes ----
inline void arcPts(float* xy, float cx, float cy, float rad, float a0, float a1, int n) {
  for (int i = 0; i < n; i++) {
    const float a = a0 + (a1 - a0) * i / (n - 1);
    xy[2 * i] = cx + cosf(a) * rad;
    xy[2 * i + 1] = cy + sinf(a) * rad;
  }
}
inline void letter(fe::Canvas& cv, char ch, float x, float y, float p, uint16_t c) {
  const float w = 13, h = 19, r = 1.25f;  // (1.25: the fast line on the robot - 1.5 takes the slow smoothed one)
  if (ch == 'S') {
    float a[2 * 10], b[2 * 10];
    const float rr = h / 4;
    arcPts(a, x + w / 2, y + rr, rr, -0.4f, -3.14159f * 1.5f, 10);
    arcPts(b, x + w / 2, y + 3 * rr, rr, -3.14159f * 0.5f, 3.14159f * 0.6f, 10);
    stroke(cv, a, 10, p * 2, r, c);
    stroke(cv, b, 10, p * 2 - 1, r, c);
  } else if (ch == 'E') {
    const float xy[] = {x + w, y, x, y, x, y + h, x + w, y + h};
    stroke(cv, xy, 4, p * 1.15f, r, c);
    const float mid[] = {x, y + h / 2, x + w * 0.78f, y + h / 2};
    stroke(cv, mid, 2, p * 2 - 1, r, c);
  } else if (ch == 'A') {
    const float xy[] = {x, y + h, x + w / 2, y, x + w, y + h};
    stroke(cv, xy, 3, p * 1.15f, r, c);
    const float bar[] = {x + w * 0.25f, y + h * 0.64f, x + w * 0.75f, y + h * 0.64f};
    stroke(cv, bar, 2, p * 2 - 1, r, c);
  } else if (ch == 'M') {
    const float xy[] = {x, y + h, x, y, x + w / 2, y + h * 0.62f, x + w, y, x + w, y + h};
    stroke(cv, xy, 5, p, r, c);
  } else if (ch == 'O') {  // a rounded oval, drawn round from the top
    float o[2 * 25];
    for (int i = 0; i < 25; i++) {
      const float a = -1.5708f + 6.2832f * i / 24;
      o[2 * i] = x + w / 2 + cosf(a) * w / 2;
      o[2 * i + 1] = y + h / 2 + sinf(a) * h / 2;
    }
    stroke(cv, o, 25, p, r, c);
  } else if (ch == 'R') {  // the stem, the bowl, the leg
    const float stem[] = {x, y + h, x, y};
    stroke(cv, stem, 2, p * 3, r, c);
    float b[2 * 12];
    const float bh = h * 0.27f;  // the bowl: a half circle from the top to the middle
    b[0] = x; b[1] = y;
    for (int i = 0; i < 10; i++) {
      const float a = -1.5708f + 3.14159f * i / 9;
      b[2 + 2 * i] = x + w * 0.55f + cosf(a) * w * 0.45f;
      b[3 + 2 * i] = y + bh + sinf(a) * bh;
    }
    b[22] = x; b[23] = y + 2 * bh;
    stroke(cv, b, 12, p * 3 - 1, r, c);
    const float leg[] = {x + w * 0.45f, y + 2 * bh, x + w, y + h};
    stroke(cv, leg, 2, p * 3 - 2, r, c);
  }
}

// ---- the blueprint: the robot from above ----
const float CX = 160, CY = 92;  // the drawing's centre
// the legs: hips at the body's corners, an upper segment straight out, a lower one angled (front legs
// forward, back legs back). k = which leg: 0 front-left, 1 front-right, 2 back-left, 3 back-right
inline void legGeo(int k, float flex, float& hx, float& hy, float& kx, float& ky, float& fx, float& fy) {
  const float side = (k & 1) ? 1 : -1, fb = k < 2 ? -1 : 1;
  hx = CX + side * 27;
  hy = CY + fb * 30;
  const float a1 = fb * (0.18f + 0.22f * flex);  // the hip swings a little in the stretch
  kx = hx + side * 34 * cosf(a1);
  ky = hy + 34 * sinf(a1);
  const float a2 = fb * (0.95f - 0.35f * flex);  // the knee bends out
  fx = kx + side * 30 * cosf(a2);
  fy = ky + 30 * sinf(a2);
}

// ---- the end: the real face, revealed through the head's octagon ----
// The face task draws the face (whatever style), then this covers everything outside an octagon window
// that grows from the middle to past the screen's edge, its orange outline fading as it goes: the last
// frame of the boot screen is the face itself.
inline void octPts(float* xy, float cx, float cy, float hw, float hh, float c) {
  const float p[] = {cx - hw + c, cy - hh, cx + hw - c, cy - hh, cx + hw, cy - hh + c, cx + hw, cy + hh - c,
                     cx + hw - c, cy + hh, cx - hw + c, cy + hh, cx - hw, cy + hh - c, cx - hw, cy - hh + c,
                     cx - hw + c, cy - hh};
  for (int i = 0; i < 18; i++) xy[i] = p[i];
}
inline void revealMask(fe::Canvas& cv, float k) {
  using namespace fe;
  const float e = easeInOut(clamp01(k));
  const float hw = 30 + (190 - 30) * e, hh = 22 + (150 - 22) * e, c = 10 + 34 * e;  // grows past the edge
  const float cx = 160, cy = 112;
  const uint16_t bg = to565(BG());
  // outside the window's box
  if (cy - hh > 0) cv.rrect(0, 0, 320, cy - hh, 0, bg);
  if (cy + hh < 240) cv.rrect(0, cy + hh, 320, 240 - (cy + hh), 0, bg);
  if (cx - hw > 0) cv.rrect(0, 0, cx - hw, 240, 0, bg);
  if (cx + hw < 320) cv.rrect(cx + hw, 0, 320 - (cx + hw), 240, 0, bg);
  // the cut corners
  cv.tri(cx - hw, cy - hh, cx - hw + c, cy - hh, cx - hw, cy - hh + c, bg);
  cv.tri(cx + hw, cy - hh, cx + hw - c, cy - hh, cx + hw, cy - hh + c, bg);
  cv.tri(cx - hw, cy + hh, cx - hw + c, cy + hh, cx - hw, cy + hh - c, bg);
  cv.tri(cx + hw, cy + hh, cx + hw - c, cy + hh, cx + hw, cy + hh - c, bg);
  // the outline: orange, fading out as the window opens
  float xy[18];
  octPts(xy, cx, cy, hw, hh, c);
  const float a = 1 - e;
  if (a > 0.03f) stroke(cv, xy, 9, 1, 1.4f + 0.8f * a, to565(mixc(BG(), mixc(MINTC(), IVORY(), 0.4f), a)));
}

// ---- a frame. t = seconds since the boot screen started. Returns the reveal's progress (0..1) once it's
// time for the face (the caller draws the face and calls revealMask), -1 before; done = hand over ----
// "Oreo wakes up": Oreo crouched in the dark stands up, its head
// screen and chest display light, OREO draws itself beside it and the bar fills as the parts start. At the
// end Oreo slides to the middle and fades, leaving its head screen - which opens into the live face.
inline float draw(fe::Canvas& cv, float t, const Info& in, float finishedAt, bool& done) {
  using namespace fe;
  const RGB bg = BG();
  const float endT = finishedAt > 0 ? fmaxf(finishedAt, 3.4f) : 1e9f;
  done = t > endT + 1.6f;
  if (t > endT + 0.6f) return seg(t, endT + 0.6f, endT + 1.5f);  // the face, out of the head screen
  // (everything is inside x 64..256 until the end's slide: clearing only that column saves time each frame)
  if (t < 0.15f || t > endT - 0.05f) cv.clear(to565(bg));
  else cv.rrect(64, 0, 192, 240, 0, to565(bg));
  const float slide = easeInOut(seg(t, endT, endT + 0.6f));  // the end: to the middle, all but the screen fades
  const float bp = 1 - slide;

  // the drawing is in the video's coordinates (1000 x 1300, floor at y 1180), scaled onto the left
  // centred, OREO underneath; the end: a little bigger, its screen onto the reveal's start (160 / 112)
  const float S = 0.19f + 0.045f * slide, OX = 160, OY = 182 + 79 * slide;
  auto X = [&](float x) { return OX + (x - 500) * S; };
  auto Y = [&](float y) { return OY + (y - 1180) * S; };
  auto fade = [&](const RGB& c, float k) { return to565(mixc(bg, c, clamp01(k))); };

  const float seen = seg(t, 0.05f, 0.5f);
  const float stand = fmaxf(0.0f, backOut(seg(t, 0.45f, 1.35f)));
  const float on = easeOut(seg(t, 1.25f, 1.65f));
  const float lit = (0.35f + 0.65f * fmaxf(fminf(stand, 1.0f), on)) * seen;  // in the dark, then lit
  auto body = [&](const RGB& c) { return fade(c, lit * bp); };

  const RGB SHELL = C(244, 242, 238), SHELL2 = C(189, 185, 177), EDGE = C(29, 29, 32), EDGE2 = C(139, 135, 127);
  const RGB SERVO = C(20, 20, 22), SERVOE = C(90, 90, 98), MINT = C(111, 227, 193), BLUE = C(159, 179, 255);
  const RGB DARK = C(22, 22, 24), DARK2 = C(31, 31, 35);

  // the pose (as the video: the legs and the neck grow as it stands)
  const float G = 1180, legH = 40 + 95 * stand, neckH = 34 + 96 * stand;
  const float baseB = G - legH, baseT = baseB - 56, shellT = baseT - 180;
  const float bob = t > 2.0f ? 6 * sinf((t - 2.0f) * 3.0f) * bp : 0;
  const float hx = 500 - 192, hy = shellT - neckH - 354 + 30 + bob;  // the head (640 x 590 at 0.6)
  auto HX = [&](float x) { return X(hx + x * 0.6f); };
  auto HY = [&](float y) { return Y(hy + y * 0.6f); };
  const float HS = 0.6f * S;

  // a leg: thigh and shin (outline + shell), the foot, its two servos
  auto leg = [&](float hipX, float hipY, float footX, float footY, float out, bool back) {
    const float kx = hipX + out, ky = hipY - 120;
    const RGB sh = back ? SHELL2 : SHELL, ed = back ? EDGE2 : EDGE;
    cv.lineFast(X(hipX), Y(hipY), X(kx), Y(ky), (back ? 20 : 26) * S, body(ed));
    cv.lineFast(X(hipX), Y(hipY), X(kx), Y(ky), (back ? 15 : 20) * S, body(sh));
    cv.lineFast(X(kx), Y(ky), X(footX), Y(footY), (back ? 17 : 22) * S, body(ed));
    cv.lineFast(X(kx), Y(ky), X(footX), Y(footY), (back ? 12 : 16) * S, body(sh));
    cv.rrect(X(footX - 16), Y(footY - 8), 32 * S, 14 * S, 2, body(sh));
    cv.rrect(X(kx - 26), Y(ky - 34), 52 * S, 68 * S, 8 * S, body(SERVOE));
    cv.rrect(X(kx - 23), Y(ky - 31), 46 * S, 62 * S, 6 * S, body(SERVO));
    cv.rrect(X(hipX - 26), Y(hipY - 26), 52 * S, 52 * S, 8 * S, body(SERVOE));
    cv.rrect(X(hipX - 23), Y(hipY - 23), 46 * S, 46 * S, 6 * S, body(SERVO));
  };
  // the back legs, the body, the front legs (back to front, as in the video)
  leg(340, baseT + 14, 180, G - 16, -120, true);
  leg(660, baseT + 14, 820, G - 16, 120, true);
  cv.rrect(X(262), Y(baseT), 476 * S, (baseB - baseT) * S, 18 * S, body(DARK));
  cv.rrect(X(290), Y(baseT + 16), 420 * S, 12 * S, 6 * S, body(C(38, 38, 42)));
  {  // the shell: chamfered top corners
    const uint16_t sc = body(SHELL);
    cv.rrect(X(272), Y(shellT + 54), 456 * S, (baseT + 6 - shellT - 54) * S, 0, sc);
    cv.rrect(X(318), Y(shellT), 364 * S, 56 * S, 0, sc);
    cv.tri(X(318), Y(shellT), X(272), Y(shellT + 54), X(318), Y(shellT + 54), sc);
    cv.tri(X(682), Y(shellT), X(728), Y(shellT + 54), X(682), Y(shellT + 54), sc);
    cv.lineFast(X(272), Y(baseT + 4), X(728), Y(baseT + 4), 2.0f * S, body(SHELL2));  // the lower edge
    cv.rrect(X(330), Y(shellT - 18), 58 * S, 22 * S, 6 * S, body(SERVO));
    cv.rrect(X(612), Y(shellT - 18), 58 * S, 22 * S, 6 * S, body(SERVO));
    for (int i = 0; i < 6; i++)  // the vents (slanted slats)
      cv.lineFast(X(552 + i * 26), Y(shellT + 56), X(540 + i * 26), Y(shellT + 110), 6 * S, body(C(26, 26, 29)));
    // the chest display: two bars - the first lights with the screen, the second fills with the boot
    cv.rrect(X(334), Y(shellT + 74), 150 * S, 64 * S, 10 * S, body(C(5, 5, 6)));
    int okN = 0;
    for (int i = 0; i < B_N; i++) okN += ((in.ok >> i) & 1) ? 1 : 0;
    static float prog = 0;
    prog += (okN / (float)B_N - prog) * 0.15f;
    if (on > 0.01f) cv.rrect(X(348), Y(shellT + 90), 96 * on * S, 8 * S, 4 * S, fade(MINT, 0.9f * on * bp));
    if (on * prog > 0.01f) cv.rrect(X(348), Y(shellT + 108), 96 * prog * on * S, 8 * S, 4 * S, fade(BLUE, 0.8f * on * bp));
  }
  leg(290, baseT + 34, 70, G, -175, false);
  leg(710, baseT + 34, 930, G, 175, false);
  // the pan-tilt neck
  cv.rrect(X(450), Y(shellT - neckH), 100 * S, neckH * S, 10 * S, body(C(58, 58, 64)));
  cv.rrect(X(430), Y(shellT - 26), 140 * S, 22 * S, 6 * S, body(C(78, 78, 86)));
  cv.rrect(X(438), Y(shellT - neckH - 8), 124 * S, 20 * S, 6 * S, body(C(78, 78, 86)));

  // the head (its local 640 x 590 coordinates)
  {
    const uint16_t sc = body(SHELL);
    cv.rrect(HX(200), HY(70), 240 * HS, 44 * HS, 18 * HS, sc);  // the camera bump
    cv.rrect(HX(0), HY(190), 640 * HS, 330 * HS, 30 * HS, sc);  // the shell
    cv.rrect(HX(110), HY(96), 420 * HS, 100 * HS, 0, sc);
    cv.tri(HX(110), HY(96), HX(0), HY(236), HX(110), HY(236), sc);  // chamfered top corners
    cv.tri(HX(530), HY(96), HX(640), HY(236), HX(530), HY(236), sc);
    cv.circle(HX(320), HY(132), 26 * HS, body(C(28, 28, 31)));  // the lens
    cv.circle(HX(320), HY(132), 14 * HS, body(C(44, 53, 80)));
    const bool blinkLed = t < 1.25f && fmodf(t, 0.4f) < 0.2f;
    cv.circle(HX(232), HY(132), 7 * HS, on > 0.5f || blinkLed ? fade(MINT, seen * bp) : body(C(42, 42, 46)));
  }
  // the screen: always shown (at the end it's what stays - the face opens out of it)
  const float sk = fmaxf(lit, slide);
  cv.rrect(HX(86), HY(156), 468 * HS, 324 * HS, 30 * HS, fade(C(24, 24, 27), sk));
  cv.rrect(HX(98), HY(168), 444 * HS, 300 * HS, 22 * HS, to565(C(0, 0, 0)));
  if (on > 0) {
    // power-on: a white line runs down the screen, then two eyes open (and blink now and then)
    if (on < 1) cv.rrect(HX(98), HY(168 + 294 * on), 444 * HS, 6 * HS, 0, fade(C(255, 255, 255), (1 - on) * 0.9f));
    const float openE = easeOut(seg(t, 1.5f, 1.8f));
    const float bl = fmodf(t, 2.6f), blink = bl > 2.45f ? 0.12f : 1;
    const float eh = 120 * openE * blink, ew = 74;
    if (eh > 2)
      for (int e = 0; e < 2; e++) {
        const float ex = e ? 410 : 230, ey = 318;
        cv.rrect(HX(ex - ew / 2), HY(ey - eh / 2), ew * HS, eh * HS, fminf(ew, eh) * 0.45f * HS, to565(C(232, 244, 255)));
      }
  }

  // OREO, and the six-segment bar (one per part as it starts: orange, red = failed)
  const float tx0 = 160 - (3 * 27 + 13) / 2.0f;  // under Oreo
  for (int i = 0; i < 4; i++) {
    const float p = easeInOut(seg(t, 1.5f + i * 0.13f, 2.1f + i * 0.13f));
    if (p > 0) letter(cv, "OREO"[i], tx0 + i * 27, 194, p, fade(IVORY(), bp));
  }
  const float under = easeOut(seg(t, 2.1f, 2.5f));
  if (under > 0) cv.lineFast(160 - 47 * under, 220, 160 + 47 * under, 220, 0.5f, fade(IVORY(), 0.35f * bp));
  for (int i = 0; i < B_N; i++) {
    const float a = easeOut(seg(t, 2.2f + i * 0.06f, 2.6f + i * 0.06f));
    if (a <= 0) continue;
    const float sw = 13, sx = 160 - (B_N * sw + (B_N - 1) * 3.2f) / 2 + i * (sw + 3.2f), sy = 227;
    const bool ok = (in.ok >> i) & 1, bad = (in.fail >> i) & 1;
    cv.rrect(sx, sy, sw * a, 3, 1.5f, fade(DIM(), bp));
    static float shownFill[B_N] = {0};
    shownFill[i] += ((ok || bad ? 1.0f : 0.0f) - shownFill[i]) * 0.2f;
    if (shownFill[i] > 0.03f) cv.rrect(sx, sy, sw * a * shownFill[i], 3, 1.5f, fade(bad ? RED() : MINTC(), bp));
  }
  return -1;
}

// What changed this frame, to send only that to the screen (a whole frame takes ~41 ms over SPI; with the
// ~32 ms drawing it ran at 13 fps). Rectangles x, y, w, h; 0 = send the whole frame. The layout above:
// Oreo centred (x ~75..245), its head at y ~22..88, the chest bars ~y 125..140, OREO + the bar y ~190..234.
inline int dirtyRects(float t, float finishedAt, int16_t r[][4]) {
  const float endT = finishedAt > 0 ? fmaxf(finishedAt, 3.4f) : 1e9f;
  if (t > endT - 0.05f) return 0;                   // sliding / the face opening: everything moves
  int n = 0;
  auto add = [&](int x, int y, int w, int h) { r[n][0] = x; r[n][1] = y; r[n][2] = w; r[n][3] = h; n++; };
  if (t < 1.9f) {                                    // fading in, standing up, the screen powering on
    add(64, 0, 192, 240);                            // (only Oreo's column, the sides stay black)
    return n;
  }
  add(116, 14, 88, 80);                              // the head (bob, blink, LED)
  add(124, 120, 38, 24);                             // the chest bars
  add(96, 188, 128, 48);                             // OREO, the underline, the status bar
  return n;
}

}  // namespace boot
