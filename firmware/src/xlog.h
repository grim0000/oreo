// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
// Flight recorder: every Explore action / decision plus telemetry, kept in a raw flash
// partition ("explog", 12.8 MB = many hours) as a ring of 4 KB sectors. Download at /log,
// clear with /log?clear=1.
//
// Flash access (write, erase, even read) stops the CPU cache for a moment. The camera
// driver can't keep up with the pixel stream then and corrupts memory (image data
// written over cam_task's stack). So nothing touches the flash while the
// camera runs: the vision task holds camLock while the camera is on and switches it off
// for a moment when asked (lockFlash). New lines wait in RTC memory, which survives crash
// resets, so the last lines before a crash still reach the log after the reboot. With the
// camera on they're written every ~25 s (or 3 KB); with it off, every second.
//
// Each sector starts with "XLOG" + a sequence number; at boot the sector with the highest
// number is the current one. Clearing just remembers a sequence number (NVS) and skips
// older sectors.
//
// With an SD card (xlogUseSd, called once the card is mounted) the log goes to
// /logs/explore.txt on the card instead. The card has its own controller and DMA - writing it
// never stops the CPU cache, so the camera doesn't pause: lines are written every second, and
// RC mode doesn't have to stop logging. Clearing keeps the old file as /logs/old-<boot>-<n>.txt.
// Settings (NVS, in flash) still wait for a camera-safe moment.
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include <esp_attr.h>
#include <esp_http_server.h>
#include <esp_partition.h>
#include <freertos/semphr.h>
#include <stdarg.h>
#include <SD_MMC.h>

