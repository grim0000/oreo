// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
// Who's this? Oreo learns the faces of the people around it and greets them by name.
// The camera task (desk / life mode) finds a face, its second-stage detector finds the eyes / nose / mouth, and
// a copy of that frame goes to this task: esp-dl's face recognizer (112 V1 S8) turns it into a "fingerprint"
// (512 numbers) and compares it with the people it knows. At most every 2 s, only while a face is in view.
// The people are kept on the SD card (/sesame/faces.bin): the chip has no partition for them.
// Included by main.cpp after gemini.h (gem::taskStart).
#pragma once
// FACE_ID 1 needs app slots over 3 MB (the recognizer model is ~1.7 MB): partitions with 5 MB slots, flashed over USB
#ifndef FACE_ID
#define FACE_ID 0  // OFF by default: its models take ~65 KB of internal RAM from 20 s after boot (idle 44 KB
                   // left instead of ~109, desk / life down to 6 KB): WiFi crawls, the chat's TLS to Google fails
#endif
#if !FACE_ID
namespace fid {  // off: the same names, doing nothing
volatile unsigned long whoMs = 0;
char whoName[24] = "";
bool wanted(unsigned long) { return false; }
void offer(const uint16_t*, const std::vector<int>&) {}
void begin() {}
const char* recent(unsigned long) { return ""; }
esp_err_t faces_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_sendstr(req, "{\"status\":\"face recognition needs the bigger flash layout (a USB flash)\",\"learning\":0,\"who\":\"\",\"sim\":0,\"ready\":0,\"off\":1,\"people\":[]}");
}
}  // namespace fid
#else
#include "face_recognition_112_v1_s8.hpp"

namespace fid {

FaceRecognition112V1S8* rec = nullptr;
uint16_t* img = nullptr;                  // the frame handed over (320 x 240 RGB565, PSRAM)
std::vector<int> kp;                      // its face's 5 points (eyes, nose, mouth corners)
volatile bool busy = false;
volatile unsigned long lastOfferMs = 0;
gem::PsTask psTask;
TaskHandle_t task = nullptr;
// the last person recognized
char whoName[24] = "";
volatile float whoSim = 0;
volatile unsigned long whoMs = 0;
// learning a new face: N more captures under this name
char enrollName[24] = "";
volatile int enrollLeft = 0;
char status[72] = "";
volatile bool saveReq = false, loaded = false;
volatile int known = 0;
const char* const FACES_FILE = "/sesame/faces.bin";
const float THRESH = 0.55f;               // similarity to call it a match (esp-dl's examples: 0.5 - 0.6)

// the camera task asks: is a frame wanted now?
bool wanted(unsigned long now) {
  if (!img || busy || !task || !rec) return false;
  return now - lastOfferMs > (enrollLeft > 0 ? 700UL : 2000UL);
}
// the camera task hands a frame over (it's copied: the camera reuses its buffer)
void offer(const uint16_t* frame, const std::vector<int>& points) {
  if (!img || busy || points.size() != 10) return;
  memcpy(img, frame, 320 * 240 * 2);
  kp = points;
  busy = true;
  lastOfferMs = millis();
  xTaskNotifyGive(task);
}

void save() {
  if (!rec || sdSizeMB < 0) return;
  if (!SD_MMC.exists("/sesame")) SD_MMC.mkdir("/sesame");
  File f = SD_MMC.open(FACES_FILE, FILE_WRITE);
  if (!f) return;
  std::vector<face_info_t> ids = rec->get_enrolled_ids();
  const uint32_t n = ids.size();
  f.write((const uint8_t*)"OFID1", 5);
  f.write((const uint8_t*)&n, 4);
  for (auto& i : ids) {
    char nm[24] = "";
    strlcpy(nm, i.name.c_str(), sizeof(nm));
    dl::Tensor<float>& e = rec->get_face_emb(i.id);
    const uint32_t len = e.get_size();
    f.write((const uint8_t*)nm, sizeof(nm));
    f.write((const uint8_t*)&len, 4);
    f.write((const uint8_t*)e.get_element_ptr(), len * sizeof(float));
  }
  f.close();
  xlog("FACES saved %u", (unsigned)n);
}
void load() {
  loaded = true;
  if (!rec || sdSizeMB < 0) return;
  File f = SD_MMC.open(FACES_FILE, FILE_READ);
  if (!f) return;
  char magic[5];
  uint32_t n = 0;
  if (f.read((uint8_t*)magic, 5) != 5 || memcmp(magic, "OFID1", 5) || f.read((uint8_t*)&n, 4) != 4 || n > 50) { f.close(); return; }
  for (uint32_t k = 0; k < n; k++) {
    char nm[24];
    uint32_t len = 0;
    if (f.read((uint8_t*)nm, sizeof(nm)) != sizeof(nm) || f.read((uint8_t*)&len, 4) != 4 || len == 0 || len > 1024) break;
    float* buf = (float*)dl::tool::calloc_aligned_prefer(len, sizeof(float), 16);
    if (!buf) break;
    if (f.read((uint8_t*)buf, len * sizeof(float)) != (int)(len * sizeof(float))) { dl::tool::free_aligned_prefer(buf); break; }
    nm[sizeof(nm) - 1] = 0;
    dl::Tensor<float> t;
    t.set_element(buf, false).set_shape({(int)len});  // (the buffer stays: the recognizer may keep pointing at it, ~2 KB a capture)
    rec->enroll_id(t, std::string(nm), false);
  }
  f.close();
  known = rec->get_enrolled_id_num();
  xlog("FACES loaded %d", (int)known);
}
// the names it knows (each person may have a few captures)
int names(char out[][24], int maxN) {
  if (!rec) return 0;
  int n = 0;
  for (auto& i : rec->get_enrolled_ids()) {
    bool dup = false;
    for (int k = 0; k < n; k++) if (!strcmp(out[k], i.name.c_str())) dup = true;
    if (!dup && n < maxN) strlcpy(out[n++], i.name.c_str(), 24);
  }
  return n;
}
int forget(const char* name) {
  if (!rec) return 0;
  int gone = 0;
  for (auto& i : rec->get_enrolled_ids_with_name(std::string(name))) { rec->delete_id(i.id, false); gone++; }
  known = rec->get_enrolled_id_num();
  if (gone) saveReq = true;
  return gone;
}

void taskFn(void*) {
  vTaskDelay(pdMS_TO_TICKS(20000));  // (after the boot: the model loads, the people it knows come off the SD card)
  rec = new FaceRecognition112V1S8();
  rec->set_thresh(THRESH);
  load();
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (saveReq && !busy) { saveReq = false; save(); continue; }
    const std::vector<int> shape = {240, 320, 3};
    if (enrollLeft > 0 && enrollName[0]) {
      const int id = rec->enroll_id(img, shape, kp, std::string(enrollName), false);
      enrollLeft--;
      known = rec->get_enrolled_id_num();
      snprintf(status, sizeof(status), id >= 0 ? "learning %s's face: %d more" : "learning %s: try again (%d more)", enrollName, (int)enrollLeft);
      if (enrollLeft == 0) {
        snprintf(status, sizeof(status), "I know %s now", enrollName);
        saveReq = true;
        strlcpy(whoName, enrollName, sizeof(whoName));
        whoSim = 1;
        whoMs = millis();
        xlog("FACES learned %s", enrollName);
      }
    } else if (rec->get_enrolled_id_num() > 0) {
      face_info_t r = rec->recognize(img, shape, kp);
      if (r.id >= 0 && r.similarity >= THRESH) {
        strlcpy(whoName, r.name.c_str(), sizeof(whoName));
        whoSim = r.similarity;
        whoMs = millis();
      }
    }
    if (saveReq) { saveReq = false; save(); }
    busy = false;
  }
}

