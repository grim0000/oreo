// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
// Face engine v2: a procedural, living face for the 320 x 240 TFT, in many drawing styles.
//
// Goal: whenever you look at the robot, the face should not feel repetitive or canned.
//  1. Expressions are data (face_lib.h, plus any /faces/*.txt on the SD card): a "rig" - lids,
//     pupils, brows, mouth, colour, hop - plus a glyph (hearts, stars, spirals, X, $ ...), up to two
//     effects (sparkles, zzz, "?", rain cloud ...) and an accessory (glasses, monocle, patch).
//     Lines can hold ranges, random sides and alternatives, so every time one is chosen it is a
//     fresh take. Each also comes in three strengths: name_mild, name, name_max.
//  2. The face never freezes: slow smooth noise keeps gaze, tilt, brows and mouth alive, plus
//     blinks (quick, slow, half, double), glances and breathing.
//  3. Micro-expressions every few seconds, fitted to the mood, never the same twice in a row;
//     the resting face shifts now and then; left alone it gets bored in its own way.
//  4. Procedural: blend two expressions, or ask for a mood (valence, arousal).
// Every change is a spring morph (small overshoot, then it settles); glyph changes hide in a blink.
// Styles draw the same rig differently (see STYLE_NAMES). Only draws through Canvas, so the same
// code runs on the robot (LovyanGFX) and on a PC (previews).
#pragma once
// The voice playing right now (gemini.h sets these 60 times a second while Oreo talks): how far
// the mouth opens (0..1, the loudness) and its shape (0 = round "oo", 1 = wide "ee" / "s": how much
// of the sound is high). -1 = no voice: talking faces chatter on their own.
volatile float faceVoiceOpen = -1, faceVoiceWide = 0.5f;
// Brightness (the page's slider, saved): every colour of the face is scaled by it. The display's
// backlight has no pin of its own here, so this is how bright it looks - not less power.
volatile float faceBright = 1.0f;
// 3D props (the page's switch, saved): the play scenes' things - balls, balloons, clouds, cakes, the
// umbrella - drawn with light and shade (a lit side, a shadow side, a shine) instead of flat colours
volatile bool props3d = false;
// the battery saver face's eye colour (0xRRGGBB; the page's picker, saved) - cyan by default
volatile uint32_t saverColor = 0x5ADCFF;
// the Battery Saver face's mouth in a chat (main.cpp loop: the voice's loudness): -1 = none, 0 = a closed
// line, 1 = wide open. The chat keeps Oreo's own face.
volatile float saverMouth = -1;
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <ctype.h>
#include "face_lib.h"
#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#endif

namespace fe {

const int W = 320, H = 240;
const float PI_F = 3.14159265f;

// ---- randomness (variety) ----
inline uint32_t& rvState() { static uint32_t s = 2463534242u; return s; }
inline void rvSeed(uint32_t x) { rvState() = x ? x : 1; }
inline float rv() { uint32_t& s = rvState(); s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s >> 8) / 16777216.0f; }
inline float rr(float a, float b) { return a + (b - a) * rv(); }
inline bool chance(float p) { return rv() < p; }

struct RGB { float r, g, b; };
inline RGB C(float r, float g, float b) { RGB c = {r, g, b}; return c; }
inline RGB mixc(const RGB& a, const RGB& b, float t) { return C(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t); }
inline RGB mulc(const RGB& a, float k) { return C(a.r * k, a.g * k, a.b * k); }
inline uint16_t to565(const RGB& c) {
  const float k = faceBright;
  int r = (int)(c.r * k), g = (int)(c.g * k), b = (int)(c.b * k);
  // a black point: the faintest tones (glow tails, dim fringes) go to true black and the rest
  // stretches - the LCD's black looks deeper, closer to an OLED
  auto deep = [](int v) { return v <= 16 ? 0 : (v - 16) * 255 / 239; };
  r = deep(r < 0 ? 0 : r > 255 ? 255 : r);
  g = deep(g < 0 ? 0 : g > 255 ? 255 : g);
  b = deep(b < 0 ? 0 : b > 255 ? 255 : b);
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
inline RGB from565(uint16_t c) { return C(((c >> 11) & 31) * 255.0f / 31, ((c >> 5) & 63) * 255.0f / 63, (c & 31) * 255.0f / 31); }
inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

struct Canvas {
  virtual void clear(uint16_t c) = 0;
  virtual void rrect(float x, float y, float w, float h, float r, uint16_t c) = 0;  // filled, smooth edges
  virtual void circle(float x, float y, float r, uint16_t c) = 0;                     // filled, smooth edges
  virtual void ellipse(float x, float y, float rx, float ry, uint16_t c) = 0;         // filled
  virtual void tri(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c) = 0;
  virtual void line(float x0, float y0, float x1, float y1, float r, uint16_t c) = 0; // thick, round ends
  // Fast versions without edge smoothing (the robot's sprite): the engine gives curves a
  // half-blended outline itself, which looks nearly as smooth at a fraction of the cost
  virtual void lineFast(float x0, float y0, float x1, float y1, float r, uint16_t c) { line(x0, y0, x1, y1, r, c); }
  virtual void circleFast(float x, float y, float r, uint16_t c) { circle(x, y, r, c); }
  bool preferFast = false;
  virtual ~Canvas() {}
};

// CRT look: every colour becomes green phosphor of the same brightness
struct MonoCanvas : Canvas {
  Canvas* o = nullptr;
  RGB phos = {90, 255, 130};
  uint16_t m(uint16_t c) const {
    const RGB x = from565(c);
    const float l = (0.3f * x.r + 0.59f * x.g + 0.11f * x.b) / 255;
    return to565(mulc(phos, powf(l, 0.8f)));
  }
  void clear(uint16_t c) override { o->clear(m(c)); }
  void rrect(float x, float y, float w, float h, float r, uint16_t c) override { o->rrect(x, y, w, h, r, m(c)); }
  void circle(float x, float y, float r, uint16_t c) override { o->circle(x, y, r, m(c)); }
  void ellipse(float x, float y, float rx, float ry, uint16_t c) override { o->ellipse(x, y, rx, ry, m(c)); }
  void tri(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c) override { o->tri(x0, y0, x1, y1, x2, y2, m(c)); }
  void line(float x0, float y0, float x1, float y1, float r, uint16_t c) override { o->line(x0, y0, x1, y1, r, m(c)); }
  void lineFast(float x0, float y0, float x1, float y1, float r, uint16_t c) override { o->lineFast(x0, y0, x1, y1, r, m(c)); }
  void circleFast(float x, float y, float r, uint16_t c) override { o->circleFast(x, y, r, m(c)); }
};

// Mirror image (a play scene's props from the other side): every x becomes W - x
struct MirrorCanvas : Canvas {
  Canvas* o = nullptr;
  static float f(float x) { return 320 - x; }
  void clear(uint16_t c) override { o->clear(c); }
  void rrect(float x, float y, float w, float h, float r, uint16_t c) override { o->rrect(f(x) - w, y, w, h, r, c); }
  void circle(float x, float y, float r, uint16_t c) override { o->circle(f(x), y, r, c); }
  void ellipse(float x, float y, float rx, float ry, uint16_t c) override { o->ellipse(f(x), y, rx, ry, c); }
  void tri(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c) override { o->tri(f(x0), y0, f(x1), y1, f(x2), y2, c); }
  void line(float x0, float y0, float x1, float y1, float r, uint16_t c) override { o->line(f(x0), y0, f(x1), y1, r, c); }
  void lineFast(float x0, float y0, float x1, float y1, float r, uint16_t c) override { o->lineFast(f(x0), y0, f(x1), y1, r, c); }
  void circleFast(float x, float y, float r, uint16_t c) override { o->circleFast(f(x), y, r, c); }
};

// LED-grid look (PIXEL): shapes are sampled at cell centres, then drawn as dots
struct GridCanvas : Canvas {
  static const int CW = 40, CH = 30, CS = 8;
  uint16_t cell[CW * CH];
  bool on[CW * CH];
  void clear(uint16_t c) override { for (int i = 0; i < CW * CH; i++) { cell[i] = c; on[i] = false; } }
  template <class F> void fill(float x0, float y0, float x1, float y1, uint16_t c, F inside) {
    int a = (int)floorf(x0 / CS) - 1, b = (int)ceilf(x1 / CS) + 1, d = (int)floorf(y0 / CS) - 1, e = (int)ceilf(y1 / CS) + 1;
    if (a < 0) a = 0;
    if (d < 0) d = 0;
    if (b > CW - 1) b = CW - 1;
    if (e > CH - 1) e = CH - 1;
    for (int j = d; j <= e; j++)
      for (int i = a; i <= b; i++) {
        const float px = i * CS + CS * 0.5f, py = j * CS + CS * 0.5f;
        if (inside(px, py)) { cell[j * CW + i] = c; on[j * CW + i] = c != 0; }
      }
  }
  void rrect(float x, float y, float w, float h, float r, uint16_t c) override {
    const float cx = x + w / 2, cy = y + h / 2, hx = w / 2, hy = h / 2;
    if (r > hx) r = hx;
    if (r > hy) r = hy;
    fill(x, y, x + w, y + h, c, [&](float px, float py) {
      const float qx = fabsf(px - cx) - hx + r, qy = fabsf(py - cy) - hy + r;
      const float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
      return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0.0f) - r <= 0; });
  }
  void circle(float x, float y, float r, uint16_t c) override {
    fill(x - r, y - r, x + r, y + r, c, [&](float px, float py) { return (px - x) * (px - x) + (py - y) * (py - y) <= r * r; });
  }
  void ellipse(float x, float y, float rx, float ry, uint16_t c) override {
    if (rx < 0.5f || ry < 0.5f) return;
    fill(x - rx, y - ry, x + rx, y + ry, c, [&](float px, float py) { const float u = (px - x) / rx, v = (py - y) / ry; return u * u + v * v <= 1; });
  }
  void tri(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c) override {
    auto e = [](float ax, float ay, float bx, float by, float px, float py) { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); };
    fill(fminf(x0, fminf(x1, x2)), fminf(y0, fminf(y1, y2)), fmaxf(x0, fmaxf(x1, x2)), fmaxf(y0, fmaxf(y1, y2)), c,
         [&](float px, float py) {
           const float a = e(x0, y0, x1, y1, px, py), b = e(x1, y1, x2, y2, px, py), d = e(x2, y2, x0, y0, px, py);
           return (a >= 0 && b >= 0 && d >= 0) || (a <= 0 && b <= 0 && d <= 0); });
  }
  void line(float x0, float y0, float x1, float y1, float r, uint16_t c) override {
    const float q = fmaxf(r, CS * 0.45f);
    fill(fminf(x0, x1) - q, fminf(y0, y1) - q, fmaxf(x0, x1) + q, fmaxf(y0, y1) + q, c, [&](float px, float py) {
      const float dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
      float t = l2 > 0 ? ((px - x0) * dx + (py - y0) * dy) / l2 : 0;
      t = clampf(t, 0, 1);
      const float ex = px - (x0 + t * dx), ey = py - (y0 + t * dy);
      return ex * ex + ey * ey <= q * q; });
  }
  void flush(Canvas& out, uint16_t offCol) {
    out.clear(0);
    for (int j = 0; j < CH; j++)
      for (int i = 0; i < CW; i++) {
        const int k = j * CW + i;
        out.rrect(i * CS + 1, j * CS + 1, CS - 2, CS - 2, 1, on[k] ? cell[k] : offCol);
      }
  }
};

// Retro LCD look: shapes are sampled at the centres of 6 px cells (like GridCanvas), then every cell
// that isn't the background becomes a dark LCD pixel in one of three shades (by how much darker
// it is), with a faint drop shadow. The cells live in PSRAM on the robot (allocated on first use).
struct LcdCanvas : Canvas {
  static const int CS = 6, CW = (W + CS - 1) / CS, CH = (H + CS - 1) / CS;
  uint16_t* cell = nullptr;
  uint16_t bg = 0;
  bool ready() {
    if (!cell) {
#if defined(ESP_PLATFORM)
      cell = (uint16_t*)heap_caps_malloc(CW * CH * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
      if (!cell)
#endif
        cell = (uint16_t*)malloc(CW * CH * sizeof(uint16_t));
    }
    return cell != nullptr;
  }
  void clear(uint16_t c) override { bg = c; if (cell) for (int i = 0; i < CW * CH; i++) cell[i] = c; }
  template <class F> void fill(float x0, float y0, float x1, float y1, uint16_t c, F inside) {
    int a = (int)floorf(x0 / CS) - 1, b = (int)ceilf(x1 / CS) + 1, d = (int)floorf(y0 / CS) - 1, e = (int)ceilf(y1 / CS) + 1;
    if (a < 0) a = 0;
    if (d < 0) d = 0;
    if (b > CW - 1) b = CW - 1;
    if (e > CH - 1) e = CH - 1;
    for (int j = d; j <= e; j++)
      for (int i = a; i <= b; i++)
        if (inside(i * CS + CS * 0.5f, j * CS + CS * 0.5f)) cell[j * CW + i] = c;
  }
  void rrect(float x, float y, float w, float h, float r, uint16_t c) override {
    const float cx = x + w / 2, cy = y + h / 2, hx = w / 2, hy = h / 2;
    if (r > hx) r = hx;
    if (r > hy) r = hy;
    fill(x, y, x + w, y + h, c, [&](float px, float py) {
      const float qx = fabsf(px - cx) - hx + r, qy = fabsf(py - cy) - hy + r;
      const float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
      return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0.0f) - r <= 0; });
  }
  void circle(float x, float y, float r, uint16_t c) override {
    fill(x - r, y - r, x + r, y + r, c, [&](float px, float py) { return (px - x) * (px - x) + (py - y) * (py - y) <= r * r; });
  }
  void ellipse(float x, float y, float rx, float ry, uint16_t c) override {
    if (rx < 0.5f || ry < 0.5f) return;
    fill(x - rx, y - ry, x + rx, y + ry, c, [&](float px, float py) { const float u = (px - x) / rx, v = (py - y) / ry; return u * u + v * v <= 1; });
  }
  void tri(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c) override {
    auto e = [](float ax, float ay, float bx, float by, float px, float py) { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); };
    fill(fminf(x0, fminf(x1, x2)), fminf(y0, fminf(y1, y2)), fmaxf(x0, fmaxf(x1, x2)), fmaxf(y0, fmaxf(y1, y2)), c,
         [&](float px, float py) {
           const float a = e(x0, y0, x1, y1, px, py), b = e(x1, y1, x2, y2, px, py), d = e(x2, y2, x0, y0, px, py);
           return (a >= 0 && b >= 0 && d >= 0) || (a <= 0 && b <= 0 && d <= 0); });
  }
  void line(float x0, float y0, float x1, float y1, float r, uint16_t c) override {
    const float q = fmaxf(r, CS * 0.42f);
    fill(fminf(x0, x1) - q, fminf(y0, y1) - q, fmaxf(x0, x1) + q, fmaxf(y0, y1) + q, c, [&](float px, float py) {
      const float dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
      float t = l2 > 0 ? ((px - x0) * dx + (py - y0) * dy) / l2 : 0;
      t = clampf(t, 0, 1);
      const float ex = px - (x0 + t * dx), ey = py - (y0 + t * dy);
      return ex * ex + ey * ey <= q * q; });
  }
  // paper: the LCD's background colour; ink: its darkest pixel
  void flush(Canvas& out, const RGB& paper, const RGB& ink) {
    out.clear(to565(paper));
    const RGB b = from565(bg);
    const float lb = 0.3f * b.r + 0.59f * b.g + 0.11f * b.b;
    const uint16_t shade[3] = {to565(mixc(paper, ink, 0.3f)), to565(mixc(paper, ink, 0.62f)), to565(ink)};
    const uint16_t shadow = to565(mixc(paper, ink, 0.14f)), grid = to565(mixc(paper, ink, 0.05f));
    for (int i = 1; i < CW; i++) out.rrect(i * CS - 1, 0, 1, H, 0, grid);  // the faint pixel matrix
    for (int j = 1; j < CH; j++) out.rrect(0, j * CS - 1, W, 1, 0, grid);
    for (int pass = 0; pass < 2; pass++)
      for (int j = 0; j < CH; j++)
        for (int i = 0; i < CW; i++) {
          const uint16_t c = cell[j * CW + i];
          if (c == bg) continue;
          const RGB x = from565(c);
          const float d = lb - (0.3f * x.r + 0.59f * x.g + 0.11f * x.b);  // how much darker than the background
          if (d < 16) continue;                                           // lighter (highlights) or about the same: off
          if (pass == 0) out.rrect(i * CS + 1, j * CS + 1, CS - 1, CS - 1, 0, shadow);
          else out.rrect(i * CS, j * CS, CS - 1, CS - 1, 0, shade[d < 60 ? 0 : d < 115 ? 1 : 2]);
        }
  }
};

// The rig (all floats: blendable, springable)
struct Pose {
  float size, sizeL, sizeR, squash, gap, y, dyL, dyR, tilt;
  float lidT, lidTA, lidB, closeL, closeR, closeCurve;
  float pupil, dilate, lookX, lookY, shine;
  float browShow, browY, browA, browCurve, browAsym;
  float tr, tg, tb, tint;
  float mSmile, mOpen, mW, mX, mWavy, mTongue, mCat;
  float blush, bounce, bounceHz, jitter, breathe, spin, sway, cross;
  float wobble, pulse, nod;  // jelly squish, size pulse (heartbeat / fuming), slow up-down nod (px)
  float puff, squeeze;       // cheeks puffed out (0..1); eyes screwed shut > < with creases (sneeze)
};
const int NP = sizeof(Pose) / sizeof(float);
#define FE_I(field) ((int)(offsetof(Pose, field) / sizeof(float)))

struct Key { const char* name; int idx; };
const Key KEYS[] = {
  {"size", FE_I(size)}, {"sizeL", FE_I(sizeL)}, {"sizeR", FE_I(sizeR)}, {"squash", FE_I(squash)}, {"gap", FE_I(gap)},
  {"y", FE_I(y)}, {"dyL", FE_I(dyL)}, {"dyR", FE_I(dyR)}, {"tilt", FE_I(tilt)}, {"lidT", FE_I(lidT)}, {"lidTA", FE_I(lidTA)},
  {"lidB", FE_I(lidB)}, {"closeL", FE_I(closeL)}, {"closeR", FE_I(closeR)}, {"closeCurve", FE_I(closeCurve)},
  {"pupil", FE_I(pupil)}, {"dilate", FE_I(dilate)}, {"lookX", FE_I(lookX)}, {"lookY", FE_I(lookY)}, {"shine", FE_I(shine)},
  {"browShow", FE_I(browShow)}, {"browY", FE_I(browY)}, {"browA", FE_I(browA)}, {"browCurve", FE_I(browCurve)},
  {"browAsym", FE_I(browAsym)}, {"smile", FE_I(mSmile)}, {"open", FE_I(mOpen)}, {"mW", FE_I(mW)}, {"mX", FE_I(mX)},
  {"wavy", FE_I(mWavy)}, {"tongue", FE_I(mTongue)}, {"cat", FE_I(mCat)}, {"blush", FE_I(blush)}, {"bounce", FE_I(bounce)},
  {"bounceHz", FE_I(bounceHz)}, {"jitter", FE_I(jitter)}, {"breathe", FE_I(breathe)}, {"spin", FE_I(spin)},
  {"sway", FE_I(sway)}, {"cross", FE_I(cross)}, {"wobble", FE_I(wobble)}, {"pulse", FE_I(pulse)}, {"nod", FE_I(nod)},
  {"puff", FE_I(puff)}, {"squeeze", FE_I(squeeze)},
};

enum Glyph : uint8_t { G_EYE, G_HEART, G_STAR, G_SPIRAL, G_X, G_CHEVRON, G_DOT, G_LINE, G_TEARY, G_SHADES, G_LOADING, G_HEARTBEAT, G_DOLLAR, G_TARGET, G_NUM };
const char* const GLYPH_NAMES[G_NUM] = {"eye", "heart", "star", "spiral", "x", "chevron", "dot", "line", "teary", "shades", "loading", "heartbeat", "dollar", "target"};
enum Fx : uint8_t {
  FX_NONE, FX_SPARKLE, FX_QUESTION, FX_EXCLAIM, FX_ZZZ, FX_HEARTS, FX_ANGER, FX_BOLT, FX_BULB, FX_NOTES,
  FX_LISTEN, FX_SPEAK, FX_WIFI, FX_BATTERY, FX_DIZZY, FX_CONFETTI, FX_TEARS, FX_CRY, FX_SWEAT, FX_STEAM,
  FX_SHINE, FX_SHOCK, FX_DOTS, FX_BLUSHLINES, FX_CLOUD, FX_MONEY, FX_ICE, FX_FIRE, FX_HEAT, FX_BUBBLE,
  FX_BUBBLES, FX_GLITCH, FX_SCAN, FX_KISS, FX_DROOL, FX_WIFIX, FX_BATTFULL, FX_STARS, FX_GLOOM, FX_FLOWERS,
  FX_SWEATBIG, FX_PROGRESS, FX_HALO, FX_HORNS, FX_ACHOO, FX_NUM
};
const char* const FX_NAMES[FX_NUM] = {
  "none", "sparkle", "question", "exclaim", "zzz", "hearts", "anger", "bolt", "bulb", "notes",
  "listen", "speak", "wifi", "battery", "dizzy", "confetti", "tears", "cry", "sweat", "steam",
  "shine", "shock", "dots", "blushlines", "cloud", "money", "ice", "fire", "heat", "bubble",
  "bubbles", "glitch", "scan", "kiss", "drool", "wifix", "battfull", "stars", "gloom", "flowers",
  "sweatbig", "progress", "halo", "horns", "achoo"};
enum Acc : uint8_t { ACC_NONE, ACC_GLASSES, ACC_MONOCLE, ACC_PATCH, ACC_NUM };
const char* const ACC_NAMES[ACC_NUM] = {"none", "glasses", "monocle", "patch"};

struct Extra {
  uint8_t glyph, fx1, fx2, acc;
  bool talk;
  float blinkEvery, glance;   // mean seconds between blinks (0 = none); how lively (glances, micro-expressions)
  float valence, arousal;     // the mood this shows: picks fitting micro-expressions
};

inline void basePose(Pose& p, Extra& x) {
  memset(&p, 0, sizeof(p));
  p.size = p.sizeL = p.sizeR = 1;
  p.gap = 1;
  p.closeCurve = 1;
  p.pupil = 1;
  p.shine = 1;
  p.browShow = 1;
  p.browCurve = 0.3f;
  p.tr = p.tg = p.tb = 255;
  p.mSmile = 0.25f;
  p.mW = 1;
  p.breathe = 1;
  x.glyph = G_EYE;
  x.fx1 = x.fx2 = FX_NONE;
  x.acc = ACC_NONE;
  x.talk = false;
  x.blinkEvery = 4;
  x.glance = 1;
  x.valence = 0.2f;
  x.arousal = 0;
}

// The expression library: text lines, indexed once. Extra libraries (SD card) are searched first.
struct LibEntry { const char* name; uint8_t nlen; const char* body; const char* end; float v, a; };
const int MAX_LIB = 700;
// the index lives in PSRAM on the robot (internal RAM is for the camera and WiFi)
inline LibEntry* libEntries() {
  static LibEntry* e = nullptr;
  if (!e) {
#if defined(ESP_PLATFORM)
    e = (LibEntry*)heap_caps_calloc(MAX_LIB, sizeof(LibEntry), MALLOC_CAP_SPIRAM);
    if (!e)
#endif
      e = (LibEntry*)calloc(MAX_LIB, sizeof(LibEntry));
  }
  return e;
}
inline int& libCount() { static int n = 0; return n; }

// Index a library text (kept in memory by the caller). Later additions win (searched first).
inline int libAdd(const char* text) {
  int added = 0;
  const char* p = text;
  while (*p) {
    const char* ls = p;
    while (*p && *p != '\n') p++;
    const char* le = p;
    if (*p) p++;
    while (ls < le && (*ls == ' ' || *ls == '\t' || *ls == '\r')) ls++;
    if (ls >= le || *ls == '#') continue;
    const char* colon = (const char*)memchr(ls, ':', le - ls);
    if (!colon) continue;
    const char* ne = ls;
    while (ne < colon && *ne != ' ' && *ne != '\t') ne++;
    if (libCount() >= MAX_LIB) break;
    LibEntry& e = libEntries()[libCount()++];
    e.name = ls;
    e.nlen = (uint8_t)(ne - ls);
    e.v = 0.2f;
    e.a = 0;
    char* q;
    const float v = strtof(ne, &q);
    if (q != ne && q < colon) { e.v = v; const float a = strtof(q, &q); if (q <= colon) e.a = a; }
    e.body = colon + 1;
    e.end = le;
    added++;
  }
  return added;
}
inline void libInit() {
  if (libCount() == 0) libAdd(FACE_LIB);
}
inline int libFind(const char* name, size_t len) {
  libInit();
  for (int i = libCount() - 1; i >= 0; i--) {  // newest first: SD packs override built-ins
    const LibEntry& e = libEntries()[i];
    if (e.nlen != len) continue;
    bool same = true;
    for (size_t k = 0; k < len && same; k++) same = tolower((unsigned char)e.name[k]) == tolower((unsigned char)name[k]);
    if (same) return i;
  }
  return -1;
}

// ---- the line parser ----
struct Parser {
  Pose* p;
  Extra* x;
  float side;
  static bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }
  static const char* matchBrace(const char* s, const char* end) {  // s at '{' -> its '}'
    int d = 0;
    for (; s < end; s++) {
      if (*s == '{') d++;
      else if (*s == '}' && --d == 0) return s;
    }
    return end;
  }
  // a number, a range a..b, s0.4 / -s0.4 (random side)
  float value(const char*& s, const char* end) {
    float sign = 1, sd = 1;
    if (s < end && *s == '-' && s + 1 < end && s[1] == 's') { sign = -1; s++; }
    if (s < end && *s == 's') { sd = side; s++; }
    char* q;
    float a = strtof(s, &q);
    s = q;
    if (s + 1 < end && s[0] == '.' && s[1] == '.') {
      s += 2;
      const float b = strtof(s, &q);
      s = q;
      a = rr(a, b);
    }
    return a * sd * sign;
  }
  static int enumFind(const char* const* names, int n, const char* s, size_t len) {
    for (int i = 0; i < n; i++) if (strlen(names[i]) == len && !strncmp(names[i], s, len)) return i;
    return -1;
  }
  void macro(const char* s, size_t len) {
    if (len == 11 && !strncmp(s, "closedHappy", 11)) { p->closeL = p->closeR = 1; p->closeCurve = 1; }
    else if (len == 10 && !strncmp(s, "closedCalm", 10)) { p->closeL = p->closeR = 1; p->closeCurve = -1; }
    else if (len == 9 && !strncmp(s, "happyEyes", 9)) {
      const float r = rv();
      if (r < 0.45f) { p->closeL = p->closeR = 1; p->closeCurve = 1; }
      else if (r < 0.8f) { p->lidB = rr(0.45f, 0.6f); p->lidT = rr(0, 0.08f); }
      else { p->lidB = 0.3f; p->size = 1.06f; }
    }
  }
  void token(const char* s, const char* e) {
    if (*s == '@') { macro(s + 1, e - s - 1); return; }
    const char* eq = s;
    while (eq < e && *eq != '=') eq++;
    if (eq >= e) {  // a flag
      if (e - s == 4 && !strncmp(s, "talk", 4)) x->talk = true;
      return;
    }
    const bool add = eq > s && eq[-1] == '+';
    const char* ke = add ? eq - 1 : eq;
    const size_t kl = ke - s;
    const char* v = eq + 1;
    if (kl == 5 && !strncmp(s, "glyph", 5)) { const int g = enumFind(GLYPH_NAMES, G_NUM, v, e - v); if (g >= 0) x->glyph = g; return; }
    if (kl == 2 && !strncmp(s, "fx", 2)) { const int f = enumFind(FX_NAMES, FX_NUM, v, e - v); if (f >= 0) x->fx1 = f; return; }
    if (kl == 3 && !strncmp(s, "fx2", 3)) { const int f = enumFind(FX_NAMES, FX_NUM, v, e - v); if (f >= 0) x->fx2 = f; return; }
    if (kl == 3 && !strncmp(s, "acc", 3)) { const int f = enumFind(ACC_NAMES, ACC_NUM, v, e - v); if (f >= 0) x->acc = f; return; }
    if (kl == 5 && !strncmp(s, "blink", 5)) { x->blinkEvery = value(v, e); return; }
    if (kl == 6 && !strncmp(s, "glance", 6)) { x->glance = value(v, e); return; }
    if (kl == 4 && !strncmp(s, "tint", 4)) {
      float c[4] = {255, 255, 255, 0};
      for (int i = 0; i < 4 && v < e; i++) { char* q; c[i] = strtof(v, &q); v = q; if (v < e && *v == ',') v++; }
      p->tr = c[0]; p->tg = c[1]; p->tb = c[2]; p->tint = c[3];
      return;
    }
    for (size_t k = 0; k < sizeof(KEYS) / sizeof(KEYS[0]); k++) {
      if (strlen(KEYS[k].name) != kl || strncmp(KEYS[k].name, s, kl)) continue;
      float* f = (float*)p + KEYS[k].idx;
      const float val = value(v, e);
      *f = add ? *f + val : val;
      return;
    }
  }
  // parse s..end
  void seq(const char* s, const char* end) {
    while (s < end) {
      while (s < end && isSpace(*s)) s++;
      if (s >= end) return;
      if (*s == '?') {  // ?0.3{ ... }
        char* q;
        const float pr = strtof(s + 1, &q);
        const char* ob = q;
        while (ob < end && *ob != '{') ob++;
        const char* cb = matchBrace(ob, end);
        if (chance(pr)) seq(ob + 1, cb);
        s = cb + 1;
        continue;
      }
      if (end - s > 5 && !strncmp(s, "pick{", 5)) {
        const char* ob = s + 4;
        const char* cb = matchBrace(ob, end);
        const char* alts[12];
        const char* altE[12];
        float w[12];
        int n = 0, d = 0;
        const char* a = ob + 1;
        for (const char* c = ob + 1; c <= cb && n < 12; c++) {
          if (c < cb && *c == '{') d++;
          else if (c < cb && *c == '}') d--;
          if (c == cb || (*c == '|' && d == 0)) {
            const char* as = a;
            while (as < c && isSpace(*as)) as++;
            float wt = 1;
            char* q;
            const float maybe = strtof(as, &q);
            if (q != as && q < c && *q == ':') { wt = maybe; as = q + 1; }  // optional "0.5:" weight
            alts[n] = as; altE[n] = c; w[n] = wt; n++;
            a = c + 1;
          }
        }
        float tot = 0;
        for (int i = 0; i < n; i++) tot += w[i];
        float r = rv() * tot;
        int pk = n - 1;
        for (int i = 0; i < n; i++) { if (r < w[i]) { pk = i; break; } r -= w[i]; }
        if (n > 0) seq(alts[pk], altE[pk]);
        s = cb + 1;
        continue;
      }
      const char* te = s;
      while (te < end && !isSpace(*te)) te++;
      token(s, te);
      s = te;
    }
  }
};