namespace xl {
const uint32_t SECTOR = 4096;
const uint32_t HDR = 8;
const uint32_t MAGIC = 0x474F4C58;       // "XLOG"
const uint32_t PEND_MAGIC = 0x444E4550;  // "PEND"
const size_t PEND_SIZE = 6 * 1024;       // RTC slow memory (8 KB on the S3)
const size_t FLUSH_BYTES = 3 * 1024;     // camera on: write at this much...
const unsigned long FLUSH_MS = 25000;    // ...or when the oldest line is this old

struct Pending {
  uint32_t magic;
  uint32_t len;
  char data[PEND_SIZE];
};
RTC_NOINIT_ATTR Pending pend;              // lines not yet in flash (survives crash resets)

const esp_partition_t* part = nullptr;
uint32_t sectors = 0, cur = 0, off = 0, seq = 0, clearSeq = 1;
uint8_t flushBuf[SECTOR];                  // internal RAM: flash writes / reads go through here
uint8_t (&httpBuf)[SECTOR] = *(uint8_t(*)[SECTOR])heap_caps_calloc(1, sizeof(uint8_t) * (SECTOR), MALLOC_CAP_SPIRAM);
volatile uint32_t dropped = 0, storedBytes = 0, flushes = 0;
volatile bool clearReq = false, flushSoon = false;
volatile bool camOn = false;               // vision task: camera running (holds camLock)
std::atomic<int> camWaiters(0);            // tasks waiting for the camera to let go
volatile unsigned long lastCamGapMs = 0;   // how long the camera was off for the last flush
volatile bool recovered = false;
volatile bool paused = false;              // RC mode: nothing logged, no flash writes, camera never stops
volatile bool holdFlash = false;           // someone watches the video / RC is open: settings wait (the
                                           // camera would have to stop for the write - "reconnecting")
volatile bool recording = true;            // lines are kept only while exploring (plus boot / system
                                           // lines, xlogSys): walk / face tracking aren't logged, so
                                           // face tracking's camera never pauses for a log write
volatile uint32_t skippedLines = 0;
volatile bool httpBusy = false;             // a log download is running (the web self-check waits)
uint32_t bootNo = 0;
volatile bool sdOn = false;                 // the log lives on the SD card
File sdFile;
// The card can stop answering now and then (every folder reads as
// empty, every file "doesn't exist", until a restart). Every 10 s (or when a page found nothing) this
// task checks it still answers and mounts it again if not. sdHeals: how often that was needed.
volatile bool sdHealReq = false;
volatile uint32_t sdHeals = 0;
volatile bool sdDead = false;  // the card stopped answering: nothing touches it any more (a restart mounts it again)
const char* SD_LOG = "/logs/explore.txt";
SemaphoreHandle_t ramMutex = nullptr, flashMutex = nullptr, camLock = nullptr;
void (*onFlashSafe)() = nullptr;           // more flash work (settings saves) while the camera is off
Preferences prefs;

// Get the flash to ourselves: the camera switches off (or is off already)
bool lockFlash(unsigned long waitMs) {
  camWaiters++;
  const bool ok = xSemaphoreTake(camLock, pdMS_TO_TICKS(waitMs)) == pdTRUE;
  camWaiters--;
  return ok;
}

void unlockFlash() { xSemaphoreGive(camLock); }

void newSector(uint32_t idx, uint32_t s) {
  esp_partition_erase_range(part, idx * SECTOR, SECTOR);
  const uint32_t hdr[2] = {MAGIC, s};
  esp_partition_write(part, idx * SECTOR, hdr, HDR);
  cur = idx;
  seq = s;
  off = HDR;
}

void writeFlash(const uint8_t* data, size_t len) {
  while (len > 0) {
    if (off >= SECTOR) newSector((cur + 1) % sectors, seq + 1);
    const size_t n = min((size_t)(SECTOR - off), len);
    esp_partition_write(part, cur * SECTOR + off, data, n);
    off += n;
    data += n;
    len -= n;
    storedBytes += n;
  }
}

// Length of the text in a sector read into buf (up to the first erased 0xFF byte)
uint32_t textEnd(const uint8_t* buf) {
  uint32_t e = HDR;
  while (e < SECTOR && buf[e] != 0xFF) e++;
  return e;
}

bool sectorSeq(uint32_t idx, uint32_t* s) {
  uint32_t hdr[2];
  if (esp_partition_read(part, idx * SECTOR, hdr, HDR) != ESP_OK || hdr[0] != MAGIC) return false;
  *s = hdr[1];
  return true;
}

// Lines for other files on the card (the governor's log, the battery log): queued from any task,
// written by this task within a second - the control loop never waits for the card. In PSRAM.
struct SideLine {
  const char* path;
  const char* header;  // written first if the file is new (or nullptr)
  char text[320];
};
const int SIDE_N = 16;
SideLine* side = nullptr;
volatile int sideHead = 0, sideCount = 0;
volatile uint32_t sideDropped = 0;
volatile uint32_t sideFails = 0;   // side lines the card wouldn't take (open / write failed)
volatile float sideMaxMs = 0;  // the slowest card write so far (ms)

void drainSide() {
  static SideLine l;
  for (;;) {
    xSemaphoreTake(ramMutex, portMAX_DELAY);
    if (!sideCount) {
      xSemaphoreGive(ramMutex);
      return;
    }
    l = side[sideHead];
    sideHead = (sideHead + 1) % SIDE_N;
    sideCount--;
    xSemaphoreGive(ramMutex);
    const unsigned long t0 = millis();
    xSemaphoreTake(flashMutex, portMAX_DELAY);
    File f = SD_MMC.open(l.path, FILE_APPEND);
    if (f) {
      if (l.header && f.size() == 0) f.println(l.header);
      if (f.print(l.text) == 0) sideFails++;
      f.close();
    } else {
      sideFails++;  // (so a side log that stops growing doesn't fail silently)
    }
    xSemaphoreGive(flashMutex);
    const float ms = millis() - t0;
    if (ms > sideMaxMs) sideMaxMs = ms;
  }
}

// SD card: move the waiting lines to the log file (no camera lock needed)
void drainSd() {
  for (;;) {
    size_t n = 0;
    xSemaphoreTake(ramMutex, portMAX_DELAY);
    if (pend.len > 0) {
      n = min((size_t)pend.len, (size_t)SECTOR);
      memcpy(flushBuf, pend.data, n);
      memmove(pend.data, pend.data + n, pend.len - n);
      pend.len -= n;
    }
    xSemaphoreGive(ramMutex);
    if (n == 0) return;
    xSemaphoreTake(flashMutex, portMAX_DELAY);
    if (sdFile) {
      const size_t w = sdFile.write(flushBuf, n);
      if (w != n) dropped += n - w;
      storedBytes = sdFile.size();
    }
    xSemaphoreGive(flashMutex);
  }
}

// Move the waiting lines to flash (flash locked)
void drain() {
  if (sdOn) { if (!sdDead) drainSd(); return; }
  for (;;) {
    size_t n = 0;
    xSemaphoreTake(ramMutex, portMAX_DELAY);
    if (pend.len > 0) {
      n = min((size_t)pend.len, (size_t)SECTOR);
      memcpy(flushBuf, pend.data, n);
      memmove(pend.data, pend.data + n, pend.len - n);
      pend.len -= n;
    }
    xSemaphoreGive(ramMutex);
    if (n == 0) return;
    xSemaphoreTake(flashMutex, portMAX_DELAY);
    writeFlash(flushBuf, n);
    xSemaphoreGive(flashMutex);
  }
}

// Before the power is switched off: every waiting line onto the card, the file closed, the card
// unmounted (a power cut mid-write can break the FAT). After it nothing more is written.
void shutdownFlush() {
  if (!sdOn || sdDead) return;  // (a dead card: the lines stay in RTC memory, written after the restart)
  // never waits long: a card stuck in the log task holds this lock, and a restart must still happen
  if (xSemaphoreTake(flashMutex, pdMS_TO_TICKS(500)) != pdTRUE) return;
  xSemaphoreGive(flashMutex);
  drainSd();
  if (sideCount) drainSide();
  if (xSemaphoreTake(flashMutex, pdMS_TO_TICKS(500)) != pdTRUE) return;
  if (sdFile) { sdFile.flush(); sdFile.close(); }
  SD_MMC.end();
  sdOn = false;
  xSemaphoreGive(flashMutex);
}

volatile unsigned long taskBeatMs = 0;  // the log task's last pass (stuck in a card call: loop() restarts the robot)
void task(void*) {
  unsigned long lastFlushMs = millis();
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    taskBeatMs = millis();
    if (sdDead) continue;  // the card stopped answering: the lines wait in RTC memory for the restart
    if (sdOn) {
      if (clearReq) {  // keep the old log on the card, start a new file
        xSemaphoreTake(flashMutex, portMAX_DELAY);
        sdFile.close();
        char old[48];
        for (int k = 1; k < 1000; k++) {
          snprintf(old, sizeof(old), "/logs/old-%u-%d.txt", (unsigned)bootNo, k);
          if (!SD_MMC.exists(old)) break;
        }
        SD_MMC.rename(SD_LOG, old);
        sdFile = SD_MMC.open(SD_LOG, FILE_APPEND);
        storedBytes = 0;
        clearReq = false;
        xSemaphoreGive(flashMutex);
      }
      if (pend.len > 0) {
        drainSd();
        xSemaphoreTake(flashMutex, portMAX_DELAY);
        if (sdFile) sdFile.flush();  // on the card within a second (a crash loses at most that)
        xSemaphoreGive(flashMutex);
        flushes++;
      }
      if (sideCount) drainSide();
      static unsigned long healMs = 0;
      static int healMisses = 0;
      if ((sdHealReq || millis() - healMs > 10000) && !httpBusy) {
        healMs = millis();
        sdHealReq = false;
        xSemaphoreTake(flashMutex, portMAX_DELAY);
        File r = SD_MMC.open("/logs");
        const bool ok = r && r.isDirectory();
        if (r) r.close();
        // Never remounted while running: SD_MMC.end() under someone else's open file (a /log download, music,
        // a voice clip) hangs the card for good. 3 misses in a row = the card is gone: logging stops
        // (the lines wait in RTC memory) and the web watchdog restarts the robot once it's idle - a clean mount.
        healMisses = ok ? 0 : healMisses + 1;
        if (healMisses >= 3 && !sdDead) {
          sdDead = true;
          sdHeals++;
          Serial.println("SD card: stopped answering - logging paused, restarting once idle");
        }
        xSemaphoreGive(flashMutex);
      }
      // settings still live in flash (NVS): those wait for a camera-safe moment as before
      if (flushSoon && !holdFlash && lockFlash(3000)) {
        flushSoon = false;
        if (onFlashSafe) onFlashSafe();
        unlockFlash();
      }
      continue;
    }
    if (!part) continue;
    if (paused) continue;  // RC mode: saved afterwards (settings too)
    const size_t waiting = pend.len;
    const bool due = clearReq || flushSoon || waiting >= FLUSH_BYTES ||
                     (waiting > 0 && (!camOn || millis() - lastFlushMs >= FLUSH_MS));
    if (!due || !lockFlash(3000)) continue;
    flushSoon = false;
    if (clearReq) {
      xSemaphoreTake(flashMutex, portMAX_DELAY);
      clearSeq = seq + 1;
      prefs.putUInt("clear", clearSeq);
      newSector((cur + 1) % sectors, seq + 1);
      storedBytes = 0;
      clearReq = false;
      xSemaphoreGive(flashMutex);
    }
    if (onFlashSafe) onFlashSafe();
    drain();
    flushes++;
    lastFlushMs = millis();
    unlockFlash();
  }
}
}  // namespace xl

