// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
// ---- Wake word: "hey Oreo" starts a conversation, hands free ----
// A microWakeWord model (trained for the wake phrase) run on the robot itself with
// TensorFlow Lite Micro (components/esp-tflite-micro + esp-nn). The model is a file on the SD card,
// /sesame/wake.tflite, read at start: a better model is a file copy, not a new firmware.
// The mic's sound -> the same front end the training used (components/mww_frontend: 30 ms windows every
// 10 ms, 40 channels 125-7500 Hz, noise reduction, PCAN gain, log) -> the streaming model every 30 ms ->
// the average of its last few scores over the cutoff -> a conversation, as if Talk was pressed.
// It listens only while the speaker / mic port is free: anything that plays (aud::begin) takes it back
// at once, a conversation too; and not while the RC video runs or internal RAM is short (its mic DMA
// buffers are ~16 KB of internal RAM).
// GET /gemini?ww=1|0 (saved), &wwcut=0.5..0.99, &wwwin=1..20; /gemini shows wwState / wwMax / wwHits.
#pragma once
#if __has_include("tensorflow/lite/micro/micro_interpreter.h")
#define WW_BUILD 1
// as the library was built (components/esp-tflite-micro: -DTF_LITE_STATIC_MEMORY): without it TfLiteTensor
// has another layout here - the input's type / dims / data read as rubbish and the first write crashes
#ifndef TF_LITE_STATIC_MEMORY
#define TF_LITE_STATIC_MEMORY
#endif
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_allocator.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/experimental/microfrontend/lib/frontend.h"
#include "tensorflow/lite/experimental/microfrontend/lib/frontend_util.h"
#endif

