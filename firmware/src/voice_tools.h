// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
// Voice control of the desk tools: Gemini's tools timer, alarm, focus, music and sleep_timer
// (declared in gemini.h's TOOLS_JSON; gemini.h's tool loop calls deskToolCall). "Set a tea timer for 4
// minutes", "wake me at 7 on weekdays", "start a focus session", "play some rain", "sleep in 30 minutes".
// Music needs the speaker the chat is using: it's queued (music.h) and the chat ends itself.
// Included by main.cpp after music.h.
#pragma once

namespace gem {

static void clockSay(char* out, size_t n, long secs) {  // "4 min 10 s"
  if (secs >= 3600) snprintf(out, n, "%ld h %ld min", secs / 3600, secs % 3600 / 60);
  else if (secs >= 60) snprintf(out, n, "%ld min %ld s", secs / 60, secs % 60);
  else snprintf(out, n, "%ld s", secs);
}

// a song by (part of) its name: -1 none
static int findSong(const char* want) {
  if (!mus::listed) mus::list();
  if (!want || !*want) return -1;
  char nm[40], w[40];
  strlcpy(w, want, sizeof(w));
  for (char* p = w; *p; p++) *p = tolower(*p);
  for (int i = 0; i < mus::nFiles; i++) {
    mus::niceName(i, nm, sizeof(nm));
    for (char* p = nm; *p; p++) *p = tolower(*p);
    if (strstr(nm, w) || strstr(w, nm)) return i;
  }
  return -1;
}

bool deskToolCall(const ToolReq& q, char* out, size_t n) {
  const unsigned long now = millis();
  if (!strcmp(q.name, "timer")) {
    if (!strcmp(q.arg, "set")) {
      const float m = q.num > 0 ? q.num : 5;
      const int i = clk::addTimer((uint32_t)lroundf(constrain(m, 0.1f, 1440.0f) * 60), q.label[0] ? q.label : "Timer");
      if (i < 0) snprintf(out, n, "All 4 timers are busy - cancel one first.");
      else { char t[24]; clockSay(t, sizeof(t), lroundf(m * 60)); snprintf(out, n, "Timer '%s' set for %s.", clk::timers[i].label, t); }
    } else if (!strcmp(q.arg, "cancel")) {
      int k = 0;
      for (auto& t : clk::timers)
        if (t.on && (!q.label[0] || strcasestr(t.label, q.label))) { t.on = false; k++; }
      if (clk::ring == clk::R_TIMER) clk::stopRing("voice");
      snprintf(out, n, k ? "Cancelled %d timer%s." : "No timer like that is running.", k, k == 1 ? "" : "s");
    } else {
      int o = 0, k = 0;
      for (auto& t : clk::timers)
        if (t.on) {
          char s[24];
          clockSay(s, sizeof(s), max(0L, (long)((int32_t)(t.endMs - now)) / 1000));
          o += snprintf(out + o, n - o, "%s'%s': %s left", k++ ? "; " : "", t.label, s);
        }
      if (!k) snprintf(out, n, "No timers running.");
    }
    return true;
  }
  if (!strcmp(q.name, "alarm")) {
    int h = -1, m = -1;
    sscanf(q.text, "%d:%d", &h, &m);
    if (!strcmp(q.arg, "set")) {
      if (h < 0 || h > 23 || m < 0 || m > 59) { snprintf(out, n, "Tell me the time as HH:MM (24 hour)."); return true; }
      if (clk::nAlarms >= clk::NA) { snprintf(out, n, "I already have 6 alarms - delete one first."); return true; }
      const uint8_t days = !strcmp(q.arg2, "every_day") ? 127 : !strcmp(q.arg2, "weekdays") ? 62 : !strcmp(q.arg2, "weekends") ? 65 : 0;
      clk::Alarm& a = clk::alarms[clk::nAlarms++];
      a.h = h; a.m = m; a.days = days; a.on = 1;
      strlcpy(a.label, q.label[0] ? q.label : "Alarm", sizeof(a.label));
      clk::save();
      xlog("CLOCK alarm by voice %02d:%02d days %d", h, m, days);
      snprintf(out, n, "Alarm set for %02d:%02d, %s.", h, m, days == 127 ? "every day" : days == 62 ? "on weekdays" : days == 65 ? "on weekends" : "once");
    } else if (!strcmp(q.arg, "delete")) {
      int k = 0;
      for (int i = clk::nAlarms - 1; i >= 0; i--) {
        const clk::Alarm& a = clk::alarms[i];
        if ((h >= 0 && a.h == h && a.m == m) || (h < 0 && q.label[0] && strcasestr(a.label, q.label)) || (h < 0 && !q.label[0])) {
          for (int j = i; j < clk::nAlarms - 1; j++) clk::alarms[j] = clk::alarms[j + 1];
          clk::nAlarms--;
          k++;
        }
      }
      if (k) clk::save();
      snprintf(out, n, k ? "Deleted %d alarm%s." : "No alarm like that.", k, k == 1 ? "" : "s");
    } else {
      int o = snprintf(out, n, clk::nAlarms ? "" : "No alarms set.");
      for (int i = 0; i < clk::nAlarms; i++) {
        const clk::Alarm& a = clk::alarms[i];
        o += snprintf(out + o, n - o, "%s%02d:%02d %s (%s)%s", i ? "; " : "", a.h, a.m, a.label,
                      a.days == 127 ? "every day" : a.days == 62 ? "weekdays" : a.days == 65 ? "weekends" : a.days ? "some days" : "once",
                      a.on ? "" : " off");
      }
    }
    return true;
  }
  if (!strcmp(q.name, "focus")) {
    if (!strcmp(q.arg, "start")) {
      const int w = q.num > 0 ? (int)lroundf(q.num) : 25, r = q.num2 > 0 ? (int)lroundf(q.num2) : 4;
      clk::focusStart(w, w >= 45 ? 10 : 5, r);
      snprintf(out, n, "Focus started: %d rounds of %d minutes with %d minute breaks. Stay quiet now until the break.", clk::fRounds, clk::fWorkMin, clk::fBreakMin);
    } else if (!strcmp(q.arg, "stop")) {
      clk::focusStop("voice");
      snprintf(out, n, "Focus stopped. %u focus minutes today.", clk::minToday());
    } else {
      if (clk::focusOn) {
        char s[24];
        clockSay(s, sizeof(s), max(0L, (long)((int32_t)(clk::fPhaseEnd - now)) / 1000));
        snprintf(out, n, "%s, round %d of %d, %s left. %u focus minutes today, streak %u days.", clk::focusWork ? "Working" : "On a break",
                 clk::fRound, clk::fRounds, s, clk::minToday(), clk::fst.streak);
      } else snprintf(out, n, "No focus session running. %u focus minutes today.", clk::minToday());
    }
    return true;
  }
  if (!strcmp(q.name, "music")) {
    if (!mus::listed) mus::list();
    if (!strcmp(q.arg, "list")) {
      int o = snprintf(out, n, mus::nFiles ? "Songs: " : "No songs on my card.");
      char nm[40];
      for (int i = 0; i < mus::nFiles && o < (int)n - 44; i++) { mus::niceName(i, nm, sizeof(nm)); o += snprintf(out + o, n - o, "%s%s", i ? ", " : "", nm); }
      return true;
    }
    if (!strcmp(q.arg, "pause") || !strcmp(q.arg, "stop")) {  // (only when it was playing before the chat: it's stopped now)
      mus::pendSong = -2;
      mus::pendAmb = 0;
      mus::stopAll("voice");
      snprintf(out, n, "Music stopped.");
      return true;
    }
    if (!strcmp(q.arg, "ambient")) {
      int k = 0;
      for (int i = 1; i <= 5; i++) if (!strcmp(q.arg2, aud::AMB_NAMES[i])) k = i;
      mus::pendAmb = k ? k : 1;
    } else {
      if (!mus::nFiles) { snprintf(out, n, "I have no songs on my card."); return true; }
      int i = findSong(q.label);
      if (!strcmp(q.arg, "next")) i = mus::nextIdx(1);
      else if (!strcmp(q.arg, "previous")) i = mus::nextIdx(-1);
      else if (!strcmp(q.arg, "resume") && mus::paused) i = -1;
      else if (i < 0) i = mus::idx >= 0 ? mus::idx : 0;
      mus::pendSong = i;
    }
    mus::pendMs = now;
    endAskedMs = now;  // the chat ends; the music starts right after (music.h)
    char nm[40] = "";
    if (mus::pendSong >= 0) mus::niceName((mus::pendSong % mus::nFiles + mus::nFiles) % mus::nFiles, nm, sizeof(nm));
    snprintf(out, n, "Starting %s%s as soon as this chat ends - say only one or two words (like 'enjoy!'), nothing else.",
             mus::pendAmb ? aud::AMB_NAMES[mus::pendAmb] : nm[0] ? nm : "the music", mus::pendAmb ? " sounds" : "");
    return true;
  }
  if (!strcmp(q.name, "sleep_timer")) {
    const int mins = constrain((int)lroundf(q.num), 0, 600);
    clk::sleepDeep = q.flag == 1;
    clk::sleepEndMs = mins ? now + mins * 60000UL : 0;
    if (mins && !clk::sleepEndMs) clk::sleepEndMs = 1;
    xlog("CLOCK sleep timer by voice %d min%s", mins, clk::sleepDeep ? " then shut down" : "");
    if (!mins) snprintf(out, n, "Sleep timer cancelled.");
    else snprintf(out, n, "In %d minutes the music fades out and I go quiet%s.", mins, clk::sleepDeep ? ", then I shut down" : "");
    return true;
  }
  return false;
}

}  // namespace gem
