// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
// Music: Oreo as a little speaker on the desk.
//  - The songs: 16-bit WAVs in /music on the card. Play, pause / resume, next / previous, shuffle, repeat
//    (all / one); the next song starts by itself.
//  - Ambient sounds made on the robot (audio.h): rain, ocean, fire, pink and brown noise - with the sleep timer.
//  - Oreo listens along: the song feeds the beat tracker (audio.h -> beat.h). Desk mode nods on the beat (its
//    head_bob reaction); walk mode bobs the head too when nobody is steering it. Faces: the headphones scene when
//    a song starts (desk), "vibing" / "excited" with the music's loudness (walk).
// Something else stopping the sound (an alarm, a chat, the sleep timer) stops the music for good - no fighting.
// Included by main.cpp after desk_clock.h; mus::tick() runs in loop(), GET /music is the page's API.
#pragma once

namespace mus {

const int MAXF = 48;
char files[MAXF][40];
int nFiles = 0;
bool listed = false;
int idx = -1;             // the song (in files)
bool wantOn = false;      // we're playing (a song or an ambient sound); false = stopped or paused
bool songOn = false;      // a song of ours is playing right now (desk_mode.h bobs along)
bool paused = false;
uint32_t pausedAt = 0;
bool shuffle = false;
int repeatMode = 1;       // 0 off, 1 all, 2 one
bool bop = true;          // head bob along
int ambient = 0;          // 0 = songs, else aud::AMB_NAMES
float vol = 0.5f;
unsigned long startedMs = 0, lastBopMs = 0, faceMs = 0;
uint32_t seed = 7;

void list() {
  nFiles = 0;
  listed = true;
  if (sdSizeMB < 0) return;
  File d = SD_MMC.open("/music");
  if (!d || !d.isDirectory()) return;
  for (File f = d.openNextFile(); f && nFiles < MAXF; f = d.openNextFile()) {
    if (f.isDirectory()) continue;
    const char* n = strrchr(f.name(), '/');
    n = n ? n + 1 : f.name();
    const size_t L = strlen(n);
    if (L < 5 || L >= sizeof(files[0]) || strcasecmp(n + L - 4, ".wav") || n[0] == '.') continue;
    strlcpy(files[nFiles++], n, sizeof(files[0]));
  }
  // sorted by name (a short list: insertion sort)
  for (int i = 1; i < nFiles; i++)
    for (int j = i; j > 0 && strcasecmp(files[j - 1], files[j]) > 0; j--) {
      char t[40];
      strcpy(t, files[j]); strcpy(files[j], files[j - 1]); strcpy(files[j - 1], t);
    }
}

// a song's name for people: "taare_zameen.wav" -> "taare zameen"
void niceName(int i, char* out, size_t n) {
  if (i < 0 || i >= nFiles) { out[0] = 0; return; }
  strlcpy(out, files[i], n);
  char* dot = strrchr(out, '.');
  if (dot) *dot = 0;
  for (char* p = out; *p; p++) if (*p == '_' || *p == '-') *p = ' ';
}

// stop whatever plays and wait for the fade (up to ~2 s)
void hush() {
  if (aud::playing) aud::stopReq = true;
  for (int t = 0; t < 100 && aud::playing; t++) delay(20);
}

bool playIdx(int i, uint32_t fromMs = 0) {
  if (!listed) list();
  if (!nFiles) return false;
  i = (i % nFiles + nFiles) % nFiles;
  hush();
  char path[64];
  snprintf(path, sizeof(path), "/music/%s", files[i]);
  aud::playStartMs = fromMs;
  if (!aud::play(path, vol)) return false;
  idx = i;
  ambient = 0;
  wantOn = songOn = true;
  paused = false;
  startedMs = millis();
  xlog("MUSIC play %s%s", files[i], fromMs ? " (resume)" : "");
  if (activeMode == MODE_DESK && !faceScenePlaying && !fromMs) faceScene("headphones");  // the headphones on
  return true;
}
int nextIdx(int dir) {
  if (nFiles <= 1) return 0;
  if (shuffle) {
    int k;
    do { seed = seed * 1664525u + 1013904223u; k = (seed >> 8) % nFiles; } while (k == idx);
    return k;
  }
  return idx + dir;
}
void stopAll(const char* why) {
  wantOn = songOn = paused = false;
  ambient = 0;
  hush();
  xlog("MUSIC stop (%s)", why);
}
void pause() {
  if (!songOn) return;
  pausedAt = aud::playPosMs;
  hush();
  songOn = false;
  wantOn = false;
  paused = true;
  xlog("MUSIC pause at %lu s", (unsigned long)pausedAt / 1000);
}
void resume() {
  if (paused && idx >= 0) playIdx(idx, pausedAt);
  else if (!songOn) playIdx(idx < 0 ? 0 : idx);
}
bool startAmbient(int kind) {
  hush();
  if (!aud::ambient(kind, vol)) return false;
  ambient = kind;
  wantOn = true;
  songOn = paused = false;
  xlog("MUSIC ambient %s", aud::AMB_NAMES[kind]);
  return true;
}

// asked for in a chat (voice_tools.h): the chat has the speaker - it starts once the chat is over
int pendSong = -2, pendAmb = 0;  // -2 none, -1 resume, >= 0 that song
unsigned long pendMs = 0;

void tick(unsigned long now) {
  if ((pendSong != -2 || pendAmb) && gem::state == gem::S_OFF && !aud::talkOn && !aud::busy) {
    if (now - pendMs > 60000) pendSong = -2, pendAmb = 0;  // (a minute late: forget it)
    else if (pendAmb) { startAmbient(pendAmb); pendAmb = 0; }
    else { if (pendSong == -1) resume(); else playIdx(pendSong); pendSong = -2; }
  }
  // the sound ended: the song's end -> the next one; anything else stopped it -> we stop too
  if (wantOn && !aud::playing && !aud::busy && now - startedMs > 1500) {
    if (songOn && aud::playEnded) {
      const int n = repeatMode == 2 ? idx : nextIdx(1);
      if (repeatMode == 0 && !shuffle && n >= nFiles) { songOn = wantOn = false; xlog("MUSIC end of the list"); }
      else playIdx(n);
    } else {
      songOn = wantOn = false;
      ambient = 0;
      xlog("MUSIC stopped by something else");
    }
  }
  if (!songOn || !aud::playing) return;
  // walk mode, nobody steering: the head bobs on the beat (desk mode does it in its head_bob reaction)
  if (activeMode == MODE_DESK) {
    if (deskm::standby) deskm::standbyExit(now, "music");
    static unsigned long lastAsk = 0;  // (it may say no - napping: ask again later, not every loop)
    if (strcmp(deskm::doingName, "head_bob") != 0 && now - startedMs > 4000 && now - lastAsk > 15000 && !faceScenePlaying &&
        deskm::doing != desk::B_REACT) {
      lastAsk = now;
      deskm::react(desk::E_MUSIC, now);
    }
    return;
  }
  if (activeMode != MODE_WALK || !bop || currentCommand.length() || rcActive() || rcHeadActive || now - headManualMs < 20000 || robotFallen) return;
  if (beat::periodMs > 250 && beat::conf > 0.3f && beat::lastBeatMs) {
    const float ph = fmodf((float)(long)(now - beat::lastBeatMs) / beat::periodMs, 1.0f);
    const float s = cosf(ph * 2 * PI);
    static float sent = 0;
    if (fabsf(s - sent) > 0.5f && now - lastBopMs > 60) {
      headSlewTo(PAN_MID, constrain(TILT_MID + 5 * s, TILT_MIN + 3, TILT_MAX - 6));
      sent = s;
      lastBopMs = now;
    }
  }
  if (now - faceMs > 2500) {  // the face follows the music: loud -> excited, else vibing
    faceMs = now;
    setFaceWithMode(aud::playLvl > 0.55f ? "excited" : "happy", FACE_ANIM_ONCE);
  }
}

// the OLED's card while music plays (oled_ui.h)
bool card(char* title, size_t tn, char* l1, size_t ln) {
  if (!wantOn || !aud::playing) return false;
  if (ambient) {
    strlcpy(title, "Ambient", tn);
    strlcpy(l1, aud::AMB_NAMES[ambient], ln);
    return true;
  }
  strlcpy(title, "Music", tn);
  niceName(idx, l1, ln);
  return true;
}

// ---- GET /music: ?list=1 (re-read the card) ?play=N ?next=1 ?prev=1 ?pause=1 ?resume=1 ?stop=1 ?shuffle=0|1
//      ?repeat=0|1|2 ?vol=0..1 ?bop=0|1 ?ambient=rain|ocean|fire|pink|brown  -> the state as JSON ----
esp_err_t music_handler(httpd_req_t* req) {
  char q[160] = {0}, v[24];
  httpd_req_get_url_query_str(req, q, sizeof(q));
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  if (!listed || httpd_query_key_value(q, "list", v, sizeof(v)) == ESP_OK) list();
  if (httpd_query_key_value(q, "vol", v, sizeof(v)) == ESP_OK) {
    vol = constrain(atof(v), 0.0, 1.0);
    if (aud::playing) aud::playVol = vol;
  }
  if (httpd_query_key_value(q, "shuffle", v, sizeof(v)) == ESP_OK) shuffle = v[0] == '1';
  if (httpd_query_key_value(q, "repeat", v, sizeof(v)) == ESP_OK) repeatMode = constrain(atoi(v), 0, 2);
  if (httpd_query_key_value(q, "bop", v, sizeof(v)) == ESP_OK) bop = v[0] == '1';
  if (httpd_query_key_value(q, "play", v, sizeof(v)) == ESP_OK) playIdx(atoi(v));
  if (httpd_query_key_value(q, "next", v, sizeof(v)) == ESP_OK) playIdx(nextIdx(1));
  if (httpd_query_key_value(q, "prev", v, sizeof(v)) == ESP_OK) playIdx(songOn && aud::playPosMs > 4000 ? idx : nextIdx(-1));  // (well in: from the top)
  if (httpd_query_key_value(q, "pause", v, sizeof(v)) == ESP_OK) pause();
  if (httpd_query_key_value(q, "resume", v, sizeof(v)) == ESP_OK) resume();
  if (httpd_query_key_value(q, "stop", v, sizeof(v)) == ESP_OK) stopAll("page");
  if (httpd_query_key_value(q, "ambient", v, sizeof(v)) == ESP_OK)
    for (int k = 1; k <= 5; k++) if (!strcmp(v, aud::AMB_NAMES[k])) { startAmbient(k); break; }

  static char* out = (char*)heap_caps_malloc(3000, MALLOC_CAP_SPIRAM);
  if (!out) return httpd_resp_send_500(req);
  int o = snprintf(out, 3000, "{\"files\":[");
  for (int i = 0; i < nFiles && o < 2600; i++) {
    char nm[40];
    niceName(i, nm, sizeof(nm));
    for (char* p = nm; *p; p++) if (*p == '"' || *p == '\\' || (uint8_t)*p < 32) *p = ' ';
    o += snprintf(out + o, 3000 - o, "%s\"%s\"", i ? "," : "", nm);
  }
  o += snprintf(out + o, 3000 - o,
                "],\"idx\":%d,\"playing\":%d,\"song\":%d,\"paused\":%d,\"ambient\":\"%s\",\"posMs\":%lu,\"lenMs\":%lu,\"vol\":%.2f,"
                "\"shuffle\":%d,\"repeat\":%d,\"bop\":%d,\"bpm\":%.0f,\"beatConf\":%.2f,\"card\":%d}",
                idx, wantOn && aud::playing ? 1 : 0, songOn ? 1 : 0, paused ? 1 : 0, aud::AMB_NAMES[ambient],
                (unsigned long)(paused ? pausedAt : aud::playPosMs), (unsigned long)aud::playLenMs, vol, shuffle ? 1 : 0, repeatMode,
                bop ? 1 : 0, (float)beat::bpm, (float)beat::conf, sdSizeMB >= 0 ? 1 : 0);
  return httpd_resp_send(req, out, o);
}

}  // namespace mus
