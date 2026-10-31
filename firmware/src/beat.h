// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
// ---- Beat tracking from the robot's mic: "dance to any song" ----
// Fed by the wake word's listen loop (it holds the mic whenever the robot is idle): 256 samples at 16 kHz = one
// 16 ms frame. Per frame an onset strength (how much louder it just got - the bass, plus the whole band); the last
// ~8 s of it. Every ~0.5 s: the tempo = the autocorrelation peak between 70 and 185 BPM (a soft preference for
// ~120, so it doesn't lock on double / half time), the phase = where a comb of beats at that tempo hits the most
// onsets. The dance reads: the beat period, when the next beat comes (millis), how sure it is, how loud the music is.
#pragma once
#include <Arduino.h>
#include <math.h>

namespace beat {

const int N = 512;          // ~8.2 s of frames
const float FRAME_MS = 16.0f;
const int LAT_MS = 40;      // the mic's DMA: a frame is heard this long before we get it
// (in PSRAM: 4 KB of internal RAM is too precious)
float* ons = nullptr;       // the onset strength per frame
float* lowE = nullptr;      // the energy per frame (for "is there music")
int head = 0, count = 0;
float floorE = 0;          // the room's quiet level (bass + mids): kept across resets, follows slowly (minutes)
float recentE = 0;         // the last ~2 s (smoothed)
unsigned long lastFrameMs = 0;
float lpS = 0, lp2 = 0, ePrevLow = 0, ePrevMid = 0, ePrevHi = 0;

volatile float periodMs = 0;     // 0 = no tempo yet
volatile float bpm = 0;
volatile float conf = 0;         // 0..1
volatile float level = 0;        // the music's bass level (dB-ish above the quiet floor)
volatile unsigned long lastBeatMs = 0;
volatile unsigned long estMs = 0; // when the last estimate was made
// What the music is like (the last ~2 s, for the dance's choices): each band's level (dB), how busy
// it is (onsets a frame: hi-hats, fast drums), and how much louder the last ~2 s are than the last ~10 s (a drop)
volatile float bassDb = 0, midDb = 0, hiDb = 0, busy = 0, rise = 0;
float slowE = 0;
// Listening WHILE dancing: its own servos are loud next to the mic and land on its own beats - heard,
// they'd make it dance to itself. So: the dance mutes the ears for each move (+ a tail) and they listen in the
// gaps; the tempo estimate is frozen meanwhile (the dance runs its own beat clock) and the level / character
// keep coming from the gaps. hintFrames: a re-check only looks near the tempo it already has (faster, surer).
volatile unsigned long muteUntilMs = 0;
volatile bool frozen = false;
volatile float hintFrames = 0;
volatile bool enabled = true;

void reset() {
  head = count = 0;
  periodMs = bpm = conf = 0;
  lastBeatMs = estMs = 0;
}

inline float at(int back) {  // the onset `back` frames ago (0 = the newest)
  return ons[(head - 1 - back + 2 * N) % N];
}

void estimate() {
  const int W = min(count, 384);
  if (frozen || W < (hintFrames > 0 ? 100 : 160)) return;
  // the mean of the window (the autocorrelation of what's above it)
  float mean = 0;
  for (int i = 0; i < W; i++) mean += at(i);
  mean /= W;
  float r0 = 0;
  for (int i = 0; i < W; i++) { const float v = at(i) - mean; r0 += v * v; }
  if (r0 <= 1e-6f) return;
  float best = -1, r[60] = {0};
  int bl = 0;
  for (int L = 20; L <= 54 && 2 * L < W; L++) {  // 187 .. 69 BPM
    float s = 0;
    for (int i = 0; i + L < W; i++) s += (at(i) - mean) * (at(i + L) - mean);
    r[L] = s / r0;
  }
  for (int L = 20; L <= 54 && 2 * L < W; L++) {
    if (hintFrames > 0 && fabsf(L - hintFrames) > hintFrames * 0.1f + 1) continue;  // a re-check: near the known tempo
    const float b = 60000.0f / (L * FRAME_MS);
    const float o = log2f(b / 120.0f) / 0.7f;
    const float score = r[L] * expf(-0.5f * o * o) + (2 * L <= 54 ? 0.5f * r[2 * L] : 0);  // (its double: the bar)
    if (score > best) { best = score; bl = L; }
  }
  if (bl < 21 || bl > 53) { conf = 0; return; }
  // a finer peak (parabola through the neighbours)
  const float a = r[bl - 1], b = r[bl], c = r[bl + 1];
  const float d = (a - 2 * b + c) != 0 ? 0.5f * (a - c) / (a - 2 * b + c) : 0;
  const float P = bl + constrain(d, -0.5f, 0.5f);  // frames
  // the phase: a comb of 8 beats against the onsets
  float bestS = -1;
  int bestPh = 0;
  for (int ph = 0; ph < (int)P; ph++) {
    float s = 0;
    for (int k = 0; k < 8; k++) {
      const int idx = (int)lroundf(ph + k * P);
      if (idx >= W) break;
      s += at(idx) * (k < 4 ? 1.0f : 0.7f);
    }
    if (s > bestS) { bestS = s; bestPh = ph; }
  }
  periodMs = P * FRAME_MS;
  bpm = 60000.0f / periodMs;
  float rm = 0, rs = 0;  // the other lags: mean / spread
  int nl = 0;
  for (int L = 20; L <= 54 && 2 * L < W; L++) { rm += r[L]; nl++; }
  rm /= max(1, nl);
  for (int L = 20; L <= 54 && 2 * L < W; L++) rs += (r[L] - rm) * (r[L] - rm);
  rs = sqrtf(rs / max(1, nl));
  const float z = rs > 1e-4f ? (b - rm) / rs : 0;  // the peak, in spreads above the rest
  conf = constrain((z - 1.0f) / 2.5f, 0.0f, 1.0f);   // z 1 -> 0, z 3.5 -> 1
  lastBeatMs = lastFrameMs - LAT_MS - (unsigned long)(bestPh * FRAME_MS);
  estMs = millis();
  // the music's level: the last ~2 s against the room's quiet level (not the quietest part of this window: right
  // after a reset in the middle of a song that would be the song itself)
  level = floorE > 0 ? 10 * log10f((recentE + 1e-3f) / (floorE + 1e-3f)) : 0;
}

// one frame of mic audio (16-bit, 16 kHz)
void feed(const int16_t* x, int n) {
  if (!enabled || n <= 0) return;
  if (!ons) {
    ons = (float*)heap_caps_calloc(N, sizeof(float), MALLOC_CAP_SPIRAM);
    lowE = (float*)heap_caps_calloc(N, sizeof(float), MALLOC_CAP_SPIRAM);
    if (!ons || !lowE) { enabled = false; return; }
  }
  // three bands - the bass alone hears nothing from a phone's speaker (no bass): low < ~200 Hz, mid ~200 Hz - 2 kHz,
  // high > 2 kHz (the hi-hats); the onset = how much each band just got louder (log energy), added up
  float eLow = 0, eMid = 0, eHi = 0;
  for (int i = 0; i < n; i++) {
    const float v = x[i];
    lpS += 0.075f * (v - lpS);  // ~200 Hz
    lp2 += 0.55f * (v - lp2);   // ~2 kHz
    eLow += lpS * lpS;
    eMid += (lp2 - lpS) * (lp2 - lpS);
    eHi += (v - lp2) * (v - lp2);
  }
  eLow = 10 * log10f(eLow / n + 1.0f);
  const float eM = 10 * log10f(eMid / n + 1.0f), eH = 10 * log10f(eHi / n + 1.0f);
  const float o = fmaxf(0, eLow - ePrevLow) + fmaxf(0, eM - ePrevMid) + 0.7f * fmaxf(0, eH - ePrevHi);
  ePrevLow = eLow;
  ePrevMid = eM;
  ePrevHi = eH;
  if ((long)(millis() - muteUntilMs) < 0) return;  // its own servos moving: not the music (the filters above stay continuous)
  ons[head] = o;
  const float e = powf(10, eLow / 10) + eMid / n;  // (the music's level: bass + mids)
  lowE[head] = e;
  recentE += (e - recentE) * 0.008f;  // ~2 s
  slowE += (e - slowE) * 0.0016f;     // ~10 s
  bassDb += (eLow - bassDb) * 0.008f;
  midDb += (eM - midDb) * 0.008f;
  hiDb += (eH - hiDb) * 0.008f;
  busy += (o - busy) * 0.008f;
  rise = slowE > 0 ? 10 * log10f((recentE + 1e-3f) / (slowE + 1e-3f)) : 0;
  if (frozen && floorE > 0) level = 10 * log10f((recentE + 1e-3f) / (floorE + 1e-3f));  // (dancing: the level live)
  if (floorE <= 0 || recentE < floorE) floorE = recentE;  // down at once...
  else floorE *= 1.00004f;                                 // ...up slowly (~+1.5 dB a minute): a new, louder room
  head = (head + 1) % N;
  if (count < N) count++;
  lastFrameMs = millis();
  static int sinceEst = 0;
  if (++sinceEst >= 32) {  // ~every 0.5 s
    sinceEst = 0;
    estimate();
  }
}

// the next beat after `t` (millis), 0 if no tempo
unsigned long nextBeat(unsigned long t) {
  if (periodMs <= 0 || !lastBeatMs) return 0;
  const float p = periodMs;
  const long d = (long)(t - lastBeatMs);
  const long k = d < 0 ? 0 : (long)(d / p) + 1;
  return lastBeatMs + (unsigned long)lroundf(k * p);
}

}  // namespace beat
