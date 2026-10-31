// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
// More play scenes: a kite, a yo-yo, fishing, a rubber duck, the cookie jar, a spinning top,
// a magic trick, a cat visitor, dominoes, a snowman, a frog friend, a sleepy moon, doodling, a paper boat,
// watering a plant, a toy rocket, ice cream; short moments: a dandelion, bubble gum, a paper ball toss.
// Included at the end of face_scenes.h (still INSIDE class fe::Face). Every take has its own middle /
// twist / ending (pickBeats), tempo and side (sc.mir); taps, pats and bangs change what happens.
// Coordinates are for the unmirrored screen (props on the right): the mirror canvas flips them.
 private:
  // ---- registry hooks (face_scenes.h calls these) ----
  float sceneDur2(int id) const {
    switch (id) {
      case SC_KITE: return 16;
      case SC_YOYO: return 14;
      case SC_FISHING: return 22;
      case SC_DUCK: return 14;
      case SC_COOKIE: return 14;
      case SC_TOP: return 13;
      case SC_MAGIC: return 13;
      case SC_CAT: return 17;
      case SC_DOMINO: return 15;
      case SC_SNOWMAN: return 18;
      case SC_FROG: return 14;
      case SC_MOON: return 19;
      case SC_DOODLE: return 16;
      case SC_BOAT: return 14;
      case SC_PLANT: return 16;
      case SC_ROCKET: return 14;
      case SC_ICECREAM: return 14;
      case SC_DANDELION: return 7;
      case SC_GUM: return 6.5f;
      case SC_PAPERBALL: return 7;
    }
    return 10;
  }
  void setupBeats2(int id) {
    switch (id) {
      case SC_KITE: pickBeats(3, 2, 3); break;
      case SC_YOYO: pickBeats(3, 1, 3); break;
      case SC_FISHING: pickBeats(3, 1, 4); break;
      case SC_DUCK: pickBeats(3, 2, 3); break;
      case SC_COOKIE: pickBeats(3, 1, 3); break;
      case SC_TOP: pickBeats(3, 1, 3); break;
      case SC_MAGIC: pickBeats(3, 1, 4); break;
      case SC_CAT: pickBeats(3, 1, 3); break;
      case SC_DOMINO: pickBeats(2, 2, 3); break;
      case SC_SNOWMAN: pickBeats(2, 2, 3); break;
      case SC_FROG: pickBeats(3, 1, 3); break;
      case SC_MOON: pickBeats(3, 1, 3); break;
      case SC_DOODLE: pickBeats(5, 2, 3); break;
      case SC_BOAT: pickBeats(3, 2, 3); break;
      case SC_PLANT: pickBeats(3, 1, 3); break;
      case SC_ROCKET: pickBeats(3, 1, 3); break;
      case SC_ICECREAM: pickBeats(3, 1, 3); break;
      case SC_DANDELION: pickBeats(1, 1, 3); break;
      case SC_GUM: pickBeats(1, 1, 3); break;
      case SC_PAPERBALL: pickBeats(1, 1, 3); break;
      default: break;
    }
  }
  float beatWeight2(int id, int kind, int i) const {
    const bool night = hourNow >= 22 || (hourNow >= 0 && hourNow < 6), wet = weatherNow == 3 || weatherNow == 4;
    if (id == SC_KITE && kind == 0 && i > 0 && wet) return 2;            // windy weather: loops and dives
    if (id == SC_SNOWMAN && kind == 2 && i == 1 && weatherNow == 6) return 0;  // it's snowing: no melting
    if (id == SC_SNOWMAN && kind == 2 && i == 1 && tempNow > 8) return 2;      // a warm day: it melts
    if (id == SC_BOAT && kind == 2 && i == 1 && wet) return 2;            // rain: a soggy boat
    if (id == SC_BOAT && kind == 0 && i == 2 && wet) return 2;            // and choppy water
    if (id == SC_MOON && kind == 2 && i == 0 && night) return 3;          // at night they both doze off
    if (id == SC_CAT && kind == 2 && i == 0 && night) return 2;           // the cat curls up
    if (id == SC_PLANT && kind == 2 && i == 0 && hourNow >= 6 && hourNow < 12) return 2;  // morning: a bloom
    return 1;
  }
  // a weight for the random pick (x the short-moment / quiet / lively weights in pickScene)
  float pickWeight2(int i, bool night, bool morning, bool evening) const {
    const bool cold = weatherNow == 6 || monthNow == 12 || monthNow == 1 || monthNow == 2 || (tempNow > -90 && tempNow < 5);
    const bool hot = tempNow > 27;
    switch (i) {
      case SC_KITE: return night ? 0.3f : weatherNow == 2 ? 1.6f : 1.0f;  // a breezy, cloudy day
      case SC_MOON: return night ? 3.0f : evening ? 1.5f : 0.35f;
      case SC_SNOWMAN: return weatherNow == 6 ? 4.0f : cold ? 1.5f : 0.35f;
      case SC_ICECREAM: return hot ? 2.5f : (tempNow > -90 && tempNow < 12) ? 0.3f : night ? 0.4f : 1.0f;
      case SC_PLANT: return morning ? 3.0f : night ? 0.4f : 1.0f;
      case SC_BOAT: return weatherNow == 3 ? 2.5f : 1.0f;
      case SC_FISHING: return night ? 0.5f : 1.0f;
      case SC_ROCKET: case SC_MAGIC: case SC_DOMINO: case SC_TOP: case SC_YOYO: case SC_PAPERBALL: return night ? 0.3f : 1.0f;
      case SC_CAT: case SC_COOKIE: return night ? 1.5f : 1.0f;  // a night visitor; a midnight snack
      case SC_DANDELION: return weatherNow == 3 || weatherNow == 4 ? 0.3f : 1.0f;
    }
    return 1;
  }
  static bool quiet2(int i) { return i == SC_MOON || i == SC_CAT || i == SC_DANDELION || i == SC_BOAT || i == SC_DOODLE || i == SC_FISHING; }
  int chainFor2(int id) const {
    switch (id) {
      case SC_KITE: return SC_BIRD;
      case SC_DANDELION: return chance(0.5f) ? SC_SEEDS : SC_SNEEZE;
      case SC_MOON: return chance(0.5f) ? SC_DREAM : SC_STARS;
      case SC_FROG: return SC_FLY;
      case SC_COOKIE: return SC_HICCUPS;
      case SC_ROCKET: return SC_COMET;
      case SC_SNOWMAN: return SC_SNOW;
      case SC_PAPERBALL: return SC_PAPERBALL;  // one more try
      case SC_ICECREAM: return SC_SNEEZE;
      case SC_GUM: return SC_HICCUPS;
    }
    return 0;
  }

 public:
  // which new scenes take which live event (desk mode routes taps / bangs / pats only to scenes that use
  // them): ev 1 tap, 2 loud, 3 pat, 4 someone arrived
  static bool sceneTakes2(const char* n, int ev) {
    static const char* const TAP[] = {"kite", "yoyo", "fishing", "duck", "cookie", "top", "cat", "dominoes", "snowman", "frog", "moon", "boat", "rocket", "gum"};
    static const char* const LOUD[] = {"gum", "cat", "moon"};
    static const char* const PAT[] = {"duck", "cookie", "magic", "cat", "frog", "plant"};
    const char* const* L = ev == 1 ? TAP : ev == 2 ? LOUD : ev == 3 ? PAT : nullptr;
    const int nl = ev == 1 ? (int)(sizeof(TAP) / sizeof(TAP[0])) : ev == 2 ? (int)(sizeof(LOUD) / sizeof(LOUD[0])) : ev == 3 ? (int)(sizeof(PAT) / sizeof(PAT[0])) : 0;
    for (int i = 0; i < nl; i++) if (ieq(n, L[i])) return true;
    return false;
  }

 private:
  // ---- shared bits ----
  float waterY(float x, float y0, float amp) const { return y0 + amp * sinf(x * 0.045f + t * 2.2f) + amp * 0.5f * sinf(x * 0.11f - t * 3.1f); }
  RGB waterCol() const { return lightBg() ? C(120, 180, 240) : C(40, 105, 200); }
  // water across the bottom: a wavy top (in: 0..1, it rises from below)
  void drawWater(float y0, float amp, float in) {
    if (in <= 0.01f) return;
    Canvas& cv = *k.cv;
    const float base = lerpf(252, y0, in);
    const RGB wc = waterCol();
    const uint16_t c = to565(wc), crest = to565(mixc(wc, C(255, 255, 255), 0.45f));
    float xs[17], ys[17];
    for (int i = 0; i <= 16; i++) { xs[i] = i * 20.0f; ys[i] = waterY(xs[i], base, amp); }
    for (int i = 0; i < 16; i++) {
      const float lo = fmaxf(ys[i], ys[i + 1]);
      cv.rrect(xs[i], lo, 20.6f, 242 - lo, 0, c);
      cv.tri(xs[i], ys[i], xs[i + 1], ys[i + 1], ys[i] < ys[i + 1] ? xs[i] : xs[i + 1], lo, c);
    }
    for (int i = 0; i < 16; i++) cv.lineFast(xs[i], ys[i], xs[i + 1], ys[i + 1], 1.0f, crest);
    for (int i = 0; i < 5; i++) {  // glints drifting on the water
      const float gx = fmodf(i * 71 + t * (8 + i * 3), 340) - 10, gy = waterY(gx, base, amp) + 8 + (i % 3) * 7;
      cv.lineFast(gx, gy, gx + 9, gy, 0.8f, crest);
    }
  }
  void ripple(float x, float y, float age, float maxR, const RGB& c) {
    if (age < 0 || age > 1) return;
    ringE(x, y, 4 + maxR * age, (4 + maxR * age) * 0.28f, 0.8f, to565(mixc(waterCol(), c, 1 - age)), false);  // (fades into the water)
  }
  // a puff of smoke: a soft rim and a lighter middle; it shrinks as it fades (fading it into the face
  // colour would leave grey blobs on the black faces)
  void smokePuff(float x, float y, float r, float a, const RGB& c) {
    if (a <= 0.05f || r < 0.5f) return;
    const float r2 = r * (0.35f + 0.65f * a);
    k.cv->circleFast(x, y, r2, to565(mulc(c, 0.86f)));
    k.cv->circleFast(x - r2 * 0.15f, y - r2 * 0.18f, r2 * 0.72f, to565(mixc(c, C(255, 255, 255), 0.4f)));
  }
  void zGlyph(float x, float y, float s, uint16_t c) {
    k.cv->lineFast(x - s, y - s, x + s, y - s, 0.9f, c);
    k.cv->lineFast(x + s, y - s, x - s, y + s, 0.9f, c);
    k.cv->lineFast(x - s, y + s, x + s, y + s, 0.9f, c);
  }
  // where the mouth is on the screen (the unmirrored x), for the gum bubble
  void mouthAt(float& x, float& y) const {
    if (style == ST_SAVER) {
      const StyleDef sd = styleDef(ST_SAVER);
      x = 160;
      y = sd.y + sd.ry + 26 + shown.y;
      return;
    }
    x = sc.mir < 0 ? W - k.cx : k.cx;
    y = k.cy + k.s.ry + (k.s.mouth == M_CUTE ? 20 : 30);
  }
  // a little pale outline under light props on light faces (white snow on a cream face)
  RGB edgeFor(const RGB& c) const { return lightBg() ? mulc(c, 0.72f) : mixc(c, lidCol(), 0.35f); }

  // timelines
  void sceneStep2(float u) {
    switch (sc.id) {
      case SC_KITE: {  // a kite: sways, loops or dives; Crumb rides it; the string snaps, it crashes, or it's reeled in
        float x, y, a;
        kitePos(x, y, a);
        const float E = bt(2, 10);
        lookPt(x, y);
        headPt(x, y, 0.45f);
        if (at(0.3f)) say("curious");
        if (at(1.8f)) { say("excited"); sfx("w:whoosh|v0.2 n:420"); }
        if (sc.mid == 0 && atb(3, 5)) say("happy");
        if (sc.mid == 1) { const float l0 = bt(0, 4.5f); if (at(l0)) sfx("w:whoosh|v0.25 n:420"); if (at(l0 + 0.4f)) say("amazed"); if (at(l0 + 1.9f)) say("laugh"); }
        if (sc.mid == 2) { const float d0 = bt(0, 4.5f); if (at(d0 + 0.3f)) { say("startled"); sfx("w:whoosh|v0.3 n:420"); } if (at(d0 + 1.9f)) say("relieved"); }
        if (sc.twist == 1) {
          const float c0 = bt(1, 7);
          if (at(c0)) { say("adoring"); sfx("w:tiny_chirp|v0.2 1760:45 0:45 2093:70"); }
          if (at(c0 + 2.2f)) sfx("w:tiny_chirp|v0.2 2093:60 2637:90");
        }
        if (sc.evId == 1) { if (evAt(0.01f)) { say("excited"); sfx("w:whoosh|v0.3 n:420"); } if (evAt(1.7f)) say("giggle"); }
        if (atb(4, 8.2f) && u < E - 0.5f) say(sc.v[2] < 0.5f ? "giddy" : "content");
        if (sc.end == 0) {
          if (at(E)) { say("shocked"); sfx("w:snap|v0.4 n:50"); }
          if (at(E + 0.9f)) say("sad");
          if (at(E + 2.4f)) say("wistful");
          if (u > E) finishIn(4.0f);
        } else if (sc.end == 1) {
          if (at(E)) say("scared");
          if (at(E + 0.75f)) { say("dizzy"); sfx("w:bonk|v0.4 220~120:90"); }
          faceShift(10 * kick(E + 0.75f, 0.06f, 6));
          if (at(E + 2.3f)) say("giggle");
          if (u > E) finishIn(3.8f);
        } else {
          if (at(E)) say("focus");
          if (at(E + 2.2f)) { say("proud"); sfx("w:sparkle|v0.3 1047:80 1319:80 1568:80 2093:260"); }
          if (u > E) finishIn(3.6f);
        }
        break;
      }
      case SC_YOYO: {  // a yo-yo: throws, a sleeper or walking the dog; tangled, around the world, or it rolls away
        float x, y, spin;
        bool str;
        yoyoPos(x, y, spin, str);
        const float S = yoyoSpecialT(), E = yoyoEndT();
        const bool tapped = sc.evId == 1 && sc.evU < E;
        if (x > -20) { lookPt(x, y); headTo(0.3f * (x - 160) / 160, (y - 110) / 260.0f); } else headTo(0, 0);
        if (at(0.3f)) say(sc.v[3] < 0.5f ? "playful" : "curious");
        if (at(1.0f) || at(2.1f) || (sc.mid == 0 && at(S))) sfx("w:whoosh|v0.12 n:150");
        if (at(1.9f) || at(3.0f)) sfx("w:snap|v0.15 n:20");
        if (at(1.4f)) say("focus");
        if (at(2.6f)) say("happy");
        if (sc.mid == 1) { if (at(S + 0.6f)) say("amazed"); if (at(S + 1.5f)) say("proud"); }
        if (sc.mid == 2) { if (at(S + 0.6f)) say("excited"); if (at(S + 1.8f)) say("giggle"); }
        if (tapped) {
          if (evAt(0.01f)) { say("startled"); sfx("w:snap|v0.3 n:30"); }
          if (evAt(1.0f)) say("giggle");
          if (evT() > 0.1f) finishIn(2.4f);
          break;
        }
        if (sc.end == 0) { if (at(E + 0.4f)) { say("confused"); sfx("w:v_uhoh|930:130 0:40 740~700:200"); } if (at(E + 1.9f)) say("sheepish"); }
        if (sc.end == 1) { if (at(E + 0.4f)) { say("excited"); sfx("w:whoosh|v0.25 n:420"); } if (at(E + 2.1f)) { say("proud"); sfx("w:sparkle|v0.3 1047:80 1319:80 1568:80 2093:260"); } }
        if (sc.end == 2) { if (at(E + 0.5f)) say("surprised"); if (at(E + 2.0f)) say("giggle"); }
        if (u > E) finishIn(3.6f);
        break;
      }
      case SC_FISHING: {  // fishing: patient, dozing or impatient; a bite; a big fish, a boot, a tiny one let go, or it gets away
        const float up = seg(0, 1.0f) * (1 - seg(sc.dur - 1.0f, sc.dur - 0.2f));
        faceShift(-18 * up);
        float bx, by, cx, cy;
        int what;
        const bool caught = fishCatch(bx, by, cx, cy, what);
        const float B = fishBiteT(), R = fishReelT();
        const bool early = sc.evId == 1 && sc.evU < B;
        if (caught) { lookPt(cx, cy); headPt(cx, cy, 0.3f); }
        else if (sc.mid == 2 && u > 3.2f && u < B && fracf(u / 2.4f) < 0.35f) { slook(-0.8f, -0.2f, 0.3f); headTo(-0.2f, 0); }
        else { lookPt(bx, by); headPt(bx, by, 0.3f); }
        if (at(0.3f)) say(sc.v[3] < 0.5f ? "happy" : "curious");
        if (at(0.6f)) sfx("w:whoosh|v0.15 n:300");
        if (at(1.4f)) sfx("w:bubble_pop|v0.3 900~1600:90");
        if (at(2.0f)) say("content");
        if (early) {
          if (evAt(0.01f)) { say("determined"); sfx("w:whoosh|v0.2 n:300"); }
          if (evAt(1.0f)) say(sc.v[4] < 0.5f ? "sheepish" : "confused");
          if (evAt(2.2f)) say("giggle");
          if (evT() > 0.1f) finishIn(3.2f);
          break;
        }
        if (sc.mid == 0) { if (atb(3, 3.6f)) { say("content"); sfx("w:hum|v0.22 523:230 587:230 659:230 784:460"); } if (atb(4, 5.6f)) say("peaceful"); }
        if (sc.mid == 1) { if (atb(3, 3.4f)) say("sleepy"); if (atb(4, 4.8f)) say("sleeping"); if (at(bt(4, 4.8f) + 0.9f)) sfx("w:snore|v0.14 120~85:800"); }
        if (sc.mid == 2) { if (atb(3, 3.4f)) say("bored"); if (atb(4, 5.4f)) say("impatient"); }
        if (at(B)) { say(sc.mid == 1 ? "startled" : "surprised"); sfx("w:bubble_pop|v0.25 1200~1800:60"); }
        if (at(B + 0.7f)) sfx("w:bubble_pop|v0.25 1200~1800:60");
        if (at(B + 1.4f)) { say("excited"); sfx("w:bubble_pop|v0.4 600~300:120"); }
        if (at(R)) { say("determined"); sfx("w:whoosh|v0.25 n:300"); }
        const float A = R + 0.7f;  // the catch is out of the water
        if (what == 0) { if (at(A)) { say("triumphant"); sfx("w:win|v0.35 523:90 659:90 784:90 1047:280"); } if (at(A + 1.6f)) say("proud"); if (u > A) finishIn(3.4f); }
        if (what == 1) { if (at(A)) say("deadpan"); if (at(A + 1.4f)) say("giggle"); if (u > A) finishIn(3.4f); }
        if (what == 2) {
          if (at(A)) say("adoring");
          if (at(A + 2.4f)) { say("happy"); sfx("w:splash|v0.4 n:150 600~200:120"); }
          if (at(A + 3.3f)) say("waving");
          if (u > A) finishIn(4.8f);
        }
        if (what == 3) {
          if (at(A)) say("triumphant");
          if (at(A + 1.0f)) { say("shocked"); sfx("w:splash|v0.4 n:150 600~200:120"); }
          if (at(A + 2.2f)) say("pout");
          if (at(A + 3.4f)) say("giggle");
          if (u > A) finishIn(4.6f);
        }
        break;
      }
      case SC_DUCK: {  // a rubber duck: squeaks, dunks or spins; a duckling; sails off, tips over, or swims close
        float x, y, flip, dunk;
        int face;
        duckPos(x, y, face, flip, dunk);
        const float E = bt(2, 8.5f), s0 = bt(0, 4.2f);
        lookPt(x, y - 10);
        headPt(x, y, 0.35f);
        const char* squeak = "w:v_eep|v0.3 1400~1900:70 0:40 1500~2000:90";
        if (at(0.3f)) say("curious");
        if (at(2.2f)) say("happy");
        if (sc.mid == 0) { if (at(s0)) sfx(squeak); if (at(s0 + 0.5f)) say("giggle"); }
        if (sc.mid == 1) { if (at(s0 + 0.2f)) say("surprised"); if (at(s0 + 1.5f)) sfx("w:bubble_pop|v0.3 1700~2600:45"); if (at(s0 + 1.8f)) say("giggle"); }
        if (sc.mid == 2) { if (at(s0 + 0.2f)) say("amazed"); if (at(s0 + 1.5f)) say("laugh"); }
        if (sc.twist == 1 && atb(1, 3.4f)) say("adoring");
        if (sc.evId == 1 || sc.evId == 3) {
          if (evAt(0.01f)) sfx(squeak);
          if (evAt(0.3f)) say(sc.evId == 1 ? "giggle" : "adoring");
        }
        if (sc.end == 0) { if (at(E)) say("waving"); if (at(E + 2.2f)) say("wistful"); if (u > E) finishIn(3.8f); }
        if (sc.end == 1) {
          if (at(E + 0.2f)) say("surprised");
          if (at(E + 1.0f)) say("laugh");
          if (at(E + 1.9f)) sfx(squeak);
          if (u > E) finishIn(3.4f);
        }
        if (sc.end == 2) { if (at(E + 1.5f)) { say("adoring"); sfx(squeak); } if (at(E + 2.8f)) say("happy"); if (u > E) finishIn(3.8f); }
        break;
      }
      case SC_COOKIE: {  // the cookie jar: sneaky looks, whistling, a clink; a cookie floats out; eaten, caught, or shared
        int end;
        float E, Cg;
        cookieTimes(end, E, Cg);
        const float L = bt(1, 4.6f);
        float x, y, s;
        const bool cOn = cookiePos(x, y, s);
        if (at(0.3f)) say("sneaky");
        if (u < L) {
          if (sc.mid == 0) { if (u < 1.0f) lookPt(262, 196); else if (u < 2.0f) slook(-1.0f, 0, 0.3f); else if (u < 3.0f) slook(0, -0.1f, 0.3f); else lookPt(262, 196); }
          if (sc.mid == 1) { if (u > 1.0f && u < 3.4f) slook(-0.4f + 0.2f * sinf(u * 2), -0.9f, 0.3f); else lookPt(262, 196); }
          if (sc.mid == 2) { if (u < 1.8f || u > 3.6f) lookPt(262, 196); else slook(0, 0, 0.3f); }
          headTo(sc.mid == 0 && u > 1.0f && u < 2.0f ? -0.35f : 0.2f, 0.1f);
        } else if (cOn) { lookPt(x, y); headPt(x, y, 0.3f); }
        else slook(0, 0, 0.3f);
        if (sc.mid == 0) { if (at(1.0f)) say("suspicious"); if (at(2.0f)) say("sneaky"); }
        if (sc.mid == 1) { if (at(1.0f)) { say("whistle"); sfx("w:hum|v0.22 523:230 587:230 659:230 784:460"); } if (at(3.4f)) say("mischievous"); }
        if (sc.mid == 2) { if (at(1.8f)) { say("nervous"); sfx("w:snap|v0.15 n:15"); } if (at(3.6f)) say("sneaky"); }
        if (at(L) && Cg > L) { say("mischievous"); sfx("w:tinkle|v0.2 2637:50 3136:100"); }
        if (at(L + 0.9f) && Cg > L + 0.9f) say("drooling");
        if (sc.evId == 3 && evAt(0.01f)) say("adoring");
        if (end == 1) {  // caught!
          if (at(Cg)) { say("startled"); sfx("w:v_eep|1300~1750:150"); }
          if (at(Cg + 0.6f)) sfx("w:snap|v0.3 n:30");
          if (at(Cg + 1.2f)) say("guilty");
          if (at(Cg + 2.4f)) say("sheepish");
          if (at(Cg + 3.4f)) say("giggle");
          if (u > Cg) finishIn(4.2f);
          break;
        }
        if (end == 0) {
          if (at(E)) { say("munching"); sfx("w:crunch|v0.4 n:45 0:60 n:40 0:70 n:35"); }
          if (at(E + 0.8f)) sfx("w:crunch|v0.4 n:45 0:60 n:40 0:70 n:35");
          if (at(E + 1.6f)) say("yum");
          if (at(E + 2.6f)) say("blissful");
          if (u > E) finishIn(3.8f);
        } else {
          if (at(E + 0.4f)) say("adoring");
          if (at(E + 1.8f)) say("happy");
          if (u > E) finishIn(3.6f);
        }
        break;
      }
      case SC_TOP: {  // a spinning top: wanders, hypnotizes or hops; falls over, spins away, or never stops
        float x, y, a, spin;
        topPos(x, y, a, spin);
        const float E = bt(2, 8.5f);
        faceShift(-20 * seg(0.2f, 1.0f) * (1 - seg(sc.dur - 0.8f, sc.dur)));
        if (x < 340) { lookPt(x, y - 28); headPt(x, y, 0.3f); } else headTo(0, 0);
        if (at(0.3f)) say("curious");
        if (at(1.0f)) { say("excited"); sfx("w:whoosh|v0.2 n:300"); }
        if (sc.mid == 0) { if (atb(3, 3.5f)) say("amazed"); if (atb(4, 6.0f)) say("happy"); }
        if (sc.mid == 1) { const float H0 = bt(0, 3.6f); if (at(H0)) say("hypnotized"); if (at(H0 + 2.6f)) say("dizzy"); if (at(H0 + 3.6f)) say("giggle"); }
        if (sc.mid == 2) { const float h0 = bt(0, 4.5f); if (at(h0)) { say("surprised"); sfx("w:boing|v0.3 300~700:120"); } if (at(h0 + 1.0f)) say("laugh"); }
        if (sc.evId == 1) { if (evAt(0.01f)) { say("excited"); sfx("w:whoosh|v0.25 n:300"); } if (evAt(1.2f)) say("happy"); }
        if (sc.end == 0) {
          if (at(E - 1.0f)) say("worried");
          if (at(E + 0.45f)) sfx("w:snap|v0.35 n:40 0:90 n:25");
          if (at(E + 0.7f)) say("sad");
          if (at(E + 1.9f)) say("giggle");
          if (u > E) finishIn(3.2f);
        }
        if (sc.end == 1) { if (at(E + 0.3f)) say("surprised"); if (at(E + 1.4f)) say("happy"); if (u > E) finishIn(2.8f); }
        if (sc.end == 2) {
          if (at(E)) say("dizzy");
          if (at(E + 1.6f)) say("woozy");
          shown.tilt += 8 * sinf(u * 3) * seg(E, E + 1);
          if (u > E) finishIn(3.4f);
        }
        break;
      }
      case SC_MAGIC: {  // a magic trick: taps of the wand, a drum roll, a stare; a bunny, a dove, flowers, or a puff of nothing
        const float in = seg(0.1f, 0.9f) * (1 - seg(sc.dur - 0.8f, sc.dur));
        faceShift(-16 * in);
        const float T0 = bt(0, 2.0f), R = magicRevealT();
        if (u < R + 0.4f) { lookPt(176, 186); headTo(0.05f, 0.25f); }
        else if (sc.end == 1) { const float e = seg(R + 0.3f, R + 2.5f); lookPt(lerpf(160, 40, e), lerpf(170, -10, e)); headTo(-0.3f * e, 0.2f - 0.6f * e); }
        else { lookPt(160, 160); headTo(0, 0.15f); }
        if (at(0.3f)) say(sc.v[3] < 0.5f ? "excited" : "curious");
        for (int q = 0; q < 3; q++) if (at(T0 + q * 0.65f)) sfx("w:tinkle|v0.2 2637:50 3136:100");
        if (sc.mid == 0 && at(T0 + 0.1f)) say("focus");
        if (sc.mid == 1 && at(T0 + 0.1f)) say("nervous");
        if (sc.mid == 2 && at(T0 + 0.1f)) say("determined");
        if (at(R)) sfx(sc.end == 3 ? "w:blow|v0.3 n:300" : "w:v_tada|920:100 1230:240");
        if (sc.evId == 3 && sc.evU > R) {  // a pat after the trick: applause, a bow
          if (evAt(0.01f)) say("bowing");
          if (evAt(1.4f)) say("happy");
          if (evT() > 0.1f) finishIn(3.0f);
          break;
        }
        if (sc.end == 0) { if (at(R + 0.5f)) say("amazed"); if (at(R + 1.8f)) say("adoring"); }
        if (sc.end == 1) { if (at(R + 0.4f)) { say("awe"); sfx("w:tiny_chirp|v0.2 1760:45 0:45 2093:70"); } if (at(R + 2.2f)) say("happy"); }
        if (sc.end == 2) { if (at(R + 0.4f)) { say("proud"); sfx("w:sparkle|v0.3 1047:80 1319:80 1568:80 2093:260"); } if (at(R + 1.8f)) say("happy"); }
        if (sc.end == 3) {
          if (at(R + 0.5f)) say("confused");
          if (at(R + 1.6f)) { say("sheepish"); sfx("w:v_uhoh|930:130 0:40 740~700:200"); }
          if (at(R + 2.8f)) say("giggle");
        }
        if (u > R) finishIn(sc.end == 3 ? 3.8f : 3.6f);
        break;
      }
      case SC_CAT: {  // a cat peeks up: a slow blink, a paw swipe, a tail; it naps, leaves or rubs; pat: it purrs
        float cx, cy;
        catPos(cx, cy);
        const float E = bt(2, 10), S = bt(0, 5);
        const bool scared = (sc.evId == 1 || sc.evId == 2), patted = sc.evId == 3;
        faceShift(-12 * clampf((300 - cy) / 88, 0, 1));  // (room for it under the eyes)
        if (cy < 260) {
          if (sc.mid == 2 && u > S && u < S + 3.2f && !scared && !patted) slook(fracf((u - S) / 1.6f) < 0.5f ? -0.7f : 0.5f, 0.3f, 0.3f);
          else lookPt(cx, cy - 10);
          headPt(cx, cy, 0.3f);
        } else headTo(0, 0);
        if (at(1.2f)) say("amazed");
        if (at(2.6f)) say("adoring");
        if (scared) {
          if (evAt(0.01f)) say("startled");
          if (evAt(0.9f)) say("sheepish");
          if (evAt(2.0f)) say("wistful");
          if (evT() > 0.1f) finishIn(2.8f);
          break;
        }
        if (patted) {
          if (evAt(0.01f)) say("blissful");
          if (evAt(1.6f)) say("adoring");
          if (evT() > 0.1f) finishIn(3.6f);
          break;
        }
        if (sc.mid == 0) { if (at(S + 2.0f)) { say("happy_blink"); sfx("w:chime_up|v0.2 1568:90 2093:170"); } if (at(S + 3.4f)) say("content"); }
        if (sc.mid == 1) { if (at(S + 0.25f)) say("startled"); if (at(S + 1.1f)) say("surprised"); if (at(S + 2.0f)) say("giggle"); }
        if (sc.mid == 2) { if (at(S + 0.3f)) say("playful"); if (at(S + 2.6f)) say("giggle"); }
        if (sc.end == 0) { if (at(E + 0.6f)) say("adoring"); if (at(E + 2.4f)) say("sleepy"); if (u > E) finishIn(4.8f); }
        if (sc.end == 1) { if (at(E + 0.2f)) say("waving"); if (at(E + 1.8f)) say("wistful"); if (u > E) finishIn(3.2f); }
        if (sc.end == 2) { if (at(E + 0.3f)) say("blissful"); if (at(E + 2.0f)) say("happy"); if (u > E) finishIn(3.6f); }
        break;
      }
      case SC_DOMINO: {  // a domino chain: steady or speeding up; a stall it has to blow on; a bell, a ball, or a rewind
        const float up = seg(0, 0.8f);
        faceShift(-10 * up);
        const float S = domStart(), E = domEndT();
        const bool tapped = sc.evId == 1 && sc.evU < S;
        int front = -1;
        for (int i = 0; i < DOM_N; i++) if (u > domFallT(i)) front = i;
        const float fx = domX(front < 0 ? 0 : front) + (front >= 0 ? 14 : 0);
        if (u < E + 0.2f) { lookPt(fx, 212); headPt(fx, 212, 0.35f); }
        else if (sc.end == 1) { const float bx = 304 + 200 * seg(E, E + 1.2f); lookPt(fminf(bx, 330), 226); headPt(fminf(bx, 330), 226, 0.35f); }
        else { lookPt(200, 212); headTo(0.15f, 0.25f); }
        if (at(0.3f)) say("curious");
        if (!tapped && at(S - 0.9f)) say("determined");
        if (!tapped && at(S)) sfx("w:snap|v0.2 n:20");
        if (!tapped) faceShift(6 * kick(S, 0.08f, 6));
        for (int i = 1; i < DOM_N; i++) if (at(domFallT(i))) sfx("w:snap|v0.15 n:12");
        if (at(S + 0.6f)) say(sc.mid == 1 ? "excited" : "amazed");
        if (tapped) {
          if (evAt(0.01f)) say("startled");
          if (evAt(1.4f)) say("giggle");
          if (sc.end == 0 && at(E)) sfx("w:ding|v0.35 1319:90 1760:340");
          if (u > E) finishIn(2.6f);
          break;
        }
        if (sc.twist == 1) {
          const float st = domFallT(5) + 0.25f;
          if (at(st)) say("worried");
          if (at(st + 1.0f)) { say("puffed"); sfx("w:blow|v0.3 n:300"); }
          if (at(domFallT(6) + 0.4f)) say("relieved");
        }
        if (sc.end == 0) { if (at(E)) sfx("w:ding|v0.35 1319:90 1760:340"); if (at(E + 0.3f)) say("proud"); if (at(E + 1.6f)) say("happy"); }
        if (sc.end == 1) { if (at(E)) say("surprised"); if (at(E + 1.4f)) say("giggle"); }
        if (sc.end == 2) { if (at(E + 0.6f)) sfx("w:whoosh|v0.2 n:420"); if (at(E + 0.8f)) say("amazed"); if (at(E + 2.0f)) say("starstruck"); }
        if (u > E) finishIn(3.4f);
        break;
      }
      case SC_SNOWMAN: {  // building a snowman: a hat or not; its head rolls off; it waves, melts, or gets a hug
        const float E = snowmanEndT();
        float hx, hy;
        snowHead(hx, hy);
        const bool hit = sc.evId == 1;
        faceShift(-14 * seg(0.2f, 1.2f) * (1 - seg(sc.dur - 0.8f, sc.dur)));
        if (u < 2.3f) { const float e = seg(0.4f, 2.2f); lookPt(lerpf(340, 290, e), 214); headTo(0.3f, 0.3f); }
        else if (u < 4.4f) { lookPt(290, u < 3.4f ? 191 : 172); headTo(0.3f, 0.15f); }
        else { lookPt(hx, hy); headPt(hx, hy, 0.35f); }
        if (at(0.3f)) say("excited");
        if (at(0.6f) || at(1.5f)) sfx("w:crunch|v0.2 n:45 0:60 n:40");
        if (at(3.0f) || at(3.9f)) sfx("w:crunch|v0.25 n:45");
        if (at(2.4f)) say("determined");
        if (at(4.7f) || at(5.3f) || at(5.9f) || at(6.5f) || (sc.mid == 1 && at(7.1f))) sfx("w:tinkle|v0.15 2637:40");
        if (at(5.5f)) say("happy");
        if (at(7.4f)) say("proud");
        if (hit) {  // a tap: a snowball, right in the face
          if (evAt(0.4f)) { say("startled"); sfx("w:crunch|v0.35 n:60"); }
          if (evAt(1.5f)) say("laugh");
          if (evT() > 0.1f) finishIn(3.2f);
          break;
        }
        if (sc.twist == 1) {
          const float h0 = bt(1, 8.0f);
          if (at(h0 + 0.2f)) say("shocked");
          if (at(h0 + 2.0f)) say("determined");
          if (at(h0 + 2.7f)) { say("relieved"); sfx("w:crunch|v0.2 n:40"); }
        }
        if (sc.end == 0) { if (at(E)) { say("amazed"); sfx("w:sparkle|v0.3 1047:80 1319:80 1568:80 2093:260"); } if (at(E + 1.2f)) say("waving"); if (at(E + 2.6f)) say("happy"); if (u > E) finishIn(3.8f); }
        if (sc.end == 1) { if (at(E + 0.6f)) sfx("w:chime_down|v0.2 2093:70 1568:90"); if (at(E + 1.2f)) say("sad"); if (at(E + 3.0f)) say("wistful"); if (u > E) finishIn(4.6f); }
        if (sc.end == 2) {
          if (at(E + 0.3f)) say("adoring");
          if (at(E + 1.4f)) say("freezing");
          faceShift(2.5f * sinf(u * 55) * seg(E + 1.4f, E + 1.6f) * (1 - seg(E + 2.4f, E + 2.7f)));
          if (at(E + 2.8f)) say("giggle");
          if (u > E) finishIn(3.8f);
        }
        break;
      }
      case SC_FROG: {  // a frog friend: hiccups (Oreo catches them), croaks, or catches a fly; hops on the head, away, or boo!
        float x, y, sq;
        frogPos(x, y, sq);
        const float E = bt(2, 8.6f), H0 = bt(0, 3.2f);
        if (x < 340) { lookPt(x, y - 10); headPt(x, y, 0.35f); } else headTo(0, 0);
        if (sc.end == 0 && u > E + 0.6f && sc.evId == 0) { slook(0, -1.2f, 0.3f); headTo(0, -0.4f); }
        if (at(0.6f) || at(1.5f)) sfx("w:boing|v0.15 300~500:80");
        if (at(0.4f)) say("curious");
        if (at(2.4f)) say("happy");
        if (sc.evId == 1) {
          if (evAt(0.01f)) { say("startled"); sfx("w:boing|v0.3 300~700:120"); }
          if (evAt(1.0f)) say("sheepish");
          if (evT() > 0.1f) finishIn(2.4f);
          break;
        }
        if (sc.evId == 3) {
          if (evAt(0.01f)) say("adoring");
          if (evAt(1.6f)) say("happy");
          if (evT() > 0.1f) finishIn(3.0f);
          break;
        }
        if (sc.mid == 0) {
          for (int q = 0; q < 3; q++) if (at(H0 + (q == 0 ? 0 : q == 1 ? 1.2f : 2.1f))) sfx("w:hic|v0.25 900~1300:60");
          if (at(H0 + 0.4f)) say("giggle");
          const float o1 = H0 + 3.0f, o2 = H0 + 3.9f;  // it's catching: Oreo hiccups too
          if (at(o1) || at(o2)) { say("hiccup"); sfx("w:hic|v0.3 600~1150:70"); }
          faceShift(-12 * fmaxf(kick(o1, 0.06f, 8), kick(o2, 0.06f, 8)));
          if (at(o2 + 0.8f)) say("embarrassed");
        }
        if (sc.mid == 1) {
          for (int q = 0; q < 2; q++) if (at(H0 + q * 1.4f)) sfx("v0.35 160~120:70 0:50 170~125:90");
          if (at(H0 + 0.5f)) say("surprised");
          if (at(H0 + 2.0f)) say("giggle");
        }
        if (sc.mid == 2) {
          if (at(H0)) sfx("w:fly_buzz|v0.3 z:900");
          if (at(H0 + 2.0f)) sfx("w:snap|v0.3 n:30");
          if (at(H0 + 2.3f)) say("amazed");
          if (at(H0 + 3.4f)) say("laugh");
        }
        if (sc.end == 0) { if (at(E)) sfx("w:boing|v0.3 300~700:120"); if (at(E + 1.0f)) say("giggle"); if (at(E + 2.4f)) say("happy"); if (u > E) finishIn(3.6f); }
        if (sc.end == 1) { if (at(E + 0.2f)) say("waving"); if (at(E + 1.8f)) say("content"); if (u > E) finishIn(3.0f); }
        if (sc.end == 2) {
          if (at(E)) { say("surprised"); sfx("w:boo|v0.35 500~1150:120"); }
          if (at(E + 1.2f)) say("laugh");
          if (at(E + 2.4f)) say("relieved");
          if (u > E) finishIn(3.4f);
        }
        break;
      }
      case SC_MOON: {  // a sleepy moon: it yawns (so does Oreo), winks, or hums; both doze, it drifts off, or a cloud blanket
        float x, y;
        moonPos(x, y);
        const float E = moonEndT();
        const bool woke = sc.evId == 1 || sc.evId == 2;
        if (!(sc.end == 0 && u > E + 1.0f)) { lookPt(x, y); headPt(x, y, 0.35f); }
        else headTo(0, 0.2f);
        if (at(0.4f)) say(sc.v[3] < 0.5f ? "awe" : "peaceful");
        if (woke) {
          if (evAt(0.01f)) say("sheepish");
          if (evAt(1.5f)) say("giggle");
          if (evT() > 0.1f) finishIn(3.4f);
          break;
        }
        if (sc.mid == 0) { const float Y = bt(0, 4); if (at(Y + 1.4f)) { say("yawn"); sfx("w:v_yawn|"); } if (at(Y + 3.2f)) say("sleepy"); }
        if (sc.mid == 1) { const float W0 = bt(0, 4.5f); if (at(W0 + 0.3f)) say("surprised"); if (at(W0 + 0.9f)) say("giggle"); if (at(W0 + 2.0f)) say(sc.v[4] < 0.5f ? "wink_left" : "wink_right"); }
        if (sc.mid == 2) {
          if (at(2.6f)) playTune(6, 0.3f);
          if (at(3.6f)) say("peaceful");
          if (u > 2.6f && u < E) headTo(0.2f * sinf((u - 2.6f) * 1.6f), -0.15f);
        }
        if (sc.end == 0) {
          if (at(E)) say("sleepy");
          if (at(E + 1.4f)) say("sleeping");
          if (at(E + 2.2f) || at(E + 4.4f)) sfx("w:snore|v0.14 120~85:800");
          if (u > E) finishIn(5.4f);
        }
        if (sc.end == 1) { if (at(E)) say("waving"); if (at(E + 2.2f)) say("sleepy"); if (u > E) finishIn(3.6f); }
        if (sc.end == 2) { if (at(E + 1.2f)) say("cozy"); if (at(E + 2.6f)) say("sleepy"); if (u > E) finishIn(4.2f); }
        break;
      }
      case SC_DOODLE: {  // doodling: a heart, a sun, a cat, a house or itself; a mistake rubbed out; proud, crumpled, or it comes alive
        const float in = seg(0.1f, 0.9f) * (sc.end == 1 ? 1 : 1 - seg(sc.dur - 0.7f, sc.dur));
        faceShift(-24 * in);
        float px, py;
        const bool drawing = doodlePen(px, py);
        const float E = doodleEndT();
        if (u < E) { lookPt(px, py); headPt(px, py, 0.3f); }
        else { lookPt(160, 200); headTo(0, 0.2f); }
        if (at(0.3f)) say("curious");
        if (at(1.4f)) say("focus");
        if (drawing && (at(1.5f) || at(3.8f))) sfx("w:page|v0.1 n:80");
        if (sc.twist == 1) {
          const float M = doodleMistakeT();
          if (at(M + 0.4f)) say("embarrassed");
          if (at(M + 0.8f)) sfx("w:page|v0.12 n:120");
          if (at(M + 2.0f)) say("focus");
        }
        if (sc.end == 0) { if (at(E)) { say("proud"); sfx("w:sparkle|v0.3 1047:80 1319:80 1568:80 2093:260"); } if (at(E + 1.6f)) say("happy"); if (u > E) finishIn(3.2f); }
        if (sc.end == 1) { if (at(E + 0.3f)) { say("silly"); sfx("w:crumple|v0.3 n:70"); } if (at(E + 1.6f)) say("giggle"); if (u > E) finishIn(2.8f); }
        if (sc.end == 2) { if (at(E + 0.2f)) say("amazed"); if (at(E + 0.4f)) sfx("w:chime_up|v0.25 1568:90 2093:170"); if (at(E + 1.6f)) say("starstruck"); if (u > E) finishIn(3.4f); }
        break;
      }
      case SC_BOAT: {  // a paper boat: drifts, blown along, or choppy water; Crumb hops on; sails off, sinks, or docks
        float x, y, a, sink;
        boatPos(x, y, a, sink);
        const float E = bt(2, 8.5f), B = bt(0, 4.0f);
        if (x > -30 && x < 350) { lookPt(x, y - 10); headPt(x, y, 0.35f); } else headTo(0, 0);
        if (at(0.3f)) say("curious");
        if (at(2.4f)) say("happy");
        if (sc.mid == 0 && atb(3, 4.6f)) say("content");
        if (sc.mid == 1) { if (at(B - 0.3f)) say("puffed"); if (at(B)) sfx("w:blow|v0.3 n:300"); if (at(B + 1.6f)) say("excited"); }
        if (sc.mid == 2) { if (atb(0, 3.5f)) say("worried"); if (at(bt(0, 3.5f) + 2.4f)) say("relieved"); }
        if (sc.twist == 1) { const float c0 = bt(1, 5.5f); if (at(c0)) { say("adoring"); sfx("w:tiny_chirp|v0.2 1760:45 0:45 2093:70"); } }
        if (sc.evId == 1) { if (evAt(0.01f)) sfx("w:splash|v0.35 n:150 600~200:120"); if (evAt(0.3f)) say("giggle"); }
        if (sc.end == 0) { if (at(E)) say("waving"); if (at(E + 2.2f)) say("content"); if (u > E) finishIn(3.6f); }
        if (sc.end == 1) {
          if (at(E + 0.4f)) say("worried");
          if (at(E + 1.4f)) sfx("w:bubble_pop|v0.3 900~1600:90");
          if (at(E + 1.6f)) say("sad");
          if (at(E + 3.0f)) say("wistful");
          if (u > E) finishIn(4.2f);
        }
        if (sc.end == 2) { if (at(E + 1.6f)) { say("proud"); sfx("w:sparkle|v0.3 1047:80 1319:80 1568:80 2093:260"); } if (at(E + 2.8f)) say("happy"); if (u > E) finishIn(3.8f); }
        break;
      }
      case SC_PLANT: {  // watering a tiny plant: once, with a song, or twice; it blooms, droops then perks up, or shoots up tall
        const float W1 = bt(0, 1.6f), E = plantBloomT();
        float tx, ty;
        plantTop(tx, ty);
        faceShift(-10 * seg(0.1f, 0.8f) * (1 - seg(sc.dur - 0.6f, sc.dur)));
        if (u < W1 + 2.4f) { lookPt(250, 190); headPt(250, 190, 0.3f); }

        else if (sc.mid == 2 && u < W1 + 5.0f) { lookPt(250, 190); headPt(250, 190, 0.3f); }
        else { lookPt(tx, ty); headPt(tx, ty, 0.35f); }
        if (at(0.3f)) say("happy");
        if (at(W1)) { say("focus"); sfx("w:rain|v0.15 n:600"); }
        if (sc.mid == 0 && at(W1 + 2.6f)) say("hopeful");
        if (sc.mid == 1) { const float S1 = W1 + 2.6f; if (at(S1)) { say("singing"); sfx("w:hum|v0.22 523:230 587:230 659:230 784:460"); } if (at(S1 + 2.2f)) say("happy"); }
        if (sc.mid == 2) { if (at(W1 + 2.6f)) say("impatient"); if (at(W1 + 3.2f)) { say("determined"); sfx("w:rain|v0.15 n:500"); } }
        if (sc.evId == 3) {
          if (evAt(0.01f)) say("adoring");
          if (evAt(1.4f)) say("happy");
          if (u > E && evT() > 0.1f) finishIn(3.0f);
        }
        if (sc.end == 0) { if (at(E)) { say("amazed"); sfx("w:chime_up|v0.25 1568:90 2093:170"); } if (at(E + 1.4f)) say("happy"); }
        if (sc.end == 1) { if (at(E - 1.6f)) say("worried"); if (at(E)) { say("relieved"); sfx("w:chime_up|v0.2 1568:90 2093:170"); } if (at(E + 1.6f)) say("happy"); }
        if (sc.end == 2) { if (at(E - 0.6f)) say("amazed"); if (at(E + 0.4f)) { say("starstruck"); sfx("w:sparkle|v0.3 1047:80 1319:80 1568:80 2093:260"); } }
        if (u > E) finishIn(3.6f);
        break;
      }
      case SC_ROCKET: {  // a toy rocket: a countdown (nods along, or a false start); to the stars, a fizzle, or it comes back and lands
        float x, y, a, flame;
        rocketPos(x, y, a, flame);
        const float L = rocketLaunchT();
        faceShift(-10 * seg(0.1f, 0.8f) * (1 - seg(sc.dur - 0.6f, sc.dur)));
        if (y > -40) { lookPt(x, y - 40); headPt(x, y - 30, 0.4f); } else { slook(0.4f, -1.1f, 0.3f); headTo(0.25f, -0.5f); }
        if (at(0.3f)) say(sc.v[3] < 0.5f ? "excited" : "curious");
        const float C0 = 1.0f, C1 = 1.8f, C2 = 2.6f;
        if (L > C2 + 0.5f) {
          if (at(C0) || at(C1)) sfx("w:beep|v0.3 880:80");
          if (at(C2)) sfx("w:beep_hi|v0.3 1320:160");
        }
        if (at(C0 + 0.1f)) say(sc.mid == 1 ? "determined" : "focus");
        if (sc.mid == 1) faceShift(8 * (kick(C0, 0.06f, 7) + kick(C1, 0.06f, 7) + kick(C2, 0.06f, 7)));
        if (sc.mid == 2) { if (at(3.4f)) sfx("w:blow|v0.25 n:200"); if (at(3.7f)) say("confused"); if (at(4.6f)) say("determined"); }
        if (sc.evId == 1 && evAt(0.01f) && sc.evU < L) say("startled");
        if (at(L)) { say("amazed"); sfx("w:whoosh|v0.4 n:420"); }
        if (sc.end == 0) { if (at(L + 2.2f)) { say("starstruck"); sfx("w:shooting_star|v0.22 2637:60 2349:60 2093:60 1760:160"); } if (at(L + 3.4f)) say("happy"); if (u > L) finishIn(4.4f); }
        if (sc.end == 1) { if (at(L + 0.7f)) say("worried"); if (at(L + 1.2f)) { say("sheepish"); sfx("w:bonk|v0.3 330~220:80"); } if (at(L + 2.6f)) say("giggle"); if (u > L) finishIn(4.0f); }
        if (sc.end == 2) {
          if (at(L + 2.6f)) say("curious");
          if (at(L + 2.8f)) sfx("w:whoosh|v0.3 n:420");
          if (at(L + 4.9f)) { say("triumphant"); sfx("w:win|v0.35 523:90 659:90 784:90 1047:280"); }
          if (u > L) finishIn(6.8f);
        }
        break;
      }
      case SC_ICECREAM: {  // ice cream: licks, a big bite (brain freeze!), a wobbly tower; all gone, a scoop falls, or Crumb takes the cherry
        const float up = seg(0.1f, 1.0f) * (1 - seg(sc.dur - 0.8f, sc.dur));
        faceShift(-28 * up);
        const float E = bt(2, 8);
        lookPt(160, 175);
        headTo(sc.mid == 2 ? 0.15f * sinf(u * 3) * seg(3, 4) * (1 - seg(E - 0.5f, E)) : 0, 0.25f * up);
        if (at(0.3f)) say("excited");
        if (at(1.2f)) say("drooling");
        for (int q = 0; q < 3; q++) if (at(iceLickT(q))) { say("tongue"); sfx("w:sip|v0.18 n:120"); }
        if (at(iceLickT(0) + 0.8f)) say("yum");
        if (sc.mid == 1) {
          const float B = bt(0, 4.4f);
          if (at(B)) { say("munching"); sfx("w:crunch|v0.3 n:45"); }
          if (at(B + 0.6f)) { say("freezing"); sfx("w:tinkle|v0.25 2637:50 3136:100"); }
          faceShift(2.5f * sinf(u * 50) * seg(B + 0.6f, B + 0.7f) * (1 - seg(B + 1.8f, B + 2.0f)));
          if (at(B + 2.2f)) say("giggle");
        }
        if (sc.mid == 2) { if (atb(0, 3.2f)) say("nervous"); if (at(bt(0, 3.2f) + 2.2f)) say("relieved"); }
        if (sc.end == 0) {
          for (int q = 0; q < 3; q++) if (at(E + q * 0.6f)) sfx("w:munch|v0.3 n:35 0:50 n:30");
          if (at(E + 1.8f)) say("blissful");
          if (at(E + 3.0f)) say("content");
          if (u > E) finishIn(3.8f);
        }
        if (sc.end == 1) {
          if (at(E + 0.15f)) say("shocked");
          if (at(E + 0.6f)) sfx("w:bubble_pop|v0.4 300~120:120");
          if (at(E + 1.1f)) say("sad");
          if (at(E + 2.5f)) say("pout");
          if (u > E) finishIn(3.6f);
        }
        if (sc.end == 2) {
          if (at(E - 0.6f)) sfx("w:tiny_chirp|v0.25 2093:40 0:30 2349:60");
          if (at(E)) say("surprised");
          if (at(E + 0.8f)) say("pout");
          if (at(E + 2.2f)) say("giggle");
          if (u > E) finishIn(3.4f);
        }
        break;
      }
      case SC_DANDELION: {  // a dandelion: puff! the seeds fly; it watches them go, one comes back (achoo), or it makes a wish
        const float B = bt(0, 2.0f);
        if (u < B + 0.4f) { lookPt(122, 192); headPt(122, 192, 0.3f); }
        else if (sc.end == 1 && u > B + 1.4f) { lookPt(160, sc.fy + 10); headTo(0, 0); shown.cross = fmaxf(shown.cross, 0.8f * seg(B + 2.0f, B + 2.4f) * (1 - seg(B + 3.5f, B + 3.6f))); }
        else { const float e = seg(B + 0.3f, B + 3.0f); lookPt(lerpf(100, -10, e), lerpf(180, 110, e)); headTo(-0.45f * e, -0.15f * e); }

        if (at(0.3f)) say("curious");
        if (at(B - 0.7f)) say("puffed");
        if (at(B)) sfx("w:blow|v0.3 n:300");
        if (at(B + 0.6f)) say("awe");
        if (sc.end == 0 && at(B + 2.0f)) say(sc.v[4] < 0.5f ? "wistful" : "peaceful");
        if (sc.end == 1) {
          if (at(B + 2.6f)) { say("sneeze_ah"); sfx("w:ah1|v0.18 400~560:280"); }
          if (at(B + 3.0f)) { say("sneeze_ahh"); sfx("w:ah2|v0.22 450~660:300"); }
          if (at(B + 3.5f)) { say("sneeze"); sfx("w:achoo|v0.6 n:280"); }
          faceShift(16 * kick(B + 3.5f, 0.06f, 7));
          if (at(B + 4.4f)) say("giggle");
        }
        if (sc.end == 2) { if (at(B + 1.2f)) say("hopeful"); if (at(B + 2.4f)) { say("happy"); sfx("w:wish|v0.16 1047:220 1319:220 1568:420"); } }
        if (u > B) finishIn(sc.end == 1 ? 5.0f : 4.2f);
        break;
      }
      case SC_GUM: {  // bubble gum: it grows... pops all over its face, a tiny pop, or floats away
        const float P = gumPopT();
        float mx, my;
        mouthAt(mx, my);
        if (at(0.2f)) say("puffed");
        const float g = gumR() / 40.0f;
        if (u < P) { slook(0, 0.9f, 0.3f); shown.cross = fmaxf(shown.cross, 0.7f * clampf(g, 0, 1)); headTo(0, -0.1f * g); }
        if (at(1.6f) || at(2.6f)) say("puffed");
        const int end = gumEnd();
        if (end == 2) {
          if (at(3.6f)) say("surprised");
          if (u > 3.6f) { const float e = seg(3.6f, 5.6f); slook(0.2f * sinf(u * 2), lerpf(0.4f, -1.2f, e), 0.3f); headTo(0, -0.45f * e); }
          if (at(4.6f)) say("happy");
        } else {
          if (at(P)) { say(end == 0 ? "startled" : "surprised"); sfx(end == 0 ? "w:balloon_pop|v0.6 n:45" : "w:bubble_pop|v0.4 900~1600:90"); }
          if (end == 0) { if (at(P + 0.8f)) say("sheepish"); if (at(P + 1.9f)) say("giggle"); }
          else if (at(P + 0.7f)) say("giggle");
          if (u > P) finishIn(end == 0 ? 2.8f : 2.0f);
        }
        break;
      }
      case SC_PAPERBALL: {  // a paper ball into the bin: swish, off the rim, or round and round the rim and in
        float x, y;
        paperPos(x, y);
        const float T0 = bt(0, 2.0f);
        lookPt(x, y);
        headPt(x, y, 0.35f);
        if (at(0.3f)) sfx("w:crumple|v0.3 n:70");
        if (at(0.6f)) say(sc.v[4] < 0.5f ? "determined" : "focus");
        if (at(T0)) sfx("w:whoosh|v0.15 n:200");
        if (at(T0 + 0.4f)) say("hopeful");
        const float H = T0 + 0.9f;
        if (sc.end == 0) { if (at(H)) { say("triumphant"); sfx("w:win|v0.35 523:90 659:90 784:90 1047:280"); } if (at(H + 1.4f)) say("proud"); }
        if (sc.end == 1) { if (at(H)) sfx("w:bonk|v0.3 330~220:80"); if (at(H + 0.4f)) say(sc.v[5] < 0.5f ? "pout" : "frustrated"); if (at(H + 1.8f)) say("giggle"); }
        if (sc.end == 2) {
          if (at(H) || at(H + 0.45f)) sfx("w:snap|v0.15 n:15");
          if (at(H + 0.1f)) say("nervous");
          if (at(H + 1.1f)) { say("celebrate"); sfx("w:win|v0.35 523:90 659:90 784:90 1047:280"); }
        }
        if (u > H) finishIn(sc.end == 2 ? 3.2f : 2.6f);
        break;
      }
    }
  }

  // positions over time (shared by the timeline and the drawing)
  void kitePos(float& x, float& y, float& a) const {
    const float u = sc.u, gust = (weatherNow == 3 || weatherNow == 4) ? 1.6f : 1.0f;
    const float in = seg(0.2f, 2.6f);
    float bx = 228 + 46 * sinf(u * 0.7f + sc.v[0] * 6) + 8 * gust * sinf(u * 2.9f);
    float by = 36 + 10 * sinf(u * 1.3f + 1) + 4 * gust * sinf(u * 4.3f);
    a = 0.22f * sinf(u * 1.7f) + 0.08f * gust * sinf(u * 5.1f);
    auto loop = [&](float l0) {
      const float e = clampf((u - l0) / 1.5f, 0, 1);
      if (e > 0 && e < 1) { const float th = ease(e) * 2 * PI_F; bx += 38 * sinf(th); by += 38 * (1 - cosf(th)); a += th; }
    };
    if (sc.mid == 1) loop(bt(0, 4.5f));
    if (sc.evId == 1) loop(sc.evU + 0.1f);
    if (sc.mid == 2) {
      const float e = clampf((u - bt(0, 4.5f)) / 1.6f, 0, 1), d = sinf(e * PI_F);
      bx = lerpf(bx, 170, d);
      by += 96 * d * d;
      a += 0.9f * sinf(e * 2 * PI_F);
    }
    x = lerpf(300, bx, in);
    y = lerpf(250, by, in);
    const float E = bt(2, 10);
    if (u > E) {
      if (sc.end == 0) { const float e = seg(E, E + 3.2f); x += -200 * e; y += -110 * e + 16 * sinf(e * 9); a += 3 * e; }
      if (sc.end == 1) { const float e = seg(E + 0.1f, E + 0.8f); x = lerpf(x, 166, e); y = lerpf(y, 40, e); a = lerpf(a, PI_F - 0.35f, e); }
      if (sc.end == 2) { const float e = seg(E, E + 2.2f); x = lerpf(x, 290, e); y = lerpf(y, 206, e); a *= 1 - e; }
    }
  }

  float yoyoSpecialT() const { return 3.2f; }
  float yoyoEndT() const { return yoyoSpecialT() + (sc.mid == 0 ? 1.0f : sc.mid == 1 ? 2.4f : 3.0f) + 0.4f; }
  void yoyoPos(float& x, float& y, float& spin, bool& str) const {
    const float u = sc.u, S = yoyoSpecialT(), E = yoyoEndT();
    x = 288;
    spin = 0;
    str = true;
    auto thr = [&](float a0) { const float e = (u - a0) / 0.9f; return e > 0 && e < 1 ? sinf(e * PI_F) : 0.0f; };
    float f = fmaxf(thr(1.0f), thr(2.1f));
    if (sc.mid == 0) f = fmaxf(f, thr(S));
    else {
      const float hold = sc.mid == 1 ? 1.6f : 2.2f;
      if (u > S && u < S + hold + 0.7f) {
        const float d = u - S;
        f = d < 0.35f ? sinf(d / 0.35f * PI_F / 2) : d < 0.35f + hold ? 1 : cosf(clampf((d - 0.35f - hold) / 0.35f, 0, 1) * PI_F / 2);
        if (d > 0.3f && d < 0.4f + hold) spin = 1;
        if (sc.mid == 2) x = lerpf(288, 172, seg(S + 0.35f, S + 0.35f + hold * 0.5f) * (1 - seg(S + 0.35f + hold * 0.5f, S + 0.35f + hold)));
      }
    }
    bool loop = false;
    const bool tapped = sc.evId == 1 && sc.evU < E;
    if (u > E && !tapped) {
      const float d = u - E;
      if (sc.end == 0) { f = 0.42f * seg(E, E + 0.4f); x += 14 * sinf(d * 3.2f) * expf(-d * 0.5f) * seg(E + 0.3f, E + 0.6f); }
      if (sc.end == 1) {
        f = d < 0.4f ? 0.5f * d / 0.4f : 0.5f * (1 - seg(E + 1.9f, E + 2.3f));
        const float e = clampf((d - 0.4f) / 1.5f, 0, 1);
        if (d >= 0.4f && e < 1) {
          const float th = ease(e) * 2 * PI_F;
          x = 226 + 62 * cosf(th);
          y = 119 - 62 * sinf(th);
          loop = true;
          spin = 1;
        }
      }
      if (sc.end == 2) { f = seg(E, E + 0.45f); str = d < 0.45f; x = 288 - 400 * seg(E + 0.5f, E + 2.6f); spin = 1; }
    }
    if (tapped) { f *= 1 - seg(sc.evU, sc.evU + 0.25f); if (u > sc.evU) spin = 0; }
    if (!loop) y = 28 + 180 * f;
  }

  float fishBiteT() const { return bt(0, sc.mid == 1 ? 8.0f : 7.2f); }
  float fishReelT() const { return sc.evId == 1 && sc.evU < fishBiteT() ? sc.evU + 0.2f : fishBiteT() + 1.9f; }
  // the bobber (bx, by) and what's on the line (cx, cy) once it's reeled in: what 0 big fish, 1 boot,
  // 2 tiny fish (let go), 3 a fish that gets away, -1 nothing
  bool fishCatch(float& bx, float& by, float& cx, float& cy, int& what) const {
    const float u = sc.u, B = fishBiteT(), R = fishReelT();
    const bool early = sc.evId == 1 && sc.evU < B;
    what = early ? -1 : sc.end;
    const float tipx = 92, tipy = 150 + 8 * sinf(clampf((u - R + 0.4f) / 0.8f, 0, 1) * PI_F);
    const float wx = 206 + 4 * sinf(u * 0.6f), wy = waterY(wx, 210, 2.5f);
    const float ce = seg(0.6f, 1.4f);
    bx = lerpf(tipx, wx, ce);
    by = u < 1.4f ? lerpf(tipy, wy - 2, ce) - 50 * sinf(ce * PI_F) : wy - 2;
    if (!early) by += 4 * kick(B, 0.05f, 8) + 4 * kick(B + 0.7f, 0.05f, 8) + 14 * seg(B + 1.4f, B + 1.55f);
    cx = bx;
    cy = by;
    if (u < R) return false;
    const float e = seg(R, R + 0.7f);
    const float hx = tipx + 6 + 9 * sinf((u - R) * 4) * expf(-(u - R) * 0.6f), hy = tipy + 34;
    cx = lerpf(wx, hx, e);
    cy = lerpf(wy, hy, e) - 40 * sinf(e * PI_F);
    const float A = R + 0.7f;
    if (what == 2 && u > A + 2.0f) { const float r = seg(A + 2.0f, A + 2.5f); cx = lerpf(hx, 190, r); cy = lerpf(hy, wy + 6, r) - 30 * sinf(r * PI_F); }
    if (what == 3 && u > A + 0.9f) { const float r = seg(A + 0.9f, A + 1.2f); cx = lerpf(hx, 130, r); cy = lerpf(hy, wy + 6, r); }
    return true;
  }

  void duckPos(float& x, float& y, int& face, float& flip, float& dunk) const {
    const float u = sc.u, E = bt(2, 8.5f), s0 = bt(0, 4.2f);
    x = lerpf(350, 226, ease(u / 2.6f)) + 12 * sinf(u * 0.6f) * seg(2.6f, 3.6f);
    face = -1;
    flip = 0;
    dunk = 0;
    if (sc.mid == 1) dunk = seg(s0, s0 + 0.3f) * (1 - seg(s0 + 1.1f, s0 + 1.4f));
    if (sc.mid == 2 && u > s0 && u < s0 + 1.2f) face = fracf((u - s0) / 0.4f) < 0.5f ? 1 : -1;
    if (u > E) {
      if (sc.end == 0) x -= 340 * seg(E, E + 2.8f);
      if (sc.end == 1) flip = seg(E, E + 0.3f) * (1 - seg(E + 1.6f, E + 1.9f));
      if (sc.end == 2) x = lerpf(x, 168, seg(E, E + 1.5f));
    }
    y = waterY(x, 218, 2.5f) - 10;
    if (sc.mid == 0) y -= 10 * sinf(clampf((u - s0) / 0.35f, 0, 1) * PI_F);
    if (sc.evId == 1 || sc.evId == 3) y -= 22 * sinf(clampf((u - sc.evU) / 0.5f, 0, 1) * PI_F);
  }

  // the cookie jar: the effective ending, when the cookie arrives (E), when it's caught (Cg)
  void cookieTimes(int& end, float& E, float& Cg) const {
    const float L = bt(1, 4.6f);
    E = L + 2.4f;
    end = sc.end;
    Cg = E - 0.6f;
    if (sc.evId == 1 && sc.evU < E) { end = 1; Cg = sc.evU; }
    else if (sc.evId == 3 && sc.evU < E) end = 2;
    if (end != 1) Cg = 1e9f;
  }
  // the floating cookie; s = its size (it comes closer when shared). false = still in the jar / gone
  bool cookiePos(float& x, float& y, float& s) const {
    int end;
    float E, Cg;
    cookieTimes(end, E, Cg);
    const float u = sc.u, F = bt(1, 4.6f) + 0.6f;
    s = 1;
    x = 262;
    y = 206;
    if (u < F || Cg < F) return false;
    const float e1 = seg(F, F + 0.5f), e2 = seg(F + 0.5f, E);
    x = lerpf(262, 176, e2) + 4 * sinf(u * 5) * (1 - e2);
    y = lerpf(lerpf(206, 136, e1), sc.fy + 62, e2);
    if (end == 1 && u > Cg) {  // caught: back into the jar it goes
      const float r = seg(Cg + 0.1f, Cg + 0.6f);
      x = lerpf(x, 262, r);
      y = lerpf(y, 206, r);
      return r < 1;
    }
    if (end == 2 && u > E) { const float r = seg(E, E + 0.8f); x = lerpf(176, 160, r); y = lerpf(sc.fy + 62, 196, r); s = 1 + 1.3f * r; }
    if (end == 0 && u > E + 2.0f) return false;
    return true;
  }

  void topPos(float& x, float& y, float& a, float& spin) const {
    const float u = sc.u, E = bt(2, 8.5f);
    const float drop = seg(0.3f, 0.75f);
    y = lerpf(-60, 234, drop) - 12 * sinf(clampf((u - 0.75f) / 0.3f, 0, 1) * PI_F);
    x = 160 + 95 * sinf((u - 1) * 0.6f + sc.v[0] * 6) * seg(1, 3);
    spin = u > 1 ? (u - 1) * 22 : 0;
    float late = sc.end == 0 ? seg(E - 1.8f, E) : 0;
    a = (0.06f + 0.25f * late) * sinf(u * 7);
    if (sc.mid == 2) { const float e = clampf((u - bt(0, 4.5f)) / 0.6f, 0, 1); y -= 34 * sinf(e * PI_F); }
    if (sc.evId == 1) { const float e = clampf((u - sc.evU) / 0.5f, 0, 1); y -= 26 * sinf(e * PI_F); spin += 25 * seg(sc.evU, sc.evU + 1); }
    if (u > E) {
      if (sc.end == 0) {
        const float f = seg(E, E + 0.45f);
        a = lerpf(a, 1.4f, f);
        y -= 27 * f;
        x += 30 * seg(E, E + 1.2f);
        spin = (E - 1) * 22 + 6 * seg(E, E + 1.5f);
      }
      if (sc.end == 1) x += 220 * (u - E) * (u - E) + 40 * (u - E);
    }
  }

  float magicRevealT() const { return bt(0, 2.0f) + 1.3f + 0.9f; }

  void catPos(float& cx, float& cy) const {
    const float u = sc.u, E = bt(2, 10);
    cx = 228;
    cy = lerpf(300, 212, seg(0.6f, 2.4f));
    if (sc.evId == 1 || sc.evId == 2) { cy = lerpf(cy, 320, seg(sc.evU, sc.evU + 0.35f)); return; }
    if (sc.evId == 3) { cx += 8 * sinf((u - sc.evU) * 3) * seg(sc.evU, sc.evU + 0.5f); return; }
    if (u > E) {
      if (sc.end == 0) cy += 10 * seg(E, E + 1.2f);
      if (sc.end == 1) cy = lerpf(cy, 320, seg(E + 0.3f, E + 1.3f));
      if (sc.end == 2) cx += 9 * sinf((u - E) * 3) * seg(E, E + 0.5f);
    }
  }

  static const int DOM_N = 10;
  float domX(int i) const { return 52 + i * 24; }
  float domStart() const { return sc.evId == 1 && sc.evU < bt(0, 2.4f) ? sc.evU + 0.1f : bt(0, 2.4f); }
  float domFallT(int i) const {
    const float S = domStart();
    if (sc.evId == 1 && sc.evU < bt(0, 2.4f)) return S + i * 0.07f;
    float tt = S;
    for (int j = 0; j < i; j++) {
      tt += sc.mid == 1 ? fmaxf(0.1f, 0.3f - j * 0.025f) : 0.21f;
      if (sc.twist == 1 && j == 5) tt += 2.0f;  // the stall: it has to blow on it
    }
    return tt;
  }
  float domEndT() const { return domFallT(DOM_N - 1) + 0.25f; }
  float domAng(int i) const {
    const bool bell = sc.end == 0;
    const float d = sc.u - domFallT(i), mx = i == DOM_N - 1 ? (bell ? 0.9f : 1.5f) : 0.44f;
    float a = d <= 0 ? 0 : d < 0.22f ? mx * (d / 0.22f) * (d / 0.22f) : mx;
    if (sc.twist == 1 && i == 6 && d < 0) {  // leaned on, wobbling
      const float w = sc.u - domFallT(5) - 0.22f;
      if (w > 0) a = 0.07f * fabsf(sinf(w * 7));
    }
    const bool tapped = sc.evId == 1 && sc.evU < bt(0, 2.4f);
    if (sc.end == 2 && !tapped) {  // the rewind: they stand back up, last first
      const float R0 = domEndT() + 0.6f + (DOM_N - 1 - i) * 0.08f;
      a *= 1 - seg(R0, R0 + 0.15f);
    }
    return a;
  }

  float snowmanEndT() const { return bt(2, 10) + (sc.twist == 1 ? 1.4f : 0); }
  void snowHead(float& x, float& y) const {
    const float u = sc.u;
    x = 290;
    y = 172;
    if (sc.twist == 1) {
      const float h0 = bt(1, 8.0f), e = seg(h0, h0 + 0.8f), b = seg(h0 + 2.0f, h0 + 2.6f);
      if (u > h0 && u < h0 + 2.6f) {
        x = 290 - 64 * e;
        y = lerpf(172, 222, e * e);
        if (u > h0 + 2.0f) { x = lerpf(226, 290, b); y = lerpf(222, 172, b) - 40 * sinf(b * PI_F); }
      }
    }
  }

  void frogPos(float& x, float& y, float& sq) const {
    const float u = sc.u, E = bt(2, 8.6f), H0 = bt(0, 3.2f);
    auto hop = [&](float a0, float d, float x0, float x1, float h) {
      const float e = clampf((u - a0) / d, 0, 1);
      if (u >= a0) { x = lerpf(x0, x1, e); y = 216 - h * sinf(e * PI_F); sq = e < 1 ? 1.15f : 1; }
    };
    x = 360;
    y = 216;
    sq = 1;
    hop(0.4f, 0.6f, 360, 296, 34);
    hop(1.3f, 0.6f, 296, 236, 34);
    if (sc.mid == 0) for (int q = 0; q < 3; q++) { const float h = H0 + (q == 0 ? 0 : q == 1 ? 1.2f : 2.1f); y -= 10 * sinf(clampf((u - h) / 0.3f, 0, 1) * PI_F); }
    if (sc.evId == 1) { hop(sc.evU, 0.9f, x, 360, 90); return; }
    if (sc.evId == 3) return;
    if (u > E) {
      if (sc.end == 0) {
        const float e = clampf((u - E) / 0.8f, 0, 1);
        x = lerpf(236, 160, e);
        y = lerpf(216, 30, e) - 60 * sinf(e * PI_F);
        sq = e < 1 ? 1.15f : 1;
      }
      if (sc.end == 1) { hop(E, 0.6f, 236, 300, 34); hop(E + 0.8f, 0.6f, 300, 370, 34); }
      if (sc.end == 2) { const float e = clampf((u - E - 0.15f) / 0.9f, 0, 1); y = 216 - 80 * sinf(e * PI_F); sq = e > 0 && e < 1 ? 1.2f : 1; }
    }
  }

  float moonEndT() const { return sc.mid == 2 ? 2.6f + tuneLen(6) + 0.4f : bt(2, 9); }
  void moonPos(float& x, float& y) const {
    const float u = sc.u, E = moonEndT();
    x = 62 + 6 * sinf(u * 0.3f);
    y = lerpf(-50, 39, seg(0.2f, 2.4f));
    if (sc.end == 1 && u > E && sc.evId == 0) { const float e = seg(E, E + 3); x += 320 * e; y -= 40 * e + 12 * sinf(e * PI_F); }
  }

  // ---- doodles: strokes in a -1..1 box; brk = a new stroke starts at this point ----
  int doodlePts(int which, float* xs, float* ys, bool* brk) const {
    int n = 0;
    auto add = [&](float x, float y, bool b) { if (n < 64) { xs[n] = x; ys[n] = y; brk[n] = b; n++; } };
    auto circ = [&](float cx, float cy, float r, int m) { for (int i = 0; i <= m; i++) { const float a = -PI_F / 2 + i * 2 * PI_F / m; add(cx + cosf(a) * r, cy + sinf(a) * r, i == 0); } };
    switch (which) {
      case 0:  // a heart
        for (int i = 0; i <= 20; i++) {
          const float th = i * 2 * PI_F / 20, s = sinf(th);
          add(0.95f * s * s * s, -(13 * cosf(th) - 5 * cosf(2 * th) - 2 * cosf(3 * th) - cosf(4 * th)) / 17.0f - 0.08f, i == 0);
        }
        break;
      case 1:  // the sun
        circ(0, 0, 0.42f, 14);
        for (int r = 0; r < 8; r++) { const float a = r * PI_F / 4; add(cosf(a) * 0.6f, sinf(a) * 0.6f, true); add(cosf(a) * 0.92f, sinf(a) * 0.92f, false); }
        add(-0.18f, -0.08f, true); add(-0.18f, 0.0f, false); add(0.18f, -0.08f, true); add(0.18f, 0.0f, false);
        add(-0.18f, 0.16f, true); add(0, 0.26f, false); add(0.18f, 0.16f, false);
        break;
      case 2:  // a cat
        circ(0, 0.12f, 0.6f, 16);
        add(-0.52f, -0.18f, true); add(-0.46f, -0.86f, false); add(-0.14f, -0.46f, false);
        add(0.14f, -0.46f, true); add(0.46f, -0.86f, false); add(0.52f, -0.18f, false);
        add(-0.24f, 0.02f, true); add(-0.24f, 0.14f, false); add(0.24f, 0.02f, true); add(0.24f, 0.14f, false);
        add(-0.12f, 0.34f, true); add(0, 0.42f, false); add(0.12f, 0.34f, false);
        add(-0.38f, 0.3f, true); add(-0.95f, 0.22f, false); add(-0.38f, 0.4f, true); add(-0.92f, 0.46f, false);
        add(0.38f, 0.3f, true); add(0.95f, 0.22f, false); add(0.38f, 0.4f, true); add(0.92f, 0.46f, false);
        break;
      case 3:  // a house
        add(-0.6f, -0.05f, true); add(-0.6f, 0.85f, false); add(0.6f, 0.85f, false); add(0.6f, -0.05f, false);
        add(-0.78f, 0.02f, true); add(0, -0.78f, false); add(0.78f, 0.02f, false);
        add(-0.15f, 0.85f, true); add(-0.15f, 0.4f, false); add(0.15f, 0.4f, false); add(0.15f, 0.85f, false);
        add(0.26f, 0.12f, true); add(0.46f, 0.12f, false); add(0.46f, 0.3f, false); add(0.26f, 0.3f, false); add(0.26f, 0.12f, false);
        add(0.36f, -0.42f, true); add(0.36f, -0.7f, false); add(0.52f, -0.7f, false); add(0.52f, -0.26f, false);
        break;
      default:  // itself: two big eyes and a smile
        for (int sd = -1; sd <= 1; sd += 2) {
          const float cx = sd * 0.42f;
          add(cx - 0.2f, -0.55f, true); add(cx + 0.2f, -0.55f, false); add(cx + 0.28f, -0.4f, false); add(cx + 0.28f, 0.0f, false);
          add(cx + 0.2f, 0.15f, false); add(cx - 0.2f, 0.15f, false); add(cx - 0.28f, 0.0f, false); add(cx - 0.28f, -0.4f, false); add(cx - 0.2f, -0.55f, false);
        }
        for (int i = 0; i <= 6; i++) { const float a = PI_F * (0.15f + 0.7f * i / 6); add(cosf(a) * 0.36f, 0.3f + sinf(a) * 0.3f, i == 0); }
        break;
    }
    return n;
  }
  float doodleMistakeT() const { return 1.4f + 0.45f * (bt(0, 7) - 1.4f); }
  // how much is drawn (0..1), with the pause for the mistake
  float doodleProg() const {
    const float D0 = 1.4f, D1 = bt(0, 7);
    float te = sc.u;
    if (sc.twist == 1) { const float M = doodleMistakeT(); te = te < M ? te : te < M + 2.0f ? M : te - 2.0f; }
    return clampf((te - D0) / (D1 - D0), 0, 1);
  }
  float doodleEndT() const { return bt(0, 7) + (sc.twist == 1 ? 2.0f : 0) + 0.2f; }
  float doodleBox(float& cx, float& cy) const {  // the pad's centre (it rises in) and the doodle's scale
    const float in = seg(0.1f, 0.9f);
    cx = 160;
    cy = 207 + (1 - in) * 90;
    if (sc.end == 0 && sc.u > doodleEndT()) cy -= 14 * seg(doodleEndT(), doodleEndT() + 0.5f);
    return 1;
  }
  // the pencil tip; true while it's drawing
  bool doodlePen(float& px, float& py) const {
    float cx, cy;
    doodleBox(cx, cy);
    float xs[64], ys[64];
    bool brk[64];
    const int n = doodlePts(sc.mid, xs, ys, brk);
    const float p = doodleProg(), c = p * (n - 1);
    const int i0 = (int)c;
    const float f = c - i0;
    const int i1 = i0 + 1 < n ? i0 + 1 : n - 1;
    const float lx = brk[i1] ? xs[i0] : lerpf(xs[i0], xs[i1], f), ly = brk[i1] ? ys[i0] : lerpf(ys[i0], ys[i1], f);
    px = cx + lx * 30;
    py = cy + ly * 26;
    const bool on = p > 0 && p < 1;
    if (sc.twist == 1) {
      const float M = doodleMistakeT();
      if (sc.u > M && sc.u < M + 2.0f) { px = cx + 50 + 8 * sinf((sc.u - M) * 18); py = cy - 4 + 6 * sinf((sc.u - M) * 7); return true; }
    }
    return on;
  }

  void boatPos(float& x, float& y, float& a, float& sink) const {
    const float u = sc.u, E = bt(2, 8.5f), amp = sc.mid == 2 ? 5.5f : 2.5f;
    x = lerpf(-40, 104, seg(0.3f, 3.0f)) + fmaxf(0, u - 3) * 5;
    if (sc.mid == 1) x += 60 * seg(bt(0, 4.0f), bt(0, 4.0f) + 1.6f);
    sink = 0;
    if (u > E) {
      if (sc.end == 0) x += 270 * seg(E, E + 3.4f);
      if (sc.end == 2) x = lerpf(x, 160, seg(E, E + 1.6f));
      if (sc.end == 1) sink = seg(E, E + 2.6f);
    }
    y = waterY(x, 218, amp) - 3 + 34 * sink;
    a = atanf((waterY(x + 10, 218, amp) - waterY(x - 10, 218, amp)) / 20) + 0.4f * sink;
    if (sc.evId == 1) { const float e = clampf((u - sc.evU) / 0.55f, 0, 1); y -= 26 * sinf(e * PI_F); a += 0.25f * sinf(e * 2 * PI_F); }
  }

  float plantBloomT() const { return bt(2, 9.2f) + (sc.mid == 2 ? 1.4f : 0); }
  float plantGrow() const {  // 0.15 a sprout .. 1 grown (the tall one: up to 2.6)
    const float W1 = bt(0, 1.6f), E = plantBloomT();
    float g = 0.15f + 0.85f * seg(W1 + 1.2f, E - 0.4f);
    if (sc.mid == 2) g = 0.15f + 0.35f * seg(W1 + 1.2f, W1 + 2.6f) + 0.5f * seg(W1 + 4.6f, E - 0.4f);
    if (sc.end == 2) g += 1.6f * seg(E - 1.0f, E + 0.2f);
    return g;
  }
  float plantDroop() const { const float E = plantBloomT(); return sc.end == 1 ? seg(E - 2.0f, E - 1.4f) * (1 - seg(E - 0.3f, E + 0.2f)) : 0; }
  void plantTop(float& x, float& y) const {
    const float g = plantGrow(), hs = 14 + 50 * fminf(g, 1) + 56 * fmaxf(0, g - 1), dr = plantDroop();
    x = 236 + 3 * sinf(sc.u * 1.1f) + 20 * dr;
    y = 204 - hs + 16 * dr;
  }

  float rocketLaunchT() const {
    const float L = sc.mid == 2 ? 5.0f : 3.4f;
    return sc.evId == 1 && sc.evU < L ? fmaxf(sc.evU + 0.3f, 0.6f) : L;
  }
  // the rocket's base centre, its tilt, the flame's length (0 = off)
  void rocketPos(float& x, float& y, float& a, float& flame) const {
    const float u = sc.u, L = rocketLaunchT();
    x = 268;
    y = 228;
    a = 0;
    flame = 0;
    if (sc.mid == 2 && u > 3.4f && u < 3.9f && L > 4) flame = 8 * fabsf(sinf(u * 40)) * (1 - seg(3.6f, 3.9f));  // a sputter
    if (u > L - 0.4f && u < L + 0.2f) x += 1.5f * sinf(u * 70);
    if (u < L) return;
    const float d = u - L;
    flame = 22 + 6 * sinf(u * 45);
    if (sc.end == 1) {
      if (d < 0.6f) y -= 34 * ease(d / 0.6f);
      else if (d < 1.0f) { y -= 34; flame = 10 * fabsf(sinf(u * 30)) * (1 - seg(L + 0.6f, L + 1.0f)); }
      else {
        flame = 0;
        const float f = seg(L + 1.0f, L + 1.3f);
        y = lerpf(194, 216, f);
        a = -1.5f * f;
        x -= 20 * f;
      }
      return;
    }
    y -= 160 * d * d;
    if (sc.end == 2 && d > 2.6f) {  // back down, tail first, and a soft landing
      const float e = seg(L + 2.6f, L + 4.8f);
      y = 228 - 320 * (1 - e) * (1 - e);
      flame = e < 1 ? 16 + 5 * sinf(u * 45) : 0;
    }
  }

  float iceLickT(int q) const { return bt(5, 2.2f) + q * 0.9f; }
  int iceScoops() const { return 2 + sc.iv[2] % 2; }

  int gumEnd() const { return (sc.evId == 1 || sc.evId == 2) && sc.evU < gumPopT0() ? 0 : sc.end; }
  float gumPopT0() const { return bt(0, 3.5f); }
  float gumPopT() const { return (sc.evId == 1 || sc.evId == 2) && sc.evU < gumPopT0() ? sc.evU : gumPopT0(); }
  float gumR() const {
    const int end = gumEnd();
    const float R = end == 0 ? 46 : end == 1 ? 18 : 34, u = sc.u;
    const float g = 0.35f * seg(0.6f, 1.2f) + 0.35f * seg(1.6f, 2.2f) + 0.3f * seg(2.6f, 3.2f);
    if (end != 2 && u >= gumPopT()) return 0;
    return 4 + R * g;
  }

  void paperPos(float& x, float& y) const {
    const float u = sc.u, T0 = bt(0, 2.0f);
    x = 150;
    y = lerpf(252, 214, seg(0.3f, 0.9f)) + 2 * sinf(u * 6) * seg(0.9f, 1.2f);
    if (u < T0) return;
    const float tx = sc.end == 0 ? 284 : sc.end == 1 ? 256 : 262, ty = sc.end == 0 ? 176 : 168;
    const float e = clampf((u - T0) / 0.9f, 0, 1);
    x = lerpf(150, tx, e);
    y = lerpf(214, ty, e) - 175 * sinf(e * PI_F);
    const float H = T0 + 0.9f;
    if (u < H) return;
    if (sc.end == 0) { y = lerpf(176, 222, seg(H, H + 0.25f)); }
    if (sc.end == 1) { const float r = clampf((u - H) / 0.7f, 0, 1); x = lerpf(256, 196, r); y = lerpf(168, 262, r) - 50 * sinf(r * PI_F * 0.6f); }
    if (sc.end == 2) {
      const float r = clampf((u - H) / 0.9f, 0, 1);
      x = lerpf(262, 302, r);
      y = 168 - 14 * fabsf(sinf(r * 2 * PI_F));
      if (u > H + 0.9f) { x = lerpf(302, 284, seg(H + 0.9f, H + 1.1f)); y = lerpf(168, 222, seg(H + 0.9f, H + 1.15f)); }
    }
  }

  // drawing (back = behind the eyes)
  void drawCatFace(float cx, float cy, const RGB& fur, float blink, bool happy, int look) {
    Canvas& cv = *k.cv;
    const bool light = lightBg();
    const RGB dark = mulc(fur, 0.62f), inner = C(255, 160, 180);
    const float lum = (fur.r + fur.g + fur.b) / 765.0f;
    if (lum < 0.35f && !light) {  // a black cat on a black face: a soft rim so it reads
      const uint16_t rim = cc(110, 110, 125);
      cv.tri(cx - 31, cy - 8, cx - 24, cy - 49, cx - 2, cy - 29, rim);
      cv.tri(cx + 31, cy - 8, cx + 24, cy - 49, cx + 2, cy - 29, rim);
      cv.circle(cx, cy, 34, rim);
    }
    cv.tri(cx - 29, cy - 10, cx - 23, cy - 46, cx - 4, cy - 29, to565(fur));
    cv.tri(cx + 29, cy - 10, cx + 23, cy - 46, cx + 4, cy - 29, to565(fur));
    cv.tri(cx - 24, cy - 16, cx - 21, cy - 38, cx - 10, cy - 28, to565(inner));
    cv.tri(cx + 24, cy - 16, cx + 21, cy - 38, cx + 10, cy - 28, to565(inner));
    cv.circle(cx, cy, 32, to565(fur));
    cv.ellipse(cx, cy + 12, 15, 10, to565(mixc(fur, C(255, 255, 255), 0.45f)));
    const RGB eyeC = (sc.iv[1] & 4) ? C(250, 200, 70) : C(150, 220, 90);
    for (int sd = -1; sd <= 1; sd += 2) {
      const float ex = cx + sd * 13, ey = cy - 4;
      if (happy) {
        cv.lineFast(ex - 6, ey + 2, ex, ey - 3, 1.4f, to565(dark));
        cv.lineFast(ex, ey - 3, ex + 6, ey + 2, 1.4f, to565(dark));
        continue;
      }
      cv.ellipse(ex, ey, 7, 8, to565(eyeC));
      cv.ellipse(ex + look * 1.8f, ey, 2.2f, 6.2f, cc(25, 25, 30));
      cv.circleFast(ex - 2, ey - 3, 1.5f, cc(255, 255, 255));
      if (blink > 0.02f) {
        cv.rrect(ex - 8, ey - 9, 16, 17 * blink + 0.5f, 3, to565(fur));
        if (blink > 0.9f) cv.lineFast(ex - 6, ey + 6, ex + 6, ey + 6, 1.2f, to565(dark));
      }
    }
    cv.tri(cx - 3.5f, cy + 6, cx + 3.5f, cy + 6, cx, cy + 10, to565(inner));
    for (int sd = -1; sd <= 1; sd += 2) {  // a little "w" of a mouth
      cv.lineFast(cx, cy + 10, cx + sd * 3.2f, cy + 13.5f, 1.0f, to565(dark));
      cv.lineFast(cx + sd * 3.2f, cy + 13.5f, cx + sd * 6.8f, cy + 11, 1.0f, to565(dark));
    }
    const uint16_t wc = to565(light ? mulc(fur, 0.5f) : C(225, 225, 235));
    for (int sd = -1; sd <= 1; sd += 2)
      for (int q = 0; q < 3; q++) cv.lineFast(cx + sd * 12, cy + 9 + q * 2, cx + sd * 36, cy + 4 + q * 6, 0.5f, wc);
  }
  void drawDuck(float x, float y, int f, float flip, float dunk, float s) {
    Canvas& cv = *k.cv;
    const RGB yel = C(255, 212, 55), org = C(255, 140, 40);
    if (flip > 0.5f) {  // upside down: the belly and two kicking feet
      oval(x, y + 2 * s, 17 * s, 10 * s, yel);
      for (int sd = -1; sd <= 1; sd += 2) cv.ellipse(x + sd * 6 * s, y - 10 * s + 2 * sinf(t * 14 + sd), 3.5f * s, 5 * s, to565(org));
      return;
    }
    const float hy = y - 14 * s + dunk * 24 * s, hx = x + 9 * f * s + dunk * 6 * f * s;
    cv.tri(x - 13 * f * s, y - 2 * s, x - 23 * f * s, y - 13 * s, x - 9 * f * s, y - 9 * s, to565(yel));
    oval(x, y, 17 * s, 11 * s, yel);
    cv.ellipse(x - 3 * f * s, y - 1 * s, 9 * s, 5.5f * s, to565(mulc(yel, 0.85f)));
    cv.circle(hx, hy, 9 * s, to565(yel));
    cv.ellipse(hx + 9 * f * s, hy + 2 * s, 6 * s, 3 * s, to565(org));
    cv.circleFast(hx + 3 * f * s, hy - 2 * s, 1.8f * s, cc(30, 30, 35));
    cv.circleFast(hx + 3.4f * f * s, hy - 2.6f * s, 0.6f * s, cc(255, 255, 255));
  }
  void drawSnowman(float cx, float ground, float melt, bool hat, bool alive, float hx, float hy, float popT[5]) {
    Canvas& cv = *k.cv;
    const bool light = lightBg();
    const RGB snow = light ? C(240, 245, 255) : C(248, 250, 255), edge = light ? C(160, 175, 210) : C(170, 185, 215);
    const float u = sc.u;
    auto pop = [&](float t0) { const float p = clampf((u - t0) / 0.25f, 0, 1); return p <= 0 ? 0.0f : p * (1 + 0.3f * sinf(p * PI_F)); };
    // the balls (melting: they squash down and spread)
    const float sy = 1 - 0.85f * melt, sx = 1 + 0.4f * melt;
    const float r1 = 18, r2 = 13, r3 = 9.5f;
    // the bottom ball rolls in
    const float e1 = seg(0.4f, 2.2f);
    const float b1x = lerpf(350, cx, e1), b1r = lerpf(8, r1, e1);
    const float b1y = ground - b1r * sy;
    cv.ellipse(b1x, b1y, b1r * sx + 1.3f, b1r * sy + 1.3f, to565(edge));
    if (props3d && melt < 0.05f) ball(b1x, b1y, b1r, snow); else cv.ellipse(b1x, b1y, b1r * sx, b1r * sy, to565(snow));
    if (e1 < 1) cv.circleFast(b1x + cosf(-u * 9) * b1r * 0.6f, b1y + sinf(-u * 9) * b1r * 0.6f, 1.6f, to565(edge));  // rolling
    const float m2 = seg(2.4f, 3.0f), m3 = seg(3.3f, 3.9f);
    const float b2y = lerpf(-30, ground - 2 * r1 * sy - r2 * sy + 7 * sy, m2);
    if (m2 > 0) {
      cv.ellipse(cx, b2y, r2 * sx + 1.3f, r2 * sy + 1.3f, to565(edge));
      if (props3d && melt < 0.05f) ball(cx, b2y, r2, snow); else cv.ellipse(cx, b2y, r2 * sx, r2 * sy, to565(snow));
      // buttons
      const float bp = pop(popT[2]);
      for (int q = 0; q < 2; q++) if (bp > 0) cv.circleFast(cx, b2y - 4 + q * 8 * sy, 1.9f * bp, cc(40, 40, 50));
    }
    float headX = hx, headY = lerpf(-30, hy, m3);
    if (melt > 0) { headX = cx; headY = b2y - r2 * sy - r3 * sy + 4 * sy; }
    if (m3 > 0) {
      cv.ellipse(headX, headY, r3 * sx + 1.3f, r3 * sy + 1.3f, to565(edge));
      if (props3d && melt < 0.05f) ball(headX, headY, r3, snow); else cv.ellipse(headX, headY, r3 * sx, r3 * sy, to565(snow));
      const float ep = pop(popT[0]), np = pop(popT[1]);
      const bool blink = alive && fracf(u * 0.5f) < 0.06f;
      if (ep > 0) for (int sd = -1; sd <= 1; sd += 2) {
        if (blink) cv.lineFast(headX + sd * 4 - 1.5f, headY - 3, headX + sd * 4 + 1.5f, headY - 3, 0.7f, cc(40, 40, 50));
        else cv.circleFast(headX + sd * 4, headY - 3, 1.7f * ep, cc(40, 40, 50));
      }
      if (np > 0) cv.tri(headX - 1, headY - 0.5f, headX - 1, headY + 2.5f, headX - 1 - 10 * np, headY + 1.2f, cc(255, 140, 40));
      if (np > 0) cv.lineFast(headX - 3, headY + 5, headX + 3, headY + 5, 0.6f, cc(40, 40, 50));
      if (hat && pop(popT[4]) > 0) {
        const float hp = pop(popT[4]);
        const uint16_t hc = to565(light ? C(55, 50, 70) : C(85, 80, 110));
        cv.rrect(headX - 10 * hp, headY - r3 * sy - 2, 20 * hp, 3, 1.5f, hc);
        cv.rrect(headX - 6.5f * hp, headY - r3 * sy - 14 * hp, 13 * hp, 13 * hp, 2, hc);
        cv.rrect(headX - 6.5f * hp, headY - r3 * sy - 5 * hp, 13 * hp, 2.5f, 0, cc(230, 70, 80));
      }
    }
    // arms (sticks), the scarf
    const float ap = pop(popT[3]);
    if (ap > 0 && m2 > 0) {
      const uint16_t st = cc(140, 90, 50);
      const float wave = alive ? 0.5f * sinf(u * 8) : 0, droop = 14 * melt;
      const float ax = cx - r2 * sx + 2, ay = b2y - 2;
      cv.lineFast(ax, ay, ax - 18 * ap, ay - 10 * ap + droop, 1.4f, st);
      cv.lineFast(ax - 12 * ap, ay - 6.5f * ap + droop * 0.7f, ax - 16 * ap, ay - 14 * ap + droop, 1.1f, st);
      const float bx = cx + r2 * sx - 2, ex = bx + 18 * ap * cosf(-0.5f - wave), ey = ay + 18 * ap * sinf(-0.5f - wave) + droop;
      cv.lineFast(bx, ay, ex, ey, 1.4f, st);
      cv.lineFast(lerpf(bx, ex, 0.65f), lerpf(ay, ey, 0.65f), ex + 2, ey - 6, 1.1f, st);
    }
    const float sp = pop(popT[4] - 0.6f);
    if (sp > 0 && m3 > 0 && melt < 0.6f) {
      const uint16_t scf = cc(230, 70, 80), scd = cc(180, 50, 60);
      const float ny = b2y - r2 * sy + 2;
      cv.rrect(cx - 10 * sp, ny - 3, 20 * sp, 6, 3, scf);
      cv.rrect(cx + 3, ny, 5, 12 * sp, 2, scd);
    }
  }
  void drawRocket(float x, float y, float a, float flame) {
    Canvas& cv = *k.cv;
    const float ca = cosf(a), sa = sinf(a);
    auto P = [&](float px, float py, float& ox, float& oy) { ox = x + px * ca - py * sa; oy = y + px * sa + py * ca; };
    auto T = [&](float ax, float ay, float bx, float by, float qx, float qy, uint16_t c) {
      float x0, y0, x1, y1, x2, y2;
      P(ax, ay, x0, y0); P(bx, by, x1, y1); P(qx, qy, x2, y2);
      cv.tri(x0, y0, x1, y1, x2, y2, c);
    };
    if (flame > 0.5f) {
      T(-7, 0, 7, 0, 0, flame, cc(255, 120, 40));
      T(-4, 0, 4, 0, 0, flame * 0.6f, cc(255, 230, 120));
    }
    const uint16_t red = cc(235, 70, 80), body = cc(240, 240, 250), shade = cc(200, 205, 220);
    T(-11, -18, -20, 4, -11, -1, red);
    T(11, -18, 20, 4, 11, -1, red);
    T(-11, -50, 11, -50, 11, 0, body);
    T(-11, -50, 11, 0, -11, 0, body);
    T(5, -50, 11, -50, 11, 0, shade);
    T(5, -50, 11, 0, 5, 0, shade);
    T(-11, -50, 11, -50, 0, -70, red);
    T(-11, -4, 11, -4, 0, 2, cc(120, 125, 140));
    float wx, wy;
    P(0, -32, wx, wy);
    cv.circle(wx, wy, 6.5f, cc(120, 125, 140));
    cv.circle(wx, wy, 5, cc(110, 190, 255));
    cv.circleFast(wx - 1.6f, wy - 1.6f, 1.6f, cc(230, 245, 255));
  }

  void drawScene2(bool back) {
    Canvas& cv = *k.cv;
    const float u = sc.u;
    const bool light = lightBg();
    switch (sc.id) {
      case SC_KITE: {
        const float E = bt(2, 10);
        if (back == (sc.end == 1 && u > E + 0.5f)) break;  // (in the sky behind the face; stuck on its head: in front)
        float x, y, a;
        kitePos(x, y, a);

        const bool snapped = sc.end == 0 && u > E;
        static const RGB kc[3][2] = {{C(255, 80, 90), C(255, 210, 70)}, {C(70, 150, 255), C(245, 245, 250)}, {C(170, 100, 255), C(255, 140, 200)}};
        const RGB c1 = kc[sc.iv[1] % 3][0], c2 = kc[sc.iv[1] % 3][1];
        const float ca = cosf(a), sa = sinf(a);
        auto P = [&](float px, float py, float& ox, float& oy) { px *= 1.35f; py *= 1.35f; ox = x + px * ca - py * sa; oy = y + px * sa + py * ca; };
        float tx, ty, lx, ly, rx2, ry2, bx, by, mx, my;
        P(0, -20, tx, ty); P(-16, -4, lx, ly); P(16, -4, rx2, ry2); P(0, 26, bx, by); P(0, -4, mx, my);
        const uint16_t strC = to565(mixc(lidCol(), inkish(), 0.6f));
        if (!snapped) curve(318, 246, lerpf(318, bx, 0.55f), lerpf(246, by, 0.45f) + 30, bx, by, 0.8f, strC, 8, false);
        else curve(bx, by, bx + 10, by + 22, bx - 4, by + 40, 0.8f, strC, 5, false);
        float px0 = bx, py0 = by;
        for (int i = 1; i <= 6; i++) {  // the tail: bows on a wavy string
          const float qx = bx + 6 * sinf(t * 5 + i * 0.9f) * i / 3.0f - i * 2.5f, qy = by + i * 10.0f;
          cv.lineFast(px0, py0, qx, qy, 0.8f, strC);
          if (i % 2 == 0) {
            const uint16_t bc = to565(i % 4 ? c1 : c2);
            cv.tri(qx, qy, qx - 7.5f, qy - 4.5f, qx - 7.5f, qy + 4.5f, bc);
            cv.tri(qx, qy, qx + 7.5f, qy - 4.5f, qx + 7.5f, qy + 4.5f, bc);
          }
          px0 = qx;
          py0 = qy;
        }
        cv.tri(tx, ty, lx, ly, mx, my, to565(c1));
        cv.tri(tx, ty, rx2, ry2, mx, my, to565(c2));
        cv.tri(lx, ly, bx, by, mx, my, to565(c2));
        cv.tri(rx2, ry2, bx, by, mx, my, to565(c1));
        const uint16_t stick = to565(mulc(c1, 0.55f));
        cv.lineFast(tx, ty, bx, by, 1.1f, stick);
        cv.lineFast(lx, ly, rx2, ry2, 1.1f, stick);
        if (sc.twist == 1) {  // Crumb hitches a ride
          const float c0 = bt(1, 7);
          if (u > c0 - 0.8f && u < c0 + 3.0f) {
            const float in = seg(c0 - 0.8f, c0), out = seg(c0 + 2.2f, c0 + 3.0f);
            const float cx = lerpf(lerpf(-20, tx, in), 340, out), cy = lerpf(lerpf(10, ty - 9, in), -20, out);
            drawCrumb(cx, cy, (in < 1 || out > 0) ? fabsf(sinf(t * 18)) : 0, out > 0 ? 1 : -1, false, 0.8f);
          }
        }
        break;
      }
      case SC_YOYO: {
        if (back) break;
        float x, y, spin;
        bool str;
        yoyoPos(x, y, spin, str);
        if (x < -20) break;
        static const RGB yc[4] = {C(255, 80, 90), C(70, 160, 255), C(90, 210, 120), C(255, 170, 50)};
        const RGB c = yc[sc.iv[1] % 4];
        const uint16_t strC = to565(mixc(lidCol(), inkish(), 0.7f));
        const float E = yoyoEndT();
        if (str) {
          cv.lineFast(288, -2, x, y - 13, 0.8f, strC);
          if (sc.end == 0 && u > E + 0.2f && !(sc.evId == 1 && sc.evU < E)) {  // the knot
            const float kx = lerpf(288, x, 0.55f), ky = lerpf(-2, y, 0.55f);
            ring(kx, ky, 4, 0.9f, strC);
            cv.lineFast(kx - 5, ky + 2, kx + 5, ky - 3, 0.8f, strC);
          }
        }
        if (props3d) ball(x, y, 16, c); else cv.circle(x, y, 16, to565(c));
        cv.circle(x, y, 11, to565(mulc(c, 0.72f)));
        const float ang = t * (spin > 0.5f ? 30 : 6);
        cv.lineFast(x - cosf(ang) * 10, y - sinf(ang) * 10, x + cosf(ang) * 10, y + sinf(ang) * 10, 1.6f, to565(mixc(c, C(255, 255, 255), 0.5f)));
        cv.circleFast(x, y, 3.6f, to565(mixc(c, C(255, 255, 255), 0.7f)));
        if (spin > 0.5f) for (int q = 0; q < 2; q++) {  // whirr lines
          const float a0 = ang * 0.7f + q * PI_F;
          cv.lineFast(x + cosf(a0) * 21, y + sinf(a0) * 21, x + cosf(a0 + 0.6f) * 21, y + sinf(a0 + 0.6f) * 21, 0.9f, to565(mixc(lidCol(), c, 0.6f)));
        }
        break;
      }
      case SC_FISHING: {
        if (back) break;
        const float up = seg(0, 1.0f) * (1 - seg(sc.dur - 1.0f, sc.dur - 0.2f));
        if (up < 0.01f) break;
        drawWater(210, 2.5f, up);
        float bx, by, cx, cy;
        int what;
        const bool caught = fishCatch(bx, by, cx, cy, what);
        const float B = fishBiteT(), R = fishReelT(), A = R + 0.7f;
        const RGB wc = mixc(waterCol(), C(255, 255, 255), 0.6f);
        const float wy = waterY(bx, 210, 2.5f);
        ripple(bx, wy + 1, (u - 1.4f) / 1.2f, 30, wc);
        if (!(sc.evId == 1 && sc.evU < B)) {
          ripple(bx, wy + 1, (u - B) / 0.8f, 16, wc);
          ripple(bx, wy + 1, (u - B - 0.7f) / 0.8f, 16, wc);
          ripple(bx, wy + 1, (u - B - 1.4f) / 1.0f, 26, wc);
        }
        if (weatherNow == 3 || weatherNow == 4)
          for (int i = 0; i < 4; i++) { const float ph = fracf(u * 0.7f + i * 0.27f); ripple(20 + fracf(i * 0.618f + floorf(u * 0.7f + i * 0.27f) * 0.37f) * 280, 222 + i * 4, ph, 14, wc); }
        if (what == 2 && u > A + 2.5f) ripple(190, 216, (u - A - 2.5f) / 1.0f, 26, wc);
        if (what == 3 && u > A + 1.2f) ripple(130, 216, (u - A - 1.2f) / 1.0f, 26, wc);
        // the rod (it slides in from the corner, and everything on the line with it)
        const float sl = (1 - up) * 80;
        const float tipx = 92 - sl, tipy = 150 + 8 * sinf(clampf((u - R + 0.4f) / 0.8f, 0, 1) * PI_F) + sl;
        bx -= sl; by += sl; cx -= sl; cy += sl;
        const uint16_t rodC = cc(150, 95, 55);
        cv.lineFast(14 - sl, 248 + sl, tipx, tipy, 2.2f, rodC);
        cv.lineFast(14 - sl, 248 + sl, 34 - sl, 222 + sl, 3.6f, cc(90, 60, 40));
        cv.circleFast(36 - sl, 228 + sl, 5.5f, cc(170, 175, 190));
        const uint16_t lineC = to565(mixc(lidCol(), light ? C(90, 90, 110) : C(220, 220, 235), 0.7f));
        if (!caught) {
          if (u < 0.6f) { bx = tipx + 2; by = tipy + 18; }  // (it hangs from the tip until the cast)
          cv.lineFast(tipx, tipy, bx, by - 7, 0.6f, lineC);
          if (by < wy + 6 + sl) {  // the bobber
            cv.lineFast(bx, by - 7, bx, by - 12, 0.9f, cc(60, 60, 70));
            cv.circle(bx, by, 7, cc(235, 60, 70));
            cv.ellipse(bx, by + 3, 6.6f, 4, cc(250, 250, 250));
          }
          break;
        }
        // what's on the line
        const bool gone = (what == 2 && u > A + 2.5f) || (what == 3 && u > A + 1.2f);
        const bool hanging = !((what == 2 && u > A + 2.0f) || (what == 3 && u > A + 0.9f));
        cv.lineFast(tipx, tipy, hanging ? cx : tipx + 6, hanging ? cy : tipy + 34, 0.6f, lineC);
        if (!hanging || what < 0) {  // an empty hook
          const float hx = hanging ? cx : tipx + 6, hy = hanging ? cy : tipy + 34;
          cv.lineFast(hx, hy, hx, hy + 7, 0.9f, cc(170, 175, 190));
          cv.lineFast(hx, hy + 7, hx + 4, hy + 3, 0.9f, cc(170, 175, 190));
        }
        if (what < 0 || gone) break;
        const float wig = sinf(t * 16) * (what == 1 ? 0 : 1);
        if (what == 1) {  // a boot, dripping
          cv.rrect(cx - 8, cy, 16, 28, 4, cc(120, 80, 50));
          cv.rrect(cx - 8, cy + 17, 27, 13, 5, cc(120, 80, 50));
          cv.rrect(cx - 8, cy + 27, 27, 4, 2, cc(70, 45, 30));
          cv.rrect(cx - 9, cy - 1, 18, 4, 2, cc(150, 105, 70));
          for (int q = 0; q < 2; q++) { const float d = fracf(u * 1.2f + q * 0.5f); drop(cx + 2 + q * 10, cy + 33 + d * 20, 1.8f, fadeTo(waterCol(), 1 - d)); }
        } else {
          const float s = what == 2 ? 0.75f : 1.35f;
          const RGB fc = what == 2 ? C(120, 200, 255) : C(255, 150, 60);
          const float fx = cx + wig * 2 * s;
          cv.tri(fx - 8 * s, cy + 32 * s, fx + 8 * s, cy + 32 * s, fx + wig * 4 * s, cy + 22 * s, to565(mulc(fc, 0.8f)));
          oval(fx, cy + 13 * s, 7.5f * s, 13 * s, fc);
          cv.circleFast(cx + 2.5f * s, cy + 6 * s, 2.4f * s, cc(255, 255, 255));
          cv.circleFast(cx + 3 * s, cy + 6 * s, 1.2f * s, cc(30, 30, 40));
          if (what != 2) cv.tri(fx - 7 * s, cy + 12 * s, fx - 12 * s, cy + 16 * s, fx - 7 * s, cy + 18 * s, to565(mulc(fc, 0.8f)));
          if (what == 0 && u > A && u < A + 1.6f)
            for (int q = 0; q < 4; q++) { const float a2 = q * 1.57f + u * 2; star4(cx + cosf(a2) * 24, cy + 14 + sinf(a2) * 24, 5 * (1 - seg(A + 1.0f, A + 1.6f)), 0.24f, cc(255, 230, 120)); }
        }
        break;
      }
      case SC_DUCK: {
        if (back) break;
        float x, y, flip, dunk;
        int face;
        duckPos(x, y, face, flip, dunk);
        const float E = bt(2, 8.5f);
        if (sc.twist == 1 && u > bt(1, 3.4f) - 1.5f) {  // a duckling, paddling after it
          const float dx = x + 34 + 6 * sinf(u * 1.3f) + 300 * (1 - seg(bt(1, 3.4f) - 1.5f, bt(1, 3.4f) + 0.5f));
          drawDuck(dx + 14, waterY(dx + 14, 218, 2.5f) - 6, -1, sc.end == 1 ? flip : 0, 0, 0.8f);
        }
        drawDuck(x, y, face, flip, dunk, 1.45f);
        drawWater(218, 2.5f, seg(0, 0.8f));
        const RGB wc = mixc(waterCol(), C(255, 255, 255), 0.6f);
        if (sc.mid == 1) { const float s0 = bt(0, 4.2f); if (u > s0 + 1.1f && u < s0 + 1.6f) cv.circleFast(x + 18, lerpf(y, y - 34, seg(s0 + 1.1f, s0 + 1.5f)), 4, to565(wc)); }
        ripple(x, y + 12, fracf(u * 0.6f), 30, wc);
        if (sc.evId == 3 || (sc.end == 2 && u > E + 1.5f)) {
          const float h0 = sc.evId == 3 ? sc.evU + 0.3f : E + 1.5f;
          if (u > h0 && u < h0 + 2) heart(x - 2, y - 44 - 16 * seg(h0, h0 + 2), 7 * (1 - seg(h0 + 1.5f, h0 + 2)), cc(255, 110, 150));
        }
        break;
      }
      case SC_COOKIE: {
        if (back) break;
        int end;
        float E, Cg;
        cookieTimes(end, E, Cg);
        const float in = seg(0.1f, 0.7f), L = bt(1, 4.6f);
        const float jx = 262, jy = 200 + (1 - in) * 90;
        const RGB glass = mixc(lidCol(), C(200, 230, 255), light ? 0.55f : 0.32f);
        cv.rrect(jx - 30, jy - 32, 60, 66, 14, to565(glass));
        const RGB ck = C(205, 150, 85);
        const float cxs[3] = {-11, 9, -1}, cys[3] = {20, 22, 8};
        for (int q = 0; q < 3; q++) {
          cv.circle(jx + cxs[q], jy + cys[q], 9, to565(ck));
          for (int d = 0; d < 3; d++) cv.circleFast(jx + cxs[q] - 3 + d * 3, jy + cys[q] - 3 + (d % 2) * 5, 1.3f, cc(90, 55, 35));
        }
        cv.lineFast(jx - 22, jy - 22, jx - 22, jy + 18, 2, to565(mixc(glass, C(255, 255, 255), 0.6f)));
        heart(jx + 14, jy - 14, 4.5f, cc(255, 120, 150));
        // the lid: a nervous clink, lifted, slammed back
        float lift = 16 * seg(L, L + 0.4f) * (1 - seg(E + 0.2f, E + 0.6f));
        if (end == 1 && Cg > L) lift *= 1 - seg(Cg + 0.5f, Cg + 0.6f);
        if (end == 1 && Cg < L) lift = 0;
        if (sc.mid == 2) lift += 3 * sinf(clampf((u - 1.8f) / 0.2f, 0, 1) * PI_F);
        const float tilt = lift > 1 ? 4 * sinf(u * 9) : 0;
        cv.rrect(jx - 30, jy - 42 - lift, 60, 12, 5, cc(230, 90, 90));
        cv.circle(jx + tilt * 0.3f, jy - 46 - lift, 5, cc(250, 120, 120));
        // the cookie
        float x, y, s;
        if (cookiePos(x, y, s)) {
          const int bites = end == 0 ? (u > E + 1.6f ? 3 : u > E + 0.8f ? 2 : u > E ? 1 : 0) : 0;
          const float r = 11 * s;
          if (props3d) ball(x, y, r, ck); else cv.circle(x, y, r, to565(ck));
          for (int d = 0; d < 5; d++) cv.circleFast(x + cosf(d * 1.3f + 0.4f) * r * 0.55f, y + sinf(d * 1.3f + 0.4f) * r * 0.55f, 1.5f * s, cc(90, 55, 35));
          for (int b = 0; b < bites; b++) cv.circle(x - r * 0.9f + b * r * 0.5f, y - r * 0.95f + b * 0.8f, r * 0.5f, to565(lidCol()));
          if (end == 2 && u > E + 0.4f && u < E + 2.4f) heart(x, y - r - 12 - 10 * seg(E + 0.4f, E + 2.4f), 6, cc(255, 110, 150));
          if (u < E && end != 1) star4(x + 14 * cosf(u * 4), y + 14 * sinf(u * 4), 4, 0.24f, cc(255, 230, 140));
        }
        if (end == 0 && u > E && u < E + 2.4f)  // crumbs
          for (int q = 0; q < 5; q++) { const float d = fracf((u - E) * 1.3f + q * 0.2f); cv.circleFast(170 + q * 5 - 10, sc.fy + 70 + d * 60, 1.6f, fadeTo(ck, 1 - d)); }
        if (end == 1 && u > Cg && u < Cg + 2.6f) {  // caught! a big "!" (a white rim so it reads anywhere)
          const float p = seg(Cg, Cg + 0.15f) * (1 - seg(Cg + 2.2f, Cg + 2.6f)) * (1 + 0.08f * sinf(u * 14));
          const float ex = jx + 24, ey = jy - 62;
          cv.rrect(ex - 7.5f * p, ey - 27.5f * p, 15 * p, 33 * p, 7 * p, cc(255, 255, 255));
          cv.circle(ex, ey + 15 * p, 7.5f * p, cc(255, 255, 255));
          cv.rrect(ex - 5.5f * p, ey - 25.5f * p, 11 * p, 29 * p, 5.5f * p, cc(255, 70, 80));
          cv.circle(ex, ey + 15 * p, 5.5f * p, cc(255, 70, 80));
        }

        break;
      }
      case SC_TOP: {
        if (back) break;
        float x, y, a, spin;
        topPos(x, y, a, spin);
        if (x > 360) break;
        static const RGB tc[3][2] = {{C(255, 90, 100), C(255, 220, 90)}, {C(80, 160, 255), C(255, 255, 255)}, {C(150, 110, 255), C(120, 230, 200)}};
        const RGB c1 = tc[sc.iv[1] % 3][0], c2 = tc[sc.iv[1] % 3][1];
        const float ca = cosf(a), sa = sinf(a);
        auto P = [&](float px, float py, float& ox, float& oy) { px *= 1.3f; py *= 1.3f; ox = x + px * ca - py * sa; oy = y + px * sa + py * ca; };
        if (y > 210) cv.ellipse(x, 236, 23, 3.5f, to565(mixc(lidCol(), C(0, 0, 0), light ? 0.15f : 0.4f)));
        static const float ox[11] = {0, -7, -20, -23, -19, -8, 8, 19, 23, 20, 7}, oy[11] = {0, -7, -18, -25, -31, -34, -34, -31, -25, -18, -7};
        float cx, cy;
        P(0, -20, cx, cy);
        for (int i = 0; i < 11; i++) {
          float x0, y0, x1, y1;
          P(ox[i], oy[i], x0, y0);
          P(ox[(i + 1) % 11], oy[(i + 1) % 11], x1, y1);
          cv.tri(cx, cy, x0, y0, x1, y1, to565(i >= 5 && i <= 8 ? mixc(c1, C(255, 255, 255), 0.2f) : c1));
        }
        for (int q = 0; q < 3; q++) {  // the stripes go round
          const float ph = spin + q * 2.094f;
          if (cosf(ph) <= 0) continue;
          float x0, y0, x1, y1;
          P(19 * sinf(ph), -19, x0, y0);
          P(21 * sinf(ph), -30, x1, y1);
          cv.lineFast(x0, y0, x1, y1, 3.4f, to565(c2));
        }
        float hx0, hy0, hx1, hy1;
        P(0, -34, hx0, hy0);
        P(0, -44, hx1, hy1);
        cv.lineFast(hx0, hy0, hx1, hy1, 3.4f, to565(mulc(c1, 0.7f)));
        if (spin > 0 && fabsf(a) < 1) for (int sd = -1; sd <= 1; sd += 2) {  // whirr
          const float wy = y - 32 + 3 * sinf(t * 20 + sd);
          cv.lineFast(x + sd * 36, wy, x + sd * 44, wy - 4, 1.0f, to565(mixc(lidCol(), c1, 0.6f)));
        }
        break;
      }
      case SC_MAGIC: {
        if (back) break;
        const float in = seg(0.1f, 0.9f) * (1 - seg(sc.dur - 0.8f, sc.dur));
        if (in < 0.01f) break;
        const float T0 = bt(0, 2.0f), R = magicRevealT();
        float lev = sc.mid == 2 && u < R ? 6 * sinf(u * 2.4f) * seg(T0, T0 + 0.5f) : 0;
        const float wob = sc.mid == 1 && u > T0 && u < R ? 3 * sinf(u * 30) : 0;
        const float hx = 160 + wob, top = 190 + (1 - in) * 90 - lev;
        const RGB hat = C(72, 60, 112), rim = C(140, 125, 195);
        // the brim (its back half) and the dark opening
        cv.ellipse(hx, top, 46, 9, to565(rim));
        cv.ellipse(hx, top, 44, 7.5f, to565(hat));
        cv.ellipse(hx, top - 0.5f, 31, 4.6f, cc(25, 20, 40));
        // what comes out
        if (u > R + 0.1f) {
          const float rise = seg(R + 0.15f, R + 0.7f);
          if (sc.end == 0) {  // a bunny
            const float z = 1.25f;
            const float by = top - 24 * rise * (1 + 0.15f * sinf(clampf((u - R - 0.15f) / 0.6f, 0, 1) * PI_F)), tw = sinf(u * 6) * 2;
            const uint16_t wh = cc(250, 250, 252), pk = cc(255, 170, 190);
            cv.ellipse(hx - 8 * z, by - 20 * z + tw, 5 * z, 14 * z, wh); cv.ellipse(hx + 8 * z, by - 20 * z - tw, 5 * z, 14 * z, wh);
            cv.ellipse(hx - 8 * z, by - 19 * z + tw, 2.4f * z, 10 * z, pk); cv.ellipse(hx + 8 * z, by - 19 * z - tw, 2.4f * z, 10 * z, pk);
            cv.circle(hx, by, 15 * z, wh);
            const bool bl = fracf(u * 0.4f) < 0.06f;
            for (int sd = -1; sd <= 1; sd += 2) {
              if (bl) cv.lineFast(hx + sd * 7.5f - 2.5f, by - 2, hx + sd * 7.5f + 2.5f, by - 2, 0.9f, cc(40, 40, 50));
              else { cv.circleFast(hx + sd * 7.5f, by - 2, 2.8f, cc(40, 40, 50)); cv.circleFast(hx + sd * 7.5f - 0.8f, by - 3, 0.9f, cc(255, 255, 255)); }
              cv.circleFast(hx + sd * 11, by + 5, 3.2f, cc(255, 190, 200));
            }
            cv.circleFast(hx, by + 4, 2 + 0.5f * sinf(u * 12), pk);
          }
          if (sc.end == 1) {  // a dove, up and away
            const float e = seg(R + 0.15f, R + 2.8f), z = 1.7f;
            const float dx = lerpf(hx, 34, e) + 14 * sinf(e * 6), dy = lerpf(top - 12, -30, e * e);
            const float fl = sinf(t * 18);
            const uint16_t wh = cc(250, 250, 255), gr = cc(195, 200, 218);
            cv.tri(dx - 2 * z, dy - 2 * z, dx + 12 * z, dy - (2 + 12 * fl) * z, dx + 8 * z, dy + 2 * z, gr);
            cv.ellipse(dx, dy, 10 * z, 6 * z, wh);
            cv.tri(dx + 7 * z, dy - 1 * z, dx + 15 * z, dy - 4 * z, dx + 15 * z, dy + 3 * z, wh);
            cv.circle(dx - 9 * z, dy - 4 * z, 4.5f * z, wh);
            cv.tri(dx - 13 * z, dy - 5 * z, dx - 17 * z, dy - 3 * z, dx - 13 * z, dy - 2 * z, cc(255, 170, 70));
            cv.circleFast(dx - 10 * z, dy - 5 * z, 1.4f, cc(30, 30, 40));
            cv.tri(dx - 2 * z, dy, dx + 10 * z, dy + (4 + 8 * fl) * z, dx + 6 * z, dy - 2 * z, wh);
          }
          if (sc.end == 2) {  // flowers spring up
            static const RGB fc[5] = {C(255, 110, 150), C(255, 210, 70), C(150, 120, 255), C(255, 140, 70), C(110, 190, 255)};
            for (int q = 0; q < 5; q++) {
              const float sp = clampf((u - R - 0.15f - q * 0.08f) / 0.4f, 0, 1), ov = sp * (1 + 0.25f * sinf(sp * PI_F));
              if (sp <= 0) continue;
              const float ang = -PI_F / 2 + (q - 2) * 0.34f, L = (q % 2 ? 34 : 42) * ov;
              const float fx = hx + cosf(ang) * L, fy = top - 2 + sinf(ang) * L;
              cv.lineFast(hx + (q - 2) * 5, top - 2, fx, fy, 1.5f, cc(90, 170, 90));
              for (int pe = 0; pe < 5; pe++) cv.circleFast(fx + cosf(pe * 1.2566f + q) * 6 * ov, fy + sinf(pe * 1.2566f + q) * 6 * ov, 4.6f * ov, to565(fc[q]));
              cv.circleFast(fx, fy, 3.3f * ov, cc(255, 240, 150));

            }
          }
        }
        // the front of the brim and the crown
        cv.rrect(hx - 46, top - 1, 92, 10, 5, to565(rim));
        cv.rrect(hx - 44, top, 88, 8, 4, to565(hat));
        cv.rrect(hx - 33, top + 6, 66, 52, 6, to565(rim));
        cv.rrect(hx - 31.5f, top + 7, 63, 50, 5, to565(hat));
        cv.rrect(hx - 31.5f, top + 12, 63, 9, 0, cc(220, 70, 90));
        if (props3d) cv.rrect(hx - 26, top + 24, 6, 28, 3, to565(mixc(hat, C(255, 255, 255), 0.18f)));
        // the wand: three taps (and a droop when the trick fails)
        float wa = -2.6f;
        for (int q = 0; q < 3; q++) wa += 0.28f * kick(T0 + q * 0.65f, 0.06f, 10);
        if (sc.end == 3) wa -= 0.6f * seg(R + 1.4f, R + 2.0f);  // (droops)
        const float pvx = 278, pvy = 222 + (1 - in) * 90, wtx = pvx + 70 * cosf(wa), wty = pvy + 70 * sinf(wa);
        cv.lineFast(pvx, pvy, wtx, wty, 3.6f, cc(150, 150, 168));
        cv.lineFast(pvx, pvy, wtx, wty, 2.6f, cc(40, 38, 50));
        cv.lineFast(wtx + (pvx - wtx) * 0.15f, wty + (pvy - wty) * 0.15f, wtx, wty, 2.6f, cc(250, 250, 250));
        for (int q = 0; q < 3; q++) {  // a sparkle on each tap
          const float d = u - T0 - q * 0.65f;
          if (d > 0 && d < 0.5f) for (int s2 = 0; s2 < 4; s2++) { const float a2 = s2 * 1.57f + q; star4(wtx + cosf(a2) * 12 * (1 + d * 2), wty + sinf(a2) * 12 * (1 + d * 2), 5 * (1 - d * 2), 0.24f, cc(255, 235, 140)); }
        }
        // the puff of smoke
        if (u > R && u < R + 1.4f) {
          const float d = (u - R) / 1.4f, big = sc.end == 3 ? 1.5f : 1;
          for (int q = 0; q < 7; q++) {
            const float a2 = -PI_F / 2 + (q - 3) * 0.45f;
            const float px = hx + cosf(a2) * 26 * d * big, py = top - 8 + sinf(a2) * 22 * d * big;
            smokePuff(px, py, (9 + 10 * d) * big, 1 - d, sc.end == 3 ? C(175, 175, 190) : C(232, 232, 242));
          }
        }
        if (sc.evId == 3 && sc.evU > R)  // applause: hearts
          for (int q = 0; q < 3; q++) {
            const float d = fracf((u - sc.evU) * 0.8f + q * 0.33f);
            heart(120 + q * 40, 170 - d * 120, 6 * (1 - d) + 2, fadeTo(C(255, 110, 150), 1 - d));
          }
        break;
      }
      case SC_CAT: {
        float cx, cy;
        catPos(cx, cy);
        const float E = bt(2, 10), S = bt(0, 5);
        const bool scared = sc.evId == 1 || sc.evId == 2, patted = sc.evId == 3;
        static const RGB furs[4] = {C(240, 160, 80), C(165, 165, 180), C(55, 55, 62), C(245, 232, 210)};
        const RGB fur = furs[sc.iv[1] % 4];
        if (back) {  // the tail swishing up at the side (mid 2)
          if (sc.mid == 2 && !scared && cy < 260) {
            const float sw = sinf(u * 2.2f), ap = seg(S - 1, S) * (1 - seg(E, E + 0.6f));
            if (ap > 0.01f) curve(330, 236, 300 + 14 * sw, 200, 296 + 24 * sw, 236 - 96 * ap, 4.5f, to565(fur), 8, true);
          }
          break;
        }
        if (cy > 285) break;
        float blink = 0;
        bool happy = patted && u > sc.evU + 0.2f;
        if (sc.mid == 0 && !scared && !patted) blink = seg(S, S + 0.5f) * (1 - seg(S + 1.1f, S + 1.6f));
        if (sc.end == 0 && u > E && !scared && !patted) blink = seg(E + 0.3f, E + 1.2f);
        if (sc.end == 2 && u > E && !scared && !patted) happy = true;
        if (blink < 0.05f && !happy && fracf(u * 0.27f + sc.v[2]) < 0.04f) blink = 1;  // a normal blink now and then
        int look = -1;
        if (sc.mid == 2 && u > S && u < S + 3.2f) look = fracf((u - S - 0.25f) / 1.6f) < 0.5f ? -1 : 1;
        drawCatFace(cx, cy, fur, blink, happy, look);
        // paws on the edge (and the swipe)
        const uint16_t pf = to565(fur);
        const float pin = clampf((cy - 300) / -88.0f, 0, 1);
        if (pin > 0.2f) {
          float lpx = cx - 18, lpy = 238;
          if (sc.mid == 1 && !scared && !patted) for (int q = 0; q < 2; q++) {
            const float e = clampf((u - S - q * 0.9f) / 0.7f, 0, 1), s2 = sinf(e * PI_F);
            if (e > 0 && e < 1) { lpx = lerpf(cx - 18, cx - 70, s2); lpy = lerpf(238, 150, s2); }
          }
          if (lpy < 230) paw(lpx, lpy, 9, fur, C(255, 160, 180)); else cv.ellipse(lpx, lpy, 10, 7, pf);
          cv.ellipse(cx + 18, 238, 10, 7, pf);
        }
        if (sc.end == 0 && u > E + 1.2f && !scared && !patted)
          for (int z = 0; z < 3; z++) { const float zz = fracf((u - E) * 0.35f + z * 0.33f); zGlyph(cx + 30 + 16 * zz, cy - 40 - 40 * zz, 3 + 3 * zz, fadeTo(C(170, 190, 255), 1 - zz)); }
        if (patted || (sc.end == 2 && u > E))
          for (int q = 0; q < 2; q++) { const float d = fracf(u * 0.6f + q * 0.5f); heart(cx - 40 + q * 80, cy - 40 - d * 50, 5 * (1 - d) + 2, fadeTo(C(255, 110, 150), 1 - d)); }
        break;
      }
      case SC_DOMINO: {
        if (back) break;
        const float dy = 0, E = domEndT();
        static const RGB rb[7] = {C(255, 95, 95), C(255, 160, 60), C(255, 215, 70), C(100, 210, 120), C(80, 190, 230), C(100, 130, 255), C(180, 110, 255)};
        for (int i = 0; i < DOM_N; i++) {  // set up one by one (each drops in), then the chain
          const float ap = seg(0.25f + i * 0.09f, 0.45f + i * 0.09f);
          if (ap <= 0) continue;
          const float a = domAng(i), ca = cosf(a), sa = sinf(a);
          const float pvx = domX(i) + 5, pvy = 232 - 40 * (1 - ap);
          auto P = [&](float lx, float ly, float& ox, float& oy) { ox = pvx + lx * ca - ly * sa; oy = pvy + lx * sa + ly * ca; };
          float x0, y0, x1, y1, x2, y2, x3, y3;
          const RGB c = rb[i % 7];
          P(1, 1, x0, y0); P(-11, 1, x1, y1); P(-11, -35, x2, y2); P(1, -35, x3, y3);
          const uint16_t ed = to565(mulc(c, 0.62f));
          cv.tri(x0, y0, x1, y1, x2, y2, ed);
          cv.tri(x0, y0, x2, y2, x3, y3, ed);
          P(0, 0, x0, y0); P(-10, 0, x1, y1); P(-10, -34, x2, y2); P(0, -34, x3, y3);
          cv.tri(x0, y0, x1, y1, x2, y2, to565(c));
          cv.tri(x0, y0, x2, y2, x3, y3, to565(c));
          float m0x, m0y, m1x, m1y;
          P(-9, -17, m0x, m0y); P(-1, -17, m1x, m1y);
          cv.lineFast(m0x, m0y, m1x, m1y, 0.8f, cc(255, 255, 255));
          const int pa = 1 + i % 3, pb = 1 + (i * 2) % 3;
          for (int q = 0; q < pa; q++) { float px, py; P(-5, -26 + q * 5 - (pa - 1) * 2.5f, px, py); cv.circleFast(px, py, 1.5f, cc(255, 255, 255)); }
          for (int q = 0; q < pb; q++) { float px, py; P(-5, -9 + q * 5 - (pb - 1) * 2.5f, px, py); cv.circleFast(px, py, 1.5f, cc(255, 255, 255)); }
        }
        const bool tapped = sc.evId == 1 && sc.evU < domStart();
        if (sc.end == 0) {  // a little bell at the end of the line
          const float sh = 2.5f * sinf(t * 50) * (u > E ? expf(-(u - E) * 2.5f) : 0);
          const float bx = 306 + sh, by = 220 + dy;
          cv.rrect(bx - 1, by - 14, 2, 6, 1, cc(150, 150, 165));
          cv.circle(bx, by - 2, 9, cc(230, 170, 40));
          cv.rrect(bx - 10, by - 2, 20, 9, 2, cc(230, 170, 40));
          cv.circle(bx - 2.5f, by - 5, 3, cc(255, 230, 140));
          cv.circleFast(bx + sh * 0.5f, by + 8, 2.5f, cc(160, 110, 20));
          if (u > E && u < E + 1.0f) for (int q = 0; q < 3; q++) { const float d = (u - E) / 1.0f; cv.lineFast(bx + 12 + d * 6, by - 8 + q * 7, bx + 18 + d * 6, by - 10 + q * 7, 1.0f, fadeTo(C(255, 220, 90), 1 - d)); }
        }
        if (sc.end == 1 && !tapped) {
          const float bx = 309 + 200 * seg(E, E + 1.2f);
          if (bx < 340) ball(bx, 225 + dy, 7, C(255, 90, 100));
        }
        if (sc.end == 2 && !tapped && u > E + 1.4f && u < E + 2.6f)
          for (int q = 0; q < 4; q++) { const float a2 = q * 1.57f + u * 2; star4(160 + cosf(a2) * 120, 200 + sinf(a2) * 24, 6 * (1 - seg(E + 2.0f, E + 2.6f)), 0.24f, cc(255, 230, 120)); }
        break;
      }
      case SC_SNOWMAN: {
        const float E = snowmanEndT(), melt = sc.end == 1 && sc.evId == 0 ? seg(E + 0.6f, E + 3.2f) : 0;
        if (back) {  // gentle snowfall and a snowy mound
          const RGB fc = light ? C(150, 175, 230) : C(235, 245, 255);
          const float in = seg(0, 1.2f) * (1 - seg(sc.dur - 1, sc.dur)) * (1 - melt);
          for (int i = 0; i < 14; i++) {
            const float sp = 18 + 12 * fracf(i * 0.71f);
            const float y = -10 + fmodf(u * sp + i * 37, 250), x = fracf(i * 0.618f + sc.v[0]) * 320 + 8 * sinf(u * 1.1f + i);
            cv.circleFast(x, y, 1.4f + (i % 3) * 0.5f, to565(mixc(lidCol(), fc, in * (0.5f + 0.5f * fracf(i * 0.33f)))));
          }
          if (melt > 0) {  // the sun comes out
            const float s = seg(E + 0.3f, E + 1.0f);
            burst(40, 30, 16 * s, 24 * s, 8, 1.4f, cc(255, 200, 60), u);
            ball(40, 30, 12 * s + 0.5f, C(255, 210, 70));
          }
          break;
        }
        const RGB mound = light ? C(232, 240, 252) : C(225, 232, 245);
        cv.ellipse(286, 250, 84 * seg(0.1f, 0.6f), 22, to565(mound));
        if (melt > 0) cv.ellipse(290, 236, 26 + 26 * melt, 4 + 4 * melt, to565(mixc(waterCol(), C(255, 255, 255), 0.3f)));
        float hx, hy;
        snowHead(hx, hy);
        float popT[5] = {4.6f, 5.2f, 5.8f, 6.4f, 7.0f};  // eyes, nose (and mouth), arms, buttons... the hat
        popT[2] = 5.0f;
        drawSnowman(290, 232, melt, sc.mid == 1, sc.end == 0 && u > E && sc.evId == 0, hx, hy, popT);
        if (sc.evId == 1) {  // the snowball
          const float d = (u - sc.evU) / 0.4f;
          if (d > 0 && d < 1) cv.circle(lerpf(270, 150, d), lerpf(185, sc.fy, d) - 30 * sinf(d * PI_F), 7, cc(250, 252, 255));
          if (d >= 1) {
            const float f = 1 - seg(sc.evU + 1.4f, sc.evU + 2.6f);
            for (int q = 0; q < 6; q++) cv.circleFast(150 + cosf(q * 1.05f) * 12, sc.fy - 6 + sinf(q * 1.05f) * 9 + (u - sc.evU) * 6, (q % 2 ? 7 : 5) * f, fadeTo(C(250, 252, 255), f));
          }
        }
        if (sc.end == 2 && u > E + 0.3f && u < E + 2.6f) heart(240, 134 - 20 * seg(E + 0.3f, E + 2.6f), 9, cc(255, 110, 150));
        break;
      }
      case SC_FROG: {
        if (back) break;
        float x, y, sq;
        frogPos(x, y, sq);
        if (x > 345) break;
        const float H0 = bt(0, 3.2f), E = bt(2, 8.6f);
        const RGB g = C(110, 200, 100), dk = C(60, 140, 70), belly = C(205, 238, 165);
        const float z = 1.4f, sx = z / sq, sy = z * sq;
        cv.ellipse(x - 16 * z, y + 9 * z, 8 * z, 6 * z, to565(dk));
        cv.ellipse(x + 16 * z, y + 9 * z, 8 * z, 6 * z, to565(dk));
        oval(x, y, 22 * sx, 15 * sy, g);
        cv.ellipse(x, y + 5 * sy, 13 * sx, 8 * sy, to565(belly));
        cv.ellipse(x - 7 * z, y + 14 * sy, 4 * z, 2.5f * z, to565(dk));
        cv.ellipse(x + 7 * z, y + 14 * sy, 4 * z, 2.5f * z, to565(dk));
        if (sc.mid == 1) {  // the croak: its throat puffs out
          float sac = 0;
          for (int q = 0; q < 2; q++) { const float e = clampf((u - H0 - q * 1.4f) / 0.7f, 0, 1); sac = fmaxf(sac, sinf(e * PI_F)); }
          if (sac > 0.02f) cv.circle(x - 2, y + 6 * sy, 11 * z * sac, to565(mixc(belly, C(255, 255, 255), 0.3f)));
        }
        const bool happy = sc.evId == 3 && u > sc.evU;
        for (int sd = -1; sd <= 1; sd += 2) {
          const float ex = x + sd * 10 * sx, ey = y - 13 * sy;
          cv.circle(ex, ey, 7.5f * z, to565(g));
          if (happy) { cv.lineFast(ex - 5, ey + 1, ex, ey - 4, 1.5f, to565(dk)); cv.lineFast(ex, ey - 4, ex + 5, ey + 1, 1.5f, to565(dk)); continue; }
          cv.circle(ex, ey, 5 * z, cc(250, 250, 240));
          cv.circleFast(ex - 2, ey - 0.5f, 2.6f * z, cc(25, 30, 25));
          cv.circleFast(ex - 3, ey - 2, 1.1f, cc(255, 255, 255));
        }
        const float gulp = sc.mid == 2 ? kick(H0 + 2.15f, 0.08f, 3) : 0;
        curve(x - 12 * sx, y - 3 * z, x, y + (4 + 2 * gulp) * z, x + 12 * sx, y - 3 * z, 1.5f, to565(dk), 6, false);
        cv.ellipse(x - 14 * sx, y + 1 * z, (3.5f + 2 * gulp) * z, (2.2f + 1.5f * gulp) * z, cc(255, 150, 160));
        cv.ellipse(x + 14 * sx, y + 1 * z, (3.5f + 2 * gulp) * z, (2.2f + 1.5f * gulp) * z, cc(255, 150, 160));
        if (sc.mid == 2 && sc.evId == 0) {  // a fly comes by... and the tongue gets it
          const float d = u - H0;
          if (d > 0 && d < 2.12f) {
            const float fx = lerpf(-20, 180, seg(H0, H0 + 1.6f)) + 14 * sinf(d * 5), fy = lerpf(70, 168, seg(H0, H0 + 1.6f)) + 10 * sinf(d * 7);
            const uint16_t wing = to565(mixc(lidCol(), C(200, 225, 255), 0.6f));
            const float fl = fabsf(sinf(t * 70));
            cv.ellipse(fx - 4, fy - 6 - fl * 3, 6, 3 + fl * 4, wing);
            cv.ellipse(fx + 4, fy - 6 - fl * 3, 6, 3 + fl * 4, wing);
            cv.ellipse(fx, fy, 7.5f, 5, to565(light ? C(45, 45, 55) : C(135, 140, 158)));
            cv.circleFast(fx + 6, fy - 1, 3.5f, to565(light ? C(35, 35, 45) : C(115, 120, 138)));
            cv.circleFast(fx + 7.5f, fy - 2, 1.4f, cc(230, 60, 60));
            if (d > 2.0f) cv.lineFast(x - 8, y - 1, fx, fy, 3.0f, cc(255, 120, 140));
          }
          if (d >= 2.12f && d < 2.3f) { const float r = (d - 2.12f) / 0.18f; cv.lineFast(x - 8, y - 1, lerpf(180, x - 8, r), lerpf(168, y - 1, r), 3.0f, cc(255, 120, 140)); }
        }
        if (sc.mid == 0 && sc.evId == 0)  // "hic" bubbles
          for (int q = 0; q < 3; q++) {
            const float h = H0 + (q == 0 ? 0 : q == 1 ? 1.2f : 2.1f), d = (u - h) / 0.8f;
            if (d > 0 && d < 1) cv.circleFast(x + 22, y - 36 - d * 24, 4 + 3 * d, fadeTo(C(200, 230, 255), 1 - d));
          }
        if (sc.evId == 3 && u > sc.evU) heart(x, y - 46 - 10 * seg(sc.evU, sc.evU + 2), 8, cc(255, 110, 150));
        if (sc.end == 2 && u > E && u < E + 0.6f && sc.evId == 0) burst(x, y - 12, 34, 46, 8, 1.4f, to565(inkish()));
        break;
      }
      case SC_MOON: {
        float x, y;
        moonPos(x, y);
        const float E = moonEndT();
        const bool woke = (sc.evId == 1 || sc.evId == 2) && u > sc.evU && u < sc.evU + 1.4f;
        if (back) {  // the night sky behind the face
          const float in = seg(0.2f, 1.6f) * (1 - seg(sc.dur - 1, sc.dur));
          const RGB sc1 = light ? C(230, 170, 40) : C(255, 250, 215);
          for (int i = 0; i < 12; i++) {
            const float sx = 14 + fracf(i * 0.618f + sc.v[0]) * 292, sy = 8 + fracf(i * 0.377f + sc.v[1]) * 60;
            if (fabsf(sx - x) < 38 && fabsf(sy - y) < 38) continue;
            const float tw = 0.6f + 0.4f * sinf(t * (1.5f + i % 3) + i);
            const uint16_t c = to565(mixc(lidCol(), sc1, in * (0.4f + 0.5f * tw)));
            if (i % 4 == 0) star4(sx, sy, 5 * tw, 0.22f, c); else cv.circleFast(sx, sy, 1.4f * tw + 0.3f, c);
          }
          break;
        }
        if (sc.mid == 2 && u > 2.6f && u < E)  // the lullaby's notes
          for (int i = 0; i < 6; i++) {
            const float born = 2.6f + i * 1.1f, age = u - born;
            if (age < 0 || age > 2.2f) continue;
            noteGlyph(x + 34 + age * 30, y + 12 + 14 * sinf(age * 3) - age * 8, 7, fadeTo(light ? C(150, 120, 200) : C(220, 210, 255), 1 - age / 2.2f));
          }
        if (y < -40) break;
        // the moon, in front (a little visitor): a crescent with a nightcap and a sleepy face
        const RGB mc = light ? C(245, 195, 70) : C(255, 240, 180);
        cv.circle(x, y, 31, to565(mc));
        cv.circle(x + 17, y - 9, 27, to565(lidCol()));
        cv.tri(x - 14, y - 26, x + 5, y - 21, x + 22, y - 47, cc(90, 120, 220));
        cv.tri(x - 14, y - 26, x - 4, y - 20, x + 5, y - 21, cc(245, 245, 255));
        cv.circleFast(x + 22, y - 47, 5, cc(245, 245, 255));
        const uint16_t ink = to565(mulc(mc, 0.45f));
        const float ex = x - 18, ey = y - 2;
        bool wink = false;
        if (sc.mid == 1) { const float W0 = bt(0, 4.5f); wink = u > W0 && u < W0 + 1.4f; }
        if (woke || wink) { cv.circleFast(ex, ey, 3.6f, ink); cv.circleFast(ex - 1.2f, ey - 1.2f, 1.2f, cc(255, 255, 255)); }
        else { cv.lineFast(ex - 5, ey - 1, ex, ey + 2.5f, 1.2f, ink); cv.lineFast(ex, ey + 2.5f, ex + 5, ey - 1, 1.2f, ink); }
        cv.circleFast(ex - 2, ey + 8, 3, cc(255, 160, 160));
        float yawn = 0;
        if (sc.mid == 0) { const float Y = bt(0, 4); yawn = seg(Y, Y + 0.6f) * (1 - seg(Y + 1.4f, Y + 1.9f)); }
        if (woke) yawn = 0.6f;
        if (yawn > 0.05f) cv.ellipse(x - 11, y + 14, 3 + 2.5f * yawn, 1.2f + 6 * yawn, ink);
        else cv.lineFast(x - 13, y + 14, x - 8, y + 14, 1.1f, ink);
        if (sc.end == 0 && u > E + 0.8f)
          for (int z = 0; z < 3; z++) { const float zz = fracf((u - E) * 0.35f + z * 0.33f); zGlyph(x + 18 + 16 * zz, y + 6 - 34 * zz, 3 + 3 * zz, fadeTo(C(170, 190, 255), 1 - zz)); }
        if (sc.end == 2 && u > E) {  // a cloud blanket
          const float e = seg(E, E + 2.2f);
          cloud(lerpf(-70, x + 6, e), y + 18, 24, to565(light ? C(205, 210, 230) : C(150, 158, 185)));
        }
        break;

      }
      case SC_DOODLE: {
        if (back) break;
        float cx, cy;
        doodleBox(cx, cy);
        const float E = doodleEndT();
        float sz = 1;
        float ox = 0, oy = 0;
        if (sc.end == 1 && u > E) {  // crumpled up and tossed
          sz = 1 - seg(E + 0.3f, E + 0.9f);
          if (sz < 0.25f) {
            const float e = seg(E + 0.9f, E + 1.6f);
            const float bx = lerpf(cx, 345, e), by = cy - 34 * sinf(e * PI_F);
            cv.circle(bx, by, 11, cc(160, 160, 170));
            cv.circle(bx, by, 10, cc(248, 246, 238));
            for (int q = 0; q < 3; q++) cv.lineFast(bx - 6 + q * 4, by - 5 + q * 3, bx - 2 + q * 4, by + 1 + q * 2, 0.7f, cc(170, 170, 180));
            break;
          }
        }
        if (sc.end == 2 && u > E) { oy = -5 * fabsf(sinf((u - E) * 7)); }
        // the pad: paper, rule lines, rings
        const float pw = 150 * sz, ph = 74 * sz;
        cv.rrect(cx - pw / 2 + 2, cy - ph / 2 + 2, pw, ph, 6, to565(mixc(lidCol(), C(0, 0, 0), light ? 0.15f : 0.5f)));
        cv.rrect(cx - pw / 2, cy - ph / 2, pw, ph, 6, cc(252, 250, 242));
        for (int q = 1; q < 4; q++) cv.lineFast(cx - pw / 2 + 6, cy - ph / 2 + q * ph / 4, cx + pw / 2 - 6, cy - ph / 2 + q * ph / 4, 0.5f, cc(205, 220, 240));
        for (int q = 0; q < 6; q++) ring(cx - pw / 2 + 16 * sz + q * 24 * sz, cy - ph / 2, 3 * sz, 0.9f, cc(150, 155, 170));
        // the doodle so far
        float xs[64], ys[64];
        bool brk[64];
        const int n = doodlePts(sc.mid, xs, ys, brk);
        const float p = doodleProg(), c = p * (n - 1);
        static const RGB inks[5] = {C(225, 60, 95), C(240, 150, 30), C(70, 70, 100), C(60, 110, 210), C(60, 60, 80)};
        const uint16_t ic = to565(inks[sc.mid % 5]);
        float pulse = 1;
        if (sc.end == 2 && u > E) pulse = 1 + 0.1f * sinf((u - E) * 9);
        const float S2x = 30 * sz * pulse, S2y = 26 * sz * pulse;
        for (int i = 1; i < n; i++) {
          if (brk[i] || i - 1 > c) continue;
          const float f = clampf(c - (i - 1), 0, 1);
          const float x0 = cx + ox + xs[i - 1] * S2x, y0 = cy + oy + ys[i - 1] * S2y;
          const float x1 = cx + ox + lerpf(xs[i - 1], xs[i], f) * S2x, y1 = cy + oy + lerpf(ys[i - 1], ys[i], f) * S2y;
          cv.lineFast(x0, y0, x1, y1, 1.9f * sz, ic);
        }
        if (sc.end == 2 && u > E && sc.mid == 2 && fracf((u - E) * 0.6f) < 0.12f)  // the cat winks
          cv.lineFast(cx + 0.2f * S2x, cy + oy + 0.08f * S2y, cx + 0.3f * S2x, cy + oy + 0.08f * S2y, 1.6f, cc(252, 250, 242));
        if (sc.twist == 1) {  // the mistake: a scribble, rubbed out
          const float M = doodleMistakeT();
          const float sv = seg(M, M + 0.4f) * (1 - seg(M + 0.9f, M + 1.8f));
          if (sv > 0.02f) for (int q = 0; q < 6; q++) cv.lineFast(cx + 42 + (q % 2) * 16, cy - 14 + q * 5, cx + 42 + ((q + 1) % 2) * 16, cy - 9 + q * 5, 1.4f, fadeTo(inks[sc.mid % 5], sv));
        }
        if (sc.end == 0 && u > E && u < E + 2.4f)
          for (int q = 0; q < 4; q++) { const float a2 = q * 1.57f + u * 1.8f; star4(cx + cosf(a2) * 90, cy + sinf(a2) * 40, 6 * (1 - seg(E + 1.8f, E + 2.4f)), 0.24f, cc(255, 225, 110)); }
        if (sc.end == 2 && u > E && u < E + 2.6f)
          for (int q = 0; q < 3; q++) { const float a2 = q * 2.1f + u * 2.5f; star4(cx + cosf(a2) * 46, cy + oy + sinf(a2) * 30, 4.5f, 0.24f, cc(255, 225, 110)); }
        // the pencil
        if (u < E + 0.3f && sz > 0.9f) {
          float px, py;
          const bool on = doodlePen(px, py);
          if (!on) { px = cx + 88; py = cy + 10; }
          const bool rub = sc.twist == 1 && u > doodleMistakeT() + 0.8f && u < doodleMistakeT() + 1.8f;
          const float wg = on ? 1.5f * sinf(t * 30) : 0;
          if (rub) {  // flipped over: the eraser end down
            cv.lineFast(px, py, px + 20, py - 32, 3.4f, cc(255, 200, 60));
            cv.rrect(px - 3, py - 3, 6, 6, 2, cc(255, 150, 170));
          } else {
            cv.lineFast(px + 4 + wg, py - 6, px + 22 + wg, py - 34, 3.4f, cc(255, 200, 60));
            cv.lineFast(px + wg * 0.3f, py, px + 4 + wg, py - 6, 1.6f, cc(235, 200, 150));
            cv.circleFast(px + wg * 0.2f, py, 1.1f, ic);
            cv.circleFast(px + 23 + wg, py - 36, 2.6f, cc(255, 150, 170));
          }
        }
        break;
      }
      case SC_BOAT: {
        const bool wet = weatherNow == 3 || weatherNow == 4;
        if (back) {
          if (wet) {  // the rain
            const RGB dropC = light ? C(80, 140, 230) : C(140, 200, 255);
            for (int i = 0; i < 14; i++) {
              const float x = fracf(i * 0.377f + sc.v[0]) * 330 - 5, y = fmodf(u * 170 + i * 37, 250) - 10;
              cv.lineFast(x, y, x - 1, y + 7, 0.8f, to565(mixc(lidCol(), dropC, 0.7f)));
            }
          }
          break;
        }
        float x, y, a, sink;
        boatPos(x, y, a, sink);
        const float E = bt(2, 8.5f), amp = sc.mid == 2 ? 5.5f : 2.5f;
        const float ca = cosf(a), sa = sinf(a);
        auto P = [&](float px, float py, float& ox, float& oy) { px *= 1.5f; py *= 1.5f; ox = x + px * ca - py * sa; oy = y + px * sa + py * ca; };
        auto T = [&](float ax, float ay, float bx, float by, float qx, float qy, uint16_t c) {
          float x0, y0, x1, y1, x2, y2;
          P(ax, ay, x0, y0); P(bx, by, x1, y1); P(qx, qy, x2, y2);
          cv.tri(x0, y0, x1, y1, x2, y2, c);
        };
        static const RGB papers[3] = {C(250, 250, 245), C(255, 225, 140), C(170, 215, 255)};
        const RGB pc = papers[sc.iv[1] % 3], sh = mulc(pc, 0.82f);
        if (x > -40 && x < 360) {
          T(-13, -5, 13, -5, 0, -31, to565(pc));
          T(0, -5, 13, -5, 0, -31, to565(sh));
          T(-24, -5, 24, -5, 15, 6, to565(pc));
          T(-24, -5, 15, 6, -15, 6, to565(pc));
          T(-24, -5, -15, 6, -15, 0, to565(sh));
          float fx0, fy0, fx1, fy1;
          P(-24, -5, fx0, fy0); P(24, -5, fx1, fy1);
          cv.lineFast(fx0, fy0, fx1, fy1, 0.7f, to565(mulc(pc, 0.7f)));
          float tx, ty;
          P(0, -31, tx, ty);
          if (sc.end == 2 && u > E + 1.4f) {  // a tiny flag
            const float fp = seg(E + 1.4f, E + 1.8f);
            cv.lineFast(tx, ty, tx, ty - 14 * fp, 1.0f, to565(inkish()));
            cv.tri(tx, ty - 14 * fp, tx, ty - 7 * fp, tx + (11 + 2 * sinf(t * 9)) * fp, ty - 11 * fp, cc(240, 70, 80));
          }
          if (sc.twist == 1) {
            const float c0 = bt(1, 5.5f);
            if (u > c0 - 0.8f && u < c0 + 3.2f) {
              const float in = seg(c0 - 0.8f, c0), out = seg(c0 + 2.4f, c0 + 3.2f);
              const float cx = lerpf(lerpf(340, tx, in), -20, out), cy = lerpf(lerpf(20, ty - 11, in), 10, out);
              drawCrumb(cx, cy, (in < 1 || out > 0) ? fabsf(sinf(t * 18)) : 0, out > 0 ? -1 : 1, false, 0.9f);
            }
          }
        }
        drawWater(218, amp, seg(0, 0.8f));
        const RGB wc = mixc(waterCol(), C(255, 255, 255), 0.6f);
        if (wet) for (int i = 0; i < 4; i++) { const float ph = fracf(u * 0.8f + i * 0.25f); ripple(20 + fracf(i * 0.618f + floorf(u * 0.8f + i * 0.25f) * 0.41f) * 280, 227 + i * 4, ph, 14, wc); }
        if (sink > 0) for (int q = 0; q < 3; q++) { const float d = fracf(u * 1.2f + q * 0.33f); cv.circleFast(x + (q - 1) * 10, y - 8 - d * 26, 2.5f + d * 2.5f, to565(wc)); }
        if (sc.mid == 1) {  // the puff of wind
          const float B = bt(0, 4.0f), d = (u - B) / 1.2f;
          if (d > 0 && d < 1) for (int q = 0; q < 3; q++) { const float wx = lerpf(170, x + 30, d) - q * 14, wy = 150 + q * 12 + 20 * d; cv.lineFast(wx, wy, wx + 14, wy + 2, 1.0f, fadeTo(C(235, 240, 250), 1 - d)); }
        }
        if (sc.evId == 1) { const float d = (u - sc.evU) / 0.7f; if (d > 0 && d < 1) for (int q = 0; q < 6; q++) cv.circleFast(x + (q - 2.5f) * 13 * (0.5f + d), waterY(x, 218, amp) - 34 * d * (1 - d) * 4 * (0.6f + 0.1f * q), 2.5f, to565(wc)); }
        break;
      }
      case SC_PLANT: {
        if (back) break;
        const float in = seg(0.1f, 0.8f) * (1 - seg(sc.dur - 0.6f, sc.dur));
        if (in < 0.01f) break;
        const float W1 = bt(0, 1.6f), E = plantBloomT(), dy = (1 - in) * 80;
        const float g = plantGrow(), dr = plantDroop();
        // a puddle (too much water)
        if (sc.end == 1 && u > W1 + 1) cv.ellipse(236, 240 + dy, 40 * seg(W1 + 1, E - 1.6f), 5, to565(mixc(waterCol(), lidCol(), 0.3f)));
        // the stem and leaves
        float tx, ty;
        plantTop(tx, ty);
        ty += dy;
        const uint16_t gc = cc(90, 180, 90), gl = cc(120, 210, 110);
        const float baseY = 204 + dy;
        curve(236, baseY, 236 + 4 * sinf(u * 0.9f) + 10 * dr, lerpf(baseY, ty, 0.5f), tx, ty, 1.8f, gc, 8, false);
        const float hs = baseY - ty;
        for (int q = 0; q < (g > 0.5f ? 2 : 1); q++) {  // leaves: a sprout's two, then more up the stem
          const float ly = baseY - hs * (q == 0 ? 0.35f : 0.65f), lx = lerpf(236, tx, q == 0 ? 0.35f : 0.65f), sz = 7 + 6 * fminf(g, 1);
          for (int sd = -1; sd <= 1; sd += 2) {
            const float sd2 = q == 1 ? -sd : sd, wav = 2 * sinf(u * 2 + sd + q) + 6 * dr;
            cv.tri(lx, ly, lx + sd2 * sz * 0.5f, ly - sz * 0.45f + wav * 0.3f, lx + sd2 * sz, ly - sz * 0.15f + wav, gl);
            cv.tri(lx, ly, lx + sd2 * sz * 0.5f, ly + sz * 0.15f + wav * 0.3f, lx + sd2 * sz, ly - sz * 0.15f + wav, gc);
          }
        }
        // the bud, then the flower
        const float bl = seg(E, E + 0.6f) * (sc.end == 2 ? 1.5f : sc.end == 1 ? 0.8f : 1);
        static const RGB pcs[4] = {C(255, 120, 170), C(150, 130, 255), C(255, 150, 70), C(90, 180, 255)};
        if (g > 0.7f && bl < 0.05f) cv.ellipse(tx, ty - 4, 4.5f, 6.5f, to565(mixc(C(110, 190, 100), pcs[sc.iv[1] % 4], 0.4f)));
        if (bl > 0.02f) {
          const bool sun = sc.end == 2;
          const RGB pc = sun ? C(255, 210, 50) : pcs[sc.iv[1] % 4];
          const int np = sun ? 10 : 6;
          for (int q = 0; q < np; q++) {
            const float a2 = q * 2 * PI_F / np + u * 0.2f;
            cv.circleFast(tx + cosf(a2) * (sun ? 11 : 9) * bl, ty + sinf(a2) * (sun ? 11 : 9) * bl, (sun ? 5.5f : 6.5f) * bl, to565(pc));
          }
          cv.circleFast(tx, ty, (sun ? 8 : 5) * bl, to565(sun ? C(130, 80, 40) : C(255, 230, 110)));
        }
        if (sc.evId == 3 && u > sc.evU && u < sc.evU + 2.2f) heart(tx + 14, ty - 14 - 16 * seg(sc.evU, sc.evU + 2.2f), 5, cc(255, 110, 150));
        if (sc.end != 1 && u > E && u < E + 1.8f)
          for (int q = 0; q < 4; q++) { const float a2 = q * 1.57f + u * 2; star4(tx + cosf(a2) * 22, ty + sinf(a2) * 22, 4.5f * (1 - seg(E + 1.2f, E + 1.8f)), 0.24f, cc(255, 230, 120)); }
        // the pot
        const RGB tc = C(205, 105, 65);
        cv.tri(214, 212 + dy, 258, 212 + dy, 252, 240 + dy, to565(tc));
        cv.tri(214, 212 + dy, 252, 240 + dy, 220, 240 + dy, to565(tc));
        cv.tri(240, 212 + dy, 258, 212 + dy, 252, 240 + dy, to565(mulc(tc, 0.85f)));
        cv.rrect(208, 202 + dy, 56, 11, 3, to565(mixc(tc, C(255, 255, 255), 0.15f)));
        cv.ellipse(236, 205 + dy, 22, 3, cc(110, 70, 45));
        // the watering can (once, or twice)
        for (int w = 0; w < (sc.mid == 2 ? 2 : 1); w++) {
          const float w0 = w == 0 ? W1 : W1 + 3.2f, wl = w == 0 ? 2.2f : 1.6f;
          if (u < w0 - 0.7f || u > w0 + wl + 0.7f) continue;
          const float slide = (1 - seg(w0 - 0.7f, w0 - 0.1f)) + seg(w0 + wl + 0.1f, w0 + wl + 0.7f);
          const float tilt = -0.6f * seg(w0 - 0.1f, w0 + 0.3f) * (1 - seg(w0 + wl - 0.3f, w0 + wl));
          const float ccx = 290 + 60 * slide, ccy = 166;
          const float ca = cosf(tilt), sa = sinf(tilt);
          auto P = [&](float px, float py, float& ox, float& oy) { px *= 1.15f; py *= 1.15f; ox = ccx + px * ca - py * sa; oy = ccy + px * sa + py * ca; };
          float a0, b0, a1, b1, a2, b2, a3, b3;
          const uint16_t can = cc(90, 185, 175), cand = cc(60, 140, 135);
          P(-16, -12, a0, b0); P(16, -12, a1, b1); P(16, 12, a2, b2); P(-16, 12, a3, b3);
          cv.tri(a0, b0, a1, b1, a2, b2, can);
          cv.tri(a0, b0, a2, b2, a3, b3, can);
          float s0x, s0y, s1x, s1y, h0x, h0y, h1x, h1y;
          P(-14, 4, s0x, s0y); P(-36, -10, s1x, s1y);
          cv.lineFast(s0x, s0y, s1x, s1y, 2.6f, cand);
          cv.circleFast(s1x, s1y, 3.4f, cand);
          P(8, -12, h0x, h0y); P(14, -22, h1x, h1y);
          cv.lineFast(h0x, h0y, h1x, h1y, 2, cand);
          P(-6, -22, h0x, h0y);
          cv.lineFast(h1x, h1y, h0x, h0y, 2, cand);
          if (tilt < -0.4f) {  // the water
            const RGB wcol = light ? C(80, 150, 230) : C(140, 200, 255);
            for (int q = 0; q < 6; q++) {
              const float d = fracf(u * 2.6f + q / 6.0f);
              const float qx = s1x + (q - 2.5f) * 1.8f - d * 4, qy = lerpf(s1y + 2, baseY - 2, d);
              cv.lineFast(qx, qy, qx - 0.5f, qy + 4, 0.8f, to565(wcol));
            }
          }
        }
        if (sc.mid == 1) {  // a song for it
          const float S1 = W1 + 2.6f;
          for (int q = 0; q < 4; q++) {
            const float age = u - S1 - q * 0.5f;
            if (age < 0 || age > 1.8f) continue;
            noteGlyph(lerpf(190, 230, age / 1.8f), 150 - age * 30 + 8 * sinf(age * 4 + q), 5.5f, fadeTo(light ? C(150, 110, 200) : C(220, 210, 255), 1 - age / 1.8f));
          }
        }
        break;
      }
      case SC_ROCKET: {
        if (back) break;
        const float in = seg(0.1f, 0.7f) * (1 - seg(sc.dur - 0.6f, sc.dur));
        if (in < 0.01f) break;
        const float dy = (1 - in) * 70, L = rocketLaunchT();
        // the pad and the countdown tower
        cv.rrect(234, 230 + dy, 68, 7, 3, cc(150, 155, 170));
        cv.lineFast(232, 232 + dy, 232, 168 + dy, 2.2f, cc(150, 155, 170));
        for (int q = 0; q < 3; q++) {
          const float lt = 1.0f + q * 0.8f;
          const bool on = u > lt && (L > 3 || u > L - 0.01f);
          static const RGB lc[3] = {C(255, 80, 80), C(255, 200, 60), C(90, 220, 110)};
          cv.circle(223, 222 - q * 17 + dy, 6, on ? to565(lc[q]) : cc(90, 92, 105));
        }
        float x, y, a, flame;
        rocketPos(x, y, a, flame);
        if (y > -90) drawRocket(x, y + dy, a, flame);
        // smoke at the launch (and the sputter)
        auto smoke = [&](float t0, float n2, float big) {
          const float d = (u - t0) / 1.6f;
          if (d < 0 || d > 1) return;
          for (int q = 0; q < (int)n2; q++) {
            const float sd = q % 2 ? 1 : -1, f = 0.4f + 0.6f * fracf(q * 0.37f);
            smokePuff(268 + sd * (8 + 50 * d * f) * big, 228 + dy - 6 * d - 4 * f, (6 + 10 * d) * big, 1 - d, C(222, 224, 232));
          }
        };
        smoke(L, 8, 1);
        if (sc.mid == 2 && L > 4) smoke(3.4f, 4, 0.6f);
        if (sc.end == 2 && u > L + 3.8f) smoke(L + 4.7f, 6, 0.8f);
        if (sc.end == 0 && u > L + 2.1f && u < L + 3.6f) {  // a new star where it went
          const float s = seg(L + 2.1f, L + 2.4f) * (1 - seg(L + 3.0f, L + 3.6f));
          star4(268, 20, 12 * s * (0.8f + 0.2f * sinf(u * 12)), 0.22f, cc(255, 240, 170));
        }
        break;
      }
      case SC_ICECREAM: {
        if (back) break;
        const float up = seg(0.1f, 1.0f) * (1 - seg(sc.dur - 0.8f, sc.dur));
        if (up < 0.01f) break;
        const float E = bt(2, 8), dy = (1 - up) * 110;
        float lift = 0;
        for (int q = 0; q < 3; q++) lift = fmaxf(lift, kick(iceLickT(q) - 0.1f, 0.15f, 6));
        const float cx = 160, ctop = 204 + dy - 8 * lift, Z = 1.45f;
        // the cone (eaten from the top at the end)
        float coneH = 64;
        if (sc.end == 0 && u > E + 1.4f) coneH *= 1 - 0.75f * seg(E + 1.4f, E + 2.6f);
        const RGB wf = C(225, 170, 95);
        const float cb = ctop + 64, ct = cb - coneH, hw = 26 * coneH / 64;
        cv.tri(cx - hw - 1.2f, ct - 0.6f, cx + hw + 1.2f, ct - 0.6f, cx, cb + 1.5f, to565(mulc(wf, 0.78f)));
        cv.tri(cx - hw, ct, cx + hw, ct, cx, cb, to565(wf));
        for (int q = 1; q < 4; q++) {
          const float yy = ct + q * coneH / 4, w2 = 26 * (cb - yy) / 64;
          cv.lineFast(cx - w2, yy, cx + w2 * 0.4f, yy + 11, 0.8f, to565(mulc(wf, 0.75f)));
          cv.lineFast(cx + w2, yy, cx - w2 * 0.4f, yy + 11, 0.8f, to565(mulc(wf, 0.75f)));
        }
        // scoops
        static const RGB fl[5] = {C(255, 160, 185), C(170, 235, 200), C(150, 95, 65), C(255, 245, 215), C(175, 170, 255)};
        const int n = iceScoops();
        float topX = cx, topY = ct;
        float topR = 0;
        for (int j = 0; j < n; j++) {
          const RGB c = fl[(sc.iv[1] + j * 2) % 5];
          float r = (16 - j * 1.2f) * Z;
          if (j == n - 1) for (int q = 0; q < 3; q++) if (u > iceLickT(q)) r -= 1.8f;
          if (sc.end == 0 && u > E) r *= 1 - seg(E + (n - 1 - j) * 0.4f, E + (n - 1 - j) * 0.4f + 0.5f);
          float sx2 = cx, sy2 = ctop - 5 - j * 22;
          if (sc.mid == 2) sx2 += j * 8 * sinf(u * 3) * seg(3, 4) * (1 - seg(E - 0.5f, E));
          if (sc.end == 1 && j == n - 1 && u > E) { const float d = u - E; sy2 += 320 * d * d; sx2 += 30 * d; }
          if (r < 0.5f || sy2 > 260) continue;
          cv.circle(sx2, sy2, r + 1.2f, to565(edgeFor(c)));
          if (props3d) ball(sx2, sy2, r, c); else cv.circle(sx2, sy2, r, to565(c));
          for (int q = 0; q < 4; q++) cv.circleFast(sx2 - r * 0.75f + q * r * 0.5f, sy2 + r * 0.72f, r * 0.26f, to565(c));  // the drippy rim
          if (sc.mid == 1 && j == n - 1 && u > bt(0, 4.4f)) cv.circle(sx2 + r * 0.7f, sy2 - r * 0.6f, r * 0.45f, to565(lidCol()));  // the big bite
          topX = sx2;
          topY = sy2 - r;
          topR = r;
        }
        // drips down the cone
        if (u > 2.5f && !(sc.end == 0 && u > E))
          for (int q = 0; q < 2; q++) {
            const float d = clampf((u - 2.5f - q * 2.2f) / 3.0f, 0, 1);
            if (d > 0) drop(cx - 11 + q * 19, ct + 5 + d * 22, 3, to565(fl[sc.iv[1] % 5]));
          }
        // the cherry
        bool cherryOn = topR > 4 && !(sc.end == 1 && u > E);
        float chx = topX, chy = topY - 4;
        if (sc.end == 2 && u > E) {  // Crumb takes it
          const float e = seg(E, E + 1.4f);
          chx = lerpf(topX, -20, e);
          chy = lerpf(topY - 4, 20, e) - 20 * sinf(e * PI_F);
        }
        if (sc.end == 2 && u > E - 0.6f && u < E + 1.5f) {
          const float in2 = seg(E - 0.6f, E);
          const float bx = u < E ? lerpf(340, topX + 12, in2) : chx + 12, by = u < E ? lerpf(30, topY - 10, in2) : chy - 8;
          drawCrumb(bx, by, fabsf(sinf(t * 18)), -1, false, 0.95f);
        }
        if (cherryOn || (sc.end == 2 && u > E)) {
          cv.lineFast(chx, chy - 5, chx + 5, chy - 14, 1.1f, cc(90, 150, 70));
          cv.circle(chx, chy, 6, cc(220, 30, 50));
          cv.circleFast(chx - 2, chy - 2, 1.6f, cc(255, 170, 170));
        }
        if (sc.mid == 1 && u > bt(0, 4.4f) + 0.6f && u < bt(0, 4.4f) + 2.0f)  // brain freeze: frosty sparkles
          for (int q = 0; q < 4; q++) { const float a2 = q * 1.57f + u * 3; star4(160 + cosf(a2) * 60, sc.fy - 40 + sinf(a2) * 20, 5, 0.24f, cc(190, 230, 255)); }
        break;
      }
      case SC_DANDELION: {
        if (back) break;
        const float B = bt(0, 2.0f), in = seg(0, 0.6f) * (1 - seg(sc.dur - 0.5f, sc.dur));
        const float hx = 122 + 2 * sinf(u * 1.3f), hy = 192 + (1 - in) * 70;
        const RGB sd = light ? C(150, 140, 165) : C(240, 240, 250);
        curve(118, 252, 116, (252 + hy) / 2, hx, hy + 4, 1.6f, cc(110, 175, 90), 6, false);
        cv.circleFast(hx, hy, 4.5f, cc(170, 160, 110));
        for (int i = 0; i < 18; i++) {
          const float a = i * 2 * PI_F / 18, rel = B + 0.04f * i * (0.5f + fracf(i * 0.618f));
          const float tx0 = hx + cosf(a) * 15, ty0 = hy + sinf(a) * 15;
          float sx, sy;
          if (u < rel) { cv.lineFast(hx, hy, tx0, ty0, 0.7f, to565(sd)); sx = tx0; sy = ty0; }
          else {  // away it drifts: mostly sideways, past the face rather than over the eyes
            const float s = u - rel, fi = fracf(i * 0.37f), gi = fracf(i * 0.71f);
            sx = tx0 - (50 + 60 * fi) * s - 4 * s * s;
            sy = ty0 - (12 + 26 * gi) * s + 6 * sinf(s * 3 + i);
            if (sc.end == 1 && i == 5 && u > B + 1.2f) {  // this one comes back... onto its nose
              const float e = seg(B + 1.2f, B + 2.4f), o = seg(B + 3.5f, B + 4.1f);
              const float s2 = 1.2f - 0.04f * i;
              const float ax = tx0 - (50 + 60 * fi) * s2 - 4 * s2 * s2, ay = ty0 - (12 + 26 * gi) * s2;
              sx = lerpf(ax, 160, e) + 120 * o;
              sy = lerpf(ay, sc.fy + 26, e) - 70 * o + 3 * sinf(u * 4) * (1 - e);
            }
            if (sy < -12 || sx < -12) continue;
            cv.lineFast(sx, sy, sx + 2, sy + 7, 0.7f, to565(sd));
            cv.circleFast(sx + 2, sy + 7.5f, 1.2f, cc(150, 120, 80));
          }
          for (int q = 0; q < 3; q++) cv.lineFast(sx, sy, sx + cosf(a + (q - 1) * 0.7f - PI_F / 2 * (u >= rel)) * 4.5f, sy + sinf(a + (q - 1) * 0.7f - PI_F / 2 * (u >= rel)) * 4.5f, 0.6f, to565(sd));
        }
        if (u > B - 0.1f && u < B + 0.6f) {  // the puff of breath
          const float d = (u - B + 0.1f) / 0.7f;
          for (int q = 0; q < 3; q++) { const float wx = lerpf(160, hx + 16, d) + q * 4, wy = sc.fy + 50 + (q - 1) * 9 + lerpf(0, hy - sc.fy - 50, d); cv.lineFast(wx, wy, wx - 10, wy + 2, 1.0f, fadeTo(C(235, 240, 250), 1 - d)); }
        }
        if (sc.end == 2 && u > B + 2.4f && u < B + 3.8f)
          for (int q = 0; q < 4; q++) { const float a2 = q * 1.57f + u * 2; star4(160 + cosf(a2) * 70, 40 + sinf(a2) * 20, 5 * (1 - seg(B + 3.2f, B + 3.8f)), 0.24f, cc(255, 235, 150)); }
        break;
      }
      case SC_GUM: {
        if (back) break;
        float mx, my;
        mouthAt(mx, my);
        const int end = gumEnd();
        const float P = gumPopT();
        const RGB pink = C(255, 140, 190);
        float r = gumR(), bx = mx, by = my;
        if (end == 2 && u > 3.6f) {  // tied off, floating away
          const float e = seg(3.6f, 5.8f);
          bx = mx + 30 * sinf(e * 5) * e;
          by = my - 190 * e;
          r = 4 + 34;
          cv.circleFast(bx, by + r + 1, 2.4f, to565(mulc(pink, 0.8f)));
        }
        if (r > 4.5f && (end == 2 || u < P)) {
          const float rx = r * (1 + 0.04f * sinf(u * 9)), ry = r * (1 - 0.04f * sinf(u * 9));
          cv.ellipse(bx, by, rx + 1, ry + 1, to565(mulc(pink, 0.85f)));
          cv.ellipse(bx, by, rx, ry, to565(pink));
          cv.ellipse(bx - r * 0.12f, by - r * 0.1f, rx * 0.8f, ry * 0.8f, to565(mixc(pink, C(255, 255, 255), 0.18f)));
          cv.ellipse(bx - r * 0.42f, by - r * 0.45f, r * 0.2f, r * 0.13f, cc(255, 240, 248));
        }
        if (end != 2 && u >= P) {
          const float d = u - P;
          if (d < 0.25f) burst(mx, my, 10 + 60 * d, 20 + 80 * d, 10, 1.4f, to565(pink));
          if (end == 0) {  // pink all over its face
            const float f = 1 - seg(P + 1.6f, P + 2.6f);
            static const float ox[9] = {-62, 58, -30, 26, -86, 84, 0, -48, 46}, oy[9] = {-70, -66, -24, -30, -18, -10, -96, 4, 2};
            static const float rs[9] = {12, 13, 9, 10, 8, 9, 7, 8, 9};
            for (int q = 0; q < 9; q++) cv.circle(mx + ox[q] * 1.1f, my + oy[q] + 6 * seg(P, P + 2.6f), rs[q] * 1.35f * f * seg(P, P + 0.12f), fadeTo(pink, f));
          }
        }
        break;
      }
      case SC_PAPERBALL: {
        if (back) break;
        const float in = seg(0, 0.5f) * (1 - seg(sc.dur - 0.5f, sc.dur));
        const float dy = (1 - in) * 70;
        float x, y;
        paperPos(x, y);
        const float T0 = bt(0, 2.0f), H = T0 + 0.9f;
        const bool inside = (sc.end == 0 && u > H + 0.1f) || (sc.end == 2 && u > H + 1.0f);
        // the bin: drawn after the ball when it's dropped in (it's inside)
        auto bin = [&]() {
          const uint16_t bc = cc(150, 162, 185), bd = cc(110, 120, 145);
          cv.tri(256, 180 + dy, 312, 180 + dy, 306, 241 + dy, bc);
          cv.tri(256, 180 + dy, 306, 241 + dy, 262, 241 + dy, bc);
          for (int q = 0; q < 4; q++) cv.lineFast(266 + q * 11.5f, 188 + dy, 268 + q * 10.5f, 236 + dy, 1.1f, bd);
          cv.rrect(251, 174 + dy, 66, 9, 4, bd);
        };
        if (!inside) bin();
        if (y < 252) {
          const float rot = u * (u > T0 ? 12 : 1);
          cv.circle(x, y + (u < T0 ? dy : 0), 12.5f, cc(170, 170, 180));
          cv.circle(x, y + (u < T0 ? dy : 0), 11.4f, cc(248, 246, 238));
          for (int q = 0; q < 3; q++) {
            const float a2 = rot + q * 2.1f;
            cv.lineFast(x + cosf(a2) * 2.5f, y + sinf(a2) * 2.5f + (u < T0 ? dy : 0), x + cosf(a2 + 0.8f) * 9, y + sinf(a2 + 0.8f) * 9 + (u < T0 ? dy : 0), 0.8f, cc(175, 175, 185));
          }
        }
        if (inside) bin();
        if ((sc.end == 0 && u > H && u < H + 1.6f) || (sc.end == 2 && u > H + 1.0f && u < H + 2.6f)) {
          const float s0 = sc.end == 0 ? H : H + 1.0f, s = 1 - seg(s0 + 1.0f, s0 + 1.6f);
          for (int q = 0; q < 4; q++) { const float a2 = q * 1.57f + u * 2.2f; star4(284 + cosf(a2) * 36, 166 + sinf(a2) * 18, 6 * s, 0.24f, cc(255, 230, 120)); }

        }
        break;
      }
    }
  }