// Append one line: "<uptime s.ms> <text>". Also echoed to Serial. Safe from any task.
// xlog: only while recording (exploring); xlogSys: always (boot / crash reason, web restarts).
void xlogv(bool always, const char* fmt, va_list ap) {
  char line[384];
  const unsigned long ms = millis();
  int n = snprintf(line, sizeof(line), "%lu.%03lu ", ms / 1000, ms % 1000);
  n += vsnprintf(line + n, sizeof(line) - n - 1, fmt, ap);
  n = min(n, (int)sizeof(line) - 2);
  while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) n--;
  line[n++] = '\n';
  line[n] = 0;
  Serial.write((const uint8_t*)line, n);
  if (!always && !xl::recording) return;
  if (xl::paused && !xl::sdOn) { xl::skippedLines++; return; }
  if (!xl::ramMutex) return;
  if (xSemaphoreTake(xl::ramMutex, pdMS_TO_TICKS(20)) != pdTRUE) { xl::dropped += n; return; }
  if (xl::pend.len + n <= xl::PEND_SIZE) {
    memcpy(xl::pend.data + xl::pend.len, line, n);
    xl::pend.len += n;
  } else {
    xl::dropped += n;
  }
  xSemaphoreGive(xl::ramMutex);
}

// One line (with its "\n") appended to a file on the SD card by the log task, within a second.
// Never waits: false if there's no card or the queue is full (counted in xl::sideDropped).
bool sdAppend(const char* path, const char* header, const char* text) {
  using namespace xl;
  if (!sdOn || !side || !ramMutex) return false;
  if (xSemaphoreTake(ramMutex, pdMS_TO_TICKS(5)) != pdTRUE) { sideDropped++; return false; }
  bool ok = sideCount < SIDE_N;
  if (ok) {
    SideLine& l = side[(sideHead + sideCount) % SIDE_N];
    l.path = path;
    l.header = header;
    strlcpy(l.text, text, sizeof(l.text));
    sideCount++;
  } else {
    sideDropped++;
  }
  xSemaphoreGive(ramMutex);
  return ok;
}

