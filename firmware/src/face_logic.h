// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
#ifndef FACE_LOGIC_H
#define FACE_LOGIC_H
// The robot's face on the TFT, drawn by face engine v2 (face_engine_v2.h): a procedural, living
// face in 11 styles with ~170 expressions (face_lib.h) plus any /faces/*.txt packs on the SD card.
// A task renders ~30 frames a second into the sprite; the camera task (the TFT's only user) puts
// each frame on the screen. The legacy calls (setFace("happy"), setFaceWithMode, enterIdle,
// delayWithFace ...) keep working - legacy face names map to the new expressions.

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <SD_MMC.h>
#include "movement-sequences.h"  // FaceAnimMode
#include "face_engine_v2.h"

extern LGFX tft;
extern LGFX_Sprite sprite, sprite2;
extern int sdSizeMB;

// The TFT has ONE owner after setup: the camera task (visionTask). Drawing on it from two tasks
// crashes the board (one task can release the SPI bus lock the other holds - even
// with a shared lock around the drawing). Everyone else only draws the face into the sprite (in
// memory, under spriteMutex) and sets faceDirty; the camera task puts it on the screen.
SemaphoreHandle_t spriteMutex = nullptr;
volatile bool faceDirty = false;
// Double buffer: the camera task pushes *faceFront to the screen; the face task draws the next
// frame into the other sprite meanwhile, then swaps (under spriteMutex). Drawing and sending
// overlap, so the frame rate is set by the slower of the two, not their sum.
LGFX_Sprite* volatile faceFront = &sprite;
volatile float facePushMs = 0;
// During a conversation (gemini.h, walk mode) this task puts its frames on the screen itself, on core 1:
// the camera task normally does it (the screen's only user), but on core 0 next to the WiFi, the
// conversation's audio and the face search a push took 80-190 ms instead of 33 (the face at ~5 fps).
// In walk mode the camera never draws on the screen, so it still has one user at a time.
volatile bool faceSelfPush = false;
// The part of *faceFront that changed since the last frame on the screen (w 0 = all of it): the
// battery saver face sends only that
volatile int16_t faceDirtyX = 0, faceDirtyY = 0, faceDirtyW = 0, faceDirtyH = 0;
volatile bool faceSaver = false;  // the battery saver style is on
volatile bool faceSleepReq = false;  // gemini.h: "sleep" - the face falls asleep (head down, snores)
volatile bool faceKeepAwake = false;  // loop(): RC / talking - no boredom, no dozing off

fe::Face faceEng;
char currentFaceName[40] = "idle";  // (a fixed buffer, not a String: set from the loop, the web server and the wake word task, read by others - a String freed under a reader)
const unsigned char* const* currentFaceFrames = nullptr;  // legacy: set to nullptr to force a redraw
FaceAnimMode currentFaceMode = FACE_ANIM_LOOP;
bool faceAnimFinished = false;
bool idleActive = false;
volatile bool faceShowing = false;  // set by loop(): the face owns the screen (not the camera / battery screen)
// The boot screen (boot_screen.h): this task draws and pushes it itself while setup() runs (loop() isn't
// pushing frames yet), then the face. setup() fills in bootInfo as each part starts.
#include "boot_screen.h"
boot::Info bootInfo;
volatile bool bootOver = false;
volatile int faceFpsWanted = 30;
int faceFps = 30;                   // the Settings page's face frame rate
volatile int faceStyleReq = -1;     // a style change for the face task
volatile float faceFpsNow = 0, faceDrawMs = 0;
volatile int faceSdEntries = 0;     // expressions loaded from the SD card

// Profiling: time spent per primitive kind over the last frames (the /face JSON shows it)
volatile float faceProf[6] = {0, 0, 0, 0, 0, 0};  // clear rrect circle ellipse tri line (ms per frame)
volatile uint16_t faceProfN[6] = {0, 0, 0, 0, 0, 0};
float profAcc[6], profFrames = 0;
uint32_t profCnt[6];
struct ProfT { int k; unsigned long t0; ProfT(int kind) : k(kind), t0(micros()) {} ~ProfT() { profAcc[k] += (micros() - t0) / 1000.0f; profCnt[k]++; } };

