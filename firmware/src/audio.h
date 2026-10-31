// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
#pragma once
// I2S speaker (MAX98357-style amp) and I2S mic (INMP441-style) sharing one clock pair.
// One I2S port in full duplex: BCLK 45 and WS 46 go to both, amp DIN 48, mic SD 47.
// GPIO 48 is also the board's WS2812 LED, so it flickers while sound plays (harmless).
// Tests: GET /audio?tone=HZ&ms=&vol=  /audio?mic=MS  /audio?echo=MS
// SD card: POST /sdPut?name=/music/x.wav (body = file), GET /sdList?dir=/music
// Player (16-bit PCM WAV from the SD card, in the background): /audio?play=/music/x.wav&vol=0.4
//   /audio?stop=1   /audio?vol=0.3 (while playing)   /audio?status=1
#include <Arduino.h>
#include <math.h>
#include <algorithm>
#include "driver/i2s.h"
#include "esp_heap_caps.h"
#include <SD_MMC.h>
extern int sdSizeMB;  // main.cpp: card size in MB, -1 = none
namespace beat { void feed(const int16_t* x, int n); void reset(); }  // beat.h (included later): a playing song feeds it
void prefPut(const char* key, int32_t val, bool isBool);  // main.cpp: a setting saved to NVS
extern bool emoteSndOn;  // main.cpp: every emote's own sound (/audio?emosnd=1|0)

#define AUD_BCLK 45
#define AUD_WS   46
#define AUD_DOUT 48
#define AUD_DIN  47