// Build an expression's rig from its line. Returns false if unknown.
inline bool buildExpr(int idx, Pose& p, Extra& x) {
  basePose(p, x);
  if (idx < 0) return false;
  const LibEntry& e = libEntries()[idx];
  x.valence = e.v;
  x.arousal = e.a;
  Parser ps;
  ps.p = &p;
  ps.x = &x;
  ps.side = chance(0.5f) ? 1.0f : -1.0f;
  ps.seq(e.body, e.end);
  return true;
}

// Other names (old faces, emotes) -> an expression
struct Alias { const char* from; const char* to; };
const Alias ALIASES[] = {
  {"neutral", "idle"}, {"default", "idle"}, {"defualt", "idle"}, {"idle_blink", "idle"}, {"stand", "idle"},
  // the emotes' own faces
  {"walk", "focus"}, {"crab", "sneaky"}, {"worm", "goofy"}, {"pushup", "straining"}, {"rest", "calm"},
  {"swim", "swimming"}, {"dance", "vibing"}, {"wave", "waving"}, {"point", "pointing"}, {"bow", "bowing"},
  {"cute", "ehehe"}, {"shake", "shaking"}, {"shrug", "question"}, {"relaxed", "calm"}, {"crazy", "freaky"},
  {"gocrazy", "freaky"}, {"patted", "love"}, {"scream", "shocked"}, {"cry", "sob"}, {"smile", "happy"},
};

// Micro-expressions: a short change laid over the current face (attack, hold, release)
struct Micro {
  const char* name;
  float vMin, vMax, aMin, aMax;  // moods it fits (valence, arousal ranges)
  float att, hold, rel;          // seconds (hold is randomised a little)
  void (*make)(Pose& d);         // the change (a delta; zero = no change)
};
#define MI(n, v0, v1, a0, a1, at, ho, re, body) { n, v0, v1, a0, a1, at, ho, re, [](Pose& d) { body } }
const Micro MICROS[] = {
  MI("brow flash", -0.5f, 1, -0.6f, 1, 0.08f, 0.25f, 0.3f, d.browY = 0.45f; d.size = 0.04f;),
  MI("curious tilt", -0.3f, 1, -0.5f, 1, 0.25f, 1.3f, 0.4f, { const float s = chance(0.5f) ? 1 : -1; d.tilt = 10 * s; d.browAsym = 0.4f * s; d.sizeR = 0.05f * s; d.lookX = 0.2f * s; }),
  MI("half smile", -0.3f, 1, -1, 0.8f, 0.3f, 1.7f, 0.5f, d.mSmile = 0.35f; d.mX = chance(0.5f) ? 0.3f : -0.3f; d.lidB = 0.12f;),
  MI("slow blink", 0.2f, 1, -1, 0.4f, 0.35f, 0.35f, 0.45f, d.closeL = 1; d.closeR = 1; d.closeCurve = -1.2f;),
  MI("squint", -1, 1, -0.5f, 1, 0.2f, 0.8f, 0.3f, d.lidT = 0.18f; d.lidB = 0.15f; d.browA = 5;),
  MI("double take", -0.5f, 1, 0, 1, 0.07f, 0.35f, 0.25f, d.lookX = chance(0.5f) ? 0.9f : -0.9f; d.size = 0.08f; d.browY = 0.3f;),
  MI("sigh", -1, 0.4f, -1, 0.2f, 0.4f, 0.6f, 0.8f, d.lidT = 0.22f; d.y = 5; d.mSmile = -0.15f; d.browA = -6;),
  MI("wiggle", 0.3f, 1, 0, 1, 0.1f, 0.9f, 0.3f, d.bounce = 4; d.bounceHz = 2.5f; d.tilt = rr(-6, 6);),
  MI("look around", -1, 1, -1, 1, 0.15f, 1.4f, 0.3f, d.lookX = rr(-1, 1); d.lookY = rr(-0.6f, 0.4f);),
  MI("pout", -1, 0.2f, -0.5f, 0.6f, 0.2f, 0.9f, 0.35f, d.mW = -0.35f; d.mSmile = -0.3f; d.mWavy = 0.3f;),
  MI("wide eyes", -1, 1, 0.2f, 1, 0.08f, 0.4f, 0.35f, d.size = 0.1f; d.pupil = -0.15f; d.browY = 0.3f;),
  MI("smirk", 0, 1, -0.4f, 0.6f, 0.25f, 1.2f, 0.4f, d.mSmile = 0.3f; d.mX = chance(0.5f) ? 0.35f : -0.35f; d.browAsym = chance(0.5f) ? 0.4f : -0.4f;),
  MI("nod", 0, 1, -0.5f, 0.8f, 0.12f, 0.12f, 0.2f, d.y = 6; d.lookY = 0.2f;),
  MI("tiny yawn", -1, 0.5f, -1, -0.3f, 0.5f, 0.9f, 0.6f, d.lidT = 0.4f; d.mOpen = 0.7f; d.mW = -0.3f; d.browY = 0.15f;),
  MI("eye roll", -1, 0.2f, -0.5f, 0.6f, 0.25f, 0.5f, 0.35f, d.lookY = -1; d.lookX = chance(0.5f) ? 0.4f : -0.4f; d.lidT = 0.15f;),
  MI("cheek puff", 0, 1, -0.3f, 0.7f, 0.2f, 0.8f, 0.3f, d.mW = -0.4f; d.mSmile = 0.1f; d.blush = 0.4f; d.size = 0.03f; d.puff = 1;),
  MI("tilt the other way", -0.5f, 1, -1, 1, 0.3f, 1.5f, 0.5f, d.tilt = chance(0.5f) ? -8 : 8; d.y = 2;),
  MI("sparkle eyes", 0.5f, 1, 0.2f, 1, 0.15f, 0.8f, 0.3f, d.size = 0.06f; d.dilate = 0.6f; d.shine = 0.3f;),
};
#undef MI
const int NMICRO = sizeof(MICROS) / sizeof(MICROS[0]);

// Styles
enum Style : uint8_t { ST_NEON, ST_SOFT, ST_REAL, ST_CAT, ST_PUPPY, ST_ANIME, ST_TOON, ST_EVE, ST_CRT, ST_MONO, ST_PIXEL,
                      ST_MOCHI, ST_GALAXY, ST_SAKURA, ST_MATCHA, ST_UBE, ST_HONEY,
                      ST_NOVA, ST_EMBER, ST_AURORA, ST_ROSE, ST_HALO, ST_HALOLITE, ST_SAVER,
                      // (appended after saver: the saved style numbers stay the same)
                      ST_OREO, ST_NEBULA, ST_MINT, ST_SUNSET, ST_PANDA, ST_PEACH, ST_SKY, ST_LEMON,
                      // (appended: the saved style numbers stay the same)
                      ST_LCD, ST_CHALK, ST_COMIC, ST_OWL, ST_FOX, ST_CYCLOPS, ST_PAINT, ST_COZY, ST_LINE, ST_TINBOT, ST_NUM };
const char* const STYLE_NAMES[ST_NUM] = {"neon", "soft", "real", "cat", "puppy", "anime", "toon", "eve", "crt", "mono", "pixel",
                                         "mochi", "galaxy", "sakura", "matcha", "ube", "honey",
                                         "nova", "ember", "aurora", "rose", "halo", "halolite", "saver",
                                         "oreo", "nebula", "mint", "sunset", "panda", "peach", "sky", "lemon",
                                         "lcd", "chalk", "comic", "owl", "fox", "cyclops", "paint", "cozy", "line", "tinbot"};
enum EyeT : uint8_t { E_NEON, E_PILL, E_REAL, E_CAT, E_DOG, E_ANIME, E_TOON, E_EVE, E_MOCHI, E_BEAN,
                      E_CHALK, E_COMIC, E_OWL, E_FOX, E_CYC, E_PAINT, E_LINE, E_TIN };
enum DecoT : uint8_t { D_NONE, D_STARS, D_BUNNY, D_SPROUT, D_KITTY, D_BEAR, D_CHALK, D_COMIC, D_HORNS, D_PAINT, D_HAT, D_TIN, D_LCD };
enum BrowT : uint8_t { B_NONE, B_NEON, B_HAIR, B_THICK, B_LINE, B_DOTS, B_BOLD };
enum MouthT : uint8_t { M_NONE, M_LINE, M_LIPS, M_CAT, M_DOG, M_CUTE, M_BEAK, M_EQ };
enum HeadT : uint8_t { H_FLAT, H_SKIN, H_CAT, H_DOG, H_OWL, H_FOX };
enum BlushT : uint8_t { BL_SOFT, BL_HATCH, BL_DOTS };

struct StyleDef {
  uint8_t eye, brow, mouth, head;
  bool single, lidBlink, lashes;
  RGB bg, bg2, skin, skin2, glow, iris, iris2, pupil, browC, mouthC, mouthIn, tongue, blush, lash, nose, muzzle;
  float rx, ry, gap, y, glowAmt, tEye, tGlow, tIris;
  float browLen, blushMin, bouncy;  // brow length (x eye width), always-on blush, extra spring bounce
  uint8_t deco;                     // a signature detail (ears, sprout, stars)
  bool light, family;               // light background; Mochi family (livelier animation)
  RGB deco1, deco2;                 // its colours
  uint8_t blushK;                   // BlushT: soft ovals, hatch strokes (chalk, line art, LCD), halftone / LED dots
  float lineW;                      // > 0: bold ink lines (brows, mouth, lid edges) this thick
};

inline StyleDef styleDef(uint8_t st) {
  StyleDef s;
  memset(&s, 0, sizeof(s));
  s.eye = E_NEON; s.brow = B_NEON; s.mouth = M_LINE; s.head = H_FLAT;
  s.tongue = C(255, 110, 150);
  s.blush = C(255, 90, 160);
  s.rx = s.ry = 40; s.gap = 136; s.y = 104;
  // defaults: the neon palette
  s.glow = C(60, 140, 255); s.iris = s.iris2 = C(30, 70, 150); s.pupil = C(8, 18, 48);
  s.browC = C(60, 130, 255); s.mouthC = C(235, 246, 255); s.mouthIn = C(255, 105, 150);
  s.glowAmt = 1; s.tEye = 0.25f; s.tGlow = 1; s.tIris = 0.5f;
  s.browLen = 1.05f;
  switch (st) {
    case ST_MOCHI:  // ultra cute: big low-set eyes close together, glossy plum pupils, tiny w mouth, blush
      s.eye = E_MOCHI; s.brow = B_LINE; s.mouth = M_CUTE; s.head = H_FLAT;
      s.bg = s.skin = C(10, 7, 16);
      s.glow = C(255, 170, 210); s.iris = C(58, 34, 96); s.iris2 = C(150, 100, 220); s.pupil = C(22, 12, 40);
      s.browC = C(255, 196, 220); s.mouthC = C(255, 200, 220); s.mouthIn = C(255, 105, 150); s.tongue = C(255, 135, 165);
      s.blush = C(255, 120, 175); s.lash = C(255, 240, 232);
      s.rx = 39; s.ry = 40; s.gap = 118; s.y = 108;
      s.glowAmt = 0.5f; s.tEye = 0.08f; s.tGlow = 0.8f; s.tIris = 0.35f;
      s.browLen = 0.55f; s.blushMin = 0.55f; s.bouncy = 1; s.family = true;
      break;
    case ST_GALAXY:  // night sky: cosmic pupils with little stars, a twinkling starfield
      s.eye = E_MOCHI; s.brow = B_LINE; s.mouth = M_CUTE; s.head = H_FLAT; s.deco = D_STARS;
      s.bg = s.skin = C(10, 12, 36);
      s.glow = C(120, 200, 255); s.iris = C(60, 30, 130); s.iris2 = C(70, 200, 240); s.pupil = C(16, 10, 44);
      s.browC = C(170, 210, 255); s.mouthC = C(190, 225, 255); s.mouthIn = C(255, 110, 170); s.tongue = C(255, 140, 180);
      s.blush = C(200, 120, 255); s.lash = C(235, 240, 255); s.deco1 = C(255, 255, 230); s.deco2 = C(150, 220, 255);
      s.rx = 39; s.ry = 40; s.gap = 118; s.y = 110;
      s.glowAmt = 0.8f; s.tEye = 0.06f; s.tGlow = 0.6f; s.tIris = 0.25f;
      s.browLen = 0.55f; s.blushMin = 0.45f; s.bouncy = 1; s.family = true;
      break;
    case ST_OREO:  // the robot's namesake: big cream eyes on black, chocolate pupils, a cream-white blush
      s.eye = E_MOCHI; s.brow = B_LINE; s.mouth = M_CUTE; s.head = H_FLAT;
      s.bg = s.skin = C(6, 5, 5);
      s.glow = C(250, 240, 222); s.iris = C(44, 30, 26); s.iris2 = C(120, 86, 66); s.pupil = C(14, 9, 8);
      s.browC = C(240, 232, 216); s.mouthC = C(246, 238, 224); s.mouthIn = C(200, 90, 110); s.tongue = C(255, 140, 160);
      s.blush = C(255, 214, 200); s.lash = C(255, 248, 236);
      s.rx = 40; s.ry = 41; s.gap = 122; s.y = 108;
      s.glowAmt = 0.4f; s.tEye = 0.08f; s.tGlow = 0.6f; s.tIris = 0.3f;
      s.browLen = 0.55f; s.blushMin = 0.35f; s.bouncy = 1; s.family = true;
      break;
    case ST_NEBULA:  // Galaxy in pink and violet: magenta pupils, a starfield
      s.eye = E_MOCHI; s.brow = B_LINE; s.mouth = M_CUTE; s.head = H_FLAT; s.deco = D_STARS;
      s.bg = s.skin = C(24, 8, 34);
      s.glow = C(255, 150, 230); s.iris = C(120, 30, 120); s.iris2 = C(255, 120, 210); s.pupil = C(34, 8, 40);
      s.browC = C(255, 190, 240); s.mouthC = C(255, 205, 240); s.mouthIn = C(255, 100, 170); s.tongue = C(255, 150, 190);
      s.blush = C(255, 120, 220); s.lash = C(255, 235, 250); s.deco1 = C(255, 240, 255); s.deco2 = C(255, 160, 230);
      s.rx = 39; s.ry = 40; s.gap = 118; s.y = 110;
      s.glowAmt = 0.8f; s.tEye = 0.06f; s.tGlow = 0.6f; s.tIris = 0.25f;
      s.browLen = 0.55f; s.blushMin = 0.45f; s.bouncy = 1; s.family = true;
      break;
    case ST_MINT:    // glowing mint visor eyes on black
    case ST_SUNSET:  // visor eyes drifting through amber, coral and pink
      s.eye = E_EVE; s.brow = B_NONE; s.mouth = M_NONE;
      s.bg = s.skin = C(0, 0, 0);
      s.rx = 44; s.ry = 32; s.gap = 150; s.y = 112;
      s.tEye = 1; s.tGlow = 1;
      break;
    case ST_PANDA:   // white face, black bear ears, dark eyes
    case ST_PEACH:   // peach, kitty ears
    case ST_SKY:     // sky blue, bunny ears
    case ST_LEMON:   // pale lemon, a leaf sprout
    case ST_SAKURA:  // soft pink, kawaii bean eyes, bunny ears, falling petals
    case ST_MATCHA:  // pastel green, a leaf sprout on top
    case ST_UBE:     // lavender, rounded kitty ears
    case ST_HONEY:   // warm cream, bear ears and freckles
      s.eye = E_BEAN; s.brow = B_LINE; s.mouth = M_CUTE; s.head = H_FLAT; s.light = true; s.family = true;
      s.rx = 30; s.ry = 34; s.gap = 128; s.y = 116;
      s.glowAmt = 0; s.tEye = 0; s.tGlow = 0; s.tIris = 0.2f;
      s.browLen = 0.8f; s.blushMin = 0.6f; s.bouncy = 1;
      if (st == ST_SAKURA) {
        s.bg = s.skin = C(255, 224, 234); s.iris = C(74, 38, 46); s.iris2 = C(170, 90, 110); s.pupil = s.iris;
        s.blush = C(255, 120, 160); s.deco = D_BUNNY; s.deco1 = C(255, 252, 252); s.deco2 = C(255, 170, 195);
      } else if (st == ST_MATCHA) {
        s.bg = s.skin = C(208, 232, 190); s.iris = C(40, 62, 34); s.iris2 = C(110, 150, 80); s.pupil = s.iris;
        s.blush = C(255, 150, 130); s.deco = D_SPROUT; s.deco1 = C(90, 170, 70); s.deco2 = C(60, 120, 50);
      } else if (st == ST_UBE) {
        s.bg = s.skin = C(222, 206, 246); s.iris = C(62, 34, 104); s.iris2 = C(150, 110, 210); s.pupil = s.iris;
        s.blush = C(255, 140, 200); s.deco = D_KITTY; s.deco1 = C(196, 172, 236); s.deco2 = C(255, 180, 215);
      } else if (st == ST_PANDA) {
        s.bg = s.skin = C(246, 246, 242); s.iris = C(22, 22, 26); s.iris2 = C(90, 90, 100); s.pupil = s.iris;
        s.blush = C(255, 160, 175); s.deco = D_BEAR; s.deco1 = C(28, 28, 32); s.deco2 = C(70, 70, 78);
      } else if (st == ST_PEACH) {
        s.bg = s.skin = C(255, 218, 196); s.iris = C(96, 44, 34); s.iris2 = C(200, 110, 86); s.pupil = s.iris;
        s.blush = C(255, 130, 120); s.deco = D_KITTY; s.deco1 = C(250, 186, 160); s.deco2 = C(255, 160, 170);
      } else if (st == ST_SKY) {
        s.bg = s.skin = C(204, 230, 255); s.iris = C(26, 52, 96); s.iris2 = C(90, 140, 210); s.pupil = s.iris;
        s.blush = C(255, 150, 190); s.deco = D_BUNNY; s.deco1 = C(250, 252, 255); s.deco2 = C(255, 182, 210);
      } else if (st == ST_LEMON) {
        s.bg = s.skin = C(255, 246, 186); s.iris = C(76, 60, 20); s.iris2 = C(170, 140, 50); s.pupil = s.iris;
        s.blush = C(255, 160, 120); s.deco = D_SPROUT; s.deco1 = C(110, 180, 70); s.deco2 = C(70, 130, 50);
      } else {
        s.bg = s.skin = C(255, 234, 188); s.iris = C(78, 46, 24); s.iris2 = C(170, 110, 60); s.pupil = s.iris;
        s.blush = C(255, 150, 110); s.deco = D_BEAR; s.deco1 = C(226, 176, 110); s.deco2 = C(250, 214, 160);
      }
      s.lash = s.iris;
      s.browC = mixc(s.iris, s.bg, 0.35f);
      s.mouthC = mixc(s.iris, s.bg, 0.15f);
      s.mouthIn = C(210, 80, 100); s.tongue = C(255, 130, 150);
      break;
    case ST_LCD:  // a retro handheld LCD (drawn into a coarse pixel grid, three shades of ink)
      s.eye = E_BEAN; s.brow = B_LINE; s.mouth = M_CUTE; s.light = true; s.family = true; s.blushK = BL_HATCH; s.deco = D_LCD;
      s.bg = s.skin = C(184, 198, 150); s.iris = C(36, 44, 30); s.iris2 = C(110, 124, 90); s.pupil = s.iris;
      s.lash = s.iris; s.browC = s.iris; s.mouthC = s.iris; s.mouthIn = C(60, 70, 50); s.tongue = C(120, 132, 100);
      s.blush = C(90, 100, 70);
      s.rx = 38; s.ry = 40; s.gap = 132; s.y = 118;
      s.tIris = 0; s.browLen = 0.7f; s.blushMin = 0.5f; s.bouncy = 1;
      break;
    case ST_CHALK:  // chalk drawings on a green slate board: dusty white eyes, pink chalk blush, a ledge
      s.eye = E_CHALK; s.brow = B_LINE; s.mouth = M_LINE; s.deco = D_CHALK; s.family = true; s.blushK = BL_HATCH;
      s.bg = s.skin = C(38, 64, 54); s.iris = C(150, 200, 190); s.iris2 = C(200, 230, 220); s.pupil = C(30, 50, 42);
      s.browC = C(228, 232, 222); s.mouthC = C(236, 238, 230); s.mouthIn = C(24, 40, 34); s.tongue = C(250, 160, 180);
      s.blush = C(255, 150, 175); s.deco1 = C(232, 234, 226); s.deco2 = C(250, 214, 120);
      s.rx = 36; s.ry = 40; s.gap = 134; s.y = 110;
      s.tEye = 0.15f; s.tIris = 0.3f; s.browLen = 0.75f; s.blushMin = 0.45f; s.bouncy = 1;
      break;
    case ST_COMIC:  // a bold comic: pop-art yellow, thick black outlines, pie-cut pupils, halftone dots
      s.eye = E_COMIC; s.brow = B_BOLD; s.mouth = M_LINE; s.deco = D_COMIC; s.light = true; s.family = true; s.blushK = BL_DOTS;
      s.lidBlink = true; s.lineW = 4.2f;
      s.bg = s.skin = C(255, 214, 62); s.pupil = C(16, 14, 18); s.iris = s.iris2 = s.pupil;
      s.lash = C(16, 14, 18); s.browC = s.lash; s.mouthC = s.lash; s.mouthIn = C(170, 30, 50); s.tongue = C(255, 110, 120);
      s.blush = C(240, 60, 70); s.deco1 = C(255, 150, 40); s.deco2 = C(240, 60, 70);
      s.rx = 36; s.ry = 42; s.gap = 132; s.y = 108;
      s.tEye = 0.2f; s.browLen = 0.8f; s.blushMin = 0.5f; s.bouncy = 1;
      break;
    case ST_OWL:  // an owl filling the screen: cream facial discs, amber eyes, a little beak, ear tufts
      s.eye = E_OWL; s.brow = B_BOLD; s.mouth = M_BEAK; s.head = H_OWL; s.light = true; s.family = true; s.lidBlink = true;
      s.lineW = 3.2f;
      s.bg = C(124, 84, 56); s.skin = C(246, 230, 204); s.skin2 = C(214, 186, 150);
      s.iris = C(240, 150, 30); s.iris2 = C(255, 214, 80); s.pupil = C(20, 14, 12);
      s.lash = C(64, 40, 28); s.browC = C(110, 72, 46); s.mouthC = C(236, 160, 50); s.mouthIn = C(120, 50, 40);
      s.blush = C(255, 140, 130); s.deco1 = C(92, 60, 40); s.deco2 = C(160, 116, 80); s.nose = C(196, 120, 30);
      s.rx = 38; s.ry = 38; s.gap = 140; s.y = 116;
      s.tIris = 0.35f; s.browLen = 0.75f; s.blushMin = 0.35f; s.bouncy = 1;
      break;
    case ST_FOX:  // a fox filling the screen: orange fur, white cheeks and muzzle, big ears with dark tips
      s.eye = E_FOX; s.brow = B_LINE; s.mouth = M_CAT; s.head = H_FOX; s.light = true; s.family = true; s.lidBlink = true;
      s.bg = s.skin = C(240, 132, 58); s.muzzle = C(255, 246, 236);
      s.iris = C(46, 26, 20); s.iris2 = C(220, 140, 50); s.pupil = C(16, 10, 8);
      s.lash = C(46, 26, 20); s.browC = C(150, 70, 30); s.mouthC = C(70, 40, 32); s.mouthIn = C(170, 60, 70);
      s.tongue = C(255, 120, 140); s.nose = C(36, 22, 20);
      s.blush = C(255, 110, 110); s.deco1 = C(214, 104, 40); s.deco2 = C(48, 30, 26);
      s.rx = 32; s.ry = 34; s.gap = 128; s.y = 112;
      s.tIris = 0.3f; s.browLen = 0.7f; s.blushMin = 0.4f; s.bouncy = 1;
      break;
    case ST_CYCLOPS:  // a one-eyed little monster: lilac skin, one big eye, tiny horns, a fang
      s.eye = E_CYC; s.brow = B_LINE; s.mouth = M_LINE; s.deco = D_HORNS; s.single = true; s.light = true; s.family = true;
      s.lidBlink = true;
      s.bg = s.skin = C(196, 170, 236); s.skin2 = C(160, 130, 210);
      s.iris = C(40, 150, 90); s.iris2 = C(140, 230, 140); s.pupil = C(18, 16, 30);
      s.lash = C(60, 34, 90); s.browC = C(90, 56, 130); s.mouthC = C(70, 40, 100); s.mouthIn = C(110, 30, 70);
      s.tongue = C(255, 120, 150); s.blush = C(255, 130, 190); s.deco1 = C(255, 246, 220); s.deco2 = C(150, 116, 205);
      s.rx = 58; s.ry = 56; s.gap = 0; s.y = 100;
      s.tIris = 0.35f; s.browLen = 0.8f; s.blushMin = 0.45f; s.bouncy = 1;
      break;
    case ST_PAINT:  // watercolour on paper: soft washed eyes with pigment pooled at the bottom, splashes
      s.eye = E_PAINT; s.brow = B_LINE; s.mouth = M_CUTE; s.deco = D_PAINT; s.light = true; s.family = true;
      s.bg = s.skin = C(250, 246, 236);
      s.iris = C(44, 62, 140); s.iris2 = C(120, 160, 225); s.pupil = C(26, 30, 80);
      s.lash = C(50, 60, 120); s.browC = C(120, 130, 180); s.mouthC = C(90, 90, 150); s.mouthIn = C(210, 90, 110);
      s.tongue = C(255, 140, 160); s.blush = C(255, 130, 150);
      s.deco1 = C(255, 170, 190); s.deco2 = C(150, 200, 240);
      s.rx = 34; s.ry = 38; s.gap = 130; s.y = 114;
      s.tIris = 0.35f; s.browLen = 0.75f; s.blushMin = 0.6f; s.bouncy = 1;
      break;
    case ST_COZY:  // warm and sleepy-cosy: oatmeal face under a knitted beanie with a pompom
      s.eye = E_BEAN; s.brow = B_LINE; s.mouth = M_CUTE; s.deco = D_HAT; s.light = true; s.family = true;
      s.bg = s.skin = C(250, 232, 212);
      s.iris = C(70, 40, 30); s.iris2 = C(160, 100, 70); s.pupil = s.iris;
      s.blush = C(255, 130, 110); s.deco1 = C(206, 84, 72); s.deco2 = C(250, 242, 228);
      s.rx = 30; s.ry = 34; s.gap = 128; s.y = 132;
      s.tIris = 0.2f; s.browLen = 0.8f; s.blushMin = 0.65f; s.bouncy = 1;
      s.lash = s.iris; s.browC = mixc(s.iris, s.bg, 0.35f); s.mouthC = mixc(s.iris, s.bg, 0.15f);
      s.mouthIn = C(210, 80, 100); s.tongue = C(255, 130, 150);
      break;
    case ST_LINE:  // minimal line art on OLED black: a lid line, a floating iris, nothing else
      s.eye = E_LINE; s.brow = B_LINE; s.mouth = M_LINE; s.lidBlink = true; s.family = true; s.blushK = BL_HATCH;
      s.bg = s.skin = C(0, 0, 0);
      s.iris = C(110, 210, 230); s.iris2 = C(190, 240, 250); s.pupil = C(0, 0, 0);
      s.browC = C(200, 206, 220); s.mouthC = C(236, 240, 250); s.mouthIn = C(255, 105, 150); s.tongue = C(255, 130, 160);
      s.blush = C(255, 110, 160); s.lash = C(240, 244, 255);
      s.rx = 40; s.ry = 30; s.gap = 144; s.y = 112;
      s.tEye = 0.15f; s.tIris = 0.6f; s.browLen = 0.8f; s.blushMin = 0.4f;
      break;
    case ST_TINBOT:  // a tin toy robot: a steel plate with rivets, lamp eyes in chrome bezels, an LED-bar mouth
      s.eye = E_TIN; s.brow = B_BOLD; s.mouth = M_EQ; s.deco = D_TIN; s.light = true; s.family = true; s.lidBlink = true;
      s.blushK = BL_DOTS; s.lineW = 4.0f;
      s.bg = s.skin = C(178, 190, 204); s.skin2 = C(120, 132, 150);
      s.iris = C(255, 176, 50); s.iris2 = C(255, 236, 150); s.pupil = C(22, 28, 44);
      s.lash = C(46, 52, 66); s.browC = C(70, 78, 96); s.mouthC = C(46, 52, 66); s.mouthIn = C(30, 34, 46);
      s.blush = C(255, 90, 130); s.deco1 = C(226, 232, 240); s.deco2 = C(120, 132, 150);
      s.rx = 36; s.ry = 34; s.gap = 140; s.y = 106;
      s.tIris = 0.6f; s.browLen = 0.7f; s.blushMin = 0.4f; s.bouncy = 1;
      break;
    case ST_MONO: s.single = true; s.rx = s.ry = 64; s.y = 100; break;
    case ST_SOFT: case ST_PIXEL:
      s.eye = E_PILL; s.brow = B_NONE; s.glowAmt = 0.6f;
      s.rx = 32; s.ry = 42; s.gap = 132;
      s.tEye = 1; s.tGlow = 1; s.tIris = 1;
      break;
    case ST_EVE:
    case ST_NOVA:    // cyan visor eyes with scanlines
    case ST_EMBER:   // orange-red eyes with a flickering hot core, embers rising
    case ST_AURORA:  // colours drifting like the northern lights
    case ST_ROSE:    // pink eyes, a glowing double ring
    case ST_HALO:    // ice-blue eyes with a glowing ring iris, a little angel halo above
    case ST_HALOLITE:  // Halo, minimal: a navy iris edged by a thin bright ring
      s.eye = E_EVE; s.brow = B_NONE; s.mouth = M_NONE;
      s.bg = s.skin = C(0, 0, 0);  // pure black: the OLED look
      s.rx = 46; s.ry = 30; s.gap = 150; s.y = 112;
      s.tEye = 1; s.tGlow = 1;
      if (st == ST_NOVA) { s.rx = 42; s.ry = 30; }
      if (st == ST_HALO || st == ST_HALOLITE) { s.rx = 40; s.ry = 34; s.gap = 148; }
      if (st == ST_ROSE) { s.rx = 42; s.ry = 34; }
      break;
    case ST_SAVER:  // Battery saver (drawn by drawSaver): EMO-like flat cyan eyes on black, no mouth
      s.eye = E_EVE; s.brow = B_NONE; s.mouth = M_NONE;
      s.bg = s.skin = C(0, 0, 0);
      s.rx = 40; s.ry = 50; s.gap = 156; s.y = 112;
      s.glowAmt = 0; s.tEye = 1;
      break;
    case ST_REAL:
      s.eye = E_REAL; s.brow = B_HAIR; s.mouth = M_LIPS; s.head = H_SKIN;
      s.bg = C(238, 196, 168); s.bg2 = C(214, 164, 134);
      s.skin = C(228, 182, 152); s.skin2 = C(196, 138, 110);    // eyelid skin, crease / shadow
      s.glow = C(210, 156, 128);                                 // eye-socket shadow
      s.iris = C(92, 62, 38); s.iris2 = C(176, 132, 74); s.pupil = C(12, 9, 8);
      s.browC = C(72, 48, 34); s.mouthC = C(182, 98, 92); s.mouthIn = C(84, 26, 30); s.tongue = C(214, 108, 112);
      s.blush = C(236, 140, 128); s.lash = C(34, 23, 20); s.nose = C(200, 146, 118);
      s.rx = 44; s.ry = 25; s.gap = 150; s.y = 100;
      s.lidBlink = s.lashes = true; s.tIris = 0.1f; s.glowAmt = 0;
      break;
    case ST_CAT:
      s.eye = E_CAT; s.brow = B_NONE; s.mouth = M_CAT; s.head = H_CAT;
      s.bg = s.skin = s.muzzle = C(0, 0, 0);                              // OLED black: only the features glow
      s.deco1 = C(255, 158, 70); s.deco2 = C(255, 160, 180);            // ear lines, inner ears
      s.nose = C(255, 130, 160);
      s.iris = C(120, 230, 70); s.iris2 = C(240, 236, 100); s.pupil = C(8, 6, 4); s.lash = C(255, 176, 96);  // lash = the glowing eye rim
      s.mouthC = C(255, 225, 215); s.mouthIn = C(190, 70, 90); s.blush = C(255, 110, 140);
      s.rx = s.ry = 38; s.gap = 132; s.y = 112;
      s.lidBlink = true; s.tIris = 0.3f; s.glowAmt = 0;
      break;
    case ST_PUPPY:
      s.eye = E_DOG; s.brow = B_DOTS; s.mouth = M_DOG; s.head = H_DOG;
      s.bg = s.skin = s.muzzle = C(0, 0, 0);                              // OLED black: only the features glow
      s.deco1 = C(235, 170, 95); s.deco2 = C(150, 95, 50);              // ear outline, ear fill
      s.nose = C(110, 78, 66);
      s.iris = C(150, 92, 40); s.iris2 = C(220, 150, 70); s.pupil = C(8, 5, 4); s.lash = C(240, 190, 120);
      s.browC = C(236, 176, 100); s.mouthC = C(250, 225, 200); s.mouthIn = C(160, 50, 60); s.tongue = C(255, 120, 140);
      s.blush = C(250, 140, 130);
      s.rx = 34; s.ry = 36; s.gap = 124; s.y = 112;
      s.lidBlink = true; s.tIris = 0.2f; s.glowAmt = 0;
      break;
    case ST_ANIME:
      s.eye = E_ANIME; s.brow = B_LINE; s.mouth = M_LINE; s.head = H_FLAT;
      s.bg = s.skin = C(255, 238, 232);
      s.iris = C(80, 45, 160); s.iris2 = C(170, 120, 240); s.pupil = C(30, 15, 55);
      s.browC = C(90, 52, 62); s.mouthC = C(170, 60, 80); s.mouthIn = C(200, 70, 90); s.tongue = C(255, 130, 150);
      s.blush = C(255, 150, 170); s.lash = C(45, 25, 45);
      s.rx = 34; s.ry = 46; s.gap = 134; s.y = 106;
      s.lidBlink = s.lashes = true; s.tIris = 0.4f; s.glowAmt = 0;
      break;
    case ST_TOON:
      s.eye = E_TOON; s.brow = B_THICK; s.mouth = M_LIPS; s.head = H_SKIN;
      s.bg = C(255, 226, 200); s.bg2 = C(246, 200, 170);
      s.skin = C(250, 212, 184); s.skin2 = C(214, 150, 118);
      s.iris = C(40, 96, 170); s.iris2 = C(120, 184, 240); s.pupil = C(14, 16, 28);
      s.browC = C(96, 58, 36); s.mouthC = C(214, 112, 104); s.mouthIn = C(112, 30, 42); s.tongue = C(236, 120, 130);
      s.blush = C(255, 150, 140); s.lash = C(60, 36, 28); s.nose = C(236, 176, 146);
      s.rx = 42; s.ry = 46; s.gap = 138; s.y = 104;
      s.lidBlink = true; s.tIris = 0.3f; s.glowAmt = 0;
      break;
    default: break;
  }
  return s;
}