void xlog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void xlog(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  xlogv(false, fmt, ap);
  va_end(ap);
}

void xlogSys(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void xlogSys(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  xlogv(true, fmt, ap);
  va_end(ap);
}

// Call first thing in setup() (camera still off). Finds where the log left off; lines that
// were waiting in RTC memory when the chip reset get written first. Returns false without
// the partition.
bool xlogBegin() {
  using namespace xl;
  ramMutex = xSemaphoreCreateMutex();
  flashMutex = xSemaphoreCreateMutex();
  camLock = xSemaphoreCreateMutex();
  recovered = pend.magic == PEND_MAGIC && pend.len > 0 && pend.len <= PEND_SIZE;
  if (pend.magic != PEND_MAGIC || pend.len > PEND_SIZE) {
    pend.magic = PEND_MAGIC;  // power-on: RTC memory holds garbage
    pend.len = 0;
  }
  prefs.begin("xlog", false);
  bootNo = prefs.getUInt("boots", 0) + 1;
  prefs.putUInt("boots", bootNo);
  clearSeq = prefs.getUInt("clear", 1);
  part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "explog");
  if (!part) return false;
  sectors = part->size / SECTOR;
  uint32_t best = 0, bestSeq = 0, count = 0;
  bool any = false;
  for (uint32_t i = 0; i < sectors; i++) {
    uint32_t s;
    if (!sectorSeq(i, &s)) continue;
    if (s >= clearSeq) count++;
    if (!any || s > bestSeq) { any = true; best = i; bestSeq = s; }
  }
  if (!any) {
    newSector(0, max(clearSeq, (uint32_t)1));
  } else {
    cur = best;
    seq = bestSeq;
    esp_partition_read(part, cur * SECTOR, flushBuf, SECTOR);
    off = textEnd(flushBuf);
    if (seq < clearSeq) newSector((cur + 1) % sectors, clearSeq);
    else if (off >= SECTOR) newSector((cur + 1) % sectors, seq + 1);
    storedBytes = count > 0 ? (count - 1) * (SECTOR - HDR) + (off - HDR) : 0;
  }
  drain();  // lines from before a crash reset (camera is off at boot)
  xTaskCreatePinnedToCore(task, "xlog", 8192, nullptr, 1, nullptr, 0);  // 8 KB stack: 4 KB overflows when saving many settings at once
  return true;
}