namespace aud {

const int RATE = 16000;
const int CHUNK = 256;  // stereo frames per read / write
bool on = false;
bool onRxOnly = false;  // started by beginListen (the wake word): no speaker side
bool onMic = false;  // started with the robot's mic too (receive DMA: 12 KB more internal RAM)
volatile bool busy = false;
// One volume for everything the speaker plays - songs, scene sounds, beeps, the phone's voice
// (the page's Volume slider, /audio?master=0..1, saved)
volatile float masterVol = 0.7f;

// Sounds, songs and the phone's voice only send: the receive side (the robot's mic, only the
// /audio mic tests use it) stays off - its DMA buffers are 12 KB of scarce internal RAM.
// bufs: DMA buffers each way (6 x 2 KB; the conversation uses 4: less internal RAM, less delay)
// The wake word listens on this port while nothing else uses it (wakeword.h): anyone else starting it
// asks for it back first, and gets it within one read (~16 ms)
volatile bool wwHeld = false, wwYield = false;
bool begin(int rate = RATE, bool mic = false, int bufs = 6) {
  if (wwHeld) {
    wwYield = true;
    for (int i = 0; i < 120 && wwHeld; i++) delay(5);
    // (still held: taking the port from under it would let its end() later cut this sound off)
    if (wwHeld) { xlogSys("AUDIO: the wake word didn't let go of the port"); return false; }
  }
  if (on && !onRxOnly && (onMic || !mic)) return true;
  if (on) {  // sending only (or the wake word's mic only), and now more is wanted: start again
    i2s_driver_uninstall(I2S_NUM_0);
    on = false;
  }
  i2s_config_t c = {};
  c.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | (mic ? I2S_MODE_RX : 0));
  c.sample_rate = rate;
  c.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;  // the mic sends 24 bits in a 32-bit slot
  c.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  c.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  c.intr_alloc_flags = 0;
  c.dma_buf_count = bufs;
  c.dma_buf_len = CHUNK;
  c.use_apll = false;
  c.tx_desc_auto_clear = true;  // silence when nothing is written
  // a driver left behind (on = false but still installed: "i2s start failed" until a restart):
  // removed, then once more
  esp_err_t ie = i2s_driver_install(I2S_NUM_0, &c, 0, nullptr);
  if (ie != ESP_OK) {
    i2s_driver_uninstall(I2S_NUM_0);
    ie = i2s_driver_install(I2S_NUM_0, &c, 0, nullptr);
  }
  // short of internal RAM (the DMA buffers): fewer of them rather than no sound at all
  while (ie == ESP_ERR_NO_MEM && c.dma_buf_count > 2) {
    c.dma_buf_count = max(2, c.dma_buf_count / 2);
    ie = i2s_driver_install(I2S_NUM_0, &c, 0, nullptr);
  }
  if (ie != ESP_OK) {
    xlogSys("AUDIO: I2S start failed (%s), %u KB internal", esp_err_to_name(ie), (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    return false;
  }
  i2s_pin_config_t p = {};
  p.mck_io_num = I2S_PIN_NO_CHANGE;
  p.bck_io_num = AUD_BCLK;
  p.ws_io_num = AUD_WS;
  p.data_out_num = AUD_DOUT;
  p.data_in_num = AUD_DIN;
  if (!mic) p.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(I2S_NUM_0, &p) != ESP_OK) {
    i2s_driver_uninstall(I2S_NUM_0);
    return false;
  }
  if (mic) { gpio_pullup_dis((gpio_num_t)AUD_DIN); gpio_pulldown_en((gpio_num_t)AUD_DIN); }  // (the mic's idle data line reads 0, not noise)
  i2s_zero_dma_buffer(I2S_NUM_0);
  on = true;
  onMic = mic;
  onRxOnly = false;  // (else, after the wake word's port, every later sound would start the driver again)
  return true;
}

// The wake word: the mic alone (no speaker side) and 3 DMA buffers - ~6 KB of internal RAM instead of
// the full-duplex ~16 KB. The speaker's data pin is held low meanwhile: zeros, so the amplifier is silent.
bool beginListen(int rate, int bufs = 3) {
  if (on) return true;
  i2s_config_t c = {};
  c.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  c.sample_rate = rate;
  c.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  c.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  c.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  c.intr_alloc_flags = 0;
  c.dma_buf_count = bufs;
  c.dma_buf_len = CHUNK;
  c.use_apll = false;
  pinMode(AUD_DOUT, OUTPUT);
  digitalWrite(AUD_DOUT, LOW);
  // a driver left behind (on = false but still installed: "i2s start failed" until a restart):
  // removed, then once more
  esp_err_t ie = i2s_driver_install(I2S_NUM_0, &c, 0, nullptr);
  if (ie != ESP_OK) {
    i2s_driver_uninstall(I2S_NUM_0);
    ie = i2s_driver_install(I2S_NUM_0, &c, 0, nullptr);
  }
  // short of internal RAM (the DMA buffers): fewer of them rather than no sound at all
  while (ie == ESP_ERR_NO_MEM && c.dma_buf_count > 2) {
    c.dma_buf_count = max(2, c.dma_buf_count / 2);
    ie = i2s_driver_install(I2S_NUM_0, &c, 0, nullptr);
  }
  if (ie != ESP_OK) {
    xlogSys("AUDIO: I2S start failed (%s), %u KB internal", esp_err_to_name(ie), (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    return false;
  }
  i2s_pin_config_t p = {};
  p.mck_io_num = I2S_PIN_NO_CHANGE;
  p.bck_io_num = AUD_BCLK;
  p.ws_io_num = AUD_WS;
  p.data_out_num = I2S_PIN_NO_CHANGE;
  p.data_in_num = AUD_DIN;
  if (i2s_set_pin(I2S_NUM_0, &p) != ESP_OK) {
    i2s_driver_uninstall(I2S_NUM_0);
    return false;
  }
  gpio_pullup_dis((gpio_num_t)AUD_DIN);
  gpio_pulldown_en((gpio_num_t)AUD_DIN);  // (the INMP441 leaves its data line floating between its 24 bits: read as 0)
  on = onMic = onRxOnly = true;
  return true;
}

void end() {
  if (!on) return;
  // silence out first: removing the driver mid-sound leaves the amplifier a cut wave - a click or a short
  // buzz (the wake word takes the port back after every sound)
  i2s_zero_dma_buffer(I2S_NUM_0);
  delay(20);
  i2s_driver_uninstall(I2S_NUM_0);
  on = onMic = onRxOnly = false;
}

// Plays mono samples (-1..1 scaled by the caller) on both channels
void playMono(const int16_t* s, int n, int32_t* buf) {
  for (int i = 0; i < n;) {
    const int k = min(CHUNK, n - i);
    for (int j = 0; j < k; j++) buf[2 * j] = buf[2 * j + 1] = (int32_t)(s[i + j] * masterVol) << 16;
    size_t w = 0;
    i2s_write(I2S_NUM_0, buf, k * 8, &w, portMAX_DELAY);
    i += k;
  }
}

void tone(float hz, int ms, float vol, int32_t* buf) {
  const int n = RATE * ms / 1000, fade = RATE / 100;  // 10 ms fade in / out: no clicks
  float ph = 0;
  const float dph = 2 * PI * hz / RATE;
  for (int i = 0; i < n;) {
    const int k = min(CHUNK, n - i);
    for (int j = 0; j < k; j++, i++) {
      float g = vol * masterVol;
      if (i < fade) g *= (float)i / fade;
      if (n - i < fade) g *= (float)(n - i) / fade;
      const int32_t v = (int32_t)(sinf(ph) * g * 2147483000.0f);
      ph += dph;
      if (ph > 2 * PI) ph -= 2 * PI;
      buf[2 * j] = buf[2 * j + 1] = v;
    }
    size_t w = 0;
    i2s_write(I2S_NUM_0, buf, k * 8, &w, portMAX_DELAY);
  }
}

struct Level { double sum = 0, sum2 = 0; int32_t peak = 0; uint32_t n = 0, zeros = 0; };

// Reads the mic for ms; per channel: DC, RMS (DC removed), peak. Optionally keeps the louder
// channel (chosen after the first 200 ms) as 16-bit samples in rec[] (up to recMax).
uint32_t micCarry = 0;  // the right slot before the next left one (see fixMic)

// Reads and throws away ms of mic input
void drop(int ms, int32_t* buf) {
  for (int skip = RATE * ms / 1000; skip > 0;) {
    size_t r = 0;
    i2s_read(I2S_NUM_0, buf, CHUNK * 8, &r, portMAX_DELAY);
    skip -= r / 8;
    if (r >= 8) micCarry = (uint32_t)buf[r / 4 - 1];  // the last right slot: fixMic() needs it
  }
}

// The mic's samples arrive one bit early on this port: the sign bit of each left sample is the
// last bit of the right slot before it (checked on every sample of a loud test tone). Read that
// way the whole range is right; ignoring it, anything past half scale flipped sign (crackle when
// someone spoke close to the mic). Rebuilds the in-place words: [2j] = the mic, [2j+1] = 0.
void fixMic(int32_t* buf, int k) {
  for (int j = 0; j < k; j++) {
    const uint32_t l = (uint32_t)buf[2 * j], r = (uint32_t)buf[2 * j + 1];
    buf[2 * j] = (int32_t)(((micCarry & 1u) << 31) | (l >> 1));
    buf[2 * j + 1] = 0;
    micCarry = r;
  }
}

void listen(int ms, Level lv[2], int32_t* buf, int16_t* rec, int recMax, int* recN, int* recCh, bool warm = true) {
  const int n = RATE * ms / 1000;
  int got = 0, keep = -1, rn = 0;
  // the mic needs ~250 ms to start (2^18 clocks) and its DC filter about a second more: skip 1.2 s
  if (warm) drop(1200, buf);
  while (got < n) {
    size_t r = 0;
    i2s_read(I2S_NUM_0, buf, CHUNK * 8, &r, portMAX_DELAY);
    const int k = r / 8;
    fixMic(buf, k);
    for (int j = 0; j < k; j++) {
      for (int c = 0; c < 2; c++) {
        const int32_t v = buf[2 * j + c] >> 8;  // 24-bit sample
        lv[c].sum += v;
        lv[c].sum2 += (double)v * v;
        if (abs(v) > lv[c].peak) lv[c].peak = abs(v);
        if (v == 0) lv[c].zeros++;
        lv[c].n++;
      }
      keep = 0;  // the mic is on the left slot (its L/R pin is tied low)
      if (rec && keep >= 0 && rn < recMax) rec[rn++] = (int16_t)constrain(buf[2 * j + keep] >> 16, -32768, 32767);
    }
    got += k;
  }
  if (recN) *recN = rn;
  if (recCh) *recCh = keep;
}

int levelJson(char* out, int len, const char* name, const Level& l) {
  const double mean = l.n ? l.sum / l.n : 0;
  const double var = l.n ? max(0.0, l.sum2 / l.n - mean * mean) : 0;
  const double rms = sqrt(var);
  const double db = rms > 0 ? 20 * log10(rms / 8388608.0) : -200;
  return snprintf(out, len, "\"%s\":{\"rms\":%.0f,\"dbfs\":%.1f,\"peak\":%ld,\"dc\":%.0f,\"zeroPct\":%.0f}", name, rms,
                  db, (long)l.peak, mean, l.n ? 100.0 * l.zeros / l.n : 0);
}

// ---- WAV player: a background task streams the file from the SD card ----
volatile bool playing = false, stopReq = false;
volatile float playVol = 0.4f;
char playPath[96] = "";
volatile uint32_t playPosMs = 0, playLenMs = 0;
volatile uint32_t playStartMs = 0;  // where the next play() starts (resume after a pause)
volatile bool playEnded = false;    // the last song played to its end (not stopped, not an error)
volatile float playLvl = 0;         // the song's loudness now (0..1, for the faces)
char playErr[48] = "";

// the song feeds the beat tracker (beat.h: 16 kHz frames) - the mic can't hear it: the speaker has the I2S port
int16_t beatFrame[256];
int beatN = 0;
uint32_t beatAcc = 0;
inline void beatFeedSample(int32_t m, uint32_t rate) {
  beatAcc += 16000;
  if (beatAcc < rate) return;
  beatAcc -= rate;
  beatFrame[beatN++] = (int16_t)constrain(m, -32767, 32767);
  if (beatN == 256) { beat::feed(beatFrame, 256); beatN = 0; }
}

void playerTask(void*) {
  File f = SD_MMC.open(playPath, FILE_READ);
  uint32_t rate = 0, dataLen = 0;
  uint16_t ch = 0, bits = 0;
  bool ok = false;
  if (f) {
    uint8_t h[12];
    if (f.read(h, 12) == 12 && !memcmp(h, "RIFF", 4) && !memcmp(h + 8, "WAVE", 4)) {
      while (f.available() >= 8) {  // walk the chunks: "fmt " then "data"
        uint8_t ck[8];
        f.read(ck, 8);
        const uint32_t len = ck[4] | ck[5] << 8 | ck[6] << 16 | (uint32_t)ck[7] << 24;
        if (!memcmp(ck, "fmt ", 4)) {
          uint8_t fm[16];
          f.read(fm, 16);
          ch = fm[2] | fm[3] << 8;
          rate = fm[4] | fm[5] << 8 | fm[6] << 16 | (uint32_t)fm[7] << 24;
          bits = fm[14] | fm[15] << 8;
          f.seek(f.position() + len - 16 + (len & 1));
        } else if (!memcmp(ck, "data", 4)) {
          dataLen = len;
          ok = true;
          break;
        } else {
          f.seek(f.position() + len + (len & 1));
        }
      }
    }
  }
  if (!ok || bits != 16 || ch < 1 || ch > 2 || rate < 8000 || rate > 48000) {
    snprintf(playErr, sizeof(playErr), f ? "not a 16-bit PCM WAV (%u ch, %u bit)" : "file not found", ch, bits);
  } else if (!begin(rate)) {
    snprintf(playErr, sizeof(playErr), "i2s start failed");
  } else {
    playErr[0] = 0;
    const uint32_t frameB = 2 * ch;
    playLenMs = (uint64_t)dataLen / frameB * 1000 / rate;
    const int N = 512;
    int16_t* in = (int16_t*)heap_caps_malloc(N * frameB, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    int32_t* out = (int32_t*)heap_caps_malloc(N * 8, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint32_t done = 0;
    if (playStartMs && playStartMs < playLenMs) {  // resume: skip what was heard
      done = (uint32_t)((uint64_t)playStartMs * rate / 1000) * frameB;
      f.seek(f.position() + done);
    }
    playStartMs = 0;
    beat::reset();
    beatN = 0;
    float g = 0;  // volume, eased so changes and the start / stop don't click
    float lvl = 0;
    while (in && out && done < dataLen) {
      const int want = min((uint32_t)(N * frameB), dataLen - done);
      const int got = f.read((uint8_t*)in, want);
      if (got <= 0) break;
      done += got;
      const int n = got / frameB;
      const float target = stopReq ? 0.0f : playVol * masterVol;
      float pk = 0;
      for (int j = 0; j < n; j++) {
        g += (target - g) * 0.002f;
        const int32_t m = ch == 2 ? ((int32_t)in[2 * j] + in[2 * j + 1]) / 2 : in[j];
        out[2 * j] = out[2 * j + 1] = (int32_t)(m * g) << 16;
        beatFeedSample(m, rate);
        pk = fmaxf(pk, fabsf((float)m));
      }
      lvl = fmaxf(pk / 32768.0f, lvl * 0.9f);
      playLvl = lvl;
      size_t w = 0;
      i2s_write(I2S_NUM_0, out, n * 8, &w, portMAX_DELAY);
      playPosMs = (uint64_t)done / frameB * 1000 / rate;
      if (stopReq && g < 0.002f) break;
    }
    playEnded = done >= dataLen && !stopReq;
    playLvl = 0;
    free(in);
    free(out);
    delay(80);
    end();
  }
  if (f) f.close();
  playing = false;
  busy = false;
  vTaskDelete(nullptr);
}

// ---- Sound effects for the face's play scenes: a tiny synth in a background task ----
// A program is space separated: "F:MS" a tone (0 = rest), "F~G:MS" a slide, "n:MS" a noise burst,
// "z:MS" a fly's buzz, "vX" the volume (0..1) from then on. Skipped while anything else uses the I2S.
char sfxProg[320] = "";
volatile float sfxVol = 0.6f;  // master level for the scene sounds

void sfxTask(void*) {
  int32_t* buf = (int32_t*)heap_caps_malloc(CHUNK * 8, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (buf && begin()) {
    float vol = 0.3f, ph = 0;
    uint32_t nz = 22222;
    const char* p = sfxProg;
    while (*p && !stopReq) {
      while (*p == ' ') p++;
      if (!*p) break;
      char* e;
      if (*p == 'v') { vol = strtof(p + 1, &e); p = e; continue; }
      char kind = 't';
      float f0 = 0, f1 = 0;
      if (*p == 'n' || *p == 'z') { kind = *p; p++; }
      else {
        f0 = f1 = strtof(p, &e);
        p = e;
        if (*p == '~') { f1 = strtof(p + 1, &e); p = e; }
      }
      int ms = 100;
      if (*p == ':') { ms = strtol(p + 1, &e, 10); p = e; }
      while (*p && *p != ' ') p++;
      ms = constrain(ms, 1, 3000);
      const int n = RATE * ms / 1000, fade = max(1, min(n / 2, RATE / 200));  // 5 ms ramps: no clicks
      for (int i = 0; i < n;) {
        const int k = min(CHUNK, n - i);
        for (int j = 0; j < k; j++, i++) {
          const float u = (float)i / n;
          float g = vol * sfxVol * masterVol, s = 0;
          if (i < fade) g *= (float)i / fade;
          if (n - i < fade) g *= (float)(n - i) / fade;
          if (kind == 'n') {  // noise that dies away (crunch, pop, sneeze, page)
            nz = nz * 1664525u + 1013904223u;
            s = ((int32_t)nz >> 8) / 8388608.0f * (1 - u) * (1 - u);
          } else if (kind == 'z') {  // buzz: a soft square with a wobble
            ph += 2 * PI * (185 + 30 * sinf(i * 0.0021f)) / RATE;
            s = (sinf(ph) > 0 ? 0.35f : -0.35f) * (0.6f + 0.4f * sinf(i * 0.0037f));
          } else if (f0 > 0) {  // a round chip tone (a touch of the 3rd harmonic)
            ph += 2 * PI * (f0 + (f1 - f0) * u) / RATE;
            s = 0.8f * sinf(ph) + 0.2f * sinf(3 * ph);
          }
          if (ph > 2 * PI) ph -= 2 * PI;
          buf[2 * j] = buf[2 * j + 1] = (int32_t)(constrain(s * g, -1.0f, 1.0f) * 2147483000.0f);
        }
        size_t w = 0;
        i2s_write(I2S_NUM_0, buf, k * 8, &w, portMAX_DELAY);
      }
    }
    delay(40);
    end();
  }
  free(buf);
  busy = false;
  vTaskDelete(nullptr);
}

bool sfx(const char* prog) {
  if (busy || playing || !prog || !*prog) return false;
  busy = true;
  stopReq = false;
  strlcpy(sfxProg, prog, sizeof(sfxProg));
  if (xTaskCreatePinnedToCore(sfxTask, "sfx", 4096, nullptr, 2, nullptr, 0) != pdPASS) {
    busy = false;
    return false;
  }
  return true;
}

// ---- Talk: the phone's microphone on the robot's speaker (the RC screen's mic button) ----
// The page sends 16 kHz 16-bit mono PCM over a WebSocket (/talk); it goes into a ring buffer in
// PSRAM and a task plays it (starting once ~120 ms is buffered, so small WiFi hiccups don't
// crackle). Silence for 1.5 s ends it. The voice wins: a playing song fades out.
const int TALK_RATE = 16000, TALK_BUF = 32768;  // ~2 s ring (a power of 2)
int16_t* talkBuf = nullptr;
volatile uint32_t talkW = 0, talkR = 0;  // samples written / played (the ring index is & (TALK_BUF - 1))
volatile unsigned long talkLastMs = 0;
volatile bool talkOn = false;
volatile float talkGain = 2.0f;  // phone mics are quiet next to a song
volatile float talkLevel = 0;    // loudness of the last packet (0..1) for the page's meter

void talkTask(void*) {
  int32_t* out = (int32_t*)heap_caps_malloc(CHUNK * 8, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (out && begin(TALK_RATE)) {
    const unsigned long t0 = millis();
    while (talkW - talkR < TALK_RATE * 12 / 100 && millis() - t0 < 400) delay(5);
    float g = 0;
    while (millis() - talkLastMs < 1500 && !stopReq) {
      if (talkW - talkR > TALK_RATE / 2) talkR = talkW - TALK_RATE / 6;  // fell behind: keep the delay short
      const uint32_t avail = talkW - talkR;
      const int k = (int)min((uint32_t)CHUNK, avail);
      if (k == 0) {  // nothing yet: a little silence
        memset(out, 0, CHUNK * 8);
        size_t w = 0;
        i2s_write(I2S_NUM_0, out, CHUNK * 8, &w, portMAX_DELAY);
        continue;
      }
      const float target = masterVol * talkGain;
      for (int j = 0; j < k; j++) {
        g += (target - g) * 0.004f;
        const float v = constrain(talkBuf[(talkR + j) & (TALK_BUF - 1)] * g, -32767.0f, 32767.0f);
        out[2 * j] = out[2 * j + 1] = (int32_t)v << 16;
      }
      talkR += k;
      size_t w = 0;
      i2s_write(I2S_NUM_0, out, k * 8, &w, portMAX_DELAY);
    }
    delay(40);
    end();
  }
  free(out);
  talkOn = false;
  busy = false;
  vTaskDelete(nullptr);
}

// n samples from the phone
void talkFeed(const int16_t* s, int n) {
  if (!talkBuf) talkBuf = (int16_t*)heap_caps_malloc(TALK_BUF * 2, MALLOC_CAP_SPIRAM);
  if (!talkBuf || n <= 0) return;
  float pk = 0;
  const uint32_t w0 = talkW;
  for (int i = 0; i < n; i++) {
    talkBuf[(w0 + i) & (TALK_BUF - 1)] = s[i];
    pk = fmaxf(pk, fabsf((float)s[i]));
  }
  talkW = w0 + n;  // published after the samples are in
  talkLevel = pk / 32768.0f;
  talkLastMs = millis();
  if (talkOn) return;
  if (playing) { stopReq = true; return; }  // a song fades out first; the next packets start the voice
  if (busy) return;                        // a scene sound finishing: next packet
  busy = talkOn = true;
  stopReq = false;
  talkR = w0;
  if (xTaskCreatePinnedToCore(talkTask, "talk", 4096, nullptr, 3, nullptr, 0) != pdPASS) busy = talkOn = false;
}

// ---- Ambient sounds, made here (no files): rain, ocean, fire, pink and brown noise. Loops until stopped ----
volatile int ambKind = 0;  // 1 rain, 2 ocean, 3 fire, 4 pink, 5 brown
const char* const AMB_NAMES[] = {"", "rain", "ocean", "fire", "pink", "brown"};
void ambientTask(void*) {
  const int R = 22050;
  int32_t* out = (int32_t*)heap_caps_malloc(CHUNK * 8, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (out && begin(R)) {
    playErr[0] = 0;
    uint32_t nz = esp_random() | 1;
    auto white = [&]() { nz ^= nz << 13; nz ^= nz >> 17; nz ^= nz << 5; return (int32_t)nz / 2147483648.0f; };
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;  // pink (Paul Kellet)
    float brown = 0, lp = 0, hp = 0, hpIn = 0, g = 0, drop = 0, dropLp = 0, crack = 0;
    uint32_t t = 0;
    const int kind = ambKind;
    while (!stopReq || g > 0.0015f) {
      const float target = stopReq ? 0.0f : playVol * masterVol;
      for (int j = 0; j < CHUNK; j++, t++) {
        g += (target - g) * (stopReq ? 0.0004f : 0.00006f);  // ~2 s fade in, ~1 s out
        const float w = white();
        b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f; b2 = 0.96900f * b2 + w * 0.1538520f;
        b3 = 0.86650f * b3 + w * 0.3104856f; b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
        const float pink = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f) * 0.11f;
        b6 = w * 0.115926f;
        brown = (brown + 0.02f * w) * 0.998f;
        float s = 0;
        const float sec = t / (float)R;
        if (kind == 1) {  // rain: soft pink hiss + drops (short bright clicks) now and then
          lp += 0.35f * (pink - lp);
          if (white() > 0.9965f) drop = 0.5f + 0.5f * fabsf(white());
          drop *= 0.985f;
          const float d = drop * white();
          dropLp += 0.6f * (d - dropLp);
          s = lp * (0.75f + 0.12f * sinf(sec * 0.31f)) + (d - dropLp) * 0.55f;
        } else if (kind == 2) {  // ocean: waves - noise that swells and falls every ~9 s
          const float sw = 0.5f + 0.5f * sinf(sec * 2 * PI / 9.0f + 0.6f * sinf(sec * 0.13f));
          lp += (0.02f + 0.25f * sw * sw) * (pink - lp);
          s = lp * (0.25f + 1.1f * sw * sw) + brown * 1.2f;
        } else if (kind == 3) {  // fire: a low rumble + crackles
          if (white() > 0.9993f) crack = 0.6f + 0.4f * fabsf(white());
          crack *= 0.96f;
          hp = 0.9f * (hp + crack * white() - hpIn);
          hpIn = crack * white();
          s = brown * 2.2f + hp * 0.9f + pink * 0.05f;
        } else if (kind == 4) s = pink * 0.8f;
        else s = brown * 3.0f;
        out[2 * j] = out[2 * j + 1] = (int32_t)(constrain(s * g * 0.8f, -1.0f, 1.0f) * 2147483000.0f);
      }
      size_t w = 0;
      i2s_write(I2S_NUM_0, out, CHUNK * 8, &w, portMAX_DELAY);
      playPosMs = t * 1000ULL / R;
    }
    delay(40);
    end();
  } else snprintf(playErr, sizeof(playErr), "i2s start failed");
  free(out);
  playEnded = false;
  playing = false;
  busy = false;
  vTaskDelete(nullptr);
}
bool ambient(int kind, float vol) {
  if (busy || playing || kind < 1 || kind > 5) return false;
  busy = playing = true;
  stopReq = false;
  playEnded = false;
  playVol = vol;
  ambKind = kind;
  playPosMs = playLenMs = 0;
  snprintf(playPath, sizeof(playPath), "ambient:%s", AMB_NAMES[kind]);
  if (xTaskCreatePinnedToCore(ambientTask, "ambient", 4096, nullptr, 2, nullptr, 0) != pdPASS) {
    busy = playing = false;
    return false;
  }
  return true;
}

bool play(const char* path, float vol) {
  if (busy || playing) return false;
  busy = playing = true;
  stopReq = false;
  playEnded = false;
  playVol = vol;
  playPosMs = playLenMs = 0;
  playErr[0] = 0;
  strlcpy(playPath, path, sizeof(playPath));
  // core 0 next to WiFi; small priority - it only waits on the SD card and the I2S DMA
  if (xTaskCreatePinnedToCore(playerTask, "player", 6144, nullptr, 2, nullptr, 0) != pdPASS) {
    busy = playing = false;
    return false;
  }
  return true;
}

}  // namespace aud

esp_err_t audio_handler(httpd_req_t* req) {
  char q[200] = {0}, v[16], path[96] = {0};
  httpd_req_get_url_query_str(req, q, sizeof(q));
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  static char (&out)[400] = *(char(*)[400])heap_caps_calloc(1, sizeof(char) * (400), MALLOC_CAP_SPIRAM);
  if (httpd_query_key_value(q, "master", v, sizeof(v)) == ESP_OK) {  // the one volume for everything
    aud::masterVol = constrain(atof(v), 0.0, 1.0);
    prefPut("volume", (int32_t)lroundf(aud::masterVol * 100), false);
  }
  if (httpd_query_key_value(q, "emosnd", v, sizeof(v)) == ESP_OK) {  // emote sounds on / off
    emoteSndOn = v[0] == '1';
    prefPut("emoSnd", emoteSndOn ? 1 : 0, false);
  }
  if (httpd_query_key_value(q, "master", v, sizeof(v)) == ESP_OK || httpd_query_key_value(q, "vols", v, sizeof(v)) == ESP_OK ||
      httpd_query_key_value(q, "emosnd", v, sizeof(v)) == ESP_OK) {
    const int o = snprintf(out, sizeof(out), "{\"master\":%.2f,\"talk\":%d,\"talkGain\":%.1f,\"emoSnd\":%d}", (float)aud::masterVol,
                           aud::talkOn ? 1 : 0, (float)aud::talkGain, emoteSndOn ? 1 : 0);
    return httpd_resp_send(req, out, o);
  }
  const bool wantPlay = httpd_query_key_value(q, "play", path, sizeof(path)) == ESP_OK;
  if (wantPlay || httpd_query_key_value(q, "stop", v, sizeof(v)) == ESP_OK ||
      httpd_query_key_value(q, "status", v, sizeof(v)) == ESP_OK ||
      (aud::playing && httpd_query_key_value(q, "vol", v, sizeof(v)) == ESP_OK)) {
    if (httpd_query_key_value(q, "vol", v, sizeof(v)) == ESP_OK) aud::playVol = constrain(atof(v), 0.0, 1.0);
    if (httpd_query_key_value(q, "stop", v, sizeof(v)) == ESP_OK) aud::stopReq = true;
    if (wantPlay) {
      if (aud::playing) {  // a new song: stop the old one first (it fades out)
        aud::stopReq = true;
        for (int t = 0; t < 100 && aud::playing; t++) delay(20);
      }
      if (!aud::play(path, aud::playVol)) return httpd_resp_sendstr(req, "{\"error\":\"busy\"}");
      delay(300);  // long enough to report a missing file
    }
    const int o = snprintf(out, sizeof(out), "{\"playing\":%d,\"file\":\"%s\",\"posMs\":%u,\"lenMs\":%u,\"vol\":%.2f,\"error\":\"%s\"}",
                           aud::playing ? 1 : 0, aud::playPath, (unsigned)aud::playPosMs, (unsigned)aud::playLenMs,
                           (float)aud::playVol, aud::playErr);
    return httpd_resp_send(req, out, o);
  }
  if (aud::busy) return httpd_resp_sendstr(req, "{\"error\":\"busy\"}");
  aud::busy = true;
  xl::httpBusy = true;  // the web self-check waits while this handler plays / listens
  int32_t* buf = (int32_t*)heap_caps_malloc(aud::CHUNK * 8, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!buf || !aud::begin(aud::RATE, true, 3)) {  // the tests here listen to the robot's mic (3 buffers: ~12 KB, not ~24 - internal RAM is short)
    free(buf);
    aud::busy = false;
    xl::httpBusy = false;
    return httpd_resp_sendstr(req, "{\"error\":\"i2s start failed\"}");
  }
  int o = 0;
  if (httpd_query_key_value(q, "tone", v, sizeof(v)) == ESP_OK) {
    const float hz = constrain(atof(v), 50.0, 8000.0);
    int ms = 600;
    float vol = 0.2f;
    if (httpd_query_key_value(q, "ms", v, sizeof(v)) == ESP_OK) ms = constrain(atoi(v), 50, 4000);
    if (httpd_query_key_value(q, "vol", v, sizeof(v)) == ESP_OK) vol = constrain(atof(v), 0.0, 0.8);
    aud::tone(hz, ms, vol, buf);
    o = snprintf(out, sizeof(out), "{\"tone\":%.0f,\"ms\":%d,\"vol\":%.2f}", hz, ms, vol);
  } else if (httpd_query_key_value(q, "mic", v, sizeof(v)) == ESP_OK) {
    const int ms = constrain(atoi(v), 100, 5000);
    aud::Level lv[2];
    aud::listen(ms, lv, buf, nullptr, 0, nullptr, nullptr);
    o = snprintf(out, sizeof(out), "{\"ms\":%d,", ms);
    o += aud::levelJson(out + o, sizeof(out) - o, "left", lv[0]);
    out[o++] = ',';
    o += aud::levelJson(out + o, sizeof(out) - o, "right", lv[1]);
    o += snprintf(out + o, sizeof(out) - o, "}");
  } else if (httpd_query_key_value(q, "raw", v, sizeof(v)) == ESP_OK) {
    // 0.5 s of raw 32-bit words (L,R,L,R...) after 100 ms of settling, sent as binary
    int32_t* big = (int32_t*)heap_caps_malloc(aud::RATE / 2 * 8, MALLOC_CAP_SPIRAM);
    for (int t = 0; t < 64; t++) { size_t r = 0; i2s_read(I2S_NUM_0, buf, aud::CHUNK * 8, &r, portMAX_DELAY); }
    size_t got = 0;
    while (big && got < aud::RATE / 2 * 8) {
      size_t r = 0;
      i2s_read(I2S_NUM_0, (uint8_t*)big + got, min((size_t)aud::CHUNK * 8, aud::RATE / 2 * 8 - got), &r, portMAX_DELAY);
      got += r;
    }
    aud::end();
    free(buf);
    aud::busy = false;
    xl::httpBusy = false;
    httpd_resp_set_type(req, "application/octet-stream");
    const esp_err_t e = httpd_resp_send(req, (const char*)big, big ? got : 0);
    free(big);
    return e;
  } else if (httpd_query_key_value(q, "duplex", v, sizeof(v)) == ESP_OK) {
    // Test: plays a tone and records at the same time (0.5 s of raw words, binary) - shows how the
    // mic handles a loud, known signal (the speaker is centimetres away)
    const float hz = constrain(atof(v), 100.0, 4000.0);
    float vol = 0.2f;
    if (httpd_query_key_value(q, "vol", v, sizeof(v)) == ESP_OK) vol = constrain(atof(v), 0.0, 0.8);
    const size_t total = aud::RATE / 2 * 8;
    int32_t* big = (int32_t*)heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
    aud::drop(1200, buf);
    float ph = 0;
    const float dph = 2 * PI * hz / aud::RATE;
    size_t got = 0;
    for (int blk = 0; big && got < total; blk++) {
      for (int j = 0; j < aud::CHUNK; j++) {
        const float g = min(1.0f, blk / 8.0f) * vol;  // fade in over ~130 ms
        buf[2 * j] = buf[2 * j + 1] = (int32_t)(sinf(ph) * g * 2147483000.0f);
        ph += dph;
        if (ph > 2 * PI) ph -= 2 * PI;
      }
      size_t w = 0, r = 0;
      i2s_write(I2S_NUM_0, buf, aud::CHUNK * 8, &w, portMAX_DELAY);
      if (blk >= 12) {  // keep the part after the fade-in
        i2s_read(I2S_NUM_0, (uint8_t*)big + got, min((size_t)aud::CHUNK * 8, total - got), &r, portMAX_DELAY);
        got += r;
      }
    }
    aud::end();
    free(buf);
    aud::busy = false;
    xl::httpBusy = false;
    httpd_resp_set_type(req, "application/octet-stream");
    const esp_err_t e = httpd_resp_send(req, (const char*)big, big ? got : 0);
    free(big);
    return e;
  } else if (httpd_query_key_value(q, "echo", v, sizeof(v)) == ESP_OK) {
    // Record, then play it back louder (peak normalised) - hear what the mic hears
    // Mic warms up first (silent), then a beep = "talk now", then it records straight away
    const int ms = constrain(atoi(v), 500, 8000);
    const int maxN = aud::RATE * ms / 1000;
    int16_t* rec = (int16_t*)heap_caps_malloc(maxN * 2, MALLOC_CAP_SPIRAM);
    if (rec) {
      aud::Level lv[2];
      int n = 0, ch = -1;
      aud::drop(1200, buf);
      aud::tone(1200, 150, 0.15f, buf);  // "recording now"
      aud::drop(260, buf);               // the beep still queued in the speaker's DMA buffers (~100 ms) plus the room ringing
      aud::listen(ms, lv, buf, rec, maxN, &n, &ch, false);
      // high-pass ~150 Hz (the mic's slow drift, hum and the room's boom), then make the loudest
      // part ~half scale
      float y = 0, xPrev = rec[0];
      const float a = 0.943f;
      int peak = 1;
      for (int i = 0; i < n; i++) {
        const float x = rec[i];
        y = a * (y + x - xPrev);
        xPrev = x;
        rec[i] = (int16_t)constrain(y, -32767.0f, 32767.0f);
        if (i > aud::RATE / 20) peak = max(peak, abs((int)rec[i]));
      }
      const float gain = min(16000.0f / peak, 20.0f);
      // Noise gate: 20 ms frames quieter than 3.5x the background (the quietest fifth of the
      // frames) are turned down to 10 %, with a smooth gain so words don't get chopped. It also
      // shortens the room's reverb tail after each word (what sounds like echo from a distance).
      const int F = aud::RATE / 50, nf = n / F;
      float* fr = (float*)heap_caps_malloc(max(nf, 1) * sizeof(float) * 2, MALLOC_CAP_SPIRAM);
      if (fr && nf >= 10) {
        for (int k = 0; k < nf; k++) {
          double e = 0;
          for (int i = k * F; i < (k + 1) * F; i++) e += (double)rec[i] * rec[i];
          fr[k] = fr[nf + k] = sqrtf(e / F);
        }
        std::sort(fr + nf, fr + 2 * nf);
        float floorRms = 0;
        for (int k = 0; k < nf / 5; k++) floorRms += fr[nf + k];
        floorRms = max(floorRms / (nf / 5), 1.0f);
        float g = 0.1f;
        for (int i = 0; i < n; i++) {
          const int k = min(i / F, nf - 1);
          const bool open = fr[k] > 3.5f * floorRms || (k > 0 && fr[k - 1] > 3.5f * floorRms) || (k + 1 < nf && fr[k + 1] > 3.5f * floorRms);
          g += ((open ? 1.0f : 0.1f) - g) * (open ? 0.01f : 0.003f);  // opens in ~6 ms, closes in ~20 ms
          rec[i] = (int16_t)constrain(rec[i] * gain * g, -32767.0f, 32767.0f);
        }
      } else {
        for (int i = 0; i < n; i++) rec[i] = (int16_t)constrain(rec[i] * gain, -32767.0f, 32767.0f);
      }
      free(fr);
      delay(250);
      aud::playMono(rec, n, buf);
      // keep it on the card too: /rec/last.wav (16 kHz mono)
      bool saved = false;
      if (sdSizeMB >= 0) {
        if (!SD_MMC.exists("/rec")) SD_MMC.mkdir("/rec");
        File f = SD_MMC.open("/rec/last.wav", FILE_WRITE);
        if (f) {
          const uint32_t dl = n * 2, rate = aud::RATE, br = rate * 2;
          uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
                           0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0};
          const uint32_t rl = 36 + dl;
          memcpy(h + 4, &rl, 4);
          memcpy(h + 24, &rate, 4);
          memcpy(h + 28, &br, 4);
          memcpy(h + 40, &dl, 4);
          saved = f.write(h, 44) == 44 && f.write((uint8_t*)rec, dl) == dl;
          f.close();
        }
      }
      free(rec);
      o = snprintf(out, sizeof(out), "{\"echo\":%d,\"samples\":%d,\"channel\":\"%s\",\"peak16\":%d,\"gain\":%.1f,\"saved\":%d}", ms,
                   n, ch == 0 ? "left" : ch == 1 ? "right" : "?", peak, gain, saved ? 1 : 0);
    } else {
      o = snprintf(out, sizeof(out), "{\"error\":\"no memory\"}");
    }
  } else {
    o = snprintf(out, sizeof(out), "{\"use\":\"/audio?tone=1000&ms=600&vol=0.2 | /audio?mic=2000 | /audio?echo=3000\"}");
  }
  delay(60);  // let the last DMA buffer play out
  aud::end();
  free(buf);
  aud::busy = false;
  xl::httpBusy = false;
  return httpd_resp_send(req, out, o);
}

volatile uint32_t sdPutBytes = 0;  // progress of the running upload

// POST /sdPut?name=/music/song.wav  - the request body is written to the SD card as that file
esp_err_t sd_put_handler(httpd_req_t* req) {
  char q[128] = {0}, name[96] = {0};
  httpd_req_get_url_query_str(req, q, sizeof(q));
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  if (sdSizeMB < 0) return httpd_resp_sendstr(req, "{\"error\":\"no SD card\"}");
  if (httpd_query_key_value(q, "name", name, sizeof(name)) != ESP_OK || name[0] != '/' || strstr(name, ".."))
    return httpd_resp_sendstr(req, "{\"error\":\"name=/folder/file needed\"}");
  for (char* p = name + 1; (p = strchr(p, '/')); p++) {  // make the folders on the way
    *p = 0;
    if (!SD_MMC.exists(name)) SD_MMC.mkdir(name);
    *p = '/';
  }
  const String tmp = String(name) + ".part";
  File f = SD_MMC.open(tmp, FILE_WRITE);
  if (!f) return httpd_resp_sendstr(req, "{\"error\":\"can't create the file\"}");
  xl::httpBusy = true;
  const size_t B = 8192;
  uint8_t* buf = (uint8_t*)heap_caps_malloc(B, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  size_t left = req->content_len, total = 0;
  const unsigned long t0 = millis();
  bool ok = buf != nullptr;
  int stalls = 0;
  while (ok && left > 0) {
    const int r = httpd_req_recv(req, (char*)buf, min(left, B));
    if (r == HTTPD_SOCK_ERR_TIMEOUT && ++stalls < 4) continue;  // ~5 s each, then give up
    stalls = 0;
    if (r <= 0) { ok = false; break; }
    if (f.write(buf, r) != (size_t)r) { ok = false; break; }
    sdPutBytes = total + r;
    left -= r;
    total += r;
  }
  f.close();
  free(buf);
  xl::httpBusy = false;
  if (!ok) {
    SD_MMC.remove(tmp);
    return httpd_resp_sendstr(req, "{\"error\":\"upload cut off\"}");
  }
  if (SD_MMC.exists(name)) SD_MMC.remove(name);
  SD_MMC.rename(tmp, name);
  char out[160];
  const unsigned long ms = max(1UL, millis() - t0);
  const int o = snprintf(out, sizeof(out), "{\"saved\":\"%s\",\"bytes\":%u,\"ms\":%lu,\"KBps\":%lu}", name, (unsigned)total, ms,
                         (unsigned long)(total / ms));
  return httpd_resp_send(req, out, o);
}

// GET /sdList?dir=/music
esp_err_t sd_list_handler(httpd_req_t* req) {
  char q[128] = {0}, dir[96] = "/";
  httpd_req_get_url_query_str(req, q, sizeof(q));
  httpd_query_key_value(q, "dir", dir, sizeof(dir));
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  String o = "{\"sizeMB\":" + String(sdSizeMB) + ",\"files\":[";
  File d = sdSizeMB >= 0 ? SD_MMC.open(dir) : File();
  if (sdSizeMB >= 0 && !d) xl::sdHealReq = true;  // the card stopped answering? (xlog mounts it again)
  int n = 0;
  if (d && d.isDirectory()) {
    for (File e = d.openNextFile(); e && n < 300; e = d.openNextFile(), n++) {  // (the String grows in PSRAM)
      if (n) o += ",";
      o += "{\"name\":\"" + String(e.name()) + "\",\"dir\":" + (e.isDirectory() ? "1" : "0") + ",\"bytes\":" + String((unsigned)e.size()) + "}";
    }
  }
  o += "]}";
  return httpd_resp_send(req, o.c_str(), o.length());
}

// ---- File upload over the USB cable (the CH343 serial port) ----
// The laptop sends "SDPUT <name> <bytes> <baud>\n" at 115200. The robot answers "SDREADY\n", both
// switch to <baud>, then the laptop sends the file in 4 KB blocks and waits for an ACK byte (0x06)
// after each one (flow control: the robot writes the block to the card first). At the end the
// robot sends "SDDONE <bytes> <sum>\n" (sum = 32-bit byte sum) and goes back to 115200.
void serialPutFile(char* args) {
  char* name = strtok(args, " ");
  char* sz = strtok(nullptr, " ");
  char* bd = strtok(nullptr, " ");
  if (!name || !sz || name[0] != '/' || strstr(name, "..") || sdSizeMB < 0) {
    Serial.println("SDERR bad request or no card");
    return;
  }
  const uint32_t size = strtoul(sz, nullptr, 10);
  const uint32_t baud = bd ? strtoul(bd, nullptr, 10) : 115200;
  for (char* p = name + 1; (p = strchr(p, '/')); p++) {
    *p = 0;
    if (!SD_MMC.exists(name)) SD_MMC.mkdir(name);
    *p = '/';
  }
  const String tmp = String(name) + ".part";
  File f = SD_MMC.open(tmp, FILE_WRITE);
  if (!f) {
    Serial.println("SDERR can't create the file");
    return;
  }
  const size_t B = 4096;
  uint8_t* buf = (uint8_t*)heap_caps_malloc(B, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  Serial.println("SDREADY");
  Serial.flush();
  Serial.updateBaudRate(baud);
  uint32_t got = 0, sum = 0;
  bool ok = buf != nullptr;
  while (ok && got < size) {
    const size_t want = min((size_t)(size - got), B);
    size_t n = 0;
    unsigned long last = millis();
    while (n < want) {
      const int a = Serial.available();
      if (a > 0) {
        n += Serial.read(buf + n, min((size_t)a, want - n));
        last = millis();
      } else if (millis() - last > 5000) {
        ok = false;  // the laptop went quiet
        break;
      } else {
        vTaskDelay(1);
      }
    }
    if (!ok) break;
    for (size_t i = 0; i < n; i++) sum += buf[i];
    if (f.write(buf, n) != n) {
      ok = false;
      break;
    }
    got += n;
    Serial.write((uint8_t)0x06);
  }
  f.close();
  free(buf);
  if (ok) {
    if (SD_MMC.exists(name)) SD_MMC.remove(name);
    SD_MMC.rename(tmp, name);
    Serial.printf("SDDONE %u %u\n", (unsigned)got, (unsigned)sum);
  } else {
    SD_MMC.remove(tmp);
    Serial.printf("SDERR cut off at %u bytes\n", (unsigned)got);
  }
  Serial.flush();
  Serial.updateBaudRate(115200);
}

void serialCmdTask(void*) {
  static char line[160];
  int n = 0;
  for (;;) {
    while (Serial.available()) {
      const char c = Serial.read();
      if (c == '\n' || c == '\r') {
        line[n] = 0;
        if (!strncmp(line, "SDPUT ", 6)) serialPutFile(line + 6);
        n = 0;
      } else if (n < (int)sizeof(line) - 1) {
        line[n++] = c;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// GET /sdGet?name=/rec/last.wav  - downloads a file from the SD card
esp_err_t sd_get_handler(httpd_req_t* req) {
  char q[128] = {0}, name[96] = {0};
  httpd_req_get_url_query_str(req, q, sizeof(q));
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  if (sdSizeMB < 0 || httpd_query_key_value(q, "name", name, sizeof(name)) != ESP_OK || strstr(name, ".."))
    return httpd_resp_send_404(req);
  File f = SD_MMC.open(name, FILE_READ);
  if (!f || f.isDirectory()) return httpd_resp_send_404(req);
  httpd_resp_set_type(req, strstr(name, ".wav") ? "audio/wav" : "application/octet-stream");
  char tv[8];
  if (httpd_query_key_value(q, "tail", tv, sizeof(tv)) == ESP_OK) {  // &tail=KB: only the end (from a line start)
    const size_t want = (size_t)constrain(atoi(tv), 1, 512) * 1024;
    if (f.size() > want) {
      f.seek(f.size() - want);
      while (f.available() && f.read() != '\n') {}
    }
  }
  xl::httpBusy = true;
  uint8_t* buf = (uint8_t*)heap_caps_malloc(8192, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!buf) buf = (uint8_t*)heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);  // (PSRAM: internal RAM may be fragmented after a chat)
  esp_err_t e = ESP_OK;
  int r;
  while (buf && e == ESP_OK && (r = f.read(buf, 8192)) > 0) e = httpd_resp_send_chunk(req, (const char*)buf, r);
  f.close();
  free(buf);
  xl::httpBusy = false;
  if (e == ESP_OK) httpd_resp_send_chunk(req, nullptr, 0);
  return e;
}

// GET /ai/...  - static files for the phone's AI (the Human library and its models) straight from
// the SD card (/ai/... on the card). A .gz next to the file is sent instead, compressed. They
// never change for a given name, so the phone may keep them for a year (and the models also go
// into its own model cache): the ~15 MB crosses the WiFi once, in AP mode too.
esp_err_t ai_file_handler(httpd_req_t* req) {
  char path[112];
  const char* uri = req->uri;
  const char* qm = strchr(uri, '?');
  const size_t ul = qm ? (size_t)(qm - uri) : strlen(uri);
  if (ul >= sizeof(path) - 4 || strstr(uri, "..") || sdSizeMB < 0) return httpd_resp_send_404(req);
  memcpy(path, uri, ul);
  path[ul] = 0;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  const char* ext = strrchr(path, '.');
  const char* type = !ext ? "application/octet-stream"
                     : !strcmp(ext, ".js") ? "application/javascript"
                     : !strcmp(ext, ".json") ? "application/json"
                     : !strcmp(ext, ".html") ? "text/html"
                     : !strcmp(ext, ".jpg") ? "image/jpeg"
                     : !strcmp(ext, ".wav") ? "audio/wav"
                     : "application/octet-stream";
  const bool noCache = strstr(path, "faces.json") != nullptr;  // the saved faces change
  char gz[116];
  snprintf(gz, sizeof(gz), "%s.gz", path);
  File f;
  bool zipped = false;
  if (SD_MMC.exists(gz)) {
    f = SD_MMC.open(gz, FILE_READ);
    zipped = (bool)f;
  }
  if (!f) f = SD_MMC.open(path, FILE_READ);
  if (!f || f.isDirectory()) {
    if (f) f.close();
    return httpd_resp_send_404(req);
  }
  httpd_resp_set_type(req, type);
  if (zipped) httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "Cache-Control", noCache ? "no-store" : "public, max-age=31536000, immutable");
  xl::httpBusy = true;
  const size_t B = 16384;
  uint8_t* buf = (uint8_t*)heap_caps_malloc(B, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  esp_err_t e = buf ? ESP_OK : ESP_FAIL;
  int r;
  while (e == ESP_OK && (r = f.read(buf, B)) > 0) e = httpd_resp_send_chunk(req, (const char*)buf, r);
  f.close();
  free(buf);
  xl::httpBusy = false;
  if (e == ESP_OK) httpd_resp_send_chunk(req, nullptr, 0);
  return e;
}