// Draws with LovyanGFX into the sprite (smooth edges for rounded shapes, circles and lines)
struct SpriteCanvas : fe::Canvas {
  LGFX_Sprite* s;
  explicit SpriteCanvas(LGFX_Sprite* sp) : s(sp) { preferFast = true; }
  void clear(uint16_t c) override { ProfT pt(0); s->fillScreen(c); }
  void rrect(float x, float y, float w, float h, float r, uint16_t c) override { ProfT pt(1);
    if (w < 1 || h < 1) return;
    if (r < 1.5f) { s->fillRect(lroundf(x), lroundf(y), lroundf(w), lroundf(h), c); return; }
    s->fillSmoothRoundRect(lroundf(x), lroundf(y), lroundf(w), lroundf(h), lroundf(r), c);
  }
  void circle(float x, float y, float r, uint16_t c) override { ProfT pt(2);
    if (r < 0.5f) return;
    if (r < 2) { s->fillCircle(lroundf(x), lroundf(y), lroundf(r), c); return; }
    s->fillSmoothCircle(lroundf(x), lroundf(y), lroundf(r), c);
  }
  void ellipse(float x, float y, float rx, float ry, uint16_t c) override { ProfT pt(3);
    if (rx < 0.5f || ry < 0.5f) return;
    s->fillEllipse(lroundf(x), lroundf(y), lroundf(rx), lroundf(ry), c);
  }
  void tri(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c) override { ProfT pt(4);
    s->fillTriangle(lroundf(x0), lroundf(y0), lroundf(x1), lroundf(y1), lroundf(x2), lroundf(y2), c);
  }
  void line(float x0, float y0, float x1, float y1, float r, uint16_t c) override { ProfT pt(5);
    if (r < 1.3f) { fastLine(x0, y0, x1, y1, r, c); return; }
    s->drawWideLine(lroundf(x0), lroundf(y0), lroundf(x1), lroundf(y1), r, c);
  }
  // no edge smoothing: two triangles and round caps (about 25x faster than drawWideLine)
  void fastLine(float x0, float y0, float x1, float y1, float r, uint16_t c) {
    if (r <= 0.9f) { s->drawLine(lroundf(x0), lroundf(y0), lroundf(x1), lroundf(y1), c); return; }
    const float dx = x1 - x0, dy = y1 - y0, L = sqrtf(dx * dx + dy * dy);
    const int ri = (int)lroundf(r);
    if (L > 0.5f) {
      const float nx = -dy / L * r, ny = dx / L * r;
      s->fillTriangle(lroundf(x0 + nx), lroundf(y0 + ny), lroundf(x1 + nx), lroundf(y1 + ny), lroundf(x1 - nx), lroundf(y1 - ny), c);
      s->fillTriangle(lroundf(x0 + nx), lroundf(y0 + ny), lroundf(x1 - nx), lroundf(y1 - ny), lroundf(x0 - nx), lroundf(y0 - ny), c);
    }
    s->fillCircle(lroundf(x0), lroundf(y0), ri, c);
    s->fillCircle(lroundf(x1), lroundf(y1), ri, c);
  }
  void lineFast(float x0, float y0, float x1, float y1, float r, uint16_t c) override { ProfT pt(5); fastLine(x0, y0, x1, y1, r, c); }
  void circleFast(float x, float y, float r, uint16_t c) override { ProfT pt(2); if (r >= 0.5f) s->fillCircle(lroundf(x), lroundf(y), lroundf(r), c); }
};

portMUX_TYPE faceMux = portMUX_INITIALIZER_UNLOCKED;
char faceReq[40] = "";  // the next expression (set from any task; the face task applies it)

// Play scenes (face_scenes.h): a request for the face task, and what a running scene wants -
// where the head should look (loop() moves it) and a sound (loop() plays it)
char faceSceneReq[24] = "";
char faceEventReq[12] = "";             // a live event for the playing scene (desk mode: tap, loud, pat, arrived)
volatile int faceWeather = 0;           // desk mode: the weather (desk::Weather order) for the scenes
volatile bool faceUserHere = false;     // desk mode: someone in front of it (the hello scene)
volatile bool faceAmbient = false;      // desk mode: the weather drawn behind the face
volatile bool faceStandby = false, faceSbNight = false;  // desk mode: the StandBy screen (and its red night look)
volatile int faceSbStyle = 0;           // its style (fe::Face::sbStyle)
volatile int faceSbNightMode = 0;       // its red night look: 0 auto, 1 never, 2 always
volatile int faceBeatMid = 0, faceBeatTwist = 0, faceBeatEnd = 0;  // the playing scene's variants (the log)
volatile bool faceScenePlaying = false, faceHeadOn = false, faceOverlayOn = false;  // (overlay: an emote's props)
volatile float faceHeadX = 0, faceHeadY = 0;
char faceSceneName[16] = "";
char faceSfx[320] = "";
void faceSceneEvent(const char* name) {
  portENTER_CRITICAL(&faceMux);
  strlcpy(faceEventReq, name, sizeof(faceEventReq));
  portEXIT_CRITICAL(&faceMux);
}
void faceScene(const char* name) {
  portENTER_CRITICAL(&faceMux);
  strlcpy(faceSceneReq, name, sizeof(faceSceneReq));
  portEXIT_CRITICAL(&faceMux);
}
// a sound the face wants played (once); false if none
bool faceTakeSfx(char* out, size_t n) {
  portENTER_CRITICAL(&faceMux);
  const bool got = faceSfx[0] != 0;
  if (got) { strlcpy(out, faceSfx, n); faceSfx[0] = 0; }
  portEXIT_CRITICAL(&faceMux);
  return got;
}