struct Particle { float x, y, vx, vy, life, max, size, rot; uint8_t kind; uint8_t col; };
const int MAXP = 24;

class Face {
 public:
  Pose cur, vel, tgt, shown;   // shown = cur + living noise + micro-expression (what's drawn)
  Extra ext;
  int expr = -1;
  uint8_t style = ST_NEON;
  uint8_t glyph = G_EYE;       // shown glyph (swaps in the middle of a blink)
  bool stills = false;         // PC previews: no blinks, glances, noise, micro-expressions, takes
  bool autoLife = true;        // micro-expressions and boredom
  float t = 0;
  const char* lastMicro = "";

  Face() {
    libInit();
    basePose(tgt, ext);
    cur = shown = tgt;
    memset(&vel, 0, sizeof(vel));
    memset(&micro, 0, sizeof(micro));
    reseedNoise();
    scheduleBlink();
    nextMicro = rr(2, 5);
    idleIdx = libFind("idle", 4);
  }

  // name, name_mild, name_max, talk_name, old names ...  -> library index, strength, talk
  static int find(const char* name, bool* talk, float* strength) {
    char n[40];
    size_t k = 0;
    for (; name[k] && k < sizeof(n) - 1; k++) n[k] = (char)tolower((unsigned char)name[k]);
    n[k] = 0;
    char* base = n;
    *talk = false;
    *strength = 1;
    if (!strncmp(base, "talk_", 5)) { base += 5; *talk = true; }
    const size_t len = strlen(base);
    if (len > 5 && !strcmp(base + len - 5, "_mild")) { *strength = 0.5f; base[len - 5] = 0; }
    else if (len > 4 && !strcmp(base + len - 4, "_max")) { *strength = 1.4f; base[len - 4] = 0; }
    for (size_t i = 0; i < sizeof(ALIASES) / sizeof(ALIASES[0]); i++)
      if (!strcmp(base, ALIASES[i].from)) { strcpy(base, ALIASES[i].to); break; }
    return libFind(base, strlen(base));
  }
  static int count() { libInit(); return libCount(); }

  // Choose an expression by name. Each call makes a fresh take.
  bool set(const char* name) {
    if (!strncmp(name, "mood:", 5)) {  // "mood:0.6,0.4" (from the web page)
      char* q;
      const float v = strtof(name + 5, &q);
      setMood(v, *q == ',' ? strtof(q + 1, nullptr) : 0);
      return true;
    }
    bool talk;
    float str;
    const int e = find(name, &talk, &str);
    if (e < 0) return false;
    if (e == baseExpr && e == expr && talk == ext.talk && str == baseStrength && t - setAt < 1.5f) return true;
    Pose p;
    Extra x;
    buildExpr(e, p, x);
    if (talk) x.talk = true;
    strengthen(p, x, str);
    if (!stills) humanize(p);
    apply(e, p, x);
    baseExpr = e;
    baseStrength = str;
    return true;
  }

  // Procedural: a mix of two expressions (k = 0 .. 1 towards b)
  bool mix(const char* a, const char* b, float k) {
    bool ta, tb;
    float sa, sb;
    const int ea = find(a, &ta, &sa), eb = find(b, &tb, &sb);
    if (ea < 0 || eb < 0) return false;
    Pose pa, pb;
    Extra xa, xb;
    buildExpr(ea, pa, xa);
    buildExpr(eb, pb, xb);
    blend(pa, pb, k);
    Extra x = k < 0.5f ? xa : xb;
    x.valence = xa.valence + (xb.valence - xa.valence) * k;
    x.arousal = xa.arousal + (xb.arousal - xa.arousal) * k;
    if (!stills) humanize(pa);
    apply(k < 0.5f ? ea : eb, pa, x);
    baseExpr = expr;
    return true;
  }

  // Procedural: a mood. valence -1 (bad) .. +1 (good), arousal -1 (low energy) .. +1 (high)
  void setMood(float v, float a) {
    v = clampf(v, -1, 1);
    a = clampf(a, -1, 1);
    const char* corner[4] = {"sad", "calm", "scared", "excited"};  // (v-,a-) (v+,a-) (v-,a+) (v+,a+)
    const float w[4] = {(1 - v) * (1 - a) / 4, (1 + v) * (1 - a) / 4, (1 - v) * (1 + a) / 4, (1 + v) * (1 + a) / 4};
    const float strength = fminf(1, sqrtf(v * v + a * a));
    Pose base, sum, pc;
    Extra xb, xc;
    basePose(base, xb);
    sum = base;
    float* s = (float*)&sum;
    const float* b = (const float*)&base;
    int best = 0;
    for (int k = 0; k < 4; k++) {
      if (w[k] > w[best]) best = k;
      buildExpr(libFind(corner[k], strlen(corner[k])), pc, xc);
      const float* c = (const float*)&pc;
      for (int i = 0; i < NP; i++) s[i] += (c[i] - b[i]) * w[k] * strength;
    }
    buildExpr(libFind(corner[best], strlen(corner[best])), pc, xc);
    Extra x = xb;
    if (strength > 0.6f && w[best] > 0.45f) x = xc;
    x.glyph = G_EYE;
    x.valence = v;
    x.arousal = a;
    if (!stills) humanize(sum);
    apply(-1, sum, x);
  }

  void lookAt(float x, float y, float holdS = 0.5f) { extX = x; extY = y; extUntil = t + holdS; }

  void step(float dt) {
    if (dt > 0.1f) dt = 0.1f;
    t += dt;
    float lx = tgt.lookX, ly = tgt.lookY;
    if (t < extUntil) { lx = extX; ly = extY; }
    else if (!stills) {
      if (t >= nextGlance) {
        const float g = ext.glance;
        glX = g > 0 && chance(0.7f) ? rr(-0.5f, 0.5f) * g : 0;
        glY = g > 0 && chance(0.5f) ? rr(-0.3f, 0.3f) * g : 0;
        nextGlance = t + rr(0.8f, 4.0f) * (style == ST_SAVER ? 2 : 1);
      }
      lx += glX;
      ly += glY;
    }
    // springs in small sub-steps (a stiff spring stepped at 30 fps would blow up)
    float* c = (float*)&cur;
    float* v = (float*)&vel;
    const float* g = (const float*)&tgt;
    const int iLX = FE_I(lookX), iLY = FE_I(lookY);
    const int sub = (int)ceilf(dt / 0.004f);
    const float h = dt / (sub > 0 ? sub : 1);
    for (int n = 0; n < sub; n++)
      for (int i = 0; i < NP; i++) {
        float target = g[i], k = springK, damp = springD * (style >= ST_MOCHI ? 0.72f : 1.0f);
        if (i == iLX) { target = clampf(lx, -1.2f, 1.2f); k = 900; damp = 0.8f; }
        if (i == iLY) { target = clampf(ly, -1.2f, 1.2f); k = 900; damp = 0.8f; }
        const float cc = 2 * sqrtf(k) * damp;
        v[i] += (k * (target - c[i]) - cc * v[i]) * h;
        c[i] += v[i] * h;
      }
    // blinks (and the glyph swap hidden in one); quick, slow, half or double
    const bool wantBlink = !stills && ext.blinkEvery > 0 && t >= nextBlink;
    if (blinkT < 0 && (wantBlink || pendingGlyph != glyph)) {
      blinkT = 0;
      blinkLen = pendingGlyph != glyph ? 0.2f : (chance(0.15f) ? rr(0.28f, 0.4f) : rr(0.13f, 0.19f));
      blinkDepth = pendingGlyph != glyph ? 1 : (chance(0.12f) ? rr(0.45f, 0.7f) : 1);
    }
    if (blinkT >= 0) {
      blinkT += dt;
      if (blinkT >= blinkLen * 0.45f && pendingGlyph != glyph) glyph = pendingGlyph;
      if (blinkT > blinkLen) {
        blinkT = -1;
        if (doubleBlink) { doubleBlink = false; nextBlink = t + rr(0.08f, 0.16f); }
        else scheduleBlink();
      }
    }
    kickV *= expf(-dt * 9);
    if (!stills) life();
    shown = cur;
    if (!stills) {
      float* s = (float*)&shown;
      const float energy = 0.5f + 0.5f * clampf(ext.glance, 0, 1);
      for (int i = 0; i < NNOISE; i++) {
        const NoiseCh& n = noise[i];
        s[n.idx] += n.amp * energy * (0.6f * sinf(t * n.f1 + n.p1) + 0.4f * sinf(t * n.f2 + n.p2));
      }
      const float e = microEnv();
      if (e > 0) {
        const float* d = (const float*)&micro;
        for (int i = 0; i < NP; i++) s[i] += d[i] * e;
      }
    }
    sceneStep();  // a play scene: its expressions, gaze and props
    emit(ext.fx1, 0);
    emit(ext.fx2, 1);
    for (int i = 0; i < MAXP; i++) {
      Particle& q = parts[i];
      if (q.life <= 0) continue;
      q.life -= dt;
      q.x += q.vx * dt;
      q.y += q.vy * dt;
      q.rot += dt * 4;
      if (q.kind == FX_TEARS || q.kind == FX_CONFETTI) q.vy += 260 * dt;
    }
  }