// Switch the log to the SD card (call once it's mounted). Lines waiting from before (boot, or a
// crash reset) go to the card first.
bool xlogUseSd() {
  using namespace xl;
  if (!SD_MMC.exists("/logs")) SD_MMC.mkdir("/logs");
  File f = SD_MMC.open(SD_LOG, FILE_APPEND);
  if (!f) return false;
  xSemaphoreTake(flashMutex, portMAX_DELAY);
  sdFile = f;
  storedBytes = f.size();
  if (!side) side = (SideLine*)heap_caps_calloc(SIDE_N, sizeof(SideLine), MALLOC_CAP_SPIRAM);
  sdOn = true;
  xSemaphoreGive(flashMutex);
  if (!part) xTaskCreatePinnedToCore(task, "xlog", 8192, nullptr, 1, nullptr, 0);  // no flash log: start the task here
  return true;
}

// GET /log            whole log since the last clear (oldest first)
// GET /log?kb=N       only about the last N KB
// GET /log?clear=1    start a new log
// The camera is off while the log is read out (flash reads stop the cache too).
esp_err_t xlogHttp(httpd_req_t* req) {
  using namespace xl;
  char q[64] = "", val[16];
  httpd_req_get_url_query_str(req, q, sizeof(q));
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  if (httpd_query_key_value(q, "clear", val, sizeof(val)) == ESP_OK && val[0] == '1') {
    clearReq = true;
    xlogSys("LOG cleared from the web page");
    return httpd_resp_sendstr(req, "log cleared\n");
  }
  const bool oldFlash = httpd_query_key_value(q, "flash", val, sizeof(val)) == ESP_OK;  // /log?flash=1: the old flash log
  if (sdOn && sdDead && !oldFlash) return httpd_resp_sendstr(req, "SD card not answering - the robot restarts once it's idle\n");
  if (sdOn && !oldFlash) {  // the log file on the SD card (the camera keeps running)
    uint32_t skip = 0;
    // (never waits forever: a card stuck in the log task holds this lock, and port 80 has only one task)
    if (xSemaphoreTake(flashMutex, pdMS_TO_TICKS(3000)) != pdTRUE) return httpd_resp_sendstr(req, "SD card busy - try again\n");
    httpBusy = true;
    drainSd();
    if (sdFile) sdFile.flush();
    xSemaphoreGive(flashMutex);
    File f = SD_MMC.open(SD_LOG, FILE_READ);
    if (f && httpd_query_key_value(q, "kb", val, sizeof(val)) == ESP_OK) {
      const uint32_t want = (uint32_t)max(1, atoi(val)) * 1024;
      if (f.size() > want) skip = f.size() - want;
      f.seek(skip);
    }
    const unsigned long t0 = millis();
    auto sendAll = [&](const char* d, size_t n) -> bool {
      while (n > 0) {
        const int r = httpd_send(req, d, n);
        if (r <= 0) return false;
        d += r;
        n -= r;
      }
      return true;
    };
    const char* head = "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n"
                       "Content-Disposition: inline; filename=\"explore-log.txt\"\r\n"
                       "Access-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    bool ok = sendAll(head, strlen(head));
    uint32_t sent = 0;
    int r;
    while (ok && f && (r = f.read(httpBuf, SECTOR)) > 0) {
      ok = sendAll((const char*)httpBuf, r);
      sent += r;
    }
    if (f) f.close();
    if (ok) {
      char foot[200];
      const unsigned long ms = millis();
      snprintf(foot, sizeof(foot),
               "# end of log (SD card): uptime %lu.%03lu s, boot #%u, %u KB sent in %lu ms, %u bytes dropped\n",
               ms / 1000, ms % 1000, (unsigned)bootNo, (unsigned)(sent / 1024), ms - t0, (unsigned)dropped);
      sendAll(foot, strlen(foot));
    }
    httpBusy = false;
    return ESP_FAIL;  // raw response: the server closes the connection
  }
  if (!part) return httpd_resp_sendstr(req, "no log partition (flash the firmware with partitions_explog.csv)\n");
  uint32_t maxSectors = sectors;
  if (httpd_query_key_value(q, "kb", val, sizeof(val)) == ESP_OK) maxSectors = max(1, atoi(val) / 4 + 1);
  if (!lockFlash(5000)) return httpd_resp_sendstr(req, "busy - try again in a moment\n");
  const unsigned long t0 = millis();
  drain();  // include everything logged so far
  xSemaphoreTake(flashMutex, portMAX_DELAY);
  const uint32_t last = cur, lastSeq = seq;
  xSemaphoreGive(flashMutex);
  // One raw response, no chunked encoding (chunked managed only ~20 KB/s - a long download took
  // minutes and the web self-check could restart the server meanwhile)
  httpBusy = true;
  auto sendAll = [&](const char* d, size_t n) -> bool {
    while (n > 0) {
      const int r = httpd_send(req, d, n);
      if (r <= 0) return false;
      d += r;
      n -= r;
    }
    return true;
  };
  const char* head = "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n"
                     "Content-Disposition: inline; filename=\"explore-log.txt\"\r\n"
                     "Access-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
  bool ok = sendAll(head, strlen(head));
  // The log is contiguous in the ring: sequence number s lives (lastSeq - s) sectors before `last`
  const uint32_t span = min(maxSectors, sectors);
  const uint32_t firstSeq = max(clearSeq, (uint32_t)(lastSeq + 1 >= span ? lastSeq + 1 - span : 0));
  uint32_t sent = 0;
  for (uint32_t s = firstSeq; s <= lastSeq && ok; s++) {
    const uint32_t idx = (last + sectors - (lastSeq - s)) % sectors;
    uint32_t hs = 0;
    xSemaphoreTake(flashMutex, portMAX_DELAY);
    const bool rd = sectorSeq(idx, &hs) && hs == s &&
                    esp_partition_read(part, idx * SECTOR, httpBuf, SECTOR) == ESP_OK;
    xSemaphoreGive(flashMutex);
    if (!rd) continue;
    const uint32_t e = textEnd(httpBuf);
    if (e > HDR) {
      ok = sendAll((const char*)httpBuf + HDR, e - HDR);
      sent += e - HDR;
    }
  }
  unlockFlash();
  if (ok) {
    char foot[200];
    const unsigned long ms = millis();
    snprintf(foot, sizeof(foot),
             "# end of log: uptime %lu.%03lu s, boot #%u, %u KB sent in %lu ms, %u bytes dropped, %u flushes\n",
             ms / 1000, ms % 1000, (unsigned)bootNo, (unsigned)(sent / 1024), ms - t0, (unsigned)dropped,
             (unsigned)flushes);
    sendAll(foot, strlen(foot));
  }
  httpBusy = false;
  return ESP_FAIL;  // raw response: the server closes the connection
}