void begin() {
  img = (uint16_t*)heap_caps_malloc(320 * 240 * 2, MALLOC_CAP_SPIRAM);
  if (!img) return;
  if (gem::taskStart(psTask, taskFn, "faceId", 12288, 1, 0)) task = psTask.h;
}
// someone it knows was seen in the last ms: their name, else ""
const char* recent(unsigned long ms) { return whoName[0] && millis() - whoMs < ms ? whoName : ""; }

// ---- GET /faces: ?enroll=<name> (3 captures - look at it), ?forget=<name>; the list ----
esp_err_t faces_handler(httpd_req_t* req) {
  char q[96] = "", v[32] = "";
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
    if (httpd_query_key_value(q, "enroll", v, sizeof(v)) == ESP_OK && v[0]) {
      for (char* c = v; *c; c++) if (*c == '+') *c = ' ';
      strlcpy(enrollName, v, sizeof(enrollName));
      enrollLeft = 3;
      snprintf(status, sizeof(status), "look at me, %s...", enrollName);
      xlog("FACES learning %s", enrollName);
    }
    if (httpd_query_key_value(q, "forget", v, sizeof(v)) == ESP_OK && v[0]) {
      for (char* c = v; *c; c++) if (*c == '+') *c = ' ';
      const int g = forget(v);
      snprintf(status, sizeof(status), g ? "forgot %s" : "%s wasn't known", v);
    }
  }
  char nm[12][24];
  const int n = names(nm, 12);
  char o[600];
  int k = snprintf(o, sizeof(o), "{\"status\":\"%s\",\"learning\":%d,\"who\":\"%s\",\"sim\":%.2f,\"ready\":%d,\"people\":[",
                   status, (int)enrollLeft, recent(10000), (float)whoSim, rec ? 1 : 0);
  for (int i = 0; i < n && k < (int)sizeof(o) - 40; i++) k += snprintf(o + k, sizeof(o) - k, "%s\"%s\"", i ? "," : "", nm[i]);
  snprintf(o + k, sizeof(o) - k, "]}");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_sendstr(req, o);
}

}  // namespace fid
#endif  // FACE_ID
