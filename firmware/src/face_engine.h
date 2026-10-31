// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
// Face engine: procedural, animated eyes (and a small mouth) for the 320 x 240 TFT.
//
// Instead of 1-bit bitmaps swapped a few times a second, every expression is a set of numbers
// (eye size, roundness, eyelids, colour, mouth, blush ...). The face on screen always moves
// towards the chosen expression with a spring - a small overshoot, then it settles - so every
// change is a smooth morph, never a cut. On top of that, what makes it look alive:
//  - blinks that squash the eye (sometimes a double blink), at random intervals
//  - quick little glances (saccades) and slow breathing
//  - squash and stretch on hops (excited, dance)
//  - small particle effects: hearts, tears, zzz, sparkles, anger marks, "?", "...", sweat, dizzy
//
// It only draws through the Canvas interface, so the same code renders on the robot (LovyanGFX
// sprite) and on a PC (to preview the animation before flashing).
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

namespace fe {

const int W = 320, H = 240;

inline uint16_t rgb(int r, int g, int b) {
  r = r < 0 ? 0 : r > 255 ? 255 : r;
  g = g < 0 ? 0 : g > 255 ? 255 : g;
  b = b < 0 ? 0 : b > 255 ? 255 : b;
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

struct Canvas {
  virtual void clear(uint16_t c) = 0;
  virtual void rrect(float x, float y, float w, float h, float r, uint16_t c) = 0;  // filled, smooth edges
  virtual void circle(float x, float y, float r, uint16_t c) = 0;                     // filled, smooth edges
  virtual void ellipse(float x, float y, float rx, float ry, uint16_t c) = 0;         // filled
  virtual void tri(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c) = 0;
  virtual void line(float x0, float y0, float x1, float y1, float r, uint16_t c) = 0; // thick, round ends
  virtual ~Canvas() {}
};

// ---- An expression: all numbers, so two of them can be blended / sprung between ----
struct Pose {
  // eyes: L = the eye on the screen's left, R = on the right
  float w, h;          // eye size (px)
  float round;         // corner roundness 0 (square-ish) .. 1 (pill / circle)
  float lidT;          // top lid closed 0..1
  float lidTA;         // top lid slope (deg): + = inner corners down (cross), - = outer down (sad)
  float lidB;          // bottom lid up 0..1: the ^ ^ of a smile
  float sizeL, sizeR;  // per-eye scale (a raised / squinting eye)
  float dyL, dyR;      // per-eye height offset
  float gap;           // distance between eye centres
  float y;             // eyes' height on screen
  float lookX, lookY;  // where it looks, -1..1 (saccades add to this)
  float cr, cg, cb;    // eye colour 0..255
  float glow;          // soft halo 0..1
  float blush;         // pink cheeks 0..1
  float mSmile;        // mouth curve: -1 frown .. +1 smile
  float mOpen;         // mouth open 0..1 (an "o")
  float mW;            // mouth width (0 = no mouth)
  float xEyes;         // 0 normal .. 1 X X (dead)
  float bounce;        // hop height (px), squash and stretch on landing
  float bounceHz;      // hops per second
  float jitter;        // shiver / dizzy wobble (px)
};
const int NP = sizeof(Pose) / sizeof(float);

enum Effect : uint8_t { FX_NONE, FX_HEARTS, FX_TEARS, FX_ZZZ, FX_SPARKLE, FX_ANGER, FX_QUESTION, FX_DOTS, FX_SWEAT, FX_DIZZY };

struct Expr {
  const char* name;
  Pose p;
  Effect fx;
  bool talk;           // mouth chatters (talk_ faces)
  float blinkEvery;    // mean seconds between blinks (0 = never)
  float glance;        // how much it glances around 0..1
};

const Expr EXPRS[] = {
  // name          w    h   rnd  lidT lidTA lidB  sL   sR   dyL dyR gap  y    lx  ly   r    g    b   glow blush smile open  mW  xE  bnc  bHz  jit     fx          talk  blink glance
  {"neutral",   {64,  84, .55f, .00f,  0, .00f, 1,   1,    0,  0, 132, 108,  0,  0,  140, 235, 255, .35f, 0,   .2f,  0,   0,  0,  0,   0,   0},  FX_NONE,     false, 4.0f, 1.0f},
  {"happy",     {70,  78, .70f, .00f,  0, .55f, 1,   1,    0,  0, 136, 106,  0, -.1f,255, 214, 110, .45f, .35f,.9f,  0,   34, 0,  3,   1.6f,0},  FX_SPARKLE,  false, 5.0f, .6f},
  {"sad",       {58,  74, .60f, .40f,-18, .00f, 1,   1,    0,  0, 128, 118,  0, .45f, 110, 160, 255, .30f, 0,  -.8f,  0,  26, 0,  0,   0,   0},  FX_TEARS,    false, 3.0f, .3f},
  {"angry",     {66,  70, .40f, .42f, 22, .10f, 1,   1,    0,  0, 128, 112,  0,  0,  255,  90,  60, .55f, 0,  -.6f,  0,  30, 0,  0,   0,  .8f}, FX_ANGER,    false, 6.0f, .2f},
  {"surprised", {78, 100, .95f, .00f,  0, .00f, 1.05f,1.05f,0,  0, 140, 104,  0, -.2f,230, 250, 255, .55f, 0,   0,  .85f, 22, 0,  0,   0,   0},  FX_NONE,     false, 9.0f, .2f},
  {"sleepy",    {66,  80, .55f, .72f, -6, .10f, 1,   1,    0,  0, 132, 116,  0, .3f, 150, 160, 255, .15f, 0,   .1f,  0,  22, 0,  0,   0,   0},  FX_ZZZ,      false, 2.5f, .1f},
  {"love",      {70,  78, .80f, .00f,  0, .45f, 1,   1,    0,  0, 136, 106,  0, -.1f,255, 110, 170, .60f, .80f,.8f,  0,  30, 0,  2,   1.2f,0},  FX_HEARTS,   false, 6.0f, .3f},
  {"excited",   {74,  88, .80f, .00f,  0, .25f, 1.05f,1.05f,0, 0, 138, 104,  0, -.2f,255, 235,  90, .60f, .30f,.9f,  .45f,34, 0,  9,   2.6f,0},  FX_SPARKLE,  false, 7.0f, .8f},
  {"confused",  {64,  80, .55f, .15f, 10, .00f, 1.12f,.82f,-6,  6, 130, 110, .2f, 0,  170, 230, 255, .35f, 0,  -.2f,  0,  20, 0,  0,   0,   0},  FX_QUESTION, false, 4.0f, .6f},
  {"thinking",  {62,  80, .55f, .25f, -8, .00f, .95f,.95f, 0,  0, 130, 110, .55f,-.6f,150, 220, 255, .30f, 0,   0,    0,  14, 0,  0,   0,   0},  FX_DOTS,     false, 6.0f, 0},
  {"dead",      {64,  84, .55f, .00f,  0, .00f, 1,   1,    0,  0, 132, 110,  0,  0,  160, 170, 180, .10f, 0,  -.3f,  0,  24, 1,  0,   0,   0},  FX_NONE,     false, 0,    0},
  {"cute",      {76,  86, .85f, .00f,  0, .35f, 1,   1,    0,  0, 128, 108,  0, -.15f,255,170, 210, .55f, .90f,.7f,  0,  22, 0,  2,   1.0f,0},  FX_HEARTS,   false, 5.0f, .4f},
  {"freaky",    {82, 104, .95f, .00f,  0, .00f, 1.1f,.85f,-4,  4, 142, 104,  0,  0,  200, 255, 160, .60f, 0,   0,  .55f, 26, 0,  0,   0,  2.2f},FX_SWEAT,    false, 12.0f,1.0f},
  {"dizzy",     {64,  80, .60f, .10f,  0, .00f, 1,   1,    0,  0, 132, 110,  0,  0,  200, 230, 255, .35f, 0,  -.1f, .3f, 22, 0,  0,   0,  1.5f},FX_DIZZY,    false, 3.0f, 0},
  {"focused",   {66,  72, .45f, .22f,  6, .08f, 1,   1,    0,  0, 132, 108,  0,  0,  120, 240, 255, .40f, 0,   .1f,  0,  18, 0,  0,   0,   0},  FX_NONE,     false, 5.0f, .35f},
  {"relaxed",   {66,  78, .60f, .38f, -4, .12f, 1,   1,    0,  0, 132, 112,  0, .15f,150, 225, 255, .25f, .15f,.4f,  0,  22, 0,  0,   0,   0},  FX_NONE,     false, 3.5f, .4f},
  {"proud",     {68,  74, .60f, .30f, -6, .40f, 1,   1,    0,  0, 134, 108,  0, -.35f,255,220, 130, .45f, .25f,.8f,  0,  28, 0,  0,   0,   0},  FX_SPARKLE,  false, 5.0f, .2f},
  {"shy",       {62,  74, .75f, .20f, -8, .35f, 1,   1,    0,  0, 128, 114, -.5f,.5f, 255, 170, 200, .40f, 1.0f,.5f,  0,  18, 0,  0,   0,   0},  FX_NONE,     false, 3.0f, .2f},
};
const int NEXPR = sizeof(EXPRS) / sizeof(EXPRS[0]);

// Old face names (bitmaps, emotes) -> an expression (+ talking)
struct Alias { const char* from; const char* to; };
const Alias ALIASES[] = {
  {"default", "neutral"}, {"idle", "neutral"}, {"idle_blink", "neutral"}, {"stand", "neutral"},
  {"walk", "focused"}, {"crab", "focused"}, {"worm", "focused"}, {"pushup", "angry"}, {"rest", "relaxed"},
  {"swim", "happy"}, {"dance", "excited"}, {"wave", "happy"}, {"point", "proud"}, {"bow", "shy"},
  {"shake", "dizzy"}, {"shrug", "confused"}, {"defualt", "neutral"},
};

// ---- Particles ----
struct Particle { float x, y, vx, vy, life, max, size; uint8_t kind; };
const int MAXP = 16;

class Face {
 public:
  Pose cur, vel, tgt;
  int expr = 0;
  bool talk = false;
  float lookExtX = 0, lookExtY = 0;  // set from outside (face tracking): overrides glances
  float lookExtUntil = 0;
  float t = 0;

  Face() { memcpy(&cur, &EXPRS[0].p, sizeof(Pose)); tgt = cur; memset(&vel, 0, sizeof(Pose)); scheduleBlink(); }

  static int find(const char* name) {
    char n[24];
    size_t k = 0;
    for (; name[k] && k < sizeof(n) - 1; k++) n[k] = (char)tolower_(name[k]);
    n[k] = 0;
    const char* base = n;
    bool isTalk = !strncmp(n, "talk_", 5);
    if (isTalk) base = n + 5;
    for (size_t i = 0; i < sizeof(ALIASES) / sizeof(ALIASES[0]); i++)
      if (!strcmp(base, ALIASES[i].from)) { base = ALIASES[i].to; break; }
    for (int i = 0; i < NEXPR; i++)
      if (!strcmp(base, EXPRS[i].name)) return isTalk ? i | 0x100 : i;
    return -1;
  }

  // Pick an expression by name (old names work too). Returns false for an unknown name.
  bool set(const char* name) {
    const int f = find(name);
    if (f < 0) return false;
    const int e = f & 0xFF;
    const bool tk = (f & 0x100) != 0;
    if (e == expr && tk == talk) return true;
    expr = e;
    talk = tk;
    tgt = EXPRS[e].p;
    fxTimer = 0;
    fxNext = t + 0.35f;
    for (int i = 0; i < MAXP; i++)  // the last expression's hearts / zzz fade out quickly
      if (parts[i].life > 0.3f) { parts[i].max = parts[i].max - parts[i].life + 0.3f; parts[i].life = 0.3f; }
    // a new expression gets a little "pop": the eyes squash then spring back (anticipation)
    kick(0.12f);
    return true;
  }

  // Look at a point (-1..1), e.g. from face tracking; holds for `holdS` seconds
  void lookAt(float x, float y, float holdS = 0.5f) { lookExtX = x; lookExtY = y; lookExtUntil = t + holdS; }

  void step(float dt) {
    if (dt > 0.1f) dt = 0.1f;
    t += dt;
    const Expr& ex = EXPRS[expr];
    // where to look: outside target, else small glances now and then
    float lx = tgt.lookX, ly = tgt.lookY;
    if (t < lookExtUntil) { lx = lookExtX; ly = lookExtY; }
    else {
      if (t >= nextGlance) {
        const float g = ex.glance;
        glanceX = g > 0 && rnd() < 0.7f ? (rnd() * 2 - 1) * 0.55f * g : 0;
        glanceY = g > 0 && rnd() < 0.5f ? (rnd() * 2 - 1) * 0.3f * g : 0;
        nextGlance = t + 0.8f + rnd() * 3.2f;
      }
      lx += glanceX;
      ly += glanceY;
    }
    // springs: shapes a little soft (visible overshoot), the gaze snappy (saccades)
    float* c = (float*)&cur;
    float* v = (float*)&vel;
    const float* g = (const float*)&tgt;
    const int iLX = (int)(offsetof(Pose, lookX) / sizeof(float)), iLY = iLX + 1;
    // small sub-steps: a stiff spring stepped at 30 fps would ring and blow up
    const int sub = (int)ceilf(dt / 0.004f);
    const float h = dt / (sub > 0 ? sub : 1);
    for (int n = 0; n < sub; n++) {
      for (int i = 0; i < NP; i++) {
        float target = g[i];
        float k = 140, damp = 0.55f;
        if (i == iLX) { target = lx; k = 900; damp = 0.8f; }
        if (i == iLY) { target = ly; k = 900; damp = 0.8f; }
        const float cc = 2 * sqrtf(k) * damp;
        v[i] += (k * (target - c[i]) - cc * v[i]) * h;
        c[i] += v[i] * h;
      }
    }
    // blink
    if (blinkT < 0 && ex.blinkEvery > 0 && t >= nextBlink) blinkT = 0;
    if (blinkT >= 0) {
      blinkT += dt;
      if (blinkT > BLINK_S) {
        blinkT = -1;
        if (doubleBlink) { doubleBlink = false; nextBlink = t + 0.12f; }
        else scheduleBlink();
      }
    }
    // squash "kick" decays
    kickV *= expf(-dt * 9);
    // particles
    fxTimer += dt;
    emit(ex.fx, dt);
    for (int i = 0; i < MAXP; i++) {
      Particle& p = parts[i];
      if (p.life <= 0) continue;
      p.life -= dt;
      p.x += p.vx * dt;
      p.y += p.vy * dt;
      if (p.kind == FX_TEARS) p.vy += 240 * dt;  // tears fall
    }
  }

  void draw(Canvas& cv) {
    cv.clear(0);
    const Pose& p = cur;
    // breathing + hop (squash and stretch on landing)
    const float breathe = sinf(t * 1.6f);
    float hop = 0, sq = 0;
    if (p.bounce > 0.5f && p.bounceHz > 0.05f) {
      const float ph = fmodf(t * p.bounceHz, 1.0f);
      hop = p.bounce * sinf(ph * 3.14159f);                   // up and down
      if (ph < 0.12f) sq = (0.12f - ph) / 0.12f * 0.18f;     // just landed: squash
      else if (ph > 0.25f && ph < 0.6f) sq = -0.06f;         // in the air: stretch
    }
    sq += kickV;
    const float jx = p.jitter > 0.05f ? p.jitter * sinf(t * 37.0f) : 0;
    const float jy = p.jitter > 0.05f ? p.jitter * cosf(t * 29.0f) : 0;
    // blink amount 0..1 (fast close, slower open)
    float bl = 0;
    if (blinkT >= 0) {
      const float u = blinkT / BLINK_S;
      bl = u < 0.4f ? u / 0.4f : 1 - (u - 0.4f) / 0.6f;
      bl = bl * bl * (3 - 2 * bl);
    }
    const float cx = W / 2 + p.lookX * 20 + jx;
    const float cy = p.y + p.lookY * 14 - hop + breathe * 1.5f + jy;
    const uint16_t col = rgb((int)p.cr, (int)p.cg, (int)p.cb);
    for (int side = 0; side < 2; side++) {
      const float s = side == 0 ? p.sizeL : p.sizeR;
      // looking to a side: the eye on that side grows a little (as if turned towards it)
      const float persp = 1 + (side == 0 ? -1 : 1) * p.lookX * 0.07f;
      float ew = p.w * s * persp * (1 + sq * 0.6f + bl * 0.12f);
      float eh = p.h * s * persp * (1 - sq + breathe * 0.012f);
      eh *= 1 - 0.93f * bl;
      if (eh < 3) eh = 3;
      const float ex = cx + (side == 0 ? -1 : 1) * p.gap / 2;
      const float ey = cy + (side == 0 ? p.dyL : p.dyR) + p.h * s * sq * 0.5f;  // squash keeps the bottom
      if (p.xEyes > 0.5f) { drawX(cv, ex, ey, p.w * 0.42f, col); continue; }
      // glow: two soft halos behind the eye
      if (p.glow > 0.02f) {
        const uint16_t g1 = rgb((int)(p.cr * 0.18f * p.glow), (int)(p.cg * 0.18f * p.glow), (int)(p.cb * 0.18f * p.glow));
        const uint16_t g2 = rgb((int)(p.cr * 0.32f * p.glow), (int)(p.cg * 0.32f * p.glow), (int)(p.cb * 0.32f * p.glow));
        cv.rrect(ex - ew / 2 - 9, ey - eh / 2 - 9, ew + 18, eh + 18, rr(ew + 18, eh + 18, p.round), g1);
        cv.rrect(ex - ew / 2 - 4, ey - eh / 2 - 4, ew + 8, eh + 8, rr(ew + 8, eh + 8, p.round), g2);
      }
      cv.rrect(ex - ew / 2, ey - eh / 2, ew, eh, rr(ew, eh, p.round), col);
      // highlight: a small bright spot, top-outer
      if (eh > 24) {
        const float hx = ex + (side == 0 ? -1 : 1) * ew * 0.2f - p.lookX * 3, hy = ey - eh * 0.24f;
        cv.circle(hx, hy, ew * 0.09f, rgb(255, 255, 255));
      }
      // lids (black over the eye)
      // the lids also cover the glow around the eye (else its edges stick out as dark tabs)
      const float top = ey - eh / 2, bot = ey + eh / 2, l = ex - ew / 2 - 12, r = ex + ew / 2 + 12;
      if (p.lidT > 0.01f || fabsf(p.lidTA) > 0.5f) {
        const float yl = top + p.lidT * eh, slope = tanf(p.lidTA * 0.01745f) * ew * 0.5f;
        // inner corner = towards the middle of the face
        const float yInner = yl + slope, yOuter = yl - slope;
        const float yL = side == 0 ? yOuter : yInner, yR = side == 0 ? yInner : yOuter;
        cv.tri(l, top - 30, r, top - 30, r, yR, 0);
        cv.tri(l, top - 30, r, yR, l, yL, 0);
      }
      if (p.lidB > 0.01f) {  // smile: a black ellipse pushing up from below = ^ ^
        const float ry = eh * 0.62f + 10, rx = ew * 0.78f + 12;
        cv.ellipse(ex, bot + ry - p.lidB * eh * 0.95f - 10, rx, ry, 0);
      }
    }
    // blush
    if (p.blush > 0.08f) {
      const float k = 0.45f + 0.45f * (p.blush > 1 ? 1 : p.blush);  // pink, not brown: never too dim
      const uint16_t bc = rgb((int)(255 * k), (int)(95 * k), (int)(140 * k));
      for (int side = 0; side < 2; side++) {
        const float ex = cx + (side == 0 ? -1 : 1) * (p.gap / 2 + 18);
        cv.ellipse(ex, cy + p.h * 0.55f + 6, 20, 8, bc);
      }
    }
    // mouth
    float open = p.mOpen;
    if (talk) open = fmaxf(open, 0.25f + 0.35f * fabsf(sinf(t * 11.0f) * sinf(t * 4.3f + 1.0f)));
    if (p.mW > 4) drawMouth(cv, cx, cy + p.h * 0.5f + 34, p.mW, p.mSmile, open, col);
    // particles
    for (int i = 0; i < MAXP; i++) if (parts[i].life > 0) drawParticle(cv, parts[i], col);
  }

 private:
  static constexpr float BLINK_S = 0.17f;
  float blinkT = -1, nextBlink = 0;
  bool doubleBlink = false;
  float nextGlance = 1, glanceX = 0, glanceY = 0;
  float kickV = 0;
  float fxTimer = 0, fxNext = 0;
  Particle parts[MAXP] = {};
  uint32_t seed = 12345;

  static int tolower_(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
  float rnd() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0f; }
  static float rr(float w, float h, float round) { const float m = (w < h ? w : h) / 2; return 3 + (m - 3) * round; }
  void kick(float a) { kickV = a; }
  void scheduleBlink() {
    const float every = EXPRS[expr].blinkEvery > 0 ? EXPRS[expr].blinkEvery : 4;
    nextBlink = t + every * (0.4f + rnd() * 1.2f);
    doubleBlink = rnd() < 0.22f;
  }

  Particle* spawn() {
    for (int i = 0; i < MAXP; i++) if (parts[i].life <= 0) return &parts[i];
    return nullptr;
  }
  void emit(Effect fx, float dt) {
    (void)dt;
    if (fx == FX_NONE || t < fxNext) return;
    Particle* q = spawn();
    if (!q) return;
    const Pose& p = cur;
    const float cx = W / 2, cy = p.y;
    q->kind = fx;
    switch (fx) {
      case FX_HEARTS:
        q->x = cx + (rnd() * 2 - 1) * 130; q->y = cy + 40; q->vx = (rnd() - 0.5f) * 20; q->vy = -40 - rnd() * 25;
        q->max = q->life = 2.2f; q->size = 7 + rnd() * 6; fxNext = t + 0.45f + rnd() * 0.5f; break;
      case FX_TEARS: {
        const float side = rnd() < 0.5f ? -1 : 1;
        q->x = cx + side * (p.gap / 2 + p.w * 0.25f); q->y = cy + p.h * 0.35f; q->vx = 0; q->vy = 10;
        q->max = q->life = 1.3f; q->size = 5; fxNext = t + 0.9f + rnd() * 0.9f; break;
      }
      case FX_ZZZ:
        q->x = cx + 95; q->y = cy - 40; q->vx = 14; q->vy = -22; q->max = q->life = 2.6f; q->size = 9;
        fxNext = t + 1.1f; break;
      case FX_SPARKLE:
        q->x = cx + (rnd() * 2 - 1) * 150; q->y = cy + (rnd() * 2 - 1) * 70; q->vx = 0; q->vy = -6;
        q->max = q->life = 0.7f; q->size = 6 + rnd() * 7; fxNext = t + 0.35f + rnd() * 0.6f; break;
      case FX_ANGER:
        q->x = cx + 100; q->y = cy - 62; q->vx = 0; q->vy = 0; q->max = q->life = 0.9f; q->size = 14;
        fxNext = t + 0.9f; break;
      case FX_QUESTION:
        q->x = cx + 110; q->y = cy - 58; q->vx = 0; q->vy = -4; q->max = q->life = 2.0f; q->size = 16;
        fxNext = t + 2.0f; break;
      case FX_DOTS:
        q->x = cx + 60; q->y = cy - 78; q->vx = 0; q->vy = 0; q->max = q->life = 1.8f; q->size = 6;
        fxNext = t + 1.8f; break;
      case FX_SWEAT:
        q->x = cx + p.gap / 2 + p.w * 0.6f; q->y = cy - p.h * 0.5f; q->vx = 0; q->vy = 26;
        q->max = q->life = 1.4f; q->size = 7; fxNext = t + 1.6f; break;
      case FX_DIZZY:
        q->x = cx; q->y = cy - 70; q->vx = 0; q->vy = 0; q->max = q->life = 3.0f; q->size = 50;
        fxNext = t + 3.0f; break;
      default: q->life = 0; break;
    }
  }

  static uint16_t fade(uint16_t c, float a) {
    const int r = ((c >> 11) & 31) * 255 / 31, g = ((c >> 5) & 63) * 255 / 63, b = (c & 31) * 255 / 31;
    return rgb((int)(r * a), (int)(g * a), (int)(b * a));
  }
  // life: fade in at the start, fade out at the end
  static float alpha(const Particle& q) {
    const float age = q.max - q.life;
    float a = q.life / (q.max * 0.35f);
    if (a > 1) a = 1;
    const float in = age / 0.15f;
    return in < 1 ? in * a : a;
  }
  void drawParticle(Canvas& cv, const Particle& q, uint16_t eyeCol) {
    const float a = alpha(q);
    switch (q.kind) {
      case FX_HEARTS: {
        const uint16_t c = fade(rgb(255, 80, 140), a);
        const float s = q.size * (0.8f + 0.2f * sinf(t * 8));
        cv.circle(q.x - s * 0.5f, q.y, s * 0.55f, c);
        cv.circle(q.x + s * 0.5f, q.y, s * 0.55f, c);
        cv.tri(q.x - s * 1.02f, q.y + s * 0.15f, q.x + s * 1.02f, q.y + s * 0.15f, q.x, q.y + s * 1.25f, c);
        break;
      }
      case FX_TEARS: {
        const uint16_t c = fade(rgb(110, 180, 255), a);
        cv.circle(q.x, q.y, q.size, c);
        cv.tri(q.x - q.size * 0.9f, q.y - q.size * 0.2f, q.x + q.size * 0.9f, q.y - q.size * 0.2f, q.x, q.y - q.size * 2.2f, c);
        break;
      }
      case FX_ZZZ: {
        const uint16_t c = fade(rgb(170, 180, 255), a);
        const float s = q.size * (1 + (q.max - q.life) * 0.35f);
        cv.line(q.x - s, q.y - s, q.x + s, q.y - s, 2, c);
        cv.line(q.x + s, q.y - s, q.x - s, q.y + s, 2, c);
        cv.line(q.x - s, q.y + s, q.x + s, q.y + s, 2, c);
        break;
      }
      case FX_SPARKLE: {
        const float grow = sinf((1 - q.life / q.max) * 3.14159f);
        const uint16_t c = fade(rgb(255, 250, 200), a);
        const float s = q.size * grow;
        cv.tri(q.x - s * 0.22f, q.y, q.x + s * 0.22f, q.y, q.x, q.y - s, c);
        cv.tri(q.x - s * 0.22f, q.y, q.x + s * 0.22f, q.y, q.x, q.y + s, c);
        cv.tri(q.x, q.y - s * 0.22f, q.x, q.y + s * 0.22f, q.x - s, q.y, c);
        cv.tri(q.x, q.y - s * 0.22f, q.x, q.y + s * 0.22f, q.x + s, q.y, c);
        break;
      }
      case FX_ANGER: {
        const float pulse = 0.85f + 0.3f * sinf((q.max - q.life) * 14);
        const uint16_t c = fade(rgb(255, 60, 50), a);
        const float s = q.size * pulse;
        // four little bent strokes: the manga "vein" mark
        cv.line(q.x - s, q.y - s * 0.3f, q.x - s * 0.3f, q.y - s * 0.3f, 2.5f, c);
        cv.line(q.x - s * 0.3f, q.y - s * 0.3f, q.x - s * 0.3f, q.y - s, 2.5f, c);
        cv.line(q.x + s, q.y - s * 0.3f, q.x + s * 0.3f, q.y - s * 0.3f, 2.5f, c);
        cv.line(q.x + s * 0.3f, q.y - s * 0.3f, q.x + s * 0.3f, q.y - s, 2.5f, c);
        cv.line(q.x - s, q.y + s * 0.3f, q.x - s * 0.3f, q.y + s * 0.3f, 2.5f, c);
        cv.line(q.x - s * 0.3f, q.y + s * 0.3f, q.x - s * 0.3f, q.y + s, 2.5f, c);
        cv.line(q.x + s, q.y + s * 0.3f, q.x + s * 0.3f, q.y + s * 0.3f, 2.5f, c);
        cv.line(q.x + s * 0.3f, q.y + s * 0.3f, q.x + s * 0.3f, q.y + s, 2.5f, c);
        break;
      }
      case FX_QUESTION: {
        const uint16_t c = fade(eyeCol, a);
        const float s = q.size, x = q.x, y = q.y + sinf(t * 3) * 3;
        // "?": an arc of short strokes, a stem and a dot
        float px = x - s * 0.55f, py = y - s * 0.4f;
        for (int k = 1; k <= 8; k++) {
          const float an = 3.4f - k * 0.5f;
          const float nx = x + cosf(an) * s * 0.55f, ny = y - s * 0.4f - sinf(an) * s * 0.55f;
          cv.line(px, py, nx, ny, 2.6f, c);
          px = nx; py = ny;
        }
        cv.line(px, py, x, y + s * 0.35f, 2.6f, c);
        cv.circle(x, y + s * 0.9f, 3.2f, c);
        break;
      }
      case FX_DOTS: {
        const float age = q.max - q.life;
        for (int k = 0; k < 3; k++) {
          const float on = age * 3 - k * 0.6f;
          if (on < 0) continue;
          const float up = sinf(fminf(on, 1.0f) * 3.14159f) * 5;
          cv.circle(q.x + k * 18, q.y - up, q.size, fade(eyeCol, a));
        }
        break;
      }
      case FX_SWEAT: {
        const uint16_t c = fade(rgb(140, 210, 255), a);
        cv.circle(q.x, q.y, q.size, c);
        cv.tri(q.x - q.size * 0.9f, q.y - q.size * 0.2f, q.x + q.size * 0.9f, q.y - q.size * 0.2f, q.x, q.y - q.size * 2.4f, c);
        break;
      }
      case FX_DIZZY: {
        const uint16_t c = fade(rgb(255, 230, 120), a);
        for (int k = 0; k < 3; k++) {  // little stars circling over the head
          const float an = t * 3.2f + k * 2.094f;
          const float sx = q.x + cosf(an) * q.size, sy = q.y + sinf(an) * q.size * 0.3f;
          const float s = 6 + 2 * sinf(an);
          cv.tri(sx - s * 0.25f, sy, sx + s * 0.25f, sy, sx, sy - s, c);
          cv.tri(sx - s * 0.25f, sy, sx + s * 0.25f, sy, sx, sy + s, c);
          cv.tri(sx, sy - s * 0.25f, sx, sy + s * 0.25f, sx - s, sy, c);
          cv.tri(sx, sy - s * 0.25f, sx, sy + s * 0.25f, sx + s, sy, c);
        }
        break;
      }
      default: break;
    }
  }

  static void drawX(Canvas& cv, float x, float y, float s, uint16_t c) {
    cv.line(x - s, y - s, x + s, y + s, 6, c);
    cv.line(x - s, y + s, x + s, y - s, 6, c);
  }

  // Mouth: a curve (smile / frown) that opens into an "o"
  static void drawMouth(Canvas& cv, float x, float y, float w, float smile, float open, uint16_t col) {
    if (open > 0.12f) {
      const float rx = w * (0.28f + 0.22f * (1 - open)), ry = 4 + open * 14;
      cv.ellipse(x, y, rx, ry, col);
      cv.ellipse(x, y + ry * 0.15f, rx * 0.72f, ry * 0.68f, rgb(40, 12, 20));
      return;
    }
    const int N = 10;
    float px = 0, py = 0;
    for (int k = 0; k <= N; k++) {
      const float u = (float)k / N * 2 - 1;
      const float nx = x + u * w / 2, ny = y + smile * 9 * (1 - u * u) - smile * 4;
      if (k) cv.line(px, py, nx, ny, 3, col);
      px = nx;
      py = ny;
    }
  }
};

}  // namespace fe