  // ---- Battery saver: flat EMO-like eyes, drawn from a few whole-pixel numbers (SaverGeo). The face
  // task draws a new frame only when those numbers change (a blink, a glance, a new expression) and
  // sends only the changed rectangle to the TFT: a face at rest costs no frames at all. No breathing,
  // bobbing, particles or scene props (they would change every frame); blinks and glances keep it alive.
  struct SaverEye {
    int16_t x, y, w, h, r;       // centre, size, corner radius
    int16_t lid, ang, bot;       // top lid (px down), its slant (deg, + = angry), bottom lid (px up)
    uint8_t close, glyph;        // 0 open, 1 happy arc, 2 calm arc, 3 flat line; the glyph
  };
  struct SaverGeo {
    SaverEye e[2];
    uint16_t col;
    int16_t mx, my, mw, mh;  // the mouth (mh 0: none)
  };
  void saverGeo(SaverGeo& g) {
    memset(&g, 0, sizeof(g));
    const Pose& p = cur;  // springs only: no living noise (it moves every frame)
    const StyleDef sd = styleDef(ST_SAVER);
    float bl = 0;
    if (blinkT >= 0) {
      const float u = blinkT / blinkLen;
      bl = u < 0.4f ? u / 0.4f : 1 - (u - 0.4f) / 0.6f;
      bl = bl * bl * (3 - 2 * bl) * blinkDepth;
    }
    const RGB base = C((saverColor >> 16) & 255, (saverColor >> 8) & 255, saverColor & 255);  // (the setting)
    g.col = to565(mixc(base, C(p.tr, p.tg, p.tb), clampf(p.tint, 0, 1) * sd.tEye));
    const float cx = W / 2 + p.lookX * 30, cy = sd.y + p.y + p.lookY * 20;
    const float gap = sd.gap * (p.gap > 0.3f ? p.gap : 1);
    for (int i = 0; i < 2; i++) {
      const int side = i ? 1 : -1;  // 0 = the left eye on the screen
      SaverEye& e = g.e[i];
      // looking to a side: the eye on that side a little bigger (it's nearer), like EMO
      const float persp = 1 + 0.08f * clampf(p.lookX, -1, 1) * side;
      const float sz = clampf(p.size, 0.5f, 1.6f) * (i ? (p.sizeR > 0.3f ? p.sizeR : 1) : (p.sizeL > 0.3f ? p.sizeL : 1)) * persp;
      float w = 2 * sd.rx * sz * (1 + 0.4f * p.squash), h = 2 * sd.ry * sz * (1 - p.squash);
      const float close = clampf(i ? p.closeR : p.closeL, 0, 1);
      h *= 1 - 0.9f * bl;
      e.x = (int16_t)lroundf(cx + side * gap / 2);
      e.y = (int16_t)lroundf(cy + (i ? p.dyR : p.dyL) + side * p.tilt * 0.6f);
      e.glyph = glyph;
      // the particles aren't drawn here: love's hearts go into the eyes instead
      if (glyph == G_EYE && (ext.fx1 == FX_HEARTS || ext.fx2 == FX_HEARTS) && close < 0.6f) e.glyph = G_HEART;
      if (close > 0.6f || p.squeeze > 0.5f) {
        e.close = p.squeeze > 0.5f ? 0 : p.closeCurve > 0.3f ? 1 : p.closeCurve < -0.3f ? 2 : 3;
        if (p.squeeze > 0.5f) e.glyph = G_CHEVRON;
        if (e.close == 3) h = 12;
      } else {
        h *= 1 - 0.8f * close;
      }
      e.w = (int16_t)lroundf(w);
      e.h = (int16_t)lroundf(fmaxf(h, 8));
      e.r = (int16_t)lroundf(fminf(e.w, e.h) * 0.3f);
      e.lid = (int16_t)lroundf(clampf(p.lidT, 0, 0.9f) * e.h);
      e.ang = (int16_t)lroundf(clampf(p.lidTA, -35, 35));
      e.bot = (int16_t)lroundf(clampf(p.lidB, 0, 0.8f) * e.h);
      if (e.lid < 2 && e.ang > -3 && e.ang < 3) e.lid = e.ang = 0;
    }
    const float mo = saverMouth;
    if (mo >= 0) {  // under the eyes, in the eyes' colour: a line when quiet, opening with the voice
      const int l0 = g.e[0].y + g.e[0].h / 2, l1 = g.e[1].y + g.e[1].h / 2, lowest = l0 > l1 ? l0 : l1;
      g.mw = (int16_t)(34 + 14 * mo);
      g.mh = (int16_t)(5 + 24 * mo);
      g.mx = (int16_t)((g.e[0].x + g.e[1].x) / 2 - g.mw / 2);
      g.my = (int16_t)(fmaxf((float)lowest, sd.y + sd.ry) + 22);
    }
  }
  static void saverBox(const SaverGeo& g, int& x0, int& y0, int& x1, int& y1) {  // what it covers
    x0 = W; y0 = H; x1 = 0; y1 = 0;
    if (g.mh > 0) {  // the mouth
      x0 = g.mx - 4; y0 = g.my - 4; x1 = g.mx + g.mw + 4; y1 = g.my + g.mh + 4;
    }
    for (int i = 0; i < 2; i++) {
      const SaverEye& e = g.e[i];
      if (!e.w) continue;
      // glyphs and the closed arcs (0.56 x the width) reach past the eye: the box must cover them,
      // or their tips stay on the screen when the eye opens again
      const int m = e.glyph == G_EYE ? 8 : 16, hw = (int)(e.w * 0.6f) + m;
      const int a = e.x - hw, b = e.x + hw, c = e.y - e.h / 2 - m, d = e.y + e.h / 2 + m;
      if (a < x0) x0 = a;
      if (b > x1) x1 = b;
      if (c < y0) y0 = c;
      if (d > y1) y1 = d;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)W) x1 = W;
    if (y1 > (int)H) y1 = H;
  }
  void drawSaver(Canvas& cv, const SaverGeo& g) {
    cv.clear(0);
    k.cv = &cv;  // a play scene's props (the face task draws every frame while one plays)
    k.st = ST_SAVER;
    k.s = styleDef(ST_SAVER);
    if (sc.id) drawScene(true);
    const uint16_t col = g.col, blk = 0;
    for (int i = 0; i < 2; i++) {
      const SaverEye& e = g.e[i];
      const int side = i ? 1 : -1;
      const float x = e.x, y = e.y, w = e.w, h = e.h, s = fminf(w, 88.0f);
      if (e.glyph == G_HEART) {
        const uint16_t c = to565(C(255, 95, 150));
        cv.circle(x - s * 0.24f, y - s * 0.14f, s * 0.29f, c);
        cv.circle(x + s * 0.24f, y - s * 0.14f, s * 0.29f, c);
        cv.tri(x - s * 0.5f, y - 0.06f * s, x + s * 0.5f, y - 0.06f * s, x, y + s * 0.5f, c);
        continue;
      }
      if (e.glyph == G_X) {
        cv.line(x - s * 0.32f, y - s * 0.32f, x + s * 0.32f, y + s * 0.32f, 6, col);
        cv.line(x - s * 0.32f, y + s * 0.32f, x + s * 0.32f, y - s * 0.32f, 6, col);
        continue;
      }
      if (e.glyph == G_SPIRAL) {
        cv.circle(x, y, s * 0.45f, col);
        cv.circle(x, y, s * 0.45f - 7, blk);
        cv.circle(x, y, s * 0.22f, col);
        cv.circle(x, y, s * 0.22f - 6, blk);
        continue;
      }
      if (e.glyph == G_CHEVRON) {  // > <  (screwed shut, laughing)
        const float d = -side;     // the left eye points right
        cv.line(x - d * s * 0.26f, y - s * 0.3f, x + d * s * 0.22f, y, 6, col);
        cv.line(x + d * s * 0.22f, y, x - d * s * 0.26f, y + s * 0.3f, 6, col);
        continue;
      }
      if (e.glyph == G_STAR) {  // a four-point sparkle
        const float a = s * 0.5f, b = s * 0.13f;
        cv.tri(x - b, y, x + b, y, x, y - a, col); cv.tri(x - b, y, x + b, y, x, y + a, col);
        cv.tri(x, y - b, x, y + b, x - a, y, col); cv.tri(x, y - b, x, y + b, x + a, y, col);
        continue;
      }
      if (e.glyph == G_DOT) { cv.circle(x, y, s * 0.2f, col); continue; }
      if (e.close == 1 || e.close == 2) {  // closed arcs: ^ ^ (happy) or a calm, sleepy curve
        const float up = e.close == 1 ? 1 : -1, ry = s * 0.42f, th = 17, hw = w * 0.56f;
        const float y0 = y + up * ry * 0.3f;
        cv.ellipse(x, y0, hw, ry, col);
        cv.ellipse(x, y0 + up * th, hw - 3, ry, blk);  // a band, thicker in the middle
        cv.rrect(x - hw - 2, up > 0 ? y0 : y0 - ry - th - 2, 2 * hw + 4, ry + th + 2, 0, blk);
        continue;
      }
      if (e.glyph == G_LINE || e.close == 3) { cv.rrect(x - w / 2, y - 6, w, 12, 6, col); continue; }
      cv.rrect(x - w / 2, y - h / 2, w, h, e.r, col);
      if (e.lid > 0 || e.ang != 0) {  // top lid: a black cut, slanted (angry: down to the nose; sad: down outside)
        const float t0 = y - h / 2 - 2, sl = tanf(e.ang * PI_F / 180) * w / 2;
        const float xin = x - side * w / 2, xout = x + side * w / 2;  // the inner (nose) edge and the outer one
        const float yin = y - h / 2 + e.lid + sl, yout = y - h / 2 + e.lid - sl;
        cv.tri(xin, t0, xout, t0, xout, yout, blk);
        cv.tri(xin, t0, xout, yout, xin, yin, blk);
      }
      if (e.bot > 0) {  // bottom lid pushed up in a curve: a smiling squint
        const float R = w * 0.62f;
        cv.ellipse(x, y + h / 2 + R - e.bot, R * 1.25f, R, blk);
      }
    }
    if (g.mh > 0) cv.rrect(g.mx, g.my, g.mw, g.mh, fminf(g.mh, g.mw) / 2.0f, col);
    k.cv = &cv;
    if (sc.id) drawScene(false);
  }

  void draw(Canvas& out) {
    if (standby) { drawStandby(out); return; }  // desk mode's StandBy screen (any style)
    if (style == ST_SAVER) {
      SaverGeo g;
      saverGeo(g);
      drawSaver(out, g);
      return;
    }
    if (style == ST_PIXEL) {
      grid.clear(0);
      drawFace(grid, ST_PIXEL);
      grid.flush(out, to565(C(16, 18, 24)));
    } else if (style == ST_LCD && lcd.ready()) {
      lcd.clear(0);
      drawFace(lcd, ST_LCD);
      const StyleDef sd = styleDef(ST_LCD);
      lcd.flush(out, sd.bg, C(28, 34, 24));
    } else if (style == ST_CRT) {
      mono.o = &out;
      mono.preferFast = out.preferFast;
      drawFace(mono, ST_CRT);
      for (int y = 1; y < H; y += 3) out.rrect(0, y, W, 1, 0, 0);  // scanlines
    } else {
      drawFace(out, style);
    }
  }

  // Someone's driving it (RC) or talking through it: no boredom - its clock starts again, and a
  // yawn / doze already on ends now (back to the plain idle face).
  // Asleep (the chat's "sleep"): eyes shut, head down, a few snores early on; anything wakes it
  bool asleep = false;
  float sleepT0 = 0, nextSnore = 0;
  int snores = 0, snoresMax = 0;
  void sleepNow() {
    asleep = true;
    sleepT0 = t;
    nextSnore = t + rr(3, 7);
    snores = 0;
    snoresMax = 3 + (int)(rv() * 2);  // 3-4 snores, then quiet
    boredStage = 4;
    tempExpr("sleeping", 1e9f);
  }
  void sleepStep() {
    if (!asleep) return;
    headOn = true;  // (the play head: down)
    headX = 0;
    headY = 0.6f;
    if (snores < snoresMax && t >= nextSnore && t - sleepT0 < 45) {
      sfx("w:snore|v0.14 120~85:800");
      snores++;
      nextSnore = t + rr(5, 12);
    }
  }
  void wake() {
    if (!asleep) return;
    asleep = false;
    if (!sc.id) { headOn = false; headX = headY = 0; }
  }
  void keepAwake() {
    wake();
    idleSince = t;
    if (boredStage > 0) {
      boredStage = 0;
      if (tempUntil > 0) tempUntil = t;
    }
  }

  void doMicro(const char* name = nullptr) {
    int pk = -1;
    if (name) {
      for (int i = 0; i < NMICRO; i++) if (!strcmp(MICROS[i].name, name)) pk = i;
    } else {
      int cand[NMICRO], n = 0;
      for (int i = 0; i < NMICRO; i++) {
        const Micro& m = MICROS[i];
        if (ext.valence < m.vMin || ext.valence > m.vMax || ext.arousal < m.aMin || ext.arousal > m.aMax) continue;
        if (!strcmp(m.name, lastMicro) || !strcmp(m.name, prevMicro)) continue;
        cand[n++] = i;
      }
      if (n) pk = cand[(int)(rv() * n) % n];
    }
    if (pk < 0) return;
    memset(&micro, 0, sizeof(micro));
    MICROS[pk].make(micro);
    microAtt = MICROS[pk].att;
    microHold = MICROS[pk].hold * rr(0.7f, 1.4f);
    microRel = MICROS[pk].rel;
    microStart = t;
    prevMicro = lastMicro;
    lastMicro = MICROS[pk].name;
  }

 private:
  float blinkT = -1, blinkLen = 0.16f, blinkDepth = 1, nextBlink = 0;
  bool doubleBlink = false;
  float nextGlance = 1, glX = 0, glY = 0;
  float extX = 0, extY = 0, extUntil = 0;
  float kickV = 0;
  float fxNext[2] = {0, 0};
  float fxStart = 0, setAt = -10;
  float springK = 140, springD = 0.55f;
  uint8_t pendingGlyph = G_EYE;
  Particle parts[MAXP] = {};
  GridCanvas grid;
  MonoCanvas mono;
  LcdCanvas lcd;
  Pose micro;
  float microStart = -100, microAtt = 0.1f, microHold = 0.1f, microRel = 0.1f, nextMicro = 3;
  const char* prevMicro = "";
  int baseExpr = -1, idleIdx = -1;
  float baseStrength = 1;
  float idleSince = 0, tempUntil = 0, nextRetake = 10;
  int boredStage = 0;
  float boredAt[4] = {20, 40, 70, 110};
  struct NoiseCh { int idx; float amp, f1, f2, p1, p2; };
  static const int NNOISE = 7;
  NoiseCh noise[NNOISE];

  void reseedNoise() {
    const int idx[NNOISE] = {FE_I(lookX), FE_I(lookY), FE_I(tilt), FE_I(browY), FE_I(mSmile), FE_I(lidT), FE_I(size)};
    const float amp[NNOISE] = {0.1f, 0.07f, 2.5f, 0.07f, 0.06f, 0.03f, 0.015f};
    for (int i = 0; i < NNOISE; i++) {
      noise[i].idx = idx[i];
      noise[i].amp = amp[i];
      noise[i].f1 = rr(0.15f, 0.5f);
      noise[i].f2 = rr(0.5f, 1.3f);
      noise[i].p1 = rr(0, 6.28f);
      noise[i].p2 = rr(0, 6.28f);
    }
  }
  float microEnv() const {
    const float u = t - microStart;
    if (u < 0 || u > microAtt + microHold + microRel) return 0;
    float e;
    if (u < microAtt) e = u / microAtt;
    else if (u < microAtt + microHold) e = 1;
    else e = 1 - (u - microAtt - microHold) / microRel;
    return e * e * (3 - 2 * e);
  }

  // mild / max versions: move the rig less / more away from calm
  static void strengthen(Pose& p, Extra& x, float k) {
    if (k == 1) return;
    Pose b;
    Extra xb;
    basePose(b, xb);
    float* f = (float*)&p;
    const float* o = (const float*)&b;
    const int skip[] = {FE_I(closeL), FE_I(closeR), FE_I(closeCurve), FE_I(tr), FE_I(tg), FE_I(tb), FE_I(bounceHz)};
    for (int i = 0; i < NP; i++) {
      bool sk = false;
      for (int j : skip) sk = sk || j == i;
      if (!sk) f[i] = o[i] + (f[i] - o[i]) * k;
    }
    p.lidT = clampf(p.lidT, 0, k > 1 ? 0.8f : 0.95f);  // max: still a sliver of eye showing
    p.lidB = clampf(p.lidB, 0, 0.8f);
    p.mSmile = clampf(p.mSmile, -1, 1);
    p.mOpen = clampf(p.mOpen, 0, 1);
    p.blush = clampf(p.blush, 0, 1);
    p.tint = clampf(p.tint, 0, 1);
    if (k < 1) x.fx2 = FX_NONE;
  }

  void life() {
    if (!autoLife || sc.id) return;  // a play scene runs the face itself
    if (t >= nextMicro && t - setAt > 1.5f && microEnv() == 0) {
      doMicro();
      const float lively = clampf(0.6f + ext.arousal * 0.4f + ext.glance * 0.25f, 0.3f, 1.3f);
      nextMicro = t + rr(1.8f, 5.5f) / lively;
    }
    // resting on one expression: every so often a fresh take of it (a posture shift)
    if (tempUntil == 0 && expr == baseExpr && expr >= 0 && t >= nextRetake) {
      if (t - setAt > 6 && microEnv() == 0) {
        Pose p;
        Extra x;
        buildExpr(baseExpr, p, x);
        if (x.glyph == ext.glyph && x.fx1 == ext.fx1) {
          strengthen(p, x, baseStrength);
          humanize(p);
          const Extra keep = ext;
          apply(baseExpr, p, keep, true);
        }
      }
      nextRetake = t + rr(8, 20);
    }
    // boredom on "idle": looks around, bored / yawns, tired, dozes (random order and times)
    if (baseExpr >= 0 && baseExpr == idleIdx) {
      const float idle = t - idleSince;
      if (boredStage == 0 && idle > boredAt[0]) { boredStage = 1; doMicro("look around"); }
      else if (boredStage == 1 && idle > boredAt[1]) { boredStage = 2; tempExpr(chance(0.5f) ? "bored" : "yawn", rr(2, 4)); }
      else if (boredStage == 2 && idle > boredAt[2]) { boredStage = 3; tempExpr(chance(0.6f) ? "tired" : "yawn", rr(3, 5)); }
      else if (boredStage == 3 && idle > boredAt[3]) { boredStage = 4; tempExpr("sleeping", 1e9f); }
    }
    if (tempUntil > 0 && t >= tempUntil) {
      tempUntil = 0;
      Pose p;
      Extra x;
      buildExpr(baseExpr, p, x);
      strengthen(p, x, baseStrength);
      humanize(p);
      apply(baseExpr, p, x, true);
    }
  }
  void tempExpr(const char* name, float secs) {
    const int e = libFind(name, strlen(name));
    if (e < 0) return;
    Pose p;
    Extra x;
    buildExpr(e, p, x);
    humanize(p);
    apply(e, p, x, true);
    tempUntil = t + secs;
  }

  // A fresh take: every time an expression starts it's a little different
  void humanize(Pose& p) {
    p.size *= 1 + rr(-0.04f, 0.05f);
    const float asym = rr(-0.035f, 0.035f);
    p.sizeL *= 1 + asym;
    p.sizeR *= 1 - asym;
    p.lidT = clampf(p.lidT + rr(-0.04f, 0.05f), 0, 0.95f);
    p.browA += rr(-4, 4);
    p.browY += rr(-0.08f, 0.08f);
    p.mW *= 1 + rr(-0.12f, 0.12f);
    p.mSmile = clampf(p.mSmile + rr(-0.1f, 0.1f), -1, 1);
    p.tilt += rr(-3, 3);
    p.lookX += rr(-0.12f, 0.12f);
    p.lookY += rr(-0.08f, 0.08f);
    if (p.bounceHz > 0) p.bounceHz *= 1 + rr(-0.1f, 0.1f);
    springK = rr(110, 190);
    springD = rr(0.45f, 0.7f);
  }
  void scheduleBlink() {
    const float every = (ext.blinkEvery > 0 ? ext.blinkEvery : 4) * (style == ST_SAVER ? 1.6f : 1);  // saver: fewer frames
    nextBlink = t + every * rr(0.4f, 1.6f);
    doubleBlink = chance(0.2f);
  }
  static void blend(Pose& a, const Pose& b, float k) {
    float* x = (float*)&a;
    const float* y = (const float*)&b;
    for (int i = 0; i < NP; i++) x[i] += (y[i] - x[i]) * k;
  }
  void apply(int e, const Pose& p, const Extra& x, bool fromLife = false) {
    const bool shift = fromLife && e == expr;
    expr = e;
    tgt = p;
    const bool newFx = x.fx1 != ext.fx1 || x.fx2 != ext.fx2;
    ext = x;
    pendingGlyph = x.glyph;
    if (stills) glyph = pendingGlyph;
    fxNext[0] = fxNext[1] = t + 0.3f;
    if (newFx) fxStart = t;
    if (!shift)
      for (int i = 0; i < MAXP; i++)
        if (parts[i].life > 0.3f) { parts[i].max = parts[i].max - parts[i].life + 0.3f; parts[i].life = 0.3f; }
    kickV = stills || shift ? 0 : rr(0.07f, 0.14f);
    setAt = t;
    if (!fromLife) {
      wake();
      idleSince = t;
      tempUntil = 0;
      boredStage = 0;
      for (int i = 0; i < 4; i++) boredAt[i] = (i == 0 ? rr(12, 30) : boredAt[i - 1] + rr(15, 45));
    }
    if (!stills) scheduleBlink();
    if (!stills && chance(0.3f)) reseedNoise();
  }

  // ---- drawing context ----
  struct Ctx {
    Canvas* cv;
    uint8_t st;
    StyleDef s;
    RGB eyeC, glowC, irisC, iris2C, under;  // under: the colour lines are drawn over (for their soft edge)
    float cx, cy, bl, sq, tiltR, pulse;
    float gRx, gRy;                         // Tinbot: the glass behind the shutters (half width, half height)
  } k;

  bool lightBg() const { return k.s.head == H_SKIN || k.st == ST_ANIME || k.s.light; }
  RGB lidCol() const { return k.s.head == H_FLAT || k.s.head == H_CAT || k.s.head == H_DOG || k.s.head == H_FOX ? k.s.bg : k.s.skin; }
  RGB inkCol() const {  // lines drawn on the face (closed eyes, glyph lines)
    if (lightBg()) return k.s.lash;
    return k.eyeC;
  }
  uint16_t fadeCol(const RGB& c, float a) { return to565(mixc(lidCol(), c, clampf(a, 0, 1))); }

  // halo: on the fast path, a slightly wider half-blended pass first = a soft edge
  void polyline(const float* xs, const float* ys, int n, float r, uint16_t c, bool halo = true) {
    Canvas& cv = *k.cv;
    if (cv.preferFast) {
      if (halo && r >= 1.3f) {
        const uint16_t h = to565(mixc(from565(c), k.under, 0.5f));
        for (int i = 1; i < n; i++) cv.lineFast(xs[i - 1], ys[i - 1], xs[i], ys[i], r + 0.7f, h);
      }
      for (int i = 1; i < n; i++) cv.lineFast(xs[i - 1], ys[i - 1], xs[i], ys[i], r, c);
      return;
    }
    for (int i = 1; i < n; i++) cv.line(xs[i - 1], ys[i - 1], xs[i], ys[i], r, c);
  }
  void curve(float x0, float y0, float x1, float y1, float x2, float y2, float r, uint16_t c, int n = 8, bool halo = true) {
    float xs[16], ys[16];
    if (n > 15) n = 15;
    for (int i = 0; i <= n; i++) {
      const float u = (float)i / n, a = (1 - u) * (1 - u), b = 2 * (1 - u) * u, d = u * u;
      xs[i] = a * x0 + b * x1 + d * x2;
      ys[i] = a * y0 + b * y1 + d * y2;
    }
    polyline(xs, ys, n + 1, r, c, halo);
  }
  void ring(float x, float y, float r, float th, uint16_t c) {
    float xs[25], ys[25];
    for (int i = 0; i <= 24; i++) { const float a = i * 2 * PI_F / 24; xs[i] = x + cosf(a) * r; ys[i] = y + sinf(a) * r; }
    polyline(xs, ys, 25, th, c);
  }
  void ringE(float x, float y, float rx, float ry, float th, uint16_t c, bool halo = true) {
    float xs[25], ys[25];
    for (int i = 0; i <= 24; i++) { const float a = i * 2 * PI_F / 24; xs[i] = x + cosf(a) * rx; ys[i] = y + sinf(a) * ry; }
    polyline(xs, ys, 25, th, c, halo);
  }
  void heart(float x, float y, float s, uint16_t c) {
    k.cv->circle(x - s * 0.5f, y - s * 0.2f, s * 0.56f, c);
    k.cv->circle(x + s * 0.5f, y - s * 0.2f, s * 0.56f, c);
    k.cv->tri(x - s * 1.04f, y - s * 0.02f, x + s * 1.04f, y - s * 0.02f, x, y + s * 1.1f, c);
  }
  void star4(float x, float y, float s, float thin, uint16_t c) {
    k.cv->tri(x - s * thin, y, x + s * thin, y, x, y - s, c);
    k.cv->tri(x - s * thin, y, x + s * thin, y, x, y + s, c);
    k.cv->tri(x, y - s * thin, x, y + s * thin, x - s, y, c);
    k.cv->tri(x, y - s * thin, x, y + s * thin, x + s, y, c);
  }
  void star5(float x, float y, float s, uint16_t c) {
    for (int i = 0; i < 5; i++) {
      const float a = -PI_F / 2 + i * 2 * PI_F / 5, a1 = a + PI_F / 5, a2 = a - PI_F / 5;
      k.cv->tri(x + cosf(a) * s, y + sinf(a) * s, x + cosf(a1) * s * 0.42f, y + sinf(a1) * s * 0.42f, x + cosf(a2) * s * 0.42f, y + sinf(a2) * s * 0.42f, c);
    }
    k.cv->circle(x, y, s * 0.42f, c);
  }
  void drop(float x, float y, float s, uint16_t c) {
    k.cv->circle(x, y, s, c);
    k.cv->tri(x - s * 0.9f, y - s * 0.2f, x + s * 0.9f, y - s * 0.2f, x, y - s * 2.3f, c);
  }
  void dollar(float x, float y, float s, uint16_t c, float th) {
    float xs[13], ys[13];
    for (int i = 0; i <= 12; i++) { const float a = PI_F * 0.25f + i * PI_F * 1.25f / 12; xs[i] = x + cosf(a) * s * 0.45f; ys[i] = y - s * 0.42f - sinf(a) * s * 0.42f; }
    polyline(xs, ys, 13, th, c);
    for (int i = 0; i <= 12; i++) { const float a = PI_F * 0.5f - i * PI_F * 1.25f / 12; xs[i] = x + cosf(a) * s * 0.45f; ys[i] = y + s * 0.42f - sinf(a) * s * 0.42f; }
    polyline(xs, ys, 13, th, c);
    k.cv->line(x, y - s * 1.05f, x, y + s * 1.05f, th * 0.8f, c);
  }

  // ---- the face ----
  void drawFace(Canvas& cv, uint8_t st) {
    k.cv = &cv;
    k.st = st;
    k.s = styleDef(st);
    if (k.s.head == H_SKIN) k.s.skin = mixc(k.s.bg, k.s.bg2, 0.4f);  // one flat skin tone (see drawHead)
    const Pose& p = shown;
    const RGB tintc = C(p.tr, p.tg, p.tb);
    const float ta = clampf(p.tint, 0, 1);
    RGB eyeBase = st == ST_EVE ? C(70, 170, 255) : (st == ST_MOCHI || st == ST_GALAXY || st == ST_NEBULA) ? C(255, 247, 238)
                : st == ST_OREO ? C(250, 242, 226) : (k.s.eye == E_PILL ? C(140, 235, 255) : C(235, 246, 255));
    if (st == ST_NOVA) eyeBase = C(40, 235, 255);
    else if (st == ST_MINT) eyeBase = C(90, 255, 190);
    else if (st == ST_SUNSET) {  // slowly through amber, coral, pink and back
      const float u = fmodf(t * 0.06f, 1.0f) * 3;
      const RGB pal[4] = {C(255, 170, 40), C(255, 100, 80), C(255, 80, 170), C(255, 170, 40)};
      const int i = (int)u;
      eyeBase = mixc(pal[i], pal[i + 1], u - i);
    }
    else if (st == ST_CHALK) eyeBase = C(236, 238, 228);
    else if (st == ST_LINE) eyeBase = C(240, 244, 255);
    else if (st == ST_TINBOT) eyeBase = C(255, 190, 70);
    else if (st == ST_COMIC || st == ST_CYCLOPS) eyeBase = C(255, 255, 250);
    else if (st == ST_EMBER) eyeBase = C(255, 110, 30);
    else if (st == ST_ROSE) eyeBase = C(255, 80, 170);
    else if (st == ST_HALO || st == ST_HALOLITE) eyeBase = C(210, 240, 255);
    else if (st == ST_AURORA) {  // slowly through green, teal, blue, violet and back
      const float u = fmodf(t * 0.07f, 1.0f) * 4;
      const RGB pal[5] = {C(60, 255, 150), C(40, 230, 230), C(70, 140, 255), C(190, 90, 255), C(60, 255, 150)};
      const int i = (int)u;
      eyeBase = mixc(pal[i], pal[i + 1], u - i);
    }
    k.eyeC = mixc(eyeBase, tintc, ta * k.s.tEye);
    const RGB glowBase = st == ST_EVE ? C(40, 120, 255) : (k.s.eye == E_PILL || k.s.eye == E_EVE) ? eyeBase : k.s.glow;
    k.glowC = mixc(glowBase, tintc, ta * k.s.tGlow);
    k.irisC = mixc(k.s.iris, tintc, ta * k.s.tIris);
    k.iris2C = mixc(k.s.iris2, tintc, ta * k.s.tIris * 0.7f);
    k.under = k.s.head == H_FLAT ? k.s.bg : k.s.skin;
    if (k.s.eye == E_PILL) { k.s.mouth = M_LINE; k.s.mouthIn = mulc(k.eyeC, 0.35f); }
    // body motion: breathing, hops (squash on landing, stretch in the air), shiver, sway
    const float breathe = sinf(t * 1.6f) * p.breathe;
    float hop = 0, sq = 0;
    if (p.bounce > 0.5f && p.bounceHz > 0.05f) {
      const float ph = fmodf(t * p.bounceHz, 1.0f);
      hop = p.bounce * sinf(ph * PI_F);
      if (ph < 0.12f) sq = (0.12f - ph) / 0.12f * 0.16f;
      else if (ph > 0.25f && ph < 0.6f) sq = -0.05f;
    }
    sq += kickV;
    // every expression moves a little: a jelly squish (more when excited), a soft bob (happy
    // moods bounce, sad ones sink slowly), a happy sway, plus the expression's own wobble / nod / pulse
    const float fam = k.s.family ? 1.0f : 0.5f;
    const float ar = clampf(ext.arousal, -1, 1), va = clampf(ext.valence, -1, 1);
    sq += (0.018f + 0.03f * fmaxf(0, ar)) * fam * sinf(t * (2.0f + 2.5f * fmaxf(0, ar)));
    sq += p.wobble * sinf(t * 7.5f);
    const float bobAmp = (va > 0 ? 1.2f + 2.2f * va * fmaxf(0.2f, ar + 0.5f) : 1.0f) * fam;
    const float bob = bobAmp * sinf(t * (va > 0 ? 2.2f + fmaxf(0, ar) : 0.9f)) + p.nod * sinf(t * 1.6f);
    k.pulse = 1 + p.pulse * sinf(t * 5.2f);
    const float swayHappy = fam * 2.5f * fmaxf(0, va) * fmaxf(0, ar + 0.3f) * sinf(t * 1.9f);
    const float jx = p.jitter > 0.05f ? p.jitter * sinf(t * 37.0f) : 0, jy = p.jitter > 0.05f ? p.jitter * cosf(t * 29.0f) : 0;
    float bl = 0;
    if (blinkT >= 0) {
      const float u = blinkT / blinkLen;
      bl = u < 0.4f ? u / 0.4f : 1 - (u - 0.4f) / 0.6f;
      bl = bl * bl * (3 - 2 * bl) * blinkDepth;
    }
    k.bl = bl;
    k.sq = sq;
    k.cx = W / 2 + p.lookX * 16 + jx;
    k.cy = k.s.y + p.y + p.lookY * 10 - hop + breathe * 1.2f + jy + bob;
    k.tiltR = (p.tilt + p.sway * sinf(t * 1.3f) + swayHappy) * PI_F / 180;
    const float gap = k.s.gap * p.gap;

    drawHead(p);  // (clears the screen)
    drawAmbient();  // desk mode: the weather, faint, behind the features
    if (k.s.deco != D_NONE) drawDeco(p);
    if (sc.id) drawScene(true);
    drawPuff(p);  // cheeks puffed out: lower on the face, the eyes sit over them
    if (st == ST_HALO) {  // a little angel halo floating above, bobbing gently
      const float hy = k.cy - k.s.ry - 56 + 3 * sinf(t * 1.8f);
      ringE(k.cx, hy, 56, 9, 5, to565(C(70, 55, 20)), false);
      ringE(k.cx, hy, 56, 9, 2.2f, to565(C(255, 214, 110)));
    }
    if (st == ST_EMBER)
      for (int i = 0; i < 6; i++) {  // embers drifting up
        const float ph = fmodf(t * 0.25f + i * 0.17f, 1.0f);
        const float x = k.cx + (i - 2.5f) * 40 + sinf(t * 2 + i) * 8, y = k.cy + 70 - ph * 150;
        k.cv->circleFast(x, y, 1.5f + (i & 1), to565(mixc(C(0, 0, 0), C(255, 140, 40), 1 - ph)));
      }
    if (glyph == G_LOADING) { drawLoading(); drawEffects(p); return; }
    if (k.s.single) {
      drawEye(2, k.cx, k.cy, p);
    } else {
      for (int side = 0; side < 2; side++) {
        const float sgn = side == 0 ? -1.0f : 1.0f;
        const float ox = sgn * gap / 2, oy = side == 0 ? p.dyL : p.dyR;
        drawEye(side, k.cx + ox * cosf(k.tiltR) - oy * sinf(k.tiltR), k.cy + ox * sinf(k.tiltR) + oy * cosf(k.tiltR), p);
      }
    }
    if (glyph == G_SHADES) drawShades(p);
    drawAccessory(p, gap);
    if (glyph != G_SHADES) drawBrows(p, gap);
    if (fmaxf(p.blush, k.s.blushMin) > 0.06f && p.puff < 0.3f) {  // (puffed cheeks blush by themselves)
      Pose pb = p;
      pb.blush = fmaxf(p.blush, k.s.blushMin);
      drawBlush(pb, gap);
      if (k.s.deco == D_BEAR)  // freckles
        for (int side = 0; side < 2; side++) {
          float rx, ry;
          eyeSize(side, p, rx, ry);
          const float bx = k.cx + (side == 0 ? -1 : 1) * (gap / 2 + 14), by = k.cy + ry + 14;
          for (int i = -1; i <= 1; i++) k.cv->circleFast(bx + i * 7, by - 1 + (i == 0 ? -3 : 0), 1.3f, to565(mixc(k.s.iris2, k.s.bg, 0.3f)));
        }
    }
    drawNose(p);
    if (k.s.head == H_CAT || k.s.head == H_DOG || k.s.head == H_FOX) k.under = k.s.muzzle;
    drawMouth(p);
    k.under = lidCol();
    drawEffects(p);
    if (sc.id) drawScene(false);
  }

  // background: flat, a skin gradient, or a cat / puppy head with ears and muzzle
  void drawHead(const Pose& p) {
    Canvas& cv = *k.cv;
    const StyleDef& s = k.s;
    if (s.head == H_FLAT) { cv.clear(to565(s.bg)); return; }
    if (s.head == H_SKIN) { cv.clear(to565(s.skin)); return; }  // flat: the lids are the same skin
    // OLED black: the pet's features glow on their own (a face on a screen, not a pet in a box)
    cv.clear(to565(s.bg));
    const float perk = clampf(p.browY * 0.8f - (p.browA < 0 ? 0.3f : 0) + (p.mSmile > 0.6f ? 0.15f : 0), -1, 1);
    const float back = clampf(p.browA / 30, -1, 1);
    const float sad = clampf(-ext.valence, 0, 1) * clampf(0.5f - ext.arousal, 0, 1) * 2;
    const float wig = sinf(t * (2 + 2 * fmaxf(0, ext.arousal))) * (1 + 2 * fmaxf(0, ext.arousal));
    const float cx = k.cx, top = k.cy - s.ry - 30;
    if (s.head == H_OWL) {  // the whole screen is the owl: ear tufts, two cream facial discs, feather marks
      for (int side = 0; side < 2; side++) {
        const float sg = side == 0 ? -1.0f : 1.0f;
        const float tx = cx + sg * (138 + sad * 22), ty = top - 44 + sad * 16 - perk * 8 + (side ? wig : -wig) * 1.5f;
        cv.tri(cx + sg * 58, top + 14, cx + sg * 150, top + 34, tx, ty, to565(s.deco1));
        cv.tri(cx + sg * 84, top + 16, cx + sg * 136, top + 26, cx + sg * 126 + (tx - cx - sg * 126) * 0.7f, top + (ty - top) * 0.7f, to565(s.deco2));
      }
      const float gap = s.gap * p.gap, R = s.rx * 1.72f;
      for (int side = 0; side < 2; side++) {  // the discs follow the eyes (tilt, gaze)
        const float sg = side == 0 ? -1.0f : 1.0f, ox = sg * gap / 2, oy = side == 0 ? p.dyL : p.dyR;
        const float ex = cx + ox * cosf(k.tiltR) - oy * sinf(k.tiltR), ey = k.cy + ox * sinf(k.tiltR) + oy * cosf(k.tiltR);
        cv.ellipse(ex, ey + 2, R + 6, R + 4, to565(s.skin2));
        cv.ellipse(ex, ey, R, R - 2, to565(s.skin));
      }
      const uint16_t fm = to565(mixc(s.bg, s.deco2, 0.8f));
      for (int i = 0; i < 5; i++) {  // little "v" feather marks on the chest
        const float x = cx + (i - 2) * 30 + (i & 1 ? 0 : 0), y = k.cy + s.ry + 74 + (i & 1) * 12;
        cv.lineFast(x - 6, y - 4, x, y + 2, 1.5f, fm);
        cv.lineFast(x, y + 2, x + 6, y - 4, 1.5f, fm);
      }
      return;
    }
    if (s.head == H_FOX) {  // the whole screen is the fox: big ears with dark tips, white cheeks and muzzle
      for (int side = 0; side < 2; side++) {
        const float sg = side == 0 ? -1.0f : 1.0f;
        const float tx = cx + sg * (124 + sad * 30), ty = top - 60 + sad * 20 - perk * 8 + (side ? wig : -wig) * 1.5f;
        const float ax = cx + sg * 40, ay = top - 10, bx = cx + sg * 170, by = top + 24;
        cv.tri(ax, ay, bx, by, tx, ty, to565(s.deco1));
        cv.tri(ax + sg * 16, ay + 2, bx - sg * 26, by - 8, tx + (ax + bx - 2 * tx) * 0.12f, ty + (ay + by - 2 * ty) * 0.12f, to565(mixc(s.muzzle, C(255, 180, 170), 0.35f)));
        cv.tri(tx + (ax - tx) * 0.3f, ty + (ay - ty) * 0.3f, tx + (bx - tx) * 0.3f, ty + (by - ty) * 0.3f, tx, ty, to565(s.deco2));
      }
      const uint16_t wc = to565(s.muzzle);
      for (int side = 0; side < 2; side++) {  // cheek fluff, with two points of fur
        const float sg = side == 0 ? -1.0f : 1.0f;
        const float x0 = cx + sg * 172, y0 = k.cy + s.ry + 18;
        cv.tri(x0, y0, x0, H + 4, cx + sg * 52, H + 4, wc);
        cv.tri(x0, y0 - 2, x0 - sg * 30, y0 + 22, x0 - sg * 4, y0 + 30, wc);
        cv.tri(x0 - sg * 22, y0 + 20, x0 - sg * 58, y0 + 46, x0 - sg * 20, y0 + 52, wc);
      }
      cv.ellipse(cx + p.mX * 20, k.cy + s.ry + 86, 90, 72, wc);  // the muzzle
      return;
    }
    if (s.head == H_CAT) {
      for (int side = 0; side < 2; side++) {  // ears: glowing outlines, pink inside; back when cross, up when alert
        const float sg = side == 0 ? -1.0f : 1.0f;
        const float bx = cx + sg * 96, by = top + 20;
        const float a = (-90 + sg * (22 + back * 30 + sad * 30 - perk * 10) + (side ? wig : -wig) * 2) * PI_F / 180;
        const float len = 44 + perk * 6 - sad * 10;
        const float tx = bx + cosf(a) * len, ty = by + sinf(a) * len;
        const float lx = bx - 32, rx2 = bx + 32;
        cv.tri(bx - 18, by, bx + 18, by, bx + (tx - bx) * 0.68f, by + (ty - by) * 0.68f, to565(mulc(s.deco2, 0.8f)));
        const uint16_t lc = to565(s.deco1);
        cv.lineFast(lx, by + 4, tx, ty, 2.6f, to565(mulc(s.deco1, 0.35f)));
        cv.lineFast(rx2, by + 4, tx, ty, 2.6f, to565(mulc(s.deco1, 0.35f)));
        cv.lineFast(lx, by + 4, tx, ty, 1.6f, lc);
        cv.lineFast(rx2, by + 4, tx, ty, 1.6f, lc);
      }
      for (int i = -1; i <= 1; i++)  // three little tabby marks on the forehead
        cv.lineFast(cx + i * 14, top + 2 - fabsf((float)i) * 4, cx + i * 14 + i * 2, top + 16 - fabsf((float)i) * 4, 2.0f, to565(mulc(s.deco1, 0.8f)));
    } else {  // puppy: floppy ears hanging at the sides, drawn as soft glowing shapes
      const float lift = perk * 14 - sad * 16;
      for (int side = 0; side < 2; side++) {
        const float sg = side == 0 ? -1.0f : 1.0f;
        const float ex = cx + sg * (118 + lift * 0.3f), ey = top + 44 - lift + (side ? wig : -wig);
        cv.ellipse(ex, ey, 26, 52, to565(s.deco1));
        cv.ellipse(ex - sg * 2, ey + 2, 22, 47, to565(s.deco2));
      }
    }
  }

  // Signature details of the Mochi family; they react to the mood like ears / a sprout would
  void drawDeco(const Pose& p) {
    Canvas& cv = *k.cv;
    const StyleDef& s = k.s;
    const float perk = clampf(p.browY * 0.8f - (p.browA < 0 ? 0.35f : 0) + (p.mSmile > 0.6f ? 0.2f : 0) + ext.arousal * 0.2f, -1, 1);
    const float sad = clampf(-ext.valence, 0, 1) * clampf(0.5f - ext.arousal, 0, 1) * 2;  // droop
    const float wig = sinf(t * (2 + 2 * fmaxf(0, ext.arousal))) * (1 + 2 * fmaxf(0, ext.arousal));
    const float cx = k.cx, top = k.cy - s.ry - 36;
    switch (s.deco) {
      case D_STARS: {  // a twinkling starfield, and a shooting star now and then
        uint32_t h = 2166136261u;
        for (int i = 0; i < 22; i++) {
          h = (h ^ (uint32_t)i) * 16777619u;
          const float x = (h % 320), y = ((h >> 9) % 240), ph = ((h >> 17) % 628) / 100.0f, rt = 1 + ((h >> 5) % 30) / 10.0f;
          const float b = 0.35f + 0.65f * (0.5f + 0.5f * sinf(t * rt + ph));
          const float sz = 0.8f + ((h >> 13) % 3) * 0.6f;
          if (sz > 1.5f && b > 0.8f) star4(x, y, sz * 2.2f * b, 0.22f, to565(mixc(s.bg, s.deco1, b)));
          else cv.circleFast(x, y, sz, to565(mixc(s.bg, i & 1 ? s.deco1 : s.deco2, b)));
        }
        const float sh = fmodf(t, 9.0f);
        if (sh < 0.8f) {
          const float u = sh / 0.8f, x = 40 + u * 240, y = 20 + u * 60;
          cv.lineFast(x - 26, y - 7, x, y, 1.2f, to565(mixc(s.bg, s.deco1, 1 - u)));
          cv.circleFast(x, y, 2, to565(s.deco1));
        }
        break;
      }
      case D_BUNNY: {  // long ears (capsules) that droop when sad, perk up and wiggle when happy; petals
        for (int side = 0; side < 2; side++) {
          const float sg = side == 0 ? -1.0f : 1.0f;
          const float bx = cx + sg * 58, by = top + 26;
          const float a = (-90 + sg * (14 + sad * 60 - perk * 8) + (side ? wig : -wig) * 3) * PI_F / 180;
          const float len = 62 + perk * 8 - sad * 10;
          const float tx = bx + cosf(a) * len, ty = by + sinf(a) * len;
          cv.lineFast(bx, by, tx, ty, 17, to565(s.deco1));
          cv.lineFast(bx + cosf(a) * 8, by + sinf(a) * 8, bx + cosf(a) * (len - 4), by + sinf(a) * (len - 4), 8, to565(s.deco2));
        }
        for (int i = 0; i < 5; i++) {  // falling petals
          const float ph = fmodf(t * 0.12f + i * 0.2f, 1.0f);
          const float x = fmodf(i * 67 + 30 + sinf(t * 1.3f + i) * 18 + ph * 40, 320), y = -10 + ph * 260;
          const float r = 3.5f + (i % 2);
          cv.ellipse(x, y, r * (0.6f + 0.4f * fabsf(sinf(t * 2 + i))), r * 0.7f, to565(mixc(s.bg, s.deco2, 0.9f)));
        }
        break;
      }
      case D_SPROUT: {  // a little sprout on top: sways, wilts when sad, stands up when happy
        const float a = (-90 + wig * 5 + p.tilt * 0.5f + sad * 55) * PI_F / 180;
        const float bx = cx, by = top + 18, len = 30 + perk * 5 - sad * 8;
        const float tx = bx + cosf(a) * len, ty = by + sinf(a) * len;
        cv.lineFast(bx, by, tx, ty, 2.4f, to565(s.deco2));
        for (int side = 0; side < 2; side++) {
          const float sg = side == 0 ? -1.0f : 1.0f;
          const float la = a + sg * (0.9f - sad * 0.5f) + wig * 0.04f;
          const float lx = tx + cosf(la) * 13, ly = ty + sinf(la) * 13;
          cv.lineFast(tx, ty, lx, ly, 7, to565(s.deco1));
          cv.lineFast(tx + cosf(la) * 3, ty + sinf(la) * 3, lx, ly, 1.2f, to565(s.deco2));
        }
        break;
      }
      case D_KITTY: {  // rounded kitty ears in the top corners
        for (int side = 0; side < 2; side++) {
          const float sg = side == 0 ? -1.0f : 1.0f;
          const float bx = cx + sg * 104, by = top + 34;
          const float lean = sg * (18 + sad * 40 - perk * 6) + (side ? wig : -wig) * 2;
          const float a = (-90 + lean) * PI_F / 180, len = 54 + perk * 6 - sad * 8;
          const float tx = bx + cosf(a) * len, ty = by + sinf(a) * len;
          cv.tri(bx - 34, by + 10, bx + 34, by + 10, tx, ty, to565(s.deco1));
          cv.circle(tx, ty + 4, 7, to565(s.deco1));
          cv.tri(bx - 18, by + 4, bx + 18, by + 4, bx + (tx - bx) * 0.7f, by + (ty - by) * 0.7f, to565(s.deco2));
        }
        break;
      }
      case D_CHALK: {  // a slate board: faint wiped smudges, two doodles, a wooden ledge with chalk and a duster
        cv.ellipse(36, 22, 74, 26, to565(mixc(s.bg, s.deco1, 0.06f)));
        cv.ellipse(70, 14, 50, 14, to565(mixc(s.bg, s.deco1, 0.09f)));
        cv.ellipse(306, 222, 52, 22, to565(mixc(s.bg, s.deco1, 0.05f)));
        star5(26, 200, 8 + 0.6f * sinf(t * 2), to565(mixc(s.bg, s.deco2, 0.75f)));
        heart(296, 198, 5.5f, to565(mixc(s.bg, s.blush, 0.7f)));
        cv.rrect(0, 229, W, 11, 0, to565(C(120, 82, 50)));
        cv.rrect(0, 229, W, 2, 0, to565(C(160, 116, 74)));
        cv.rrect(204, 222, 32, 7, 3, to565(s.deco1));
        cv.rrect(250, 219, 40, 10, 2, to565(C(96, 100, 112)));
        cv.rrect(250, 216, 40, 5, 2, to565(C(150, 110, 70)));
        break;
      }
      case D_COMIC: {  // halftone dots fading in from two corners (none where the eyelids reach)
        const uint16_t dc = to565(s.deco1);
        for (int c = 0; c < 2; c++) {
          const float ox = c ? W : 0, oy = c ? H : 0;
          for (int j = 0; j < 9; j++)
            for (int i = 0; i < 9; i++) {
              const float x = c ? W - 6 - i * 14 - (j & 1) * 7 : 6 + i * 14 + (j & 1) * 7, y = c ? H - 6 - j * 14 : 6 + j * 14;
              const float d = sqrtf((x - ox) * (x - ox) + (y - oy) * (y - oy)), r = 5.2f * (1 - d / 118);
              if (r < 1 || (x > 40 && x < 280 && y > 44 && y < 168)) continue;
              cv.circleFast(x, y, r, dc);
            }
        }
        break;
      }
      case D_HORNS: {  // two little horns peeking in at the top, and a few darker spots on the skin
        for (int side = 0; side < 2; side++) {
          const float sg = side == 0 ? -1.0f : 1.0f, bx = cx + sg * 66, by = 30 + (k.cy - s.y) * 0.8f;
          const float tx = bx + sg * (12 + (side ? wig : -wig)), ty = by - 40 - perk * 4 + sad * 6;
          cv.ellipse(bx, by + 2, 19, 7, to565(s.skin2));
          cv.tri(bx - 16, by, bx + 16, by, tx, ty, to565(s.deco1));
          cv.circle(tx - sg * 1.5f, ty + 3, 4, to565(s.deco1));
          cv.tri(bx - 16, by, bx + 16, by, bx + (tx - bx) * 0.3f, by - 12, to565(mixc(s.deco1, s.skin2, 0.35f)));
        }
        const uint16_t sc2 = to565(mixc(s.bg, s.deco2, 0.7f));
        const float sp[6][3] = {{30, 52, 8}, {52, 30, 4}, {288, 64, 7}, {300, 40, 3.5f}, {24, 206, 6}, {298, 200, 7}};
        for (int i = 0; i < 6; i++) cv.circleFast(sp[i][0], sp[i][1], sp[i][2], sc2);
        break;
      }
      case D_PAINT: {  // watercolour washes in two corners and a few splashes
        cv.ellipse(10, 6, 92, 46, to565(mixc(s.bg, s.deco1, 0.22f)));
        cv.ellipse(40, 0, 60, 30, to565(mixc(s.bg, s.deco1, 0.34f)));
        cv.ellipse(0, 30, 30, 22, to565(mixc(s.bg, s.deco1, 0.44f)));
        cv.ellipse(312, 236, 96, 48, to565(mixc(s.bg, s.deco2, 0.22f)));
        cv.ellipse(290, 244, 64, 30, to565(mixc(s.bg, s.deco2, 0.34f)));
        cv.ellipse(320, 210, 26, 22, to565(mixc(s.bg, s.deco2, 0.46f)));
        const float sp[6][3] = {{118, 22, 3}, {132, 14, 1.8f}, {300, 150, 2.5f}, {22, 160, 3.2f}, {34, 176, 1.8f}, {200, 228, 2.2f}};
        for (int i = 0; i < 6; i++) cv.circleFast(sp[i][0], sp[i][1], sp[i][2], to565(mixc(s.bg, i & 2 ? s.deco2 : s.deco1, 0.75f)));
        break;
      }
      case D_HAT: {  // a knitted beanie: v-stitch rows, a ribbed cuff, a pompom that bobs (slips down when sad)
        const float hy = (k.cy - s.y) * 0.6f + sad * 6;
        const RGB knit = s.deco1;
        cv.rrect(-20, -40 + hy, W + 40, 86, 30, to565(knit));
        const uint16_t st2 = to565(mulc(knit, 0.82f));
        for (int row = 0; row < 2; row++)
          for (int i = 0; i < 21; i++) {
            const float x = i * 16 + row * 8 - 4, y = 6 + row * 13 + hy;
            cv.tri(x - 6, y - 4, x - 2, y - 4, x, y + 4, st2);
            cv.tri(x + 6, y - 4, x + 2, y - 4, x, y + 4, st2);
          }
        cv.rrect(-10, 30 + hy, W + 20, 26, 12, to565(mulc(knit, 0.9f)));
        const uint16_t rib = to565(mulc(knit, 0.76f));
        for (int x = 4; x < W; x += 9) cv.rrect(x, 33 + hy, 3, 20, 0, rib);
        const float px = cx + wig * 1.5f, py = -4 + hy + sinf(t * 2.2f) * 1.5f;
        cv.circle(px, py, 27, to565(mixc(s.deco2, knit, 0.12f)));
        cv.circle(px - 2, py - 2, 24, to565(s.deco2));
        for (int i = 0; i < 6; i++) cv.circleFast(px - 14 + i * 6, py + 6 + (i & 1) * 5, 2, to565(mixc(s.deco2, knit, 0.18f)));
        break;
      }
      case D_LCD: {  // a handheld's status bar: a heart and a 4-box meter that fills with the mood
        const uint16_t on = to565(mixc(s.bg, s.iris, 0.6f)), off = to565(mixc(s.bg, s.iris, 0.3f));
        heart(19, 17, 6, on);
        const int fill = (int)lroundf(clampf((ext.valence + 1) * 2, 0, 4));
        for (int i = 0; i < 4; i++) cv.rrect(34 + i * 12, 12, 8, 9, 0, i < fill ? on : off);
        break;
      }
      case D_TIN: {  // a steel plate: lighter at the top, two seams, rivets, three little status lights
        cv.rrect(0, 0, W, 26, 0, to565(mixc(s.bg, s.deco1, 0.35f)));
        cv.rrect(0, 26, W, 1, 0, to565(s.deco2));
        cv.rrect(0, 214, W, 1, 0, to565(s.deco2));
        cv.rrect(0, 215, W, 25, 0, to565(mixc(s.bg, s.deco2, 0.25f)));
        const float rv2[6][2] = {{12, 13}, {308, 13}, {12, 227}, {308, 227}, {12, 120}, {308, 120}};
        for (int i = 0; i < 6; i++) {
          cv.circle(rv2[i][0], rv2[i][1], 5.5f, to565(s.deco2));
          cv.circle(rv2[i][0] - 0.5f, rv2[i][1] - 0.5f, 4, to565(s.deco1));
          cv.circleFast(rv2[i][0] - 1.5f, rv2[i][1] - 1.5f, 1.2f, to565(C(255, 255, 255)));
        }
        const RGB led[3] = {C(90, 230, 120), C(255, 190, 60), C(255, 80, 90)};
        for (int i = 0; i < 3; i++) {
          const float on = 0.35f + 0.65f * (0.5f + 0.5f * sinf(t * (1.3f + i * 0.7f) + i * 2));
          cv.circle(140 + i * 20, 13, 4, to565(s.deco2));
          cv.circle(140 + i * 20, 13, 3, to565(mixc(mulc(led[i], 0.35f), led[i], on)));
        }
        break;
      }
      case D_BEAR: {  // round bear ears that bob
        for (int side = 0; side < 2; side++) {
          const float sg = side == 0 ? -1.0f : 1.0f;
          const float x = cx + sg * (100 + sad * 14), y = top + 8 + sad * 16 - perk * 5 + (side ? wig : -wig);
          cv.circle(x, y, 30, to565(s.deco1));
          cv.circle(x, y + 3, 17, to565(s.deco2));
        }
        break;
      }
      default: break;
    }
  }

  void eyeSize(int side, const Pose& p, float& rx, float& ry) {
    const float s = p.size * k.pulse * (side == 0 ? p.sizeL : side == 1 ? p.sizeR : (p.sizeL + p.sizeR) * 0.5f);
    const float persp = side == 2 ? 1 : 1 + (side == 0 ? -1 : 1) * p.lookX * 0.06f;
    rx = k.s.rx * s * persp * (1 - p.squash * 0.3f) * (1 + k.sq * 0.6f);
    ry = k.s.ry * s * persp * (1 + p.squash * 0.3f) * (1 - k.sq);
  }

  // side: 0 left, 1 right, 2 the single eye (MONO)
  void drawEye(int side, float ex, float ey, const Pose& p) {
    float rx, ry;
    eyeSize(side, p, rx, ry);
    const float close = clampf(side == 0 ? p.closeL : side == 1 ? p.closeR : (p.closeL + p.closeR) * 0.5f, 0, 1);
    const float bl = k.bl;
    Canvas& cv = *k.cv;
    const float gs = 1 - 0.85f * bl;  // glyphs shrink through a blink (they swap while small)
    const RGB tintRGB = C(p.tr, p.tg, p.tb);
    // Tinbot: the eye is a lamp behind dark glass in a chrome bezel - glyphs show on the glass
    const bool tin = k.s.eye == E_TIN;
    const bool lb = lightBg() && !tin;
    const uint16_t ink = to565(tin ? k.eyeC : inkCol());
    if (tin && glyph != G_SHADES) tinFrame(ex, ey, rx, ry);
    switch (glyph) {
      case G_HEART: {
        const float pulse = 1 + 0.06f * sinf(t * 9);
        if (!lb) heart(ex, ey, rx * 0.95f * gs * pulse * 1.12f, to565(mixc(k.s.bg, tintRGB, 0.35f)));
        heart(ex, ey, rx * 0.95f * gs * pulse, to565(tintRGB));
        cv.circle(ex - rx * 0.45f, ey - rx * 0.35f, rx * 0.14f * gs, to565(C(255, 235, 245)));
        return;
      }
      case G_HEARTBEAT: {
        const float beat = fmodf(t * 1.3f, 1.0f), pulse = 1 + (beat < 0.15f ? sinf(beat / 0.15f * PI_F) * 0.12f : 0);
        const float s = rx * 0.95f * gs * pulse;
        heart(ex, ey, s, to565(tintRGB));
        heart(ex, ey, s * 0.8f, to565(lidCol()));
        float xs[7] = {ex - s * 0.8f, ex - s * 0.3f, ex - s * 0.15f, ex, ex + s * 0.15f, ex + s * 0.3f, ex + s * 0.8f};
        float ys[7] = {ey, ey, ey - s * 0.45f, ey + s * 0.35f, ey - s * 0.2f, ey, ey};
        polyline(xs, ys, 7, 2.2f, to565(tintRGB));
        return;
      }
      case G_SPIRAL: {
        float xs[40], ys[40];
        const float rot = t * 5 * (side == 0 ? 1 : -1);
        for (int i = 0; i < 40; i++) {
          const float u = i / 39.0f, a = rot + u * 3 * 2 * PI_F, r = rx * 0.9f * u * gs;
          xs[i] = ex + cosf(a) * r;
          ys[i] = ey + sinf(a) * r;
        }
        polyline(xs, ys, 40, 2.4f, k.s.eye == E_CAT || k.s.eye == E_DOG ? to565(k.irisC) : ink);
        return;
      }
      case G_X: {
        const float s = rx * 0.62f * gs;
        const uint16_t c = to565(lb ? mulc(tintRGB, 0.6f) : mixc(C(235, 240, 255), tintRGB, clampf(p.tint, 0, 1)));
        cv.line(ex - s, ey - s, ex + s, ey + s, 5.5f, c);
        cv.line(ex - s, ey + s, ex + s, ey - s, 5.5f, c);
        return;
      }
      case G_CHEVRON: {
        // squeeze: screwed shut - flatter, wider, bolder, with creases at the outer corners
        const float q = clampf(p.squeeze, 0, 1);
        const float s = rx * 0.6f * gs, d = side == 1 ? -1 : 1;  // >  <
        const float hy = s * (0.75f - 0.3f * q), w0 = s * (0.7f + 0.3f * q), th = 5 + 1.5f * q;
        cv.line(ex - w0 * d, ey - hy, ex + s * 0.6f * d, ey, th, ink);
        cv.line(ex + s * 0.6f * d, ey, ex - w0 * d, ey + hy, th, ink);
        if (q > 0.2f && side != 2) {
          const float ox = ex - (w0 + 8) * d;
          const uint16_t cr = to565(mixc(lidCol(), inkCol(), 0.85f * q));
          for (int i = -1; i <= 1; i++) cv.lineFast(ox, ey + i * hy * 0.8f, ox - d * (i == 0 ? 10 : 8), ey + i * hy * 1.45f, 1.5f, cr);
        }
        return;
      }
      case G_DOT: cv.circle(ex, ey, rx * 0.22f * gs + 2, lb ? to565(k.s.pupil) : ink); return;
      case G_LINE: cv.line(ex - rx * 0.7f, ey + 4, ex + rx * 0.7f, ey + 4, 4, ink); return;
      case G_DOLLAR: {
        if (!lb) ring(ex, ey, rx * 0.95f, 3, to565(mixc(k.s.bg, C(255, 215, 60), 0.35f)));
        dollar(ex, ey, rx * 0.72f * gs, to565(lb ? C(40, 150, 60) : C(255, 215, 60)), 4);
        return;
      }
      case G_TARGET: {
        const uint16_t c = to565(tintRGB.r > 250 && tintRGB.g > 250 ? (lb ? C(30, 140, 70) : C(90, 255, 140)) : tintRGB);
        ring(ex, ey, rx * 0.8f * gs, 2.4f, c);
        ring(ex, ey, rx * 0.35f * gs, 2, c);
        cv.line(ex - rx, ey, ex - rx * 0.45f, ey, 2, c);
        cv.line(ex + rx * 0.45f, ey, ex + rx, ey, 2, c);
        cv.line(ex, ey - rx, ex, ey - rx * 0.45f, 2, c);
        cv.line(ex, ey + rx * 0.45f, ex, ey + rx, 2, c);
        return;
      }
      case G_SHADES: return;
      default: break;
    }
    // closed (wink, ^ ^, calm) - or fully blinked in the squash styles; small bean eyes whose
    // lids leave almost nothing showing are drawn as a closed "u" too
    // (the glowing EVE eyes too: on black a sliver under a heavy lid is just a few stray pixels)
    const bool beanShut = ((k.s.eye == E_BEAN || k.s.eye == E_PAINT) && (p.lidT + p.lidB * 0.95f > 0.72f)) || (k.s.eye == E_EVE && (p.lidT + p.lidB * 0.95f > 0.8f));
    if (close > 0.55f || beanShut || (!k.s.lidBlink && bl > 0.9f && glyph != G_TEARY)) {
      const float cd = close > 0.55f ? p.closeCurve : beanShut ? -0.8f : -0.15f;
      const float w = rx * 0.85f, hg = rx * 0.42f * cd;
      if (k.s.eye == E_NEON || k.s.eye == E_EVE) curve(ex - w, ey + hg * 0.4f, ex, ey - hg * 1.4f, ex + w, ey + hg * 0.4f, 6, to565(mulc(k.glowC, 0.45f)), 8, false);
      if (tin) cv.rrect(ex - rx, ey - ry, 2 * rx, 2 * ry, 12, to565(tinShutter()));  // the shutter down
      curve(ex - w, ey + hg * 0.4f, ex, ey - hg * 1.4f, ex + w, ey + hg * 0.4f, k.st == ST_REAL ? 2.6f : k.s.lineW > 0 ? k.s.lineW : 3.4f, tin ? to565(k.s.lash) : ink);
      return;
    }
    const float openness = 1 - clampf(close / 0.55f, 0, 1) * 0.6f;
    float ry2 = ry * openness;
    if (!k.s.lidBlink) ry2 *= 1 - 0.93f * bl;
    if (ry2 < 3) ry2 = 3;
    const float rxb = rx * (1 + (!k.s.lidBlink ? bl * 0.1f : 0));
    const bool teary = glyph == G_TEARY;
    const float pr = rx * 0.62f * p.pupil * (1 + 0.28f * p.dilate) * (teary ? 1.25f : 1);
    const float maxOff = fmaxf(0, fminf(rxb, ry2) - pr * 0.9f);
    const float inward = side == 0 ? 1.0f : side == 1 ? -1.0f : 0;
    const float lx = clampf(p.lookX + inward * p.cross, -1, 1);
    const float px = ex + lx * maxOff * 0.95f, py = ey + clampf(p.lookY, -1, 1) * maxOff * 0.8f;
    drawEyeBody(side, ex, ey, rx, ry, rxb, ry2, px, py, pr, p, teary);
    if (k.s.eye == E_BEAN)  // bean eyes are smaller than the eye area: the lids close on the bean itself
      drawLids(side, ex + (px - ex) * 0.4f, ey + (py - ey) * 0.4f, rxb * 0.66f + 3, ry2 * 0.8f + 3, p);
    else if (k.s.eye == E_PAINT)  // (and the wash bleeding around it)
      drawLids(side, ex + (px - ex) * 0.4f, ey + (py - ey) * 0.4f, rxb * 0.74f + 5, ry2 * 0.86f + 4, p);
    else
      drawLids(side, ex, ey, rxb, ry2, p);
    if (teary && ry2 > 10 && k.s.eye != E_PILL && k.s.eye != E_EVE) cv.ellipse(ex, ey + ry2 * 0.88f, rxb * 0.7f, ry2 * 0.14f, to565(C(170, 215, 255)));
  }

  void drawEyeBody(int side, float ex, float ey, float rx, float ry, float rxb, float ry2, float px, float py, float pr, const Pose& p, bool teary) {
    Canvas& cv = *k.cv;
    const StyleDef& s = k.s;
    switch (s.eye) {
      case E_NEON: {
        for (int g = 3; g >= 1; g--) cv.ellipse(ex, ey, rxb + g * 4.5f, ry2 + g * 4.5f, to565(mulc(k.glowC, 0.13f * (4 - g))));
        cv.ellipse(ex, ey, rxb + 2.5f, ry2 + 2.5f, to565(mixc(k.glowC, k.eyeC, 0.5f)));
        cv.ellipse(ex, ey, rxb, ry2, to565(k.eyeC));
        if (ry2 > 8) {
          const float pr2 = fminf(pr, ry2 * 0.95f);
          cv.circle(px, py, pr2, to565(k.irisC));
          cv.circle(px, py, pr2 * 0.8f, to565(s.pupil));
          highlights(px, py, pr2, p, glyph == G_STAR, teary);
        }
        break;
      }
      case E_PILL: {
        const float r = 3 + (fminf(rxb, ry2) - 3) * 0.6f;
        for (int g = 2; g >= 1; g--)
          cv.rrect(ex - rxb - g * 4.5f, ey - ry2 - g * 4.5f, 2 * rxb + g * 9, 2 * ry2 + g * 9, r + g * 4, to565(mulc(k.eyeC, 0.12f * (3 - g) * s.glowAmt)));
        cv.rrect(ex - rxb, ey - ry2, 2 * rxb, 2 * ry2, r, to565(k.eyeC));
        if (ry2 > 10) {
          if (glyph == G_STAR) star4(px, py, rx * 0.42f, 0.2f, to565(C(255, 255, 255)));
          else cv.circle(ex + (side == 0 ? -1 : 1) * rxb * 0.35f - p.lookX * 3, ey - ry2 * 0.45f, rxb * 0.16f, to565(C(255, 255, 255)));
          if (teary) cv.ellipse(ex, ey + ry2 * 0.62f, rxb * 0.8f, ry2 * 0.22f, to565(C(200, 235, 255)));
        }
        break;
      }
      case E_EVE: {
        const float gx = (px - ex) * 0.25f, gy = (py - ey) * 0.25f;  // the bright core follows the gaze a little
        if (k.st == ST_NOVA) {  // a rounded-square visor eye with scanlines
          const float r = 3 + (fminf(rxb, ry2) - 3) * 0.45f;
          for (int g = 3; g >= 1; g--) cv.rrect(ex - rxb - g * 4, ey - ry2 - g * 4, 2 * rxb + g * 8, 2 * ry2 + g * 8, r + g * 4, to565(mulc(k.glowC, 0.1f * (4 - g))));
          cv.rrect(ex - rxb, ey - ry2, 2 * rxb, 2 * ry2, r, to565(k.eyeC));
          cv.rrect(ex - rxb * 0.7f + gx, ey - ry2 * 0.55f + gy, rxb * 1.4f, ry2 * 0.5f, r * 0.6f, to565(mixc(k.eyeC, C(255, 255, 255), 0.45f)));
          const float off = fmodf(t * 12, 4.0f);
          for (float yy = ey - ry2 + off; yy < ey + ry2; yy += 4) {
            const float d = fmaxf(0, fmaxf(ey - ry2 + r - yy, yy - (ey + ry2 - r)));  // inside a rounded corner row
            const float in = d > 0 ? r - sqrtf(fmaxf(0, r * r - d * d)) : 0;
            cv.rrect(ex - rxb + in, yy, 2 * (rxb - in), 1, 0, to565(mulc(k.eyeC, 0.35f)));
          }
        } else if (k.st == ST_HALO) {
          // proper eyes around a glowing ring iris (the "halo"): dark glass with an outline and a
          // lash line, a bright iris ring, a dark pupil, sparkles
          if (ry2 > 7) {
            const RGB irisC = mixc(k.eyeC, C(90, 195, 255), 0.55f);
            for (int g = 3; g >= 1; g--) cv.ellipse(ex, ey, rxb + g * 4, ry2 + g * 4, to565(mulc(k.glowC, 0.06f * (4 - g))));
            cv.ellipse(ex, ey, rxb + 2, ry2 + 2, to565(mulc(k.eyeC, 0.55f)));
            cv.ellipse(ex, ey, rxb, ry2, to565(C(14, 20, 36)));
            const float ir = fminf(rxb * 0.72f, ry2 * 0.8f);
            const float ix = ex + clampf(px - ex, -(rxb - ir), rxb - ir), iy = ey + clampf(py - ey, -(ry2 - ir), ry2 - ir);
            cv.circle(ix, iy, ir + 3, to565(mulc(irisC, 0.35f)));
            cv.circle(ix, iy, ir, to565(irisC));
            cv.circle(ix, iy, ir * 0.8f, to565(mixc(irisC, C(20, 40, 80), 0.5f)));
            ring(ix, iy, ir * 0.62f, 1.1f, to565(mixc(irisC, C(255, 255, 255), 0.3f)));
            cv.circle(ix, iy, ir * 0.42f * p.pupil * (1 + 0.3f * p.dilate), to565(C(6, 8, 20)));
            const uint16_t w = to565(C(255, 255, 255));
            if (glyph == G_STAR) star4(ix, iy, ir * 0.55f, 0.22f, w);
            else {
              cv.ellipse(ix - ir * 0.36f, iy - ir * 0.4f, ir * 0.26f, ir * 0.2f, w);
              cv.circle(ix + ir * 0.36f, iy + ir * 0.34f, ir * 0.1f, w);
            }
            const float sgn = side == 0 ? -1.0f : 1.0f;  // the lash line, with a flick at the outer corner
            curve(ex - rxb * 1.04f, ey - ry2 * 0.3f, ex, ey - ry2 * 1.32f, ex + rxb * 1.04f, ey - ry2 * 0.3f, 2.0f, to565(k.eyeC), 10, false);
            if (side != 2) cv.lineFast(ex + sgn * rxb * 1.0f, ey - ry2 * 0.36f, ex + sgn * rxb * 1.24f, ey - ry2 * 0.62f, 1.4f, to565(k.eyeC));
          } else {
            curve(ex - rxb, ey - 1, ex, ey + 6, ex + rxb, ey - 1, 2.2f, to565(k.eyeC), 8, false);
          }
        } else if (k.st == ST_HALOLITE) {
          // Halo Lite, the minimal one: a glowing ice-blue eye, a navy iris edged by one thin
          // bright ring, one highlight
          if (ry2 > 7) {
            for (int g = 3; g >= 1; g--) cv.ellipse(ex, ey, rxb + g * 4, ry2 + g * 4, to565(mulc(k.glowC, 0.08f * (4 - g))));
            cv.ellipse(ex, ey, rxb, ry2, to565(k.eyeC));
            const float ir = fminf(rxb * 0.62f, ry2 * 0.72f);
            const float ix = ex + clampf(px - ex, -(rxb - ir), rxb - ir), iy = ey + clampf(py - ey, -(ry2 - ir), ry2 - ir);
            cv.circle(ix, iy, ir, to565(C(22, 44, 90)));
            ring(ix, iy, ir * 0.78f, 1.4f, to565(mixc(k.eyeC, C(120, 210, 255), 0.5f)));
            const uint16_t w = to565(C(255, 255, 255));
            if (glyph == G_STAR) star4(ix, iy, ir * 0.55f, 0.22f, w);
            else cv.circle(ix - ir * 0.3f, iy - ir * 0.34f, ir * 0.2f, w);
          } else {
            curve(ex - rxb, ey - 1, ex, ey + 6, ex + rxb, ey - 1, 2.2f, to565(k.eyeC), 8, false);
          }
        } else {  // oval eyes: EVE, Ember, Aurora, Rose
          for (int g = 4; g >= 1; g--) cv.ellipse(ex, ey, rxb + g * 5, ry2 + g * 5, to565(mulc(k.glowC, 0.1f * (5 - g))));
          if (k.st == ST_ROSE) {
            ring(ex, ey, fmaxf(rxb, ry2) + 9, 1.6f, to565(mulc(k.eyeC, 0.8f)));
            ring(ex, ey, fmaxf(rxb, ry2) + 14, 1.0f, to565(mulc(k.eyeC, 0.45f)));
          }
          cv.ellipse(ex, ey, rxb, ry2, to565(k.eyeC));
          if (k.st == ST_EMBER) {  // a hot flickering core
            const float f = 0.85f + 0.15f * sinf(t * 13 + side) * sinf(t * 5.3f);
            cv.ellipse(ex + gx, ey + ry2 * 0.1f + gy, rxb * 0.62f * f, ry2 * 0.55f * f, to565(C(255, 190, 60)));
            cv.ellipse(ex + gx, ey + ry2 * 0.18f + gy, rxb * 0.3f * f, ry2 * 0.28f * f, to565(C(255, 245, 190)));
          } else {
            cv.ellipse(ex + gx, ey - ry2 * 0.12f + gy, rxb * 0.72f, ry2 * 0.6f, to565(mixc(k.eyeC, C(255, 255, 255), 0.35f)));
          }
          if (k.st == ST_ROSE && ry2 > 10) star4(ex + rxb * 0.45f, ey - ry2 * 0.45f, 4 + 2 * sinf(t * 3 + side), 0.25f, to565(C(255, 255, 255)));
        }
        break;
      }
      case E_REAL: {
        cv.ellipse(ex, ey + 1, rxb + 14, ry2 + 14, to565(mixc(s.skin, s.glow, 0.5f)));  // socket shading
        cv.ellipse(ex, ey + 1, rxb + 7, ry2 + 7, to565(s.glow));
        cv.ellipse(ex, ey, rxb, ry2, to565(C(226, 214, 210)));                           // sclera, shadowed at the top
        cv.ellipse(ex, ey + ry2 * 0.16f, rxb * 0.97f, ry2 * 0.84f, to565(C(246, 242, 238)));
        cv.ellipse(ex - (side == 0 ? -1 : 1) * rxb * 0.84f, ey + 1, rxb * 0.12f, ry2 * 0.3f, to565(C(232, 164, 160)));  // tear duct
        const float ir = rx * 0.52f * (teary ? 1.12f : 1);
        cv.circle(px, py, ir, to565(mulc(k.irisC, 0.45f)));  // limbal ring
        for (int i = 5; i >= 0; i--) cv.circleFast(px, py, ir * (0.42f + i * 0.1f), to565(mixc(k.iris2C, k.irisC, i / 5.0f)));
        for (int i = 0; i < 18; i++) {
          const float a = i * 2 * PI_F / 18 + 0.15f;
          cv.lineFast(px + cosf(a) * ir * 0.4f, py + sinf(a) * ir * 0.4f, px + cosf(a) * ir * 0.92f, py + sinf(a) * ir * 0.92f, 0.5f,
                  to565(i & 1 ? mulc(k.irisC, 0.7f) : mixc(k.iris2C, C(255, 240, 200), 0.3f)));
        }
        if (glyph == G_STAR) star4(px, py, ir * 0.5f, 0.22f, to565(s.pupil));
        else cv.circle(px, py, ir * 0.36f * (1 + 0.45f * p.dilate) * p.pupil, to565(s.pupil));
        cv.rrect(px - ir * 0.52f, py - ir * 0.56f, ir * 0.34f, ir * 0.26f, 2, to565(C(255, 255, 255)));  // window reflection
        cv.circle(px + ir * 0.35f, py + ir * 0.3f, ir * 0.08f, to565(C(240, 240, 240)));
        break;
      }
      case E_CAT: {
        cv.ellipse(ex, ey, rxb + 5, ry2 + 5, to565(s.lash));
        const float ir = fminf(rxb, ry2 + 2);
        const float cx = px * 0.3f + ex * 0.7f, cy = py * 0.3f + ey * 0.7f;
        for (int i = 4; i >= 0; i--) {  // only the outer ring's edge shows: the inner ones needn't be smooth
          if (i == 4) cv.circle(cx, cy, ir * (0.4f + i * 0.15f), to565(mixc(k.iris2C, k.irisC, i / 4.0f)));
          else cv.circleFast(cx, cy, ir * (0.4f + i * 0.15f), to565(mixc(k.iris2C, k.irisC, i / 4.0f)));
        }
        const float slit = clampf(0.16f + 0.3f * (p.dilate + 1) * p.pupil - 0.15f, 0.08f, 0.62f);
        if (glyph == G_STAR) star4(px, py, ir * 0.55f, 0.2f, to565(s.pupil));
        else cv.ellipse(px, py, ir * slit, ir * 0.8f, to565(s.pupil));
        highlights(px, py, ir * 0.7f, p, false, teary);
        break;
      }
      case E_DOG: {
        cv.ellipse(ex, ey, rxb + 4, ry2 + 4, to565(s.lash));
        cv.ellipse(ex, ey, rxb, ry2, to565(C(246, 240, 232)));
        const float ir = fminf(rxb, ry2) * 0.9f * (teary ? 1.08f : 1);
        cv.circle(px, py, ir, to565(k.irisC));
        cv.ellipse(px, py + ir * 0.25f, ir * 0.8f, ir * 0.6f, to565(k.iris2C));
        if (glyph == G_STAR) star4(px, py, ir * 0.6f, 0.22f, to565(s.pupil));
        else cv.circle(px, py, ir * 0.62f * (1 + 0.2f * p.dilate) * p.pupil, to565(s.pupil));
        highlights(px, py, ir * 1.1f, p, false, true);  // puppy eyes: always glossy
        break;
      }
      case E_ANIME: {
        cv.ellipse(ex, ey, rxb, ry2, to565(C(255, 255, 255)));
        const float irx = rx * 0.78f, iry = ry * 0.82f * (teary ? 1.05f : 1);
        const float icy = py + ry * 0.08f;
        cv.ellipse(px, icy, irx, iry, to565(k.irisC));
        cv.ellipse(px, icy + iry * 0.28f, irx * 0.82f, iry * 0.62f, to565(mixc(k.irisC, k.iris2C, 0.55f)));
        cv.ellipse(px, icy + iry * 0.42f, irx * 0.6f, iry * 0.4f, to565(k.iris2C));
        cv.ellipse(px, icy - iry * 0.05f, irx * 0.42f, iry * 0.5f * (1 + 0.3f * p.dilate), to565(s.pupil));
        highlights(px, icy, irx * 1.1f, p, glyph == G_STAR, teary);
        break;
      }
      case E_MOCHI: {
        // a soft squishy eye (a little wider at the bottom), a faint pink glow, a big glossy pupil
        cv.ellipse(ex, ey + 2, rxb + 7, ry2 + 6, to565(mixc(s.bg, k.glowC, 0.16f * s.glowAmt)));
        cv.ellipse(ex, ey, rxb, ry2, to565(k.eyeC));
        cv.ellipse(ex, ey + ry2 * 0.2f, rxb * 1.05f, ry2 * 0.8f, to565(k.eyeC));
        if (ry2 > 8) {
          const float pp = fminf(rxb, ry2) * 0.74f * p.pupil * (1 + 0.2f * p.dilate) * (teary ? 1.1f : 1);
          const float qx = ex + (px - ex) * 0.8f, qy = ey + (py - ey) * 0.8f;
          cv.circle(qx, qy, pp, to565(k.irisC));
          cv.circleFast(qx, qy + pp * 0.22f, pp * 0.74f, to565(k.iris2C));
          cv.circleFast(qx, qy - pp * 0.06f, pp * 0.6f, to565(s.pupil));
          if (s.deco == D_STARS)  // galaxy: little stars turning slowly inside the pupil
            for (int i = 0; i < 4; i++) {
              const float a = t * 0.4f + i * 1.57f + side, r = pp * (0.25f + 0.1f * i);
              cv.circleFast(qx + cosf(a) * r, qy + sinf(a) * r * 0.8f, 1 + (i & 1), to565(C(220, 240, 255)));
            }
          const uint16_t w = to565(C(255, 255, 255));
          if (glyph == G_STAR) star4(qx, qy, pp * 0.7f, 0.22f, w);
          else {
            cv.ellipse(qx - pp * 0.32f, qy - pp * 0.36f, pp * 0.34f, pp * 0.28f, w);
            cv.circle(qx + pp * 0.34f, qy + pp * 0.34f, pp * 0.13f, w);
            const float tw = sinf(t * 2.3f + side * 1.7f);
            if (tw > 0.55f) star4(qx + pp * 0.42f, qy - pp * 0.5f, pp * 0.22f * (tw - 0.55f) / 0.45f, 0.24f, w);  // a twinkle
          }
          if (teary) cv.ellipse(ex, ey + ry2 * 0.75f, rxb * 0.72f, ry2 * 0.16f, to565(C(200, 230, 255)));
        }
        break;
      }
      case E_BEAN: {
        // kawaii bean eyes: a dark glossy oval that shifts a little with the gaze, big highlights
        const float bw = rxb * 0.66f, bh = ry2 * 0.8f;
        const float qx = ex + (px - ex) * 0.4f, qy = ey + (py - ey) * 0.4f;
        cv.ellipse(qx, qy, bw, bh, to565(k.irisC));
        if (bh > 6 && k.st == ST_LCD) {  // LCD: a plain dark oval, one square highlight
          if (glyph == G_STAR) star4(qx, qy, bw * 0.9f, 0.22f, to565(k.s.bg));
          else cv.rrect(qx - bw * 0.5f, qy - bh * 0.55f, bw * 0.48f, bh * 0.36f, 0, to565(C(255, 255, 255)));
        } else if (bh > 6) {
          cv.ellipse(qx, qy + bh * 0.4f, bw * 0.7f, bh * 0.4f, to565(mixc(k.irisC, k.iris2C, 0.5f)));
          const uint16_t w = to565(C(255, 255, 255));
          if (glyph == G_STAR) star4(qx, qy, bw * 0.9f, 0.22f, w);
          else {
            cv.ellipse(qx - bw * 0.28f, qy - bh * 0.36f, bw * 0.4f, bh * 0.3f, w);
            cv.circle(qx + bw * 0.34f, qy + bh * 0.3f, bw * 0.16f, w);
            const float tw = sinf(t * 2.1f + side * 1.9f);
            if (tw > 0.6f) star4(qx + bw * 0.5f, qy - bh * 0.62f, bw * 0.34f * (tw - 0.6f) / 0.4f, 0.24f, w);
          }
          if (teary) cv.ellipse(qx, qy + bh * 0.9f, bw * 0.9f, bh * 0.2f, to565(C(150, 205, 255)));
        }
        break;
      }
      case E_CHALK: {  // chalk: a dusty edge, a rubbed-in iris, the board showing through as the pupil, grain
        const RGB ch = k.eyeC;
        cv.ellipse(ex, ey, rxb + 4, ry2 + 4, to565(mixc(s.bg, ch, 0.22f)));
        cv.ellipse(ex, ey, rxb, ry2, to565(ch));
        static const float gr[10][2] = {{-0.6f, -0.3f}, {-0.2f, -0.7f}, {0.5f, -0.5f}, {0.7f, 0.1f}, {-0.7f, 0.3f},
                                        {-0.3f, 0.6f}, {0.3f, 0.65f}, {0.1f, -0.2f}, {-0.45f, 0.0f}, {0.45f, 0.35f}};
        const uint16_t gc = to565(mixc(ch, s.bg, 0.3f));
        for (int i = 0; i < 10; i++) cv.circleFast(ex + gr[i][0] * rxb, ey + gr[i][1] * ry2, i & 1 ? 1.2f : 0.8f, gc);
        if (ry2 > 8) {
          const float pr2 = fminf(pr, fminf(rxb, ry2) * 0.82f);
          cv.circle(px, py, pr2, to565(mixc(s.bg, k.irisC, 0.6f)));
          cv.circle(px, py, pr2 * 0.62f, to565(s.pupil));
          if (glyph == G_STAR) star4(px, py, pr2 * 0.75f, 0.22f, to565(ch));
          else {
            cv.circle(px - pr2 * 0.32f, py - pr2 * 0.34f, pr2 * 0.24f * p.shine, to565(ch));
            cv.circleFast(px + pr2 * 0.3f, py + pr2 * 0.3f, pr2 * 0.1f, to565(ch));
          }
        }
        break;
      }
      case E_COMIC: {  // thick ink outline, a white eye, a tall black "pie-cut" pupil
        cv.ellipse(ex, ey, rxb + 5, ry2 + 5, to565(s.lash));
        cv.ellipse(ex, ey, rxb, ry2, to565(k.eyeC));
        if (ry2 > 7) {
          const float pw = fminf(rxb * 0.42f, pr * 0.72f) * (1 + 0.15f * p.dilate) * (teary ? 1.1f : 1), ph = fminf(ry2 * 0.7f, pw * 1.55f);
          const float mx = fmaxf(0, rxb - pw - 4), my = fmaxf(0, ry2 - ph - 4);
          const float qx = ex + clampf(px - ex, -mx, mx), qy = ey + clampf(py - ey, -my, my);
          cv.ellipse(qx, qy, pw, ph, to565(s.pupil));
          const uint16_t w = to565(C(255, 255, 255));
          if (glyph == G_STAR) star4(qx, qy, pw * 0.95f, 0.24f, w);
          else if (p.shine > 0.05f) cv.tri(qx + pw * 0.05f, qy - ph * 0.15f, qx + pw * 1.0f, qy - ph * 0.5f, qx + pw * 0.4f, qy - ph * 1.02f, w);
          if (teary) cv.circle(qx - pw * 0.4f, qy + ph * 0.3f, pw * 0.22f, w);
        }
        break;
      }
      case E_OWL: {  // big round amber eyes in a dark rim, a huge pupil
        cv.ellipse(ex, ey, rxb + 5, ry2 + 5, to565(s.lash));
        cv.ellipse(ex, ey, rxb, ry2, to565(k.irisC));
        if (ry2 > 6) {
          cv.ellipse(ex, ey + ry2 * 0.3f, rxb * 0.84f, ry2 * 0.6f, to565(k.iris2C));
          const float pp = fminf(rxb, ry2) * 0.56f * p.pupil * (1 + 0.35f * p.dilate) * (teary ? 1.08f : 1);
          const float mx = fmaxf(0, rxb - pp - 3), my = fmaxf(0, ry2 - pp - 3);
          const float qx = ex + clampf((px - ex) * 0.8f, -mx, mx), qy = ey + clampf((py - ey) * 0.8f, -my, my);
          cv.circle(qx, qy, pp, to565(s.pupil));
          highlights(qx, qy, pp * 1.3f, p, glyph == G_STAR, teary);
        }
        break;
      }
      case E_FOX: {  // amber eyes, a dark liner, a big dark iris
        cv.ellipse(ex, ey, rxb + 3.5f, ry2 + 3.5f, to565(s.lash));
        cv.ellipse(ex, ey, rxb, ry2, to565(k.iris2C));
        if (ry2 > 6) {
          const float pp = fminf(rxb, ry2) * 0.74f * p.pupil * (1 + 0.2f * p.dilate) * (teary ? 1.08f : 1);
          const float mx = fmaxf(0, rxb - pp * 0.9f), my = fmaxf(0, ry2 - pp * 0.9f);
          const float qx = ex + clampf((px - ex) * 0.7f, -mx, mx), qy = ey + clampf((py - ey) * 0.7f, -my, my);
          cv.circle(qx, qy, pp, to565(k.irisC));
          cv.circleFast(qx, qy, pp * 0.58f, to565(s.pupil));
          highlights(qx, qy, pp * 1.15f, p, glyph == G_STAR, teary);
        }
        break;
      }
      case E_CYC: {  // one big monster eye: an ink outline, a shaded eyeball, a green iris
        cv.ellipse(ex, ey, rxb + 4.5f, ry2 + 4.5f, to565(s.lash));
        cv.ellipse(ex, ey, rxb, ry2, to565(mixc(k.eyeC, s.skin2, 0.3f)));
        cv.ellipse(ex, ey + ry2 * 0.1f, rxb * 0.96f, ry2 * 0.9f, to565(k.eyeC));
        if (ry2 > 8) {
          const float ir = fminf(rxb, ry2) * 0.52f * (teary ? 1.08f : 1);
          cv.circle(px, py, ir, to565(mulc(k.irisC, 0.6f)));
          cv.circle(px, py, ir * 0.9f, to565(k.irisC));
          cv.ellipse(px, py + ir * 0.28f, ir * 0.7f, ir * 0.52f, to565(k.iris2C));
          if (glyph == G_STAR) star4(px, py, ir * 0.6f, 0.22f, to565(s.pupil));
          else cv.circle(px, py, ir * 0.44f * (1 + 0.35f * p.dilate) * p.pupil, to565(s.pupil));
          highlights(px, py, ir * 1.1f, p, false, teary);
        }
        break;
      }
      case E_PAINT: {  // a watercolour wash: a pale bleed, pigment pooled at the bottom, paper left white
        const float bw = rxb * 0.72f, bh = ry2 * 0.84f;
        const float qx = ex + (px - ex) * 0.4f, qy = ey + (py - ey) * 0.4f, wob = sinf(t * 0.7f + side * 2.1f);
        cv.ellipse(qx + 1.5f, qy + 2, bw + 4 + wob, bh + 3, to565(mixc(s.bg, k.iris2C, 0.3f)));
        cv.ellipse(qx - 1, qy - 1, bw, bh, to565(mixc(s.bg, k.iris2C, 0.85f)));
        if (bh > 6) {
          cv.ellipse(qx + 0.5f, qy + bh * 0.14f, bw * 0.86f, bh * 0.82f, to565(mixc(k.iris2C, k.irisC, 0.55f)));
          cv.ellipse(qx + 1, qy + bh * 0.3f, bw * 0.66f, bh * 0.62f, to565(k.irisC));
          cv.circle(qx + 0.5f, qy + bh * 0.12f, fminf(bw, bh) * 0.48f * p.pupil * (1 + 0.2f * p.dilate), to565(s.pupil));
          const uint16_t w = to565(s.bg);
          if (glyph == G_STAR) star4(qx, qy, bw * 0.9f, 0.22f, w);
          else {
            cv.ellipse(qx - bw * 0.3f, qy - bh * 0.4f, bw * 0.34f, bh * 0.24f, w);
            cv.circle(qx + bw * 0.36f, qy + bh * 0.12f, bw * 0.13f, w);
          }
          if (teary) cv.ellipse(qx, qy + bh * 0.92f, bw * 0.9f, bh * 0.2f, to565(C(150, 205, 255)));
        }
        break;
      }
      case E_LINE: {  // line art: no fill, just an iris floating under the lid line (drawn with the lids)
        if (ry2 > 5) {
          const float ir = fminf(rxb * 0.5f, ry2 * 0.8f) * (teary ? 1.06f : 1);
          const float mx = fmaxf(0, rxb - ir - 3), my = fmaxf(0, ry2 - ir);
          const float ix = ex + clampf(px - ex, -mx, mx), iy = ey + clampf(py - ey, -my, my);
          cv.circle(ix, iy, ir, to565(k.irisC));
          cv.ellipse(ix, iy + ir * 0.3f, ir * 0.72f, ir * 0.52f, to565(k.iris2C));
          if (glyph == G_STAR) star4(ix, iy, ir * 0.7f, 0.22f, to565(C(255, 255, 255)));
          else {
            cv.circle(ix, iy, ir * 0.48f * p.pupil * (1 + 0.3f * p.dilate), to565(s.pupil));
            highlights(ix, iy, ir * 1.1f, p, false, teary);
          }
        }
        break;
      }
      case E_TIN: {  // a lamp behind the glass (tinFrame drew the bezel and the glass)
        const float lr = fminf(rxb, ry2) * 0.6f * p.pupil * (1 + 0.25f * p.dilate);
        const float mx = fmaxf(0, k.gRx - lr - 6), my = fmaxf(0, k.gRy - lr - 6);
        const float qx = ex + clampf((px - ex) * 0.8f, -mx, mx), qy = ey + clampf((py - ey) * 0.8f, -my, my);
        cv.circle(qx, qy, lr + 6, to565(mixc(tinGlass(), k.irisC, 0.28f)));
        cv.circle(qx, qy, lr, to565(k.irisC));
        if (glyph == G_STAR) star4(qx, qy, lr * 0.85f, 0.22f, to565(C(255, 255, 255)));
        else cv.circle(qx, qy - lr * 0.06f, lr * 0.6f, to565(k.iris2C));
        cv.ellipse(ex - k.gRx * 0.5f, ey - k.gRy * 0.6f, k.gRx * 0.24f, k.gRy * 0.12f, to565(mixc(tinGlass(), C(255, 255, 255), 0.55f)));
        cv.circleFast(ex + k.gRx * 0.6f, ey + k.gRy * 0.58f, 2, to565(mixc(tinGlass(), C(255, 255, 255), 0.4f)));
        break;
      }
      case E_TOON: {
        cv.ellipse(ex, ey + 4, rxb + 3, ry2 + 3, to565(mixc(s.bg, s.skin2, 0.5f)));  // soft shadow under the eye
        cv.ellipse(ex, ey, rxb, ry2, to565(C(214, 214, 230)));                       // eyeball, darker at the top
        cv.ellipse(ex, ey + ry2 * 0.12f, rxb * 0.96f, ry2 * 0.88f, to565(C(252, 252, 255)));
        const float ir = rx * 0.56f * (teary ? 1.1f : 1);
        cv.circle(px, py, ir, to565(mulc(k.irisC, 0.55f)));
        cv.circle(px, py, ir * 0.9f, to565(k.irisC));
        cv.ellipse(px, py + ir * 0.25f, ir * 0.72f, ir * 0.55f, to565(k.iris2C));
        if (glyph == G_STAR) star4(px, py, ir * 0.55f, 0.22f, to565(s.pupil));
        else cv.circle(px, py, ir * 0.42f * (1 + 0.35f * p.dilate) * p.pupil, to565(s.pupil));
        highlights(px, py, ir * 1.15f, p, false, teary);
        break;
      }
    }
  }

  void drawLids(int side, float ex, float ey, float rxb, float ry2, const Pose& p) {
    Canvas& cv = *k.cv;
    const StyleDef& s = k.s;
    if (s.eye == E_TIN) { tinLids(ex, ey, side, p); return; }
    const float top = ey - ry2, bot = ey + ry2;
    const float margin = s.eye == E_NEON ? 16 : s.eye == E_REAL ? 18 : s.eye == E_CAT || s.eye == E_DOG ? 6 : s.eye == E_OWL || s.eye == E_FOX ? 7 : 14;
    const float up = margin + 4;  // how far above the eye the lid reaches (only over the eye and its glow)
    const float l = ex - rxb - margin, r = ex + rxb + margin;
    float lidT = clampf(p.lidT, 0, 1);
    if (s.lidBlink) lidT = lidT + (1.02f - lidT) * k.bl;
    const float inner = side == 0 ? 1 : side == 1 ? -1 : 0;
    float yL = top, yR = top;
    const bool lidOn = lidT > 0.01f || fabsf(p.lidTA) > 0.5f;
    const uint16_t lc = to565(lidCol());
    if (lidOn) {
      const float yl = top + lidT * 2 * ry2, slope = tanf(p.lidTA * PI_F / 180) * rxb;
      yL = yl - slope * inner;
      yR = yl + slope * inner;
      cv.tri(l, top - up, r, top - up, r, yR, lc);
      cv.tri(l, top - up, r, yR, l, yL, lc);
      if (s.eye == E_NEON && lidT > 0.12f) {
        cv.line(ex - rxb * 0.98f, yL, ex + rxb * 0.98f, yR, 3.4f, to565(mulc(k.glowC, 0.45f)));
        cv.line(ex - rxb * 0.98f, yL, ex + rxb * 0.98f, yR, 1.8f, to565(mixc(k.glowC, k.eyeC, 0.4f)));
      }
    }
    const bool inked = s.eye == E_COMIC || s.eye == E_OWL || s.eye == E_FOX || s.eye == E_CYC;  // (wide enough to cover their outline)
    if (p.lidB > 0.01f) {
      const float ery = ry2 * 0.62f + 10, erx = inked ? rxb + 16 : rxb * 0.8f + 12;
      cv.ellipse(ex, bot + ery - p.lidB * ry2 * 1.9f - 10, erx, ery, lc);
    }
    // the upper lid edge: lashes (real / anime), a thick lid line (toon, cat, puppy)
    const bool bold = s.eye == E_COMIC || s.eye == E_OWL || s.eye == E_FOX || s.eye == E_CYC || s.eye == E_LINE;
    const bool edge = s.lashes || s.eye == E_TOON || s.eye == E_CAT || s.eye == E_DOG || bold;
    if (!edge) return;
    float xs[13], ys[13];
    for (int i = 0; i <= 12; i++) {
      const float u = i / 6.0f - 1;
      const float eyeTop = ey - ry2 * sqrtf(fmaxf(0, 1 - u * u));
      const float lidY = lidOn ? yL + (yR - yL) * (u + 1) / 2 : -1e9f;
      xs[i] = ex + u * rxb;
      ys[i] = fmaxf(eyeTop, lidY);
    }
    if (s.eye == E_TOON) {
      polyline(xs, ys, 13, 4.5f, to565(s.skin2));
      polyline(xs, ys, 13, 2.0f, to565(s.lash));
      return;
    }
    if (s.eye == E_CAT || s.eye == E_DOG) {
      if (lidOn || k.bl > 0.05f) polyline(xs, ys, 13, 3.0f, to565(s.lash));
      return;
    }
    if (bold) {  // the newer ink styles: a bold upper lid edge, the lower lid's edge, a liner flick
      const bool line = s.eye == E_LINE;
      const RGB lc = line ? k.eyeC : s.lash;
      const float w = s.eye == E_COMIC ? s.lineW * 0.85f : line ? 2.4f : 3.0f;
      if (lidOn || k.bl > 0.05f || line) polyline(xs, ys, 13, w, to565(lc));
      const float sg = side == 0 ? -1.0f : 1.0f;
      if ((line || s.eye == E_FOX) && side != 2) {
        const int e = side == 0 ? 0 : 12;
        k.cv->lineFast(xs[e], ys[e], xs[e] + sg * 9, ys[e] - 6, line ? 1.6f : 2.4f, to565(lc));
      }
      if (p.lidB > 0.05f) lowerLidLine(ex, ey, rxb, ry2, p.lidB, inked ? rxb + 16 : rxb * 0.8f + 12, line ? 1.6f : w * 0.8f, to565(lc));
      else if (line) curve(ex - rxb * 0.8f, ey + ry2 * 0.55f, ex, ey + ry2 * 1.1f, ex + rxb * 0.8f, ey + ry2 * 0.55f, 1.2f, to565(mulc(k.eyeC, 0.6f)), 8, false);
      return;
    }
    const float lw = k.st == ST_ANIME ? 3.4f : 2.4f;
    polyline(xs, ys, 13, lw, to565(s.lash));
    const float outerSign = side == 0 ? -1.0f : 1.0f;
    if (k.st == ST_ANIME) {
      const float ox = side == 0 ? xs[0] : xs[12], oy = side == 0 ? ys[0] : ys[12];
      cv.line(ox, oy, ox + outerSign * 10, oy - 7, 3, to565(s.lash));
    } else {
      // individual lashes, longer towards the outer corner, curving out and up
      for (int i = 2; i <= 11; i++) {
        const float u = i / 12.0f;
        const float o = side == 0 ? 1 - u : u;  // 0 inner .. 1 outer
        const float len = 3 + 5 * o;
        cv.lineFast(xs[i], ys[i], xs[i] + outerSign * (1 + 3 * o), ys[i] - len, 0.8f, to565(s.lash));
      }
      // crease above the eye
      curve(ex - rxb * 0.95f, ey - ry2 * 0.5f - 8, ex, ey - ry2 - 14, ex + rxb * 0.95f, ey - ry2 * 0.5f - 8, 1.4f, to565(s.skin2));
    }
    if (p.lidB < 0.1f)
      curve(ex - rxb * 0.85f, ey + ry2 * 0.5f, ex, ey + ry2 * 1.12f, ex + rxb * 0.85f, ey + ry2 * 0.5f, 1.0f, to565(mixc(s.lash, lidCol(), 0.55f)));
  }

  // the top of the lower lid (drawLids' ellipse) where it crosses the eye: an ink edge for the bold styles
  void lowerLidLine(float ex, float ey, float rxb, float ry2, float lidB, float erx, float w, uint16_t c) {
    const float ery = ry2 * 0.62f + 10, cy = ey + ry2 + ery - lidB * ry2 * 1.9f - 10;
    float xs[11], ys[11];
    int n = 0;
    for (int i = 0; i <= 10; i++) {
      const float u = i / 5.0f - 1, x = ex + u * rxb * 0.96f, v = (x - ex) / erx;
      const float y = cy - ery * sqrtf(fmaxf(0, 1 - v * v)), eb = ey + ry2 * sqrtf(fmaxf(0, 1 - u * u * 0.92f));
      if (y < eb - 1) { xs[n] = x; ys[n] = y; n++; }
      else if (n > 1) break;
    }
    if (n > 1) polyline(xs, ys, n, w, c);
  }
  // Tinbot's eye: a chrome bezel (lit from the top left) around dark glass; metal shutters for lids
  RGB tinGlass() const { return C(26, 32, 50); }
  RGB tinShutter() const { return mixc(k.s.bg, k.s.skin2, 0.35f); }
  void tinFrame(float ex, float ey, float rx, float ry) {
    Canvas& cv = *k.cv;
    k.gRx = rx; k.gRy = ry;
    cv.rrect(ex - rx - 9, ey - ry - 9, 2 * rx + 18, 2 * ry + 18, 20, to565(mulc(k.s.skin2, 0.7f)));
    cv.rrect(ex - rx - 8, ey - ry - 8, 2 * rx + 15, 2 * ry + 15, 19, to565(k.s.deco1));
    cv.rrect(ex - rx - 5, ey - ry - 5, 2 * rx + 11, 2 * ry + 11, 16, to565(mixc(k.s.deco1, k.s.skin2, 0.55f)));
    cv.rrect(ex - rx - 3, ey - ry - 3, 2 * rx + 6, 2 * ry + 6, 14, to565(mulc(k.s.skin2, 0.55f)));
    cv.rrect(ex - rx, ey - ry, 2 * rx, 2 * ry, 12, to565(tinGlass()));
  }
  void tinLids(float ex, float ey, int side, const Pose& p) {
    Canvas& cv = *k.cv;
    const float rx = k.gRx - 1, ry = k.gRy - 1, top = ey - ry, bot = ey + ry;
    const float lidT = clampf(p.lidT, 0, 1) + (1.0f - clampf(p.lidT, 0, 1)) * k.bl;
    const float inner = side == 0 ? 1 : side == 1 ? -1 : 0;
    const uint16_t sc = to565(tinShutter()), ec = to565(k.s.lash);
    if (lidT > 0.01f || fabsf(p.lidTA) > 0.5f) {  // the top shutter, slanted (angry: down to the nose)
      const float yl = top + lidT * 2 * ry, sl = tanf(p.lidTA * PI_F / 180) * rx;
      const float yL = clampf(yl - sl * inner, top, bot), yR = clampf(yl + sl * inner, top, bot);
      cv.tri(ex - rx, top, ex + rx, top, ex + rx, yR, sc);
      cv.tri(ex - rx, top, ex + rx, yR, ex - rx, yL, sc);
      cv.lineFast(ex - rx, yL, ex + rx, yR, 1.8f, ec);
    }
    if (p.lidB > 0.01f) {  // the bottom shutter rises to a point: a smiling squint
      const float h = clampf(p.lidB, 0, 0.8f) * 2 * ry * 0.9f, yc = bot - h, ys = bot - h * 0.55f;
      cv.tri(ex - rx, bot, ex + rx, bot, ex + rx, ys, sc);
      cv.tri(ex - rx, bot, ex + rx, ys, ex, yc, sc);
      cv.tri(ex - rx, bot, ex, yc, ex - rx, ys, sc);
      cv.lineFast(ex - rx, ys, ex, yc, 1.8f, ec);
      cv.lineFast(ex, yc, ex + rx, ys, 1.8f, ec);
    }
  }

  void highlights(float x, float y, float r, const Pose& p, bool starEye, bool teary) {
    if (p.shine < 0.05f) return;
    Canvas& cv = *k.cv;
    const uint16_t w = to565(C(255, 255, 255));
    if (starEye) { star4(x, y, r * 0.62f, 0.2f, w); return; }
    cv.circle(x - r * 0.32f, y - r * 0.34f, r * 0.26f * p.shine * (teary ? 1.2f : 1), w);
    cv.circle(x + r * 0.3f, y + r * 0.28f, r * 0.11f * p.shine, w);
    if (teary) { cv.circle(x + r * 0.05f, y - r * 0.5f, r * 0.08f, w); cv.circle(x - r * 0.45f, y + r * 0.15f, r * 0.07f, w); }
  }

  void drawShades(const Pose& p) {
    Canvas& cv = *k.cv;
    const float w = k.s.gap * p.gap * 0.55f + k.s.rx * 1.35f, hg = k.s.rx * 1.3f;
    const float y = k.cy - hg * 0.5f;
    const uint16_t frame = to565(lightBg() ? C(25, 25, 30) : C(28, 34, 52));
    const uint16_t edge = to565(lightBg() ? C(20, 20, 24) : C(90, 130, 255));
    for (int side = 0; side < 2; side++) {
      const float x = k.cx + (side == 0 ? -1 : 1) * w * 0.52f - w * 0.46f;
      cv.rrect(x - 2, y - 2, w * 0.92f + 4, hg + 4, 8, edge);
      cv.rrect(x, y, w * 0.92f, hg, 7, frame);
      const float cs = hg / 5;
      for (int j = 0; j < 2; j++)
        for (int i = 0; i < 2; i++) cv.rrect(x + cs * (1 + i * 1.2f), y + cs * (0.8f + j * 1.2f), cs, cs, 1, to565(C(235, 240, 255)));
    }
    cv.line(k.cx - w * 0.1f, y + 8, k.cx + w * 0.1f, y + 8, 3, edge);
  }

  void drawAccessory(const Pose& p, float gap) {
    if (ext.acc == ACC_NONE) return;
    Canvas& cv = *k.cv;
    const RGB frameC = lightBg() ? C(40, 30, 30) : C(200, 210, 230);
    float rx, ry;
    eyeSize(1, p, rx, ry);
    const float r = fmaxf(rx, ry) + 10;
    const float xl = k.cx - gap / 2, xr = k.cx + gap / 2;
    if (ext.acc == ACC_GLASSES && k.st == ST_CYCLOPS) {  // one eye: one big round lens
      ring(k.cx, k.cy, r, 3.2f, to565(frameC));
      cv.lineFast(k.cx - r, k.cy - 4, 0, k.cy - 14, 1.6f, to565(frameC));
      cv.lineFast(k.cx + r, k.cy - 4, W, k.cy - 14, 1.6f, to565(frameC));
    } else if (ext.acc == ACC_GLASSES) {
      ring(xl, k.cy, r, 3.2f, to565(frameC));
      ring(xr, k.cy, r, 3.2f, to565(frameC));
      curve(xl + r * 0.9f, k.cy - 4, k.cx, k.cy - 12, xr - r * 0.9f, k.cy - 4, 3, to565(frameC));
    } else if (ext.acc == ACC_MONOCLE) {
      ring(xr, k.cy, r, 3.2f, to565(C(215, 180, 80)));
      curve(xr + r * 0.6f, k.cy + r * 0.8f, xr + r * 0.9f, k.cy + r * 2.2f, xr + r * 0.2f, H + 10, 1.4f, to565(C(215, 180, 80)));
    } else if (ext.acc == ACC_PATCH) {
      cv.line(0, k.cy - 70, W, k.cy + 10, 3, to565(C(30, 24, 22)));
      cv.ellipse(xr, k.cy + 2, r * 0.95f, r * 0.85f, to565(C(24, 20, 18)));
    }
  }

  void drawBrows(const Pose& p, float gap) {
    if (p.browShow < 0.1f || k.s.brow == B_NONE) return;
    const int n = k.s.single ? 1 : 2;
    for (int i = 0; i < n; i++) {
      const int side = k.s.single ? 2 : i;
      float rx, ry;
      eyeSize(side, p, rx, ry);
      const float sgn = side == 0 ? -1.0f : side == 1 ? 1.0f : 0;
      const float raise = (p.browY + (side == 0 ? p.browAsym : side == 1 ? -p.browAsym : 0) * 0.5f) * 22;
      const float ox = sgn * gap / 2, oy = (side == 0 ? p.dyL : side == 1 ? p.dyR : 0) - ry - 16 - raise + (k.st == ST_ANIME ? -8 : 0) - (k.s.eye == E_REAL ? 8 : 0);
      const float bx = k.cx + ox * cosf(k.tiltR) - oy * sinf(k.tiltR), by = k.cy + ox * sinf(k.tiltR) + oy * cosf(k.tiltR);
      if (k.s.brow == B_DOTS) {  // puppy: two soft dots that move
        const float dx = (side == 0 ? 1 : -1) * p.browA * 0.25f;
        k.cv->ellipse(bx + dx * 0.6f - sgn * 4, by + fabsf(p.browA) * 0.2f, 11, 7, to565(k.s.browC));
        continue;
      }
      const float len = rx * (k.s.eye == E_REAL ? 1.2f : k.s.browLen);
      const float a = (side == 2 ? 0 : -sgn * p.browA) * PI_F / 180 + k.tiltR;
      const float dx = cosf(a) * len, dy = sinf(a) * len;
      const float arch = p.browCurve * 10 - (side == 2 ? p.browA * 0.3f : 0);
      const float x0 = bx - dx, y0 = by - dy, x2 = bx + dx, y2 = by + dy;
      const uint16_t bc = to565(k.s.brow == B_NEON ? k.glowC : k.s.browC);
      switch (k.s.brow) {
        case B_NEON:
          curve(x0, y0, bx, by - arch, x2, y2, 5.5f, to565(mulc(k.glowC, 0.35f)), 8, false);
          curve(x0, y0, bx, by - arch, x2, y2, 2.8f, bc);
          break;
        case B_THICK:
        case B_HAIR: {
          // along the brow, thick at the inner end; real brows also get hair strokes
          const float xs0 = sgn > 0 ? x0 : x2, ys0 = sgn > 0 ? y0 : y2, xe = sgn > 0 ? x2 : x0, ye = sgn > 0 ? y2 : y0;
          const bool hair = k.s.brow == B_HAIR;
          const float t0 = hair ? 3.2f : 6.5f, t1 = hair ? 1.0f : 2.2f;
          float X[11], Y[11];
          for (int j = 0; j <= 10; j++) {
            const float u = j / 10.0f, a2 = (1 - u) * (1 - u), b2 = 2 * (1 - u) * u, d2 = u * u;
            X[j] = a2 * xs0 + b2 * bx + d2 * xe;
            Y[j] = a2 * ys0 + b2 * (by - arch) + d2 * ye;
          }
          for (int j = 0; j < 10; j++) k.cv->line(X[j], Y[j], X[j + 1], Y[j + 1], t0 + (t1 - t0) * j / 9.0f, hair ? to565(mixc(k.s.browC, lidCol(), 0.35f)) : bc);
          if (hair) {
            const float dirx = xe - xs0, diry = ye - ys0;
            const float dl = sqrtf(dirx * dirx + diry * diry) + 0.01f;
            for (int j = 0; j < 16; j++) {
              const float u = (j + 0.5f) / 16.0f;
              const int q = (int)(u * 10);
              const float fx = X[q] + (X[q + 1] - X[q]) * (u * 10 - q), fy = Y[q] + (Y[q + 1] - Y[q]) * (u * 10 - q);
              const float up = (j & 1) ? 3.0f : 1.5f;
              k.cv->lineFast(fx - dirx / dl * 3, fy + up, fx + dirx / dl * 4, fy - up - 1, 0.7f, bc);
            }
          }
          break;
        }
        case B_BOLD:
          curve(x0, y0, bx, by - arch, x2, y2, k.s.lineW * 1.1f, bc);
          break;
        default:
          curve(x0, y0, bx, by - arch, x2, y2, k.st == ST_ANIME ? 2.0f : 3.0f, bc);
          break;
      }
    }
  }

  // soft blush: a faint wide edge, a warmer middle and a tiny shine (not a flat oval)
  void blushSpot(float x, float y, float a) {
    const RGB lid = lidCol(), full = k.s.blush;
    if (k.s.blushK == BL_HATCH) {  // three short strokes (chalk: over a faint rubbed smear)
      if (k.st == ST_CHALK) k.cv->ellipse(x, y, 21, 8, to565(mixc(lid, full, 0.14f + 0.12f * a)));
      const uint16_t c = to565(mixc(lid, full, fminf(1, 0.6f + 0.4f * a)));
      for (int i = -1; i <= 1; i++) k.cv->lineFast(x + i * 8 - 3.5f, y + 5, x + i * 8 + 3.5f, y - 5, 1.6f, c);
      return;
    }
    if (k.s.blushK == BL_DOTS) {
      if (k.st == ST_TINBOT) {  // three pink cheek LEDs
        for (int i = -1; i <= 1; i++) {
          k.cv->circle(x + i * 10, y, 4.6f, to565(mixc(lid, k.s.skin2, 0.6f)));
          k.cv->circle(x + i * 10, y, 3.4f, to565(mixc(mulc(full, 0.4f), full, 0.4f + 0.6f * a)));
          k.cv->circleFast(x + i * 10 - 1, y - 1, 1, to565(mixc(full, C(255, 255, 255), 0.6f)));
        }
      } else {  // halftone: dots biggest in the middle
        const uint16_t c = to565(mixc(lid, full, 0.75f + 0.25f * a));
        for (int j = -1; j <= 1; j++)
          for (int i = -2; i <= 2; i++) {
            const float dx = i * 8 + (j & 1) * 4, dy = j * 7, d = sqrtf(dx * dx * 0.6f + dy * dy) / 10;
            const float r = (3.8f - 1.6f * d) * (0.65f + 0.35f * a);
            if (r > 0.8f) k.cv->circleFast(x + dx, y + dy, r, c);
          }
      }
      return;
    }
    const float base = (lightBg() ? 0.25f : 0.45f) + 0.45f * a;
    k.cv->ellipse(x, y, 22, 9, to565(mixc(lid, full, base * 0.32f)));
    k.cv->ellipse(x, y, 17.5f, 7, to565(mixc(lid, full, base * 0.66f)));
    k.cv->ellipse(x, y + 0.5f, 11.5f, 4.4f, to565(mixc(lid, full, fminf(1, base * 1.05f))));
    if (a > 0.3f) k.cv->circleFast(x - 7, y - 2.5f, 1.5f, to565(mixc(lid, C(255, 255, 255), 0.3f + 0.3f * a)));
  }
  void drawBlush(const Pose& p, float gap) {
    const float a = clampf(p.blush, 0, 1);
    const int n = k.s.single ? 1 : 2;
    for (int i = 0; i < n; i++) {
      const int side = k.s.single ? 2 : i;
      float rx, ry;
      eyeSize(side, p, rx, ry);
      const float by = k.cy + ry + 14;
      if (k.s.single) {
        blushSpot(k.cx - rx * 0.9f, by, a);
        blushSpot(k.cx + rx * 0.9f, by, a);
      } else {
        blushSpot(k.cx + (side == 0 ? -1 : 1) * (gap / 2 + 14), by, a);
      }
    }
  }

  void mouthPos(const Pose& p, float& mx, float& my) const {
    const StyleDef& s = k.s;
    mx = k.cx + p.mX * 30 - sinf(k.tiltR) * 40;
    if (s.mouth == M_BEAK) { my = k.cy + 4; return; }  // the owl's beak: between the eyes
    my = k.cy + s.ry + (s.mouth == M_CAT ? 44 : s.mouth == M_DOG ? 50 : s.mouth == M_LIPS ? 48 : s.mouth == M_CUTE ? 20 : s.mouth == M_EQ ? 40 : 30);
  }

  // The owl's beak: a little hooked triangle, shaded on one side; it opens to talk
  void drawBeak(float mx, float my, float open) {
    Canvas& cv = *k.cv;
    const StyleDef& s = k.s;
    const float bw = 13, bh = 24 * (1 - 0.3f * clampf(open, 0, 1));
    const uint16_t bc = to565(s.mouthC), dk = to565(mulc(s.mouthC, 0.74f)), ln = to565(s.lash);
    if (open > 0.1f) {
      const float gp = 2 + open * 12;
      cv.tri(mx - bw * 0.8f, my + bh * 0.4f, mx + bw * 0.8f, my + bh * 0.4f, mx, my + bh + gp + 6, to565(s.mouthIn));
      cv.tri(mx - bw * 0.62f - 1.5f, my + bh * 0.55f + gp, mx + bw * 0.62f + 1.5f, my + bh * 0.55f + gp, mx, my + bh + gp + 8, ln);
      cv.tri(mx - bw * 0.62f, my + bh * 0.55f + gp + 1, mx + bw * 0.62f, my + bh * 0.55f + gp + 1, mx, my + bh + gp + 6, dk);
    }
    cv.tri(mx - bw - 2, my - 2, mx + bw + 2, my - 2, mx, my + bh + 3, ln);
    cv.tri(mx - bw, my - 0.5f, mx + bw, my - 0.5f, mx, my + bh, bc);
    cv.tri(mx, my - 0.5f, mx + bw, my - 0.5f, mx, my + bh, dk);
    cv.ellipse(mx - bw * 0.4f, my + 3, 3, 1.6f, to565(mixc(s.mouthC, C(255, 255, 255), 0.55f)));
  }

  // Tinbot's mouth: LED bars behind a grille, in a curve that smiles or frowns; they bounce when it talks
  void drawEqMouth(float mx, float my, float mw, float open, const Pose& p) {
    Canvas& cv = *k.cv;
    const StyleDef& s = k.s;
    const int N = 7;
    const float wid = 58 * clampf(mw / 40, 0.45f, 1.3f), sp = wid / (N - 1);
    cv.rrect(mx - wid / 2 - 12, my - 17, wid + 24, 34, 12, to565(mixc(s.bg, s.skin2, 0.75f)));
    cv.rrect(mx - wid / 2 - 9, my - 14, wid + 18, 28, 10, to565(s.mouthIn));
    const uint16_t bc = to565(k.irisC), dim = to565(mixc(s.mouthIn, k.irisC, 0.22f));
    for (int i = 0; i < N; i++) {
      const float u = (float)i / (N - 1) * 2 - 1, x = mx - wid / 2 + i * sp;
      const float yc = my + p.mSmile * 7 * (1 - u * u) - p.mSmile * 3.5f;
      float h = 4.5f + open * 16 * (1 - u * u * 0.55f);
      if (ext.talk) h = 4.5f + open * 18 * (0.4f + 0.6f * fabsf(sinf(t * 9 + i * 1.3f)));
      h = fminf(h, 24 - fabsf(yc - my) * 1.6f);
      cv.rrect(x - 2.6f, my - 11, 5.2f, 22, 2.6f, dim);
      cv.rrect(x - 2.6f, yc - h / 2, 5.2f, h, 2.6f, bc);
    }
  }
  // the cyclops' fang: a small white tooth hanging from (x, y)
  void fang(float x, float y, float sz) {
    k.cv->tri(x - sz * 0.6f - 1.4f, y - 1, x + sz * 0.6f + 1.4f, y - 1, x, y + sz + 2, to565(k.s.lash));
    k.cv->tri(x - sz * 0.6f, y, x + sz * 0.6f, y, x, y + sz, to565(C(255, 252, 244)));
  }

  // Puffed-out cheeks. Light faces: the skin bulges - a soft round blush, a curved contour line on
  // the outside and a small shine (a pale ball looked like cotton wool). On black: a round pink
  // glow with a glowing contour and a shine (a dark disc looked like a button). Mouthless styles
  // get them too; the mouth, if there is one, goes small and pursed (drawMouth).
  void drawPuff(const Pose& p) {
    const float a = clampf(p.puff, 0, 1.3f);
    if (a < 0.05f) return;
    const StyleDef& s = k.s;
    Canvas& cv = *k.cv;
    float erx, ery;
    eyeSize(0, p, erx, ery);
    const bool light = lightBg();
    const RGB lid = (s.mouth == M_CAT || s.mouth == M_DOG) && s.head != H_FOX ? s.muzzle : lidCol();  // (the fox's cheeks are fur)
    const RGB bl = light ? mixc(s.blush, C(255, 150, 175), 0.45f) : s.blush;  // (pink on green skin went muddy)
    const float r = 15 + 9 * a;
    const float sp = s.single ? erx * 0.9f + r * 0.35f : s.gap * p.gap * 0.5f + 4 + 4 * a;
    float cy = k.cy + ery * (light ? 0.85f : 1.0f) + r * (light ? 0.55f : 0.42f);  // on black: fully below the eyes
    if (s.mouth != M_NONE && s.mouth != M_BEAK && s.head != H_FOX) {
      float mx, my;
      mouthPos(p, mx, my);
      cy = fmaxf(cy, my - 10);
    }
    const RGB line = light ? mulc(mixc(lid, bl, 0.45f), 0.8f) : mixc(lid, bl, 0.9f);
    const RGB shine = mixc(lid, C(255, 255, 255), light ? 0.72f : 0.62f);
    for (int sd = -1; sd <= 1; sd += 2) {
      const float x = k.cx + sd * sp, y = cy;
      if (k.st == ST_COMIC) {  // comic: a halftone flush and a bold ink contour
        const uint16_t dc = to565(s.blush);
        for (int j = -2; j <= 2; j++)
          for (int i = -3; i <= 3; i++) {
            const float dx = i * 7 + (j & 1) * 3.5f, dy = j * 6.5f, d = sqrtf(dx * dx + dy * dy) / (r * 1.05f);
            const float rad = 3.6f * (1 - d * d);
            if (rad > 0.8f) cv.circleFast(x + dx, y + r * 0.1f + dy, rad, dc);
          }
        curve(x + sd * r * 0.55f, y - r * 0.8f, x + sd * r * 1.35f, y + r * 0.25f, x + sd * r * 0.1f, y + r * 1.0f, 2.6f, to565(s.lash), 8, false);
        cv.ellipse(x - sd * r * 0.3f, y - r * 0.42f, r * 0.2f, r * 0.12f, to565(C(255, 255, 255)));
        continue;
      }
      if (k.st == ST_TINBOT) {  // tinbot: the cheek panels pop out as round domes, their LEDs lit
        cv.circle(x, y, r + 2, to565(mulc(s.skin2, 0.8f)));
        cv.circle(x - 1, y - 1, r, to565(mixc(s.bg, s.deco1, 0.5f)));
        cv.circle(x + 1.5f, y + 2, r * 0.78f, to565(mixc(s.bg, s.deco1, 0.15f)));
        cv.ellipse(x - r * 0.38f, y - r * 0.45f, r * 0.26f, r * 0.14f, to565(C(255, 255, 255)));
        for (int i = -1; i <= 1; i++) {
          cv.circle(x + i * 8, y + r * 0.2f, 3.2f, to565(s.blush));
          cv.circleFast(x + i * 8 - 1, y + r * 0.2f - 1, 1, to565(mixc(s.blush, C(255, 255, 255), 0.6f)));
        }
        continue;
      }
      if (light) {
        cv.circle(x, y, r, to565(mixc(lid, bl, 0.1f + 0.05f * a)));
        cv.circle(x + sd * r * 0.05f, y + r * 0.08f, r * 0.7f, to565(mixc(lid, bl, 0.22f + 0.08f * a)));
        cv.circle(x + sd * r * 0.08f, y + r * 0.12f, r * 0.42f, to565(mixc(lid, bl, 0.34f + 0.1f * a)));
        curve(x + sd * r * 0.5f, y - r * 0.88f, x + sd * r * 1.32f, y + r * 0.3f, x + sd * r * 0.05f, y + r * 1.05f, 1.4f, to565(line), 8, false);
        cv.ellipse(x - sd * r * 0.3f, y - r * 0.42f, r * 0.2f, r * 0.12f, to565(shine));
      } else {  // on black: a bigger soft blush (any filled ball read as a button) and a glowing "(" for the puff
        const float by = y + r * 0.15f, base = 0.5f + 0.4f * fminf(1, a);
        cv.ellipse(x, by, r * 1.15f, r * 0.52f, to565(mixc(lid, bl, base * 0.32f)));
        cv.ellipse(x, by, r * 0.9f, r * 0.4f, to565(mixc(lid, bl, base * 0.66f)));
        cv.ellipse(x, by + 0.5f, r * 0.58f, r * 0.26f, to565(mixc(lid, bl, fminf(1, base * 1.05f))));
        cv.circleFast(x - sd * r * 0.35f, by - r * 0.12f, 1.6f, to565(shine));
        curve(x + sd * r * 0.6f, y - r * 0.7f, x + sd * r * 1.45f, y + r * 0.2f, x + sd * r * 0.25f, y + r * 0.9f, 1.6f, to565(line), 8, false);
      }
    }
  }

  void drawNose(const Pose& p) {
    Canvas& cv = *k.cv;
    const StyleDef& s = k.s;
    const float nx = k.cx + p.mX * 6, ny = k.cy + s.ry + 20;
    if (k.st == ST_REAL) {
      cv.ellipse(nx, ny + 4, 14, 7, to565(mixc(s.skin, s.nose, 0.6f)));
      cv.ellipse(nx - 6, ny + 6, 3.5f, 2.2f, to565(mixc(s.nose, C(90, 50, 40), 0.55f)));
      cv.ellipse(nx + 6, ny + 6, 3.5f, 2.2f, to565(mixc(s.nose, C(90, 50, 40), 0.55f)));
      cv.ellipse(nx - 2, ny - 1, 4, 3, to565(mixc(s.skin, C(255, 240, 225), 0.4f)));
    } else if (k.st == ST_TOON) {
      cv.ellipse(nx, ny + 2, 12, 8, to565(s.nose));
      cv.ellipse(nx - 3, ny - 1, 4, 3, to565(mixc(s.nose, C(255, 245, 235), 0.6f)));
    } else if (k.st == ST_CAT) {
      const float y = k.cy + s.ry + 22;
      cv.tri(nx - 11, y - 5, nx + 11, y - 5, nx, y + 7, to565(s.nose));
      cv.circle(nx, y - 3, 5, to565(s.nose));
      const uint16_t wc = to565(C(252, 248, 240));
      for (int side = 0; side < 2; side++) {
        const float sg = side == 0 ? -1.0f : 1.0f;
        for (int i = -1; i <= 1; i++) cv.line(nx + sg * 46, y + 14 + i * 7, nx + sg * 112, y + 6 + i * 16, 1.1f, wc);
      }
    } else if (k.st == ST_FOX) {  // a little black button nose at the top of the muzzle
      float mx, my;
      mouthPos(p, mx, my);
      const float y = my - 17;
      cv.ellipse(mx, y, 10, 6.5f, to565(s.nose));
      cv.tri(mx - 8, y + 1, mx + 8, y + 1, mx, y + 9, to565(s.nose));
      cv.ellipse(mx - 3, y - 2.5f, 3.5f, 1.8f, to565(mixc(s.nose, C(255, 255, 255), 0.5f)));
    } else if (k.st == ST_PUPPY) {
      const float y = k.cy + s.ry + 24;
      cv.ellipse(nx, y, 19, 13, to565(mulc(s.deco1, 0.55f)));
      cv.ellipse(nx, y, 17, 11, to565(s.nose));
      cv.ellipse(nx - 5, y - 4, 6, 3, to565(C(230, 210, 200)));
    }
  }

  void drawMouth(const Pose& p) {
    const StyleDef& s = k.s;
    if (p.mW < 0.05f || s.mouth == M_NONE) return;
    Canvas& cv = *k.cv;
    const float puff = clampf(p.puff, 0, 1);
    const bool voice = ext.talk && faceVoiceOpen >= 0;  // lip sync: the real voice shapes the mouth
    const float mw = (s.mouth == M_CUTE ? 26 : 40) * p.mW * (1 - 0.55f * puff) * (voice ? 0.7f + 0.6f * faceVoiceWide : 1);
    float mx, my;
    mouthPos(p, mx, my);
    float open = p.mOpen;
    if (voice) open = fmaxf(open * 0.3f, faceVoiceOpen);
    else if (ext.talk) open = fmaxf(open, 0.2f + 0.4f * fabsf(sinf(t * 11.0f) * sinf(t * 4.3f + 1.0f)));
    open *= 1 - 0.8f * puff;
    if (s.mouth == M_BEAK) { drawBeak(mx, my, open); return; }
    if (s.mouth == M_EQ) { drawEqMouth(mx, my, mw, open, p); return; }
    const bool lips = s.mouth == M_LIPS;
    const uint16_t lc = to565(s.eye == E_PILL ? k.eyeC : s.mouthC);
    const float lw = s.lineW > 0 ? s.lineW : lips ? 2.2f : k.st == ST_ANIME ? 2.0f : 2.6f;
    const uint16_t around = to565(s.mouth == M_CAT || s.mouth == M_DOG ? s.muzzle : lidCol());
    if (open > 0.1f) {
      const float rx = mw * (0.35f + 0.25f * fabsf(p.mSmile)) + 3, ry = 3 + open * 13;
      const float ow = lips ? 4 : s.lineW > 0 ? s.lineW * 0.9f : 2.5f;
      cv.ellipse(mx, my, rx + ow, ry + ow, lc);
      cv.ellipse(mx, my, rx, ry, to565(s.eye == E_PILL ? mulc(k.eyeC, 0.35f) : s.mouthIn));
      if (open > 0.3f && lips) cv.rrect(mx - rx * 0.7f, my - ry, rx * 1.4f, ry * 0.35f, 2, to565(C(250, 248, 240)));
      if (p.mTongue > 0.3f || (p.mSmile > 0.5f && open > 0.35f)) cv.ellipse(mx, my + ry * 0.45f, rx * 0.6f, ry * 0.45f, to565(s.tongue));
      const float ext2 = s.lineW > 0 ? ow - 2.5f : 0;  // a bold outline: cover a little more
      if (p.mSmile > 0.35f) {
        cv.rrect(mx - rx - 6, my - ry - 8 - ext2, 2 * rx + 12, ry * 0.55f + 8 - 2.5f + ext2, 0, around);
        cv.line(mx - rx - 1, my - ry * 0.45f + 2.5f, mx + rx + 1, my - ry * 0.45f + 2.5f, lips ? 3.5f : s.lineW > 0 ? s.lineW * 0.8f : 2.2f, lc);
      } else if (p.mSmile < -0.35f) {
        cv.rrect(mx - rx - 6, my + ry * 0.45f + 2.5f, 2 * rx + 12, ry + 8 + ext2, 0, around);
      }
      if (k.st == ST_CYCLOPS) fang(mx + rx * 0.42f, p.mSmile > 0.35f ? my - ry * 0.45f + 3.5f : my - ry + 1, 7);
      return;
    }
    if (s.mouth == M_CUTE) {  // a tiny w, closer to the eyes
      const float sz = 5 + 4 * p.mW, yy = k.cy + s.ry + 16;
      const float xx = k.cx + p.mX * 20 - sinf(k.tiltR) * 30;
      const float dip = sz * (0.4f + 0.8f * clampf(p.mSmile, -0.2f, 1));
      curve(xx - 2 * sz, yy - 1, xx - sz, yy + dip, xx, yy - 1, 2.4f, lc, 6);
      curve(xx, yy - 1, xx + sz, yy + dip, xx + 2 * sz, yy - 1, 2.4f, lc, 6);
      if (p.mTongue > 0.3f) cv.rrect(xx - 5, yy + 2, 10, 11, 5, to565(s.tongue));
      return;
    }
    if (s.mouth == M_CAT || s.mouth == M_DOG || p.mCat > 0.5f) {  // "w"
      const float sz = mw * 0.3f + 6;
      curve(mx - 2 * sz, my - 2, mx - sz, my + sz * 0.9f * (0.5f + p.mSmile), mx, my - 2, lw, lc, 8);
      curve(mx, my - 2, mx + sz, my + sz * 0.9f * (0.5f + p.mSmile), mx + 2 * sz, my - 2, lw, lc, 8);
      if (s.mouth == M_DOG || s.mouth == M_CAT) cv.line(mx, my - 14, mx, my - 2, lw, lc);
      if (p.mTongue > 0.3f) { cv.rrect(mx - 7, my, 14, 16, 7, to565(s.tongue)); cv.line(mx, my + 3, mx, my + 12, 1, to565(mulc(s.tongue, 0.75f))); }
      return;
    }
    float xs[17], ys[17];
    const int N = 10;
    for (int i = 0; i <= N; i++) {
      const float u = (float)i / N * 2 - 1;
      xs[i] = mx + u * mw / 2;
      ys[i] = my + p.mSmile * 8 * (1 - u * u) - p.mSmile * 3 + p.mWavy * 2.6f * sinf(u * 3 * PI_F + t * 2);
    }
    if (lips) {  // upper lip, lower lip (fuller, lighter), the line between
      float up[17], lo[17];
      for (int i = 0; i <= N; i++) {
        const float u = (float)i / N * 2 - 1, env = 1 - u * u;
        up[i] = ys[i] - 2.5f * env - 1;
        lo[i] = ys[i] + 4.5f * env + 1;
      }
      polyline(xs, up, N + 1, 2.4f, to565(mulc(s.mouthC, 0.85f)));
      polyline(xs, lo, N + 1, 3.6f, to565(mixc(s.mouthC, C(255, 200, 190), 0.2f)));
      polyline(xs, ys, N + 1, 1.2f, to565(mulc(s.mouthIn, 0.9f)));
    } else {
      if (k.st == ST_CYCLOPS) fang(xs[7], ys[7] + 1, 7 * clampf(p.mW, 0.5f, 1.2f));
      if (s.eye == E_NEON) polyline(xs, ys, N + 1, lw + 2.2f, to565(mulc(k.glowC, 0.3f)), false);
      polyline(xs, ys, N + 1, lw, lc);
    }
    if (p.mTongue > 0.3f) {
      cv.rrect(mx - 7, my + p.mSmile * 5, 14, 14, 6, to565(s.tongue));
      cv.line(mx, my + p.mSmile * 5 + 3, mx, my + p.mSmile * 5 + 10, 1, to565(mulc(s.tongue, 0.75f)));
    }
  }

  void drawLoading() {
    Canvas& cv = *k.cv;
    const RGB c = lightBg() ? k.irisC : k.glowC;
    for (int i = 0; i < 10; i++) {
      const float a = i * 2 * PI_F / 10 - PI_F / 2;
      const float ph = fmodf(t * 1.2f - i / 10.0f + 10, 1.0f);
      const float b = 0.25f + 0.75f * powf(1 - ph, 3);
      cv.circle(k.cx + cosf(a) * 44, k.cy + sinf(a) * 44, 4 + 3 * b, to565(mixc(lidCol(), c, b)));
    }
  }

  // ---- effects ----
  Particle* spawn() {
    for (int i = 0; i < MAXP; i++) if (parts[i].life <= 0) return &parts[i];
    return nullptr;
  }
  void emit(uint8_t fx, int slot) {
    if (fx == FX_NONE || t < fxNext[slot]) return;
    Particle* q = spawn();
    if (!q) return;
    const float cx = W / 2, cy = 104 + cur.y;
    q->kind = fx;
    q->vx = q->vy = 0;
    q->rot = rr(0, 6);
    q->col = (uint8_t)(rv() * 5);
    float gapNext = 1;
    switch (fx) {
      case FX_HEARTS: q->x = cx + rr(-130, 130); q->y = cy + 40; q->vx = rr(-10, 10); q->vy = rr(-65, -40); q->max = 2.2f; q->size = rr(7, 13); gapNext = rr(0.45f, 0.95f); break;
      case FX_TEARS: { const float s = chance(0.5f) ? -1 : 1; q->x = cx + s * 100; q->y = cy - 10; q->vx = s * 50; q->vy = -60; q->max = 1.0f; q->size = 5; gapNext = rr(0.25f, 0.55f); break; }
      case FX_CRY: { const float s = chance(0.5f) ? -1 : 1; q->x = cx + s * rr(66, 78); q->y = cy + 28; q->vy = 90; q->max = 1.2f; q->size = 5; gapNext = 0.12f; break; }
      case FX_ZZZ: q->x = cx + 100; q->y = cy - 36; q->vx = rr(10, 18); q->vy = -22; q->max = 2.6f; q->size = 8; gapNext = rr(0.9f, 1.4f); break;
      case FX_SPARKLE: q->x = cx + rr(-150, 150); q->y = cy + rr(-80, 80); q->vy = -6; q->max = 0.7f; q->size = rr(6, 14); gapNext = rr(0.3f, 0.8f); break;
      case FX_STARS: q->x = cx + rr(-150, 150); q->y = cy + rr(-95, -30); q->max = rr(0.8f, 1.6f); q->size = rr(3, 6); gapNext = rr(0.15f, 0.4f); break;
      case FX_CONFETTI: q->x = rr(0, W); q->y = -6; q->vx = rr(-30, 30); q->vy = rr(30, 70); q->max = 2.5f; q->size = rr(4, 7); gapNext = 0.06f; break;
      case FX_NOTES: q->x = cx + (chance(0.5f) ? -1 : 1) * rr(100, 130); q->y = cy + 10; q->vx = rr(-10, 10); q->vy = -35; q->max = 2.0f; q->size = 9; gapNext = rr(0.5f, 0.9f); break;
      case FX_SWEAT: q->x = cx + 118; q->y = cy - 60; q->vy = 24; q->max = 1.6f; q->size = 7; gapNext = rr(1.4f, 2.2f); break;
      case FX_STEAM: q->x = cx + (chance(0.5f) ? -1 : 1) * 120; q->y = cy - 70; q->vy = -30; q->max = 1.0f; q->size = 8; gapNext = 0.4f; break;
      case FX_MONEY: q->x = cx + rr(-140, 140); q->y = cy + 60; q->vy = rr(-55, -35); q->vx = rr(-8, 8); q->max = 2.0f; q->size = rr(8, 12); gapNext = rr(0.3f, 0.6f); break;
      case FX_ICE: q->x = rr(0, W); q->y = -8; q->vx = rr(-12, 12); q->vy = rr(18, 34); q->max = 5.0f; q->size = rr(4, 8); gapNext = rr(0.15f, 0.35f); break;
      case FX_BUBBLES: q->x = cx + rr(-40, 40); q->y = cy + 50; q->vx = rr(-10, 10); q->vy = rr(-45, -25); q->max = 1.6f; q->size = rr(3, 7); gapNext = rr(0.3f, 0.8f); break;
      case FX_KISS: q->x = cx + 20; q->y = cy + 58; q->vx = rr(30, 60); q->vy = rr(-45, -25); q->max = 1.6f; q->size = rr(7, 11); gapNext = rr(0.7f, 1.3f); break;
      case FX_FLOWERS: q->x = cx + rr(-150, 150); q->y = cy + rr(-90, 70); q->vy = -5; q->max = rr(1.2f, 2.0f); q->size = rr(5, 9); gapNext = rr(0.35f, 0.7f); break;
      case FX_CLOUD: q->x = cx + 60 + rr(-30, 30); q->y = cy - 70; q->vy = 120; q->max = 0.6f; q->size = 2; gapNext = 0.07f; break;  // rain drops
      case FX_ACHOO: {  // one burst of spray and puffs from the mouth, right as the sneeze lands
        if (t - fxStart > 0.75f) { q->life = 0; fxNext[slot] = t + 100; return; }
        const float a = rr(-0.35f, PI_F + 0.35f), v = rr(90, 210);
        q->x = cx + rr(-8, 8);
        q->y = cy + 62;
        q->vx = cosf(a) * v;
        q->vy = sinf(a) * v * 0.6f - 25;
        const bool puff = q->col < 2;
        if (puff) { q->vx *= 0.45f; q->vy *= 0.45f; }
        q->max = puff ? rr(0.5f, 0.8f) : rr(0.35f, 0.6f);
        q->size = puff ? rr(5, 9) : rr(1.6f, 3.2f);
        gapNext = 0.02f;
        break;
      }
      default: q->life = 0; return;  // the rest are badges, not particles
    }
    q->life = q->max;
    fxNext[slot] = t + gapNext;
  }

  // Badges: a fixed icon with its own little animation; pops in when the expression starts
  void badge(uint8_t fx, const Pose& p) {
    Canvas& cv = *k.cv;
    const float age = t - fxStart;
    const float pop = age < 0.35f ? sinf(age / 0.35f * PI_F * 0.5f) * (1 + 0.2f * sinf(age / 0.35f * PI_F)) : 1;
    const float bx = k.cx + 112, by = k.cy - 78;
    const bool light = lightBg();
    const RGB white = light ? C(70, 55, 60) : C(235, 245, 255);
    switch (fx) {
      case FX_QUESTION: {
        const float s = 17 * pop, y = by + sinf(t * 3) * 3;
        const uint16_t c = to565(k.s.eye == E_NEON ? C(80, 200, 255) : white);
        float xs[10], ys[10];
        for (int i = 0; i < 9; i++) { const float an = PI_F - i * PI_F * 1.25f / 8; xs[i] = bx + cosf(an) * s * 0.5f; ys[i] = y - s * 0.45f - sinf(an) * s * 0.5f; }
        polyline(xs, ys, 9, 2.8f, c);
        cv.line(xs[8], ys[8], bx, y + s * 0.3f, 2.8f, c);
        cv.circle(bx, y + s * 0.85f, 3.2f, c);
        break;
      }
      case FX_EXCLAIM: {
        const uint16_t c = to565(C(255, 60, 60));
        for (int i = 0; i < 2; i++) {
          const float x = bx - 8 + i * 14, y = by + (i ? -4 : 0);
          cv.line(x, y - 16 * pop, x - 2, y + 4 * pop, 3.5f, c);
          cv.circle(x - 2.5f, y + 12 * pop, 3.5f * pop, c);
        }
        break;
      }
      case FX_SHOCK: {
        const uint16_t c = to565(p.tint > 0.5f ? C(p.tr, p.tg, p.tb) : C(255, 200, 40));
        for (int i = 0; i < 3; i++) {
          const float a = (-130 + i * 40) * PI_F / 180, r0 = 12, r1 = 12 + 16 * pop;
          cv.line(bx - 8 + cosf(a) * r0, by + 6 + sinf(a) * r0, bx - 8 + cosf(a) * r1, by + 6 + sinf(a) * r1, 2.8f, c);
        }
        break;
      }
      case FX_ANGER: {
        const float s = 12 * pop * (0.9f + 0.2f * sinf(age * 12));
        const uint16_t c = to565(C(255, 45, 45));
        const float x = k.cx - 118, y = k.cy - 80;
        for (int q = 0; q < 4; q++) {
          const float sx = (q & 1) ? 1 : -1, sy = (q & 2) ? 1 : -1;
          cv.line(x + sx * s, y + sy * s * 0.3f, x + sx * s * 0.3f, y + sy * s * 0.3f, 2.6f, c);
          cv.line(x + sx * s * 0.3f, y + sy * s * 0.3f, x + sx * s * 0.3f, y + sy * s, 2.6f, c);
        }
        break;
      }
      case FX_BOLT: {
        const float s = 22 * pop, x = k.cx, y = k.cy - 82 + sinf(t * 4) * 2;
        cv.circle(x, y, s * 1.1f, to565(mixc(lidCol(), C(90, 255, 90), 0.3f)));
        const uint16_t c = to565(C(80, 220, 80));
        cv.tri(x + s * 0.2f, y - s, x - s * 0.5f, y + s * 0.15f, x + s * 0.05f, y + s * 0.15f, c);
        cv.tri(x - s * 0.2f, y + s, x + s * 0.5f, y - s * 0.15f, x - s * 0.05f, y - s * 0.15f, c);
        break;
      }
      case FX_BULB: {
        const float s = 16 * pop, x = k.cx, y = k.cy - 88;
        const float gl = 0.6f + 0.4f * sinf(t * 5);
        for (int i = 0; i < 7; i++) {
          const float a = (-180 + i * 30) * PI_F / 180;
          cv.line(x + cosf(a) * s * 1.35f, y + sinf(a) * s * 1.35f, x + cosf(a) * s * 1.8f, y + sinf(a) * s * 1.8f, 1.8f, to565(mixc(lidCol(), C(255, 200, 40), gl)));
        }
        cv.circle(x, y, s, to565(C(255, 215, 60)));
        cv.rrect(x - s * 0.45f, y + s * 0.7f, s * 0.9f, s * 0.6f, 2, to565(C(150, 150, 160)));
        cv.circle(x - s * 0.3f, y - s * 0.3f, s * 0.25f, to565(C(255, 250, 220)));
        break;
      }
      case FX_WIFI:
      case FX_WIFIX: {
        const RGB wc = light ? C(60, 120, 200) : C(90, 200, 255);
        const float x = k.cx, y = k.cy - 60;
        const int lit = fx == FX_WIFIX ? 0 : (int)fmodf(t * 3, 4);
        for (int i = 0; i < 3; i++) {
          const float r = 12 + i * 12;
          float xs[9], ys[9];
          for (int j = 0; j < 9; j++) { const float a = (-135 + j * 90 / 8.0f) * PI_F / 180; xs[j] = x + cosf(a) * r; ys[j] = y + sinf(a) * r; }
          polyline(xs, ys, 9, 3, to565(i < lit ? wc : mixc(lidCol(), wc, 0.3f)));
        }
        cv.circle(x, y, 4, to565(wc));
        if (fx == FX_WIFIX) {
          const uint16_t rc = to565(C(255, 60, 60));
          cv.line(x + 22, y - 30, x + 38, y - 14, 3, rc);
          cv.line(x + 22, y - 14, x + 38, y - 30, 3, rc);
        }
        break;
      }
      case FX_BATTERY:
      case FX_BATTFULL: {
        const float x = k.cx + 96, y = k.cy - 84;
        const uint16_t f = to565(fx == FX_BATTERY ? C(255, 70, 70) : C(80, 210, 100));
        cv.rrect(x - 2, y - 2, 40, 22, 4, f);
        cv.rrect(x + 1, y + 1, 34, 16, 3, to565(lidCol()));
        cv.rrect(x + 38, y + 5, 4, 8, 1, f);
        if (fx == FX_BATTERY) { if (fmodf(t, 1.0f) < 0.6f) cv.rrect(x + 3, y + 3, 8, 12, 1, f); }
        else for (int i = 0; i < 4; i++) cv.rrect(x + 3 + i * 8, y + 3, 6, 12, 1, f);
        break;
      }
      case FX_LISTEN:
      case FX_SPEAK: {
        const RGB c = light ? C(80, 110, 220) : C(70, 140, 255);
        for (int side = 0; side < 2; side++) {
          if (fx == FX_SPEAK && side == 0) continue;
          const float s = side == 0 ? -1.0f : 1.0f;
          const float x = k.cx + s * (fx == FX_SPEAK ? 58 : 140), y = fx == FX_SPEAK ? k.cy + k.s.ry + 30 : k.cy;
          for (int i = 0; i < 3; i++) {
            const float ph = fmodf(t * 1.5f + i / 3.0f, 1.0f);
            const float r = 8 + i * 9 + ph * 6;
            float xs[7], ys[7];
            for (int j = 0; j < 7; j++) { const float a = (s > 0 ? -40 + j * 80 / 6.0f : 140 + j * 80 / 6.0f) * PI_F / 180; xs[j] = x + cosf(a) * r; ys[j] = y + sinf(a) * r; }
            polyline(xs, ys, 7, 2.6f, to565(mixc(lidCol(), c, 1 - ph * 0.7f)));
          }
        }
        break;
      }
      case FX_DIZZY: {
        for (int i = 0; i < 3; i++) {
          const float a = t * 3.2f + i * 2.094f;
          star5(k.cx + cosf(a) * 60, k.cy - 82 + sinf(a) * 14, 7 + 2 * sinf(a), to565(C(255, 200, 40)));
        }
        break;
      }
      case FX_SHINE: {
        const RGB c = C(p.tr, p.tg, p.tb);
        for (int i = 0; i < 10; i++) {
          const float a = i * 2 * PI_F / 10 + t * 0.6f, ph = fmodf(t * 1.2f + i * 0.13f, 1.0f);
          const float r0 = 118 + ph * 10, r1 = r0 + 10 * pop;
          cv.line(k.cx + cosf(a) * r0, k.cy + sinf(a) * r0 * 0.75f, k.cx + cosf(a) * r1, k.cy + sinf(a) * r1 * 0.75f, 2.2f, to565(mixc(lidCol(), c, 1 - ph)));
        }
        break;
      }
      case FX_DOTS: {
        for (int i = 0; i < 3; i++) {
          const float on = fmodf(age * 1.5f, 1.6f) * 3 - i * 0.6f;
          const float up = on > 0 && on < 1 ? sinf(on * PI_F) * 5 : 0;
          cv.circle(k.cx + 64 + i * 16, k.cy - 82 - up, 5, to565(white));
        }
        break;
      }
      case FX_BLUSHLINES: {
        const uint16_t c = to565(mixc(lidCol(), k.s.blush, 0.9f));
        for (int side = 0; side < 2; side++) {
          float rx, ry;
          eyeSize(side, p, rx, ry);
          const float x = k.cx + (side == 0 ? -1 : 1) * (k.s.gap * p.gap / 2 + 14), y = k.cy + ry + 14;
          for (int i = -1; i <= 1; i++) cv.line(x + i * 7 - 3, y + 5, x + i * 7 + 3, y - 5, 1.6f, c);
        }
        break;
      }
      case FX_CLOUD: {  // a grey rain cloud (the drops are particles)
        const float x = k.cx + 60, y = k.cy - 90 + sinf(t * 1.5f) * 2;
        const uint16_t c = to565(C(150, 156, 172));
        cv.circle(x - 22, y + 4, 14 * pop, c);
        cv.circle(x, y - 4, 19 * pop, c);
        cv.circle(x + 22, y + 4, 14 * pop, c);
        cv.rrect(x - 34, y + 2, 68, 16 * pop, 8, c);
        break;
      }
      case FX_FIRE: {
        for (int side = 0; side < 2; side++) {
          const float x = k.cx + (side == 0 ? -120 : 120), y = k.cy - 60;
          const float f = 0.8f + 0.25f * sinf(t * 17 + side * 2) * sinf(t * 7);
          cv.tri(x - 12, y + 10, x + 12, y + 10, x, y - 26 * f * pop, to565(C(255, 90, 30)));
          cv.tri(x - 7, y + 10, x + 7, y + 10, x, y - 14 * f * pop, to565(C(255, 210, 60)));
        }
        break;
      }
      case FX_HEAT: {
        const uint16_t c = to565(C(255, 120, 80));
        for (int i = 0; i < 3; i++) {
          const float x = k.cx - 30 + i * 30;
          float xs[8], ys[8];
          for (int j = 0; j < 8; j++) { ys[j] = k.cy - 70 - j * 5; xs[j] = x + 4 * sinf(j * 1.2f + t * 6 + i); }
          polyline(xs, ys, 8, 1.8f, c);
        }
        break;
      }
      case FX_BUBBLE: {  // the snore / sneeze bubble, growing and shrinking
        const float g = 0.5f + 0.5f * sinf(t * 1.6f);
        const float x = k.cx + 26, y = k.cy + k.s.ry + 26;
        ring(x + 10 * g, y, 6 + 12 * g, 1.5f, to565(light ? C(120, 180, 230) : C(140, 200, 255)));
        cv.circle(x + 6 * g, y - 5 * g, 2 + 2 * g, to565(C(255, 255, 255)));
        break;
      }
      case FX_GLITCH: {
        const RGB cols[3] = {C(255, 40, 80), C(40, 255, 220), C(255, 255, 255)};
        for (int i = 0; i < 5; i++) {
          if (rv() < 0.4f) continue;
          cv.rrect(rr(0, W - 60), rr(0, H), rr(20, 90), rr(2, 6), 0, to565(cols[(int)(rv() * 3) % 3]));
        }
        break;
      }
      case FX_SCAN: {
        const float y = k.cy - 50 + fmodf(t * 70, 100);
        const RGB c = light ? C(30, 150, 70) : C(90, 255, 140);
        cv.rrect(k.cx - 140, y, 280, 2, 0, to565(c));
        cv.rrect(k.cx - 140, y - 4, 280, 1, 0, to565(mixc(lidCol(), c, 0.4f)));
        break;
      }
      case FX_DROOL: {
        const float ph = fmodf(t * 0.5f, 1.0f);
        const float x = k.cx + 22, y = k.cy + k.s.ry + 34;
        const uint16_t c = to565(C(150, 210, 255));
        cv.line(x, y, x, y + 4 + ph * 22, 2.2f, c);
        cv.circle(x, y + 6 + ph * 22, 3.5f, c);
        break;
      }
      case FX_GLOOM: {
        const uint16_t c = to565(mixc(lidCol(), C(110, 90, 170), 0.8f));
        for (int i = 0; i < 6; i++) {
          const float x = k.cx - 70 + i * 28, y0 = k.cy - 110, len = 26 + 10 * sinf(i * 1.7f + t * 2);
          cv.line(x, y0, x, y0 + len * pop, 2.2f, c);
        }
        break;
      }
      case FX_SWEATBIG: {
        const float x = k.cx + 116, y = k.cy - 68 + fmodf(age * 6, 6);
        drop(x, y, 11, to565(C(130, 200, 255)));
        cv.circle(x - 3, y - 3, 3, to565(C(235, 248, 255)));
        break;
      }
      case FX_PROGRESS: {
        const float x = k.cx - 70, y = k.cy + 70, w = 140;
        const float f = fmodf(t * 0.25f, 1.0f);
        const RGB c = light ? C(60, 160, 90) : C(90, 230, 130);
        cv.rrect(x - 2, y - 2, w + 4, 14, 6, to565(c));
        cv.rrect(x, y, w, 10, 5, to565(lidCol()));
        cv.rrect(x + 1, y + 1, (w - 2) * f, 8, 4, to565(c));
        break;
      }
      case FX_HALO: {
        const float y = k.cy - 88 + sinf(t * 2) * 3;
        float xs[25], ys[25];
        for (int i = 0; i <= 24; i++) { const float a = i * 2 * PI_F / 24; xs[i] = k.cx + cosf(a) * 46; ys[i] = y + sinf(a) * 10; }
        polyline(xs, ys, 25, 3.2f, to565(C(255, 215, 90)));
        break;
      }
      case FX_HORNS: {
        const uint16_t c = to565(C(220, 40, 40));
        for (int side = 0; side < 2; side++) {
          const float sg = side == 0 ? -1.0f : 1.0f, x = k.cx + sg * 70, y = k.cy - 78;
          cv.tri(x - 12, y + 6, x + 12, y + 6, x + sg * 10, y - 26 * pop, c);
        }
        break;
      }
      default: break;
    }
  }

  void drawEffects(const Pose& p) {
    badge(ext.fx1, p);
    if (ext.fx2 != ext.fx1) badge(ext.fx2, p);
    Canvas& cv = *k.cv;
    const RGB conf[5] = {C(255, 90, 90), C(255, 210, 60), C(90, 220, 120), C(90, 170, 255), C(230, 110, 255)};
    const bool light = lightBg();
    for (int i = 0; i < MAXP; i++) {
      const Particle& q = parts[i];
      if (q.life <= 0) continue;
      const float age = q.max - q.life;
      float a = fminf(1, q.life / (q.max * 0.35f));
      if (age < 0.15f) a *= age / 0.15f;
      switch (q.kind) {
        case FX_HEARTS: case FX_KISS: heart(q.x, q.y, q.size * (0.8f + 0.2f * sinf(t * 8)), fadeCol(C(255, 70, 130), a)); break;
        case FX_TEARS: case FX_CRY: case FX_SWEAT: drop(q.x, q.y, q.size, fadeCol(q.kind == FX_SWEAT ? C(130, 200, 255) : C(100, 170, 255), a)); break;
        case FX_CLOUD: cv.line(q.x, q.y, q.x - 1, q.y + 6, 1.2f, fadeCol(C(110, 160, 255), a)); break;
        case FX_ZZZ: {
          const uint16_t c = fadeCol(light ? C(80, 80, 170) : C(170, 180, 255), a);
          const float s = q.size * (1 + age * 0.35f);
          cv.line(q.x - s, q.y - s, q.x + s, q.y - s, 2, c);
          cv.line(q.x + s, q.y - s, q.x - s, q.y + s, 2, c);
          cv.line(q.x - s, q.y + s, q.x + s, q.y + s, 2, c);
          break;
        }
        case FX_SPARKLE: star4(q.x, q.y, q.size * sinf(fminf(1, age / q.max) * PI_F), 0.22f, fadeCol(light ? C(255, 170, 30) : C(255, 245, 190), a)); break;
        case FX_STARS: star4(q.x, q.y, q.size * (0.6f + 0.4f * sinf(age * 9)), 0.25f, fadeCol(light ? C(250, 180, 40) : C(255, 255, 220), a)); break;
        case FX_CONFETTI: {
          const float c1 = cosf(q.rot) * q.size, s1 = sinf(q.rot) * q.size * 0.5f;
          cv.tri(q.x - c1, q.y - s1, q.x + c1, q.y + s1, q.x + s1, q.y - c1 * 0.5f, fadeCol(conf[q.col % 5], a));
          break;
        }
        case FX_NOTES: {
          const uint16_t c = fadeCol(light ? C(110, 70, 190) : C(170, 150, 255), a);
          cv.ellipse(q.x, q.y, q.size * 0.6f, q.size * 0.45f, c);
          cv.line(q.x + q.size * 0.5f, q.y, q.x + q.size * 0.5f, q.y - q.size * 1.8f, 1.6f, c);
          cv.line(q.x + q.size * 0.5f, q.y - q.size * 1.8f, q.x + q.size * 1.3f, q.y - q.size * 1.3f, 1.6f, c);
          break;
        }
        case FX_STEAM: cv.circle(q.x, q.y, q.size * (1 + age), fadeCol(C(210, 210, 220), a * 0.6f)); break;
        case FX_MONEY: dollar(q.x, q.y, q.size, fadeCol(C(70, 190, 80), a), 2); break;
        case FX_ICE: {
          const uint16_t c = fadeCol(light ? C(120, 170, 230) : C(210, 235, 255), a);
          for (int j = 0; j < 3; j++) { const float an = q.rot * 0.3f + j * PI_F / 3; cv.line(q.x - cosf(an) * q.size, q.y - sinf(an) * q.size, q.x + cosf(an) * q.size, q.y + sinf(an) * q.size, 1.2f, c); }
          break;
        }
        case FX_BUBBLES: ring(q.x, q.y, q.size, 1.2f, fadeCol(light ? C(110, 160, 220) : C(170, 220, 255), a)); break;
        case FX_ACHOO:
          if (q.col < 2) cv.circleFast(q.x, q.y, q.size * (1 + age * 2.5f), fadeCol(light ? C(205, 215, 230) : C(225, 235, 250), a * 0.75f));
          else cv.circleFast(q.x, q.y, q.size, fadeCol(light ? C(110, 170, 235) : C(160, 215, 255), a));
          break;
        case FX_FLOWERS: {
          const uint16_t pc = fadeCol(C(255, 150, 190), a), cc = fadeCol(C(255, 215, 80), a);
          for (int j = 0; j < 5; j++) { const float an = q.rot * 0.2f + j * 2 * PI_F / 5; cv.circle(q.x + cosf(an) * q.size * 0.7f, q.y + sinf(an) * q.size * 0.7f, q.size * 0.45f, pc); }
          cv.circle(q.x, q.y, q.size * 0.35f, cc);
          break;
        }
        default: break;
      }
    }
  }

#include "face_scenes.h"  // play scenes (a fly, a book, the piano ...): part of this class
};

}  // namespace fe