namespace ww {

const char* const MODEL_FILE = "/sesame/wake.tflite";
// A crash in the wake word must never become a boot loop: this survives a reset. Set while it starts up
// (the first 60 s); a crash then -> the next boot leaves the wake word off and says so.
RTC_NOINIT_ATTR uint32_t startingMagic;
const uint32_t STARTING = 0x57414B45;  // "WAKE"

volatile bool enabled = true;      // the page's switch (saved, "wwOn")
float cutoff = 0.98f;              // the averaged score that wakes it ("wwCut"); 0.98 suits sd/sesame/wake.tflite
int window = 5;                    // how many recent scores are averaged ("wwWin")
const char* stateName = "off";     // off / no model / waiting / listening / error
volatile int micLvl = 0;  // the mic's loudest sample lately (16-bit scale: is sound coming in?)
volatile float probNow = 0, probMax = 0;  // the latest average; the highest in the last 10 s (tuning)
volatile uint32_t hits = 0;        // how many times it woke up
volatile unsigned long lastHitMs = 0;
char err[64] = "";

void load() {
  enabled = prefs.getBool("wwOn", true);
  cutoff = prefs.getFloat("wwCut", cutoff);
  window = prefs.getInt("wwWin", window);
}

bool holdingNow = false;
const char* whyNot = "";

#if WW_BUILD
// the training's front end (pymicro-features / microWakeWord): keep in step with it
void frontendConfig(FrontendConfig& c) {
  c.window.size_ms = 30;
  c.window.step_size_ms = 10;
  c.filterbank.num_channels = 40;
  c.filterbank.lower_band_limit = 125.0;
  c.filterbank.upper_band_limit = 7500.0;
  c.noise_reduction.smoothing_bits = 10;
  c.noise_reduction.even_smoothing = 0.025;
  c.noise_reduction.odd_smoothing = 0.06;
  c.noise_reduction.min_signal_remaining = 0.05;
  c.pcan_gain_control.enable_pcan = 1;
  c.pcan_gain_control.strength = 0.95;
  c.pcan_gain_control.offset = 80.0;
  c.pcan_gain_control.gain_bits = 21;
  c.log_scale.enable_log = 1;
  c.log_scale.scale_shift = 6;
}

// May it hold the speaker / mic port now?
// (the task holds the mic port: its ~17 KB are already counted in the free RAM)
// why it isn't listening right now (/gemini wwWhy)
bool mayListen() {
  const size_t need = holdingNow ? 12000u : 30000u;  // (start with room; once on, only a real shortage stops it:
                                                      //  its own ~17 KB are already counted in the free RAM)
  static char busyWhy[48];
  if (aud::busy) snprintf(busyWhy, sizeof(busyWhy), "speaker busy: %s%.24s", aud::playing ? "file " : aud::talkOn ? "chat" : "sfx ",
                          aud::playing ? aud::playPath : aud::talkOn ? "" : aud::sfxProg);
  whyNot = !enabled ? "off" : aud::busy ? busyWhy : aud::playing ? "playing" : aud::talkOn ? "talking"
         : gem::rj.state != 0 ? "web request" : gem::vsState == 1 ? "voice sample" : gem::wantStart || gem::starting ? "chat starting"
         : camViewWanted ? "video" : rcActive() ? "RC" : otaActive ? "update"
         : heap_caps_get_free_size(MALLOC_CAP_INTERNAL) <= need ? "low RAM" : "";
  return !whyNot[0];
}

void task(void*) {
  // the model, from the SD card into PSRAM
  uint8_t* modelData = nullptr;
  File f = sdSizeMB >= 0 ? SD_MMC.open(MODEL_FILE, FILE_READ) : File();
  if (f) {
    const size_t n = f.size();
    modelData = (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (modelData && f.read(modelData, n) != (int)n) { heap_caps_free(modelData); modelData = nullptr; }
    f.close();
  }
  if (!modelData) {
    bootInfo.fail |= 1 << boot::B_WAKE;
    stateName = "no model";
    vTaskDelete(nullptr);
    return;
  }
  const tflite::Model* model = tflite::GetModel(modelData);
  static tflite::MicroMutableOpResolver<24> ops;
  ops.AddCallOnce(); ops.AddVarHandle(); ops.AddReshape(); ops.AddReadVariable(); ops.AddStridedSlice();
  ops.AddConcatenation(); ops.AddAssignVariable(); ops.AddConv2D(); ops.AddMul(); ops.AddAdd(); ops.AddMean();
  ops.AddFullyConnected(); ops.AddLogistic(); ops.AddQuantize(); ops.AddDepthwiseConv2D(); ops.AddAveragePool2D();
  ops.AddMaxPool2D(); ops.AddPad(); ops.AddPack(); ops.AddSplitV();
  const size_t ARENA = 64 * 1024, VAR_ARENA = 2048;
  uint8_t* arena = (uint8_t*)heap_caps_malloc(ARENA, MALLOC_CAP_SPIRAM);
  uint8_t* varArena = (uint8_t*)heap_caps_malloc(VAR_ARENA, MALLOC_CAP_SPIRAM);
  tflite::MicroAllocator* ma = varArena ? tflite::MicroAllocator::Create(varArena, VAR_ARENA) : nullptr;
  tflite::MicroResourceVariables* mrv = ma ? tflite::MicroResourceVariables::Create(ma, 20) : nullptr;
  static tflite::MicroInterpreter* it = nullptr;
  if (arena && mrv) it = new tflite::MicroInterpreter(model, ops, arena, ARENA, mrv);
  if (!it || it->AllocateTensors() != kTfLiteOk) {
    strlcpy(err, "the model didn't load (ops / memory?)", sizeof(err));
    stateName = "error";
    vTaskDelete(nullptr);
    return;
  }
  TfLiteTensor* in = it->input(0);
  TfLiteTensor* out = it->output(0);
  // the input: [1, stride, 40] int8 - written by its size in bytes, never past it (a write past it
  // crashes the robot)
  {
    char dims[40] = "";
    int k = 0;
    for (int i = 0; in && in->dims && i < in->dims->size && k < 34; i++) k += snprintf(dims + k, sizeof(dims) - k, "%s%d", i ? "x" : "", in->dims->data[i]);
    xlogSys("WAKE model: input %s type %d %u bytes, output type %d", dims, in ? (int)in->type : -1, in ? (unsigned)in->bytes : 0,
            out ? (int)out->type : -1);
  }
  if (!in || !out || in->type != kTfLiteInt8 || !in->data.int8 || in->bytes < 40 || !out->data.raw) {
    strlcpy(err, "the model's input / output isn't what the wake word expects", sizeof(err));
    stateName = "error";
    vTaskDelete(nullptr);
    return;
  }
  const int stride = (int)(in->bytes / 40);  // feature frames per inference (3)
  const float inScale = in->params.scale;
  const int inZero = in->params.zero_point;

  FrontendConfig fc;
  FrontendState fs;
  frontendConfig(fc);
  if (!FrontendPopulateState(&fc, &fs, aud::RATE)) {
    strlcpy(err, "front end", sizeof(err));
    stateName = "error";
    vTaskDelete(nullptr);
    return;
  }
  int32_t* buf = (int32_t*)heap_caps_malloc(aud::CHUNK * 8, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  int16_t pcm[aud::CHUNK];
  float hist[20] = {0};
  int hn = 0, frames = 0;
  uint64_t prevFrame = 0;
  unsigned long freeSinceMs = 0, maxResetMs = 0, ignoreUntilMs = 0;
  bool holding = false;
  stateName = "waiting";
  bootInfo.ok |= 1 << boot::B_WAKE;
  startingMagic = STARTING;  // (cleared once it has run a minute)
  const unsigned long startedMs = millis();

  for (;;) {
    const unsigned long now = millis();
    if (startingMagic == STARTING && now - startedMs > 60000) startingMagic = 0;  // (it runs fine)
    if (now - maxResetMs > 10000) { probMax = 0; maxResetMs = now; }
    // give the port back at once when anything else wants it
    if (holding && (aud::wwYield || !mayListen())) {
      aud::end();
      holding = holdingNow = false;
      aud::wwHeld = false;
      aud::wwYield = false;
      stateName = "waiting";
    }
    if (!holding) {
      aud::wwYield = false;
      if (!mayListen()) { freeSinceMs = 0; delay(100); continue; }
      if (!freeSinceMs) freeSinceMs = now;
      if (now - freeSinceMs < 600) { delay(50); continue; }  // free for a moment first (a scene's sound: back in 0.6 s)
      if (!aud::beginListen(aud::RATE, 3)) { delay(500); continue; }  // (the mic alone: ~6 KB, not ~16)
      aud::wwHeld = holding = holdingNow = true;
      gem::micFind(buf);  // where the mic's bits are this start
      FrontendReset(&fs);
      hn = frames = 0;
      memset(hist, 0, sizeof(hist));
      ignoreUntilMs = millis() + 600;  // (the front end's noise estimate settles)
      stateName = "listening";
    }
    size_t r = 0;
    i2s_read(I2S_NUM_0, buf, aud::CHUNK * 8, &r, pdMS_TO_TICKS(100));
    const int n = r / 8;
    int pk = 0;
    for (int j = 0; j < n; j++) {
      const uint64_t cur = ((uint64_t)(uint32_t)buf[2 * j] << 32) | (uint32_t)buf[2 * j + 1];
      const float x = gem::micSample(prevFrame, cur) * (1.0f / 256) * (gem::micGain * 0.75f);  // 16-bit scale; the Mic sensitivity (8 -> 6)
      prevFrame = cur;
      pcm[j] = (int16_t)constrain(x, -32767.0f, 32767.0f);
      if (abs(pcm[j]) > pk) pk = abs(pcm[j]);
    }
    micLvl = max(pk, micLvl - micLvl / 16);  // (a slow fall)
    if (!aud::playing) beat::feed(pcm, n);  // the beat in the room (dance to any song); a song on the speaker feeds it itself
    const int16_t* p = pcm;
    size_t left = n;
    while (left > 0) {
      size_t used = 0;
      FrontendOutput fo = FrontendProcessSamples(&fs, p, left, &used);
      p += used;
      left -= used;
      if (!fo.size) continue;
      // one feature frame (10 ms) into the model's input; every `stride` frames: a score
      int8_t* row = in->data.int8 + frames * 40;
      for (size_t i = 0; i < fo.size && i < 40; i++) {
        const float v = fo.values[i] * 0.0390625f;
        row[i] = (int8_t)constrain((int)lroundf(v / inScale) + inZero, -128, 127);
      }
      if (++frames < stride) continue;
      frames = 0;
      if (it->Invoke() != kTfLiteOk) continue;
      float prob = out->type == kTfLiteUInt8 ? out->data.uint8[0] / 255.0f
                                               : (out->data.int8[0] - out->params.zero_point) * out->params.scale;
      hist[hn++ % 20] = prob;
      const int w = constrain(window, 1, 20);
      float avg = 0;
      for (int i = 0; i < w; i++) avg += hist[(hn - 1 - i + 20) % 20];
      avg /= w;
      probNow = avg;
      if (avg > probMax) probMax = avg;
      // (no Apple-style "second chance" - a lower bar for a few seconds after a near miss: it let room
      // noise in at 0.80-0.89)
      static unsigned long secondUntil = 0;
      const float cut = cutoff;
      if (avg >= cut && millis() > ignoreUntilMs) {
        secondUntil = 0;
        gem::wakeAckUntil = millis() + 8000;  // the OLED: "heard you!" now, not after the connection
        setFace("curious");
        hits++;
        lastHitMs = millis();
        xlogSys("WAKE: wake word (score %.2f) - a conversation", avg);
        memset(hist, 0, sizeof(hist));
        ignoreUntilMs = millis() + 3000;
        aud::end();  // the conversation takes the port
        holding = holdingNow = false;
        aud::wwHeld = false;
        stateName = "waiting";
        gem::wantStart = true;
        break;
      }
    }
  }
}
#endif

void begin() {
  load();
  if (!enabled) bootInfo.ok |= 1 << boot::B_WAKE;  // (off by choice: not a fault)
  if (startingMagic == STARTING && esp_reset_reason() == ESP_RST_PANIC) {
    startingMagic = 0;
    stateName = "off (it crashed last time)";
    strlcpy(err, "the last start crashed - off until the next restart", sizeof(err));
    xlogSys("WAKE: the last start crashed - the wake word is off this time");
    return;
  }
  startingMagic = 0;
#if WW_BUILD
  static gem::PsTask t;
  gem::taskStart(t, task, "wake", 12288, 3, 0);
#else
  stateName = "not in this build";
#endif
}

}  // namespace ww