// force: apply even if it's the same name (a fresh take) - the web page's "try" button
volatile unsigned long faceSetMs = 0;  // when a face was last picked (play scenes wait a while after)
void setFaceNamed(const char* name, bool force) {
  faceSetMs = millis();
  if (!force && !strcmp(currentFaceName, name)) return;
  portENTER_CRITICAL(&faceMux);
  strlcpy(currentFaceName, name, sizeof(currentFaceName));
  strlcpy(faceReq, name, sizeof(faceReq));
  portEXIT_CRITICAL(&faceMux);
  faceAnimFinished = false;
}
void setFace(const String& faceName) { setFaceNamed(faceName.c_str(), false); }

void setFaceMode(FaceAnimMode mode) {
  currentFaceMode = mode;
  faceAnimFinished = false;
}

void setFaceWithMode(const String& faceName, FaceAnimMode mode) {
  setFaceMode(mode);
  setFace(faceName);
}

void updateAnimatedFace() {}  // the face task animates by itself
void delayWithFace(unsigned long ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
void enterIdle() { idleActive = true; setFace("idle"); }
void exitIdle() { idleActive = false; }
void updateIdleBlink() {}  // blinks are part of every expression

// Eyes follow a point (-1..1, e.g. where a face was seen)
void faceLookAt(float x, float y, float holdS = 0.6f) { faceEng.lookAt(x, y, holdS); }

int faceStyleFromName(const char* n) {
  for (int i = 0; i < fe::ST_NUM; i++) if (!strcasecmp(n, fe::STYLE_NAMES[i])) return i;
  if (!strcasecmp(n, "pet")) return fe::ST_CAT;
  return -1;
}

void faceTask(void*) {
  SpriteCanvas cvA(&sprite), cvB(&sprite2);
  unsigned long last = millis(), fpsT0 = millis();
  int frames = 0;
  float drawSum = 0;
  for (;;) {
    const bool saver = faceEng.style == fe::ST_SAVER;
    faceSaver = saver;
    // saver: the engine ticks 12 times a second (a frame is drawn only when something moved)
    // (a play scene's props move every frame: then the usual rate)
    const bool sceneOn = faceEng.playing();
    const int fps = !bootOver ? 30 : saver && !sceneOn ? min(12, max(5, (int)faceFpsWanted)) : max(5, (int)faceFpsWanted);  // (the boot screen: full speed, whatever the style)
    static unsigned long nextAt = 0;
    const unsigned long nowW = millis();
    const long wait = (long)(nextAt - nowW);
    vTaskDelay(pdMS_TO_TICKS(wait > 1 ? wait : 1));
    nextAt = max(nextAt + 1000 / fps, (unsigned long)millis());
    const unsigned long now = millis();
    const float dt = (now - last) / 1000.0f;
    last = now;
    char req[40] = "", sreq[24] = "";
    portENTER_CRITICAL(&faceMux);
    if (faceReq[0]) { strlcpy(req, faceReq, sizeof(req)); faceReq[0] = 0; }
    if (faceSceneReq[0]) { strlcpy(sreq, faceSceneReq, sizeof(sreq)); faceSceneReq[0] = 0; }
    char ereq[12] = "";
    if (faceEventReq[0]) { strlcpy(ereq, faceEventReq, sizeof(ereq)); faceEventReq[0] = 0; }
    portEXIT_CRITICAL(&faceMux);
    faceEng.weatherNow = faceWeather;
    faceEng.userHere = faceUserHere;
    faceEng.ambientOn = faceAmbient;
    faceEng.standby = faceStandby;
    faceEng.sbNight = faceSbNight;
    faceEng.sbStyle = faceSbStyle;
    faceEng.sbNightMode = faceSbNightMode;
    if (ereq[0]) faceEng.sceneEvent(ereq);
    // While a scene plays, "idle" (the robot at rest) doesn't interrupt it; anything else does.
    // An emote's props (overlay) stay while the emote changes its face.
    if (req[0]) {
      const bool scene = faceEng.playing() && !faceEng.overlayOn();
      if (!(scene && !strcmp(req, "idle"))) {
        if (scene) faceEng.stopScene();
        faceEng.set(req);
      }
    }
    if (sreq[0] && faceEng.play(sreq)) { int m, tw, e; faceEng.beats(m, tw, e); faceBeatMid = m; faceBeatTwist = tw; faceBeatEnd = e; }
    if (faceStyleReq >= 0) { faceEng.style = faceStyleReq; faceStyleReq = -1; }
    // a sleep request: now or never (left pending, it would fall asleep out of the blue later)
    if (faceSleepReq) { faceSleepReq = false; if (!faceKeepAwake) faceEng.sleepNow(); }
    if (faceKeepAwake) faceEng.keepAwake();
    faceEng.sleepStep();
    faceEng.step(dt);
    faceOverlayOn = faceEng.overlayOn();
    faceScenePlaying = faceEng.playing() && !faceOverlayOn;
    faceHeadOn = (faceScenePlaying || faceEng.asleep) && faceEng.headOn;
    faceHeadX = faceEng.headX;
    faceHeadY = faceEng.headY;
    {
      char sf[320];
      const bool got = faceEng.takeSfx(sf, sizeof(sf));
      portENTER_CRITICAL(&faceMux);
      strlcpy(faceSceneName, faceEng.sceneName(), sizeof(faceSceneName));
      if (got) strlcpy(faceSfx, sf, sizeof(faceSfx));
      portEXIT_CRITICAL(&faceMux);
    }
    if (now - fpsT0 >= 2000) {
      faceFpsNow = frames * 1000.0f / (now - fpsT0);
      faceDrawMs = frames ? drawSum / frames : 0;
      frames = 0;
      drawSum = 0;
      fpsT0 = now;
    }
    static bool saverFull = true;
    static float brightWas = -1;  // (a new brightness: the battery saver face redraws all of it)
    if (faceBright != brightWas) { brightWas = faceBright; saverFull = true; }
    static uint32_t colorWas = 0;  // (a new saver eye colour: all of it again)
    if (saverColor != colorWas) { colorWas = saverColor; saverFull = true; }
    static uint8_t lastStyle = 255;
    if (faceEng.style != lastStyle) { lastStyle = faceEng.style; saverFull = true; }
    if (!bootOver) {
      static unsigned long bt0 = millis();
      static float finishedAt = -1;
      const float bt = (millis() - bt0) / 1000.0f;
      if (bootInfo.finished && finishedAt < 0) finishedAt = bt;
      bool done = false;
      static uint32_t bFrames = 0, bDrawUs = 0, bPushUs = 0;
      const uint32_t bd0 = micros();
      const float reveal = boot::draw(cvA, bt, bootInfo, finishedAt, done);
      if (reveal >= 0) {  // the end: the real face (this style), shown through the growing octagon
        if (saver) {
          fe::Face::SaverGeo g;
          faceEng.saverGeo(g);
          faceEng.drawSaver(cvA, g);
        } else {
          faceEng.draw(cvA);
        }
        boot::revealMask(cvA, reveal);
      }
      const uint32_t bd1 = micros();
      {  // only what changed (bFrames < 2: the first frames whole)
        int16_t rr[4][4];
        const int nr = bFrames < 2 ? 0 : boot::dirtyRects(bt, finishedAt, rr);
        if (nr == 0) sprite.pushSprite(0, 0);
        for (int i = 0; i < nr; i++) {
          tft.setClipRect(rr[i][0], rr[i][1], rr[i][2], rr[i][3]);
          sprite.pushSprite(0, 0);
          tft.clearClipRect();
        }
      }
      bFrames++;
      bDrawUs += bd1 - bd0;
      bPushUs += micros() - bd1;
      if (done) {
        bootOver = true;  // the face from here on (its first frame: what the reveal just showed)
        saverFull = true;
        xlogSys("BOOT screen: %u frames in %.1f s (%.1f fps), draw %u ms, push %u ms a frame", (unsigned)bFrames, bt,
                bFrames / fmaxf(bt, 0.1f), (unsigned)(bDrawUs / 1000 / max(1u, (unsigned)bFrames)),
                (unsigned)(bPushUs / 1000 / max(1u, (unsigned)bFrames)));
      }
      continue;
    }
    if (!faceShowing || !spriteMutex) { saverFull = true; continue; }
    if (faceDirty) continue;  // the last frame isn't on the screen yet: don't queue up
    // draw into the buffer that isn't on its way to the screen (no lock needed for that)
    const bool frontA = faceFront == &sprite;
    const unsigned long t0 = micros();
    int16_t dx = 0, dy = 0, dw = 0, dh = 0;  // w 0: the whole frame goes to the screen
    if (saver) {
      // Battery saver: a frame only when the eyes changed by a pixel, and only the rectangle that
      // changed goes to the screen (the whole frame again every 30 s, and after the camera / a
      // style change)
      static fe::Face::SaverGeo last;
      static unsigned long fullMs = 0;
      fe::Face::SaverGeo g;
      faceEng.saverGeo(g);
      const bool full = saverFull || sceneOn || now - fullMs > 30000;  // a scene: its props move every frame
      if (!full && !memcmp(&g, &last, sizeof(g))) continue;  // nothing moved: no frame at all
      if (full) {
        fullMs = now;
        saverFull = false;
      } else {
        int a0, b0, a1, b1, c0, d0, c1, d1;
        fe::Face::saverBox(last, a0, b0, a1, b1);
        fe::Face::saverBox(g, c0, d0, c1, d1);
        dx = min(a0, c0);
        dy = min(b0, d0);
        dw = max(a1, c1) - dx;
        dh = max(b1, d1) - dy;
        if (dw <= 0 || dh <= 0) dx = dy = dw = dh = 0;
      }
      last = g;
      faceEng.drawSaver(frontA ? cvB : cvA, g);
    } else {
      faceEng.draw(frontA ? cvB : cvA);
    }
    drawSum += (micros() - t0) / 1000.0f;
    if (++profFrames >= 20) {
      for (int i = 0; i < 6; i++) { faceProf[i] = profAcc[i] / profFrames; faceProfN[i] = profCnt[i] / profFrames; profAcc[i] = 0; profCnt[i] = 0; }
      profFrames = 0;
    }
    xSemaphoreTake(spriteMutex, portMAX_DELAY);
    faceFront = frontA ? &sprite2 : &sprite;
    faceDirtyX = dx;
    faceDirtyY = dy;
    faceDirtyW = dw;
    faceDirtyH = dh;
    faceDirty = true;
    if (faceSelfPush) {
      faceDirty = false;
      const unsigned long p0 = micros();
      if (dw > 0 && dh > 0) {
        tft.setClipRect(dx, dy, dw, dh);
        faceFront->pushSprite(0, 0);
        tft.clearClipRect();
      } else {
        faceFront->pushSprite(0, 0);
      }
      facePushMs = facePushMs * 0.9f + (micros() - p0) / 1000.0f * 0.1f;
    }
    xSemaphoreGive(spriteMutex);
    frames++;
  }
}

// Extra expression packs on the SD card: every /faces/*.txt, same format as face_lib.h. They're
// kept in PSRAM (the library points into them) and win over built-in ones with the same name.
void faceLoadSd() {
  if (sdSizeMB < 0) return;
  if (!SD_MMC.exists("/faces")) { SD_MMC.mkdir("/faces"); return; }
  File d = SD_MMC.open("/faces");
  if (!d || !d.isDirectory()) return;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    const String n = f.name();
    if (f.isDirectory() || !n.endsWith(".txt") || f.size() > 200000) continue;
    char* buf = (char*)heap_caps_malloc(f.size() + 1, MALLOC_CAP_SPIRAM);
    if (!buf) continue;
    const size_t got = f.read((uint8_t*)buf, f.size());
    buf[got] = 0;
    faceSdEntries += fe::libAdd(buf);
  }
}

// Call after the SD card is mounted (packs load before the task starts reading the library)
void faceBegin(int style) {
  fe::rvSeed(esp_random());
  fe::libInit();
  faceLoadSd();
  if (style >= 0 && style < fe::ST_NUM) faceEng.style = style;
  faceEng.set("idle");
  xTaskCreatePinnedToCore(faceTask, "face", 6144, nullptr, 1, nullptr, 1);  // (~3.4 KB used)
}

#endif
