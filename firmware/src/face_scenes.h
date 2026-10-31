// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
// Play scenes: little things the robot does by itself when it's idle - a fly buzzing past, reading
// a book, playing the piano, a snack, bubbles, a butterfly landing on its head, dreaming of sheep ...
// Each scene is a short timeline: it picks expressions and where the eyes look, draws its props on
// the screen, says where the real head should turn (headX / headY) and asks for sounds (takeSfx).
// Included INSIDE class fe::Face (face_engine_v2.h), so it runs the same on the PC preview.
//
// Sound programs (played by aud::sfx on the robot): space separated
//   F:MS  a tone (0 = rest)   F~G:MS  a slide   n:MS  a noise burst   z:MS  a fly's buzz   vX  volume 0..1
//   "w:name|<program>" plays /sfx/name.wav from the SD card (real sounds, tools/make_sfx.py) and the
//   program only if that file is missing
 public:
  enum SceneId : uint8_t {
    SC_NONE, SC_FLY, SC_BOOK, SC_PIANO, SC_SNACK, SC_BUBBLES, SC_BUTTERFLY, SC_DREAM, SC_STARS, SC_TEA, SC_GAME,
    SC_HEADPHONES, SC_SNOW, SC_PAINT, SC_PEEKABOO, SC_FEATHER, SC_BALLOON, SC_COIN, SC_HICCUPS, SC_IDEA, SC_PLANE,
    SC_LADYBUG, SC_SING, SC_CAKE, SC_RAIN, SC_JUGGLE, SC_GHOST, SC_MOVIE, SC_SELFIE,
    SC_DOT, SC_GLIDER, SC_ANT, SC_PILLOW, SC_MUG, SC_HELLO,  // desk mode
    SC_CRUMB, SC_CLOCK,  // Crumb the bird; playing with the time
    SC_DICE, SC_RPS, SC_GIFT,  // toys: a dice roll, rock-paper-scissors, a present
    // desk-life scenes (face_scenes_more.h)
    SC_KITE, SC_YOYO, SC_FISHING, SC_DUCK, SC_COOKIE, SC_TOP, SC_MAGIC, SC_CAT, SC_DOMINO, SC_SNOWMAN, SC_FROG,
    SC_MOON, SC_DOODLE, SC_BOAT, SC_PLANT, SC_ROCKET, SC_ICECREAM,
    // short moments (3-6 s), played more often than the full scenes - Snoopy-watch style
    SC_M_FIRST, SC_LEAF = SC_M_FIRST, SC_BIRD, SC_BEE, SC_FIREFLY, SC_SPIDER, SC_COMET, SC_HEART, SC_NOTES,
    SC_SNEEZE, SC_YAWN, SC_LOOKOFF, SC_SNAIL, SC_SEEDS, SC_WINK,
    SC_DANDELION, SC_GUM, SC_PAPERBALL,  // (face_scenes_more.h)
    // props for the emotes only (an overlay: no face, head or sound of its own - the emote has those)
    SC_O_FIRST, SC_O_WAVE = SC_O_FIRST, SC_O_SHAKE, SC_O_SHRUG, SC_O_CRAB, SC_O_POINT, SC_O_SWEAT, SC_O_SPARKLE,
    SC_NUM
  };
  static const char* sceneNameAt(int i) {
    static const char* const N[SC_NUM] = {
      "none", "fly", "book", "piano", "snack", "bubbles", "butterfly", "dream", "stargaze", "tea", "game",
      "headphones", "snow", "paint", "peekaboo", "feather", "balloon", "coin", "hiccups", "idea", "plane",
      "ladybug", "sing", "cake", "raincloud", "juggle", "ghost", "movie", "selfie",
      "dot", "glider", "ant", "pillow", "mug", "hello",
      "crumb", "clock",
      "dice", "rps", "gift",
      "kite", "yoyo", "fishing", "duck", "cookie", "top", "magic", "cat", "dominoes", "snowman", "frog",
      "moon", "doodle", "boat", "plant", "rocket", "icecream",
      "leaf", "bird", "bee", "firefly", "spider", "comet", "heart", "notes", "sneeze", "yawn", "lookoff", "snail", "seeds", "wink",
      "dandelion", "gum", "paperball",
      "wave", "shake", "shrug", "crab", "point", "sweat", "sparkle"};
    return i >= 0 && i < SC_NUM ? N[i] : "";
  }
  // Where the scene wants the real head: -1..1 in screen directions (+x = the screen's right,
  // i.e. the robot's left; +y = down). headOn = false: leave the head alone.
  float headX = 0, headY = 0;
  bool headOn = false;
  bool playing() const { return sc.id != SC_NONE; }
  bool overlayOn() const { return sc.id != SC_NONE && sc.overlay; }  // props for an emote
  bool dozing() const { return boredStage >= 4; }  // bored so long it fell asleep (the OLED shows it)
  const char* sceneName() const { return sceneNameAt(sc.id); }
  float sceneLeft() const { return sc.id ? (sc.dur - sc.u) / sc.speed : 0; }
  int hourNow = -1;  // local hour (0-23) from the robot's clock, -1 = unknown: night / morning picks

  // ---- beats: every take its own middle, twist and ending; events can change the end ----
  int weatherNow = 0;
  bool userHere = false;  // desk mode: someone is in front of it (the hello scene)
  bool ambientOn = false;  // desk mode: the weather drawn faintly behind the face
  // StandBy (desk mode, idle a while): full-screen clock / calendar / weather / dozing Oreo, red at night
  bool standby = false, sbNight = false;
  int sbStyle = 0;  // 0 widgets (pages turn), 1 big clock, 2 analog, 3 flip, 4 clock + calendar, 5 weather, 6 dozing Oreo
  static const int SB_STYLES = 20;
  int sbNightMode = 0;  // 0 auto (night or dark: red), 1 never, 2 always
  int dayNow = -1, monthNow = -1, yearNow = -1, wdayNow = -1;  // monthNow 1..12, wdayNow 0 = Sunday
  float tempNow = -99;
  char cityNow[20] = "";
  int minuteNow = -1;     // the robot clock's minute (with hourNow): the clock scene
  int crumbFear = 0, crumbFriend = 0;  // Crumb remembers: scared off (it peeks in first) / patted, stayed (it comes closer)  // 0 unknown, 1 clear, 2 cloudy, 3 rain, 4 storm, 5 fog, 6 snow (desk mode sets it)
  // A live event for the playing scene ("tap", "loud", "pat", "arrived"); the scene decides what it does
  void sceneEvent(const char* name) {
    if (!sc.id || sc.overlay || sc.evId || sc.u >= sc.dur - 1) return;
    const uint8_t e = ieq(name, "tap") ? 1 : ieq(name, "loud") ? 2 : ieq(name, "pat") ? 3 : ieq(name, "arrived") ? 4 : 0;
    if (!e) return;
    sc.evId = e;
    sc.evU = sc.u;
  }
  void beats(int& m, int& tw, int& e) const { m = sc.mid; tw = sc.twist; e = sc.end; }
  int forceMid = -1, forceTwist = -1, forceEnd = -1;  // (the PC preview: the next take's variants)

  // Start a scene by name ("random" picks one that hasn't played lately; "stop" ends it).
  // "o:<name>" = props only, laid over an emote (it keeps its own face, head and sounds).
  bool play(const char* name) {
    if (ieq(name, "stop") || ieq(name, "none")) { stopScene(); return true; }
    bool overlay = false;
    if (name[0] == 'o' && name[1] == ':') { overlay = true; name += 2; }
    int id = -1;
    if (!*name || ieq(name, "random")) id = pickScene();
    else
      for (int i = 1; i < SC_NUM; i++) if (ieq(name, sceneNameAt(i))) id = i;
    if (id <= 0) return false;
    if (id >= SC_O_FIRST) overlay = true;
    if (!sc.id) { sceneBase = baseExpr; sceneBaseStr = baseStrength; }
    memset(&sc, 0, sizeof(sc));
    sc.id = (uint8_t)id;
    sc.overlay = overlay;
    sc.t0 = t;
    sc.lastU = -1;
    sc.fy = styleDef(style).y;
    for (int i = 0; i < 8; i++) sc.v[i] = rv();
    for (int i = 0; i < 4; i++) sc.iv[i] = (int)(rv() * 1000);
    sc.dur = sceneDur(id);
    for (int i = 0; i < 8; i++) sc.jit[i] = rr(-0.2f, 0.2f);
    sc.evU = -1;
    setupBeats(id);
    // every take is different: its own tempo, props from the left or the right
    sc.speed = rr(0.82f, 1.2f);
    sc.mir = chance(0.5f) ? -1 : 1;
    if (id == SC_CLOCK) sc.mir = 1;  // (numbers can't be mirrored: 10:07 read "70:01")
    memmove(sceneHist + 1, sceneHist, sizeof(sceneHist) - 1);
    sceneHist[0] = (uint8_t)id;
    headOn = false;
    headX = headY = 0;
    extUntil = 0;
    return true;
  }
  void stopScene() {
    if (!sc.id) return;
    const bool wasOverlay = sc.overlay;
    sc.id = SC_NONE;
    sc.overlay = false;
    headOn = false;
    headX = headY = 0;
    extUntil = 0;
    if (!wasOverlay && sceneBase >= 0) {  // back to what it was doing before (an emote's props: the emote sets the face)
      Pose p;
      Extra x;
      buildExpr(sceneBase, p, x);
      strengthen(p, x, sceneBaseStr);
      if (!stills) humanize(p);
      apply(sceneBase, p, x);
      baseExpr = sceneBase;
      baseStrength = sceneBaseStr;
    }
  }
  // The next sound for the robot to play (once)
  bool takeSfx(char* out, size_t n) {
    if (!sfxBuf[0] || n < 2) return false;
    strncpy(out, sfxBuf, n - 1);
    out[n - 1] = 0;
    sfxBuf[0] = 0;
    return true;
  }

 private:
  struct SceneSt {
    uint8_t id; float t0, dur, u, lastU, fy; float v[8]; int iv[4]; float speed; int8_t mir; bool overlay;
    // beats: this take's middle / twist / ending, beat-time jitter, a live event (1 tap, 2 loud,
    // 3 pat, 4 arrived) and when it came
    uint8_t mid, twist, end, evId; float evU; float jit[8];
  };
  uint8_t lastCombo[SC_NUM] = {0};
  SceneSt sc = {};
  char sfxBuf[320] = "";
  uint8_t sceneHist[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  MirrorCanvas mirrorCv;
  int sceneBase = -1;
  float sceneBaseStr = 1;

  static bool ieq(const char* a, const char* b) {
    for (; *a && *b; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    return *a == *b;
  }
  // Weighted random pick, never one of the last 8: short moments ~2/3 of the time; night and
  // morning favour their own things; drowsy favours the quiet ones
  int pickScene() {
    float w[SC_NUM], sum = 0;
    const bool night = hourNow >= 22 || (hourNow >= 0 && hourNow < 6), morning = hourNow >= 6 && hourNow < 10,
               evening = hourNow >= 18 && hourNow < 22;
    for (int i = 0; i < SC_NUM; i++) {
      w[i] = 0;
      if (i == 0 || i >= SC_O_FIRST) continue;  // (the emote props aren't scenes of their own)
      if (i == SC_HELLO && !userHere) continue;  // (a wave for nobody)
      if (i == SC_CLOCK) continue;  // (desk mode asks for the clock itself)
      bool recent = false;
      for (uint8_t h : sceneHist) recent = recent || h == i;
      if (recent) continue;
      float x = i >= SC_M_FIRST ? 2.0f : 1.0f;
      const bool quiet = i == SC_DREAM || i == SC_STARS || i == SC_TEA || i == SC_HEADPHONES || i == SC_FIREFLY ||
                         i == SC_COMET || i == SC_YAWN || i == SC_SNAIL || i == SC_SEEDS || quiet2(i);
      const bool lively = i == SC_GAME || i == SC_JUGGLE || i == SC_BALLOON || i == SC_SING || i == SC_PIANO ||
                          i == SC_CAKE || i == SC_SELFIE || i == SC_BEE || i == SC_ROCKET || i == SC_MAGIC ||
                          i == SC_DOMINO || i == SC_TOP || i == SC_YOYO || i == SC_KITE;
      if (night) { if (quiet) x *= 3; if (lively) x *= 0.3f; if (i == SC_BIRD || i == SC_TEA) x *= 0.3f; }
      if (!night && (i == SC_FIREFLY || i == SC_STARS || i == SC_COMET)) x *= 0.4f;
      if (morning && (i == SC_TEA || i == SC_YAWN || i == SC_BIRD || i == SC_SING)) x *= 3;
      if (evening && (i == SC_MOVIE || i == SC_HEADPHONES || i == SC_STARS || i == SC_BOOK)) x *= 2.5f;
      if (boredStage >= 3 && quiet) x *= 3;
      x *= pickWeight2(i, night, morning, evening);  // the face_scenes_more.h scenes: weather, season, time of day
      w[i] = x;
      sum += x;
    }
    if (sum <= 0) return 1 + (int)(rv() * (SC_O_FIRST - 1)) % (SC_O_FIRST - 1);
    float r = rv() * sum;
    for (int i = 1; i < SC_O_FIRST; i++) { r -= w[i]; if (r <= 0 && w[i] > 0) return i; }
    return SC_LEAF;
  }
  // sometimes one thing leads to another
  int chainFor(int id) const {
    switch (id) {
      case SC_SNACK: return chance(0.5f) ? SC_LADYBUG : SC_BEE;
      case SC_TEA: return chance(0.5f) ? SC_DREAM : SC_YAWN;
      case SC_BUBBLES: return SC_SNEEZE;
      case SC_HEADPHONES: return SC_NOTES;
      case SC_STARS: return chance(0.5f) ? SC_COMET : SC_FIREFLY;
      case SC_FLY: return SC_BEE;
      case SC_BALLOON: return SC_BIRD;
      case SC_BOOK: return SC_YAWN;
      case SC_RAIN: return SC_SNAIL;
      case SC_SEEDS: return SC_SNEEZE;
      case SC_LEAF: return SC_SEEDS;
      case SC_BIRD: return SC_SEEDS;
      case SC_YAWN: return SC_DREAM;
    }
    return chainFor2(id);
  }

  // ---- tunes: white keys from C4 (0) to B5 (13), in half beats ----
  struct Note { int8_t key; uint8_t half; };
  static float keyHz(int k) {
    static const float hz[14] = {262, 294, 330, 349, 392, 440, 494, 523, 587, 659, 698, 784, 880, 988};
    return k >= 0 && k < 14 ? hz[k] : 0;
  }
  static const Note* tune(int which, int& n) {
    static const Note twinkle[] = {{0, 2}, {0, 2}, {4, 2}, {4, 2}, {5, 2}, {5, 2}, {4, 4}, {3, 2}, {3, 2}, {2, 2}, {2, 2}, {1, 2}, {1, 2}, {0, 4}};
    static const Note ode[] = {{2, 2}, {2, 2}, {3, 2}, {4, 2}, {4, 2}, {3, 2}, {2, 2}, {1, 2}, {0, 2}, {0, 2}, {1, 2}, {2, 2}, {2, 3}, {1, 1}, {1, 4}};
    static const Note mary[] = {{2, 2}, {1, 2}, {0, 2}, {1, 2}, {2, 2}, {2, 2}, {2, 4}, {1, 2}, {1, 2}, {1, 4}, {2, 2}, {4, 2}, {4, 4}};
    static const Note jacques[] = {{7, 2}, {8, 2}, {9, 2}, {7, 2}, {7, 2}, {8, 2}, {9, 2}, {7, 2}, {9, 2}, {10, 2}, {11, 4}, {9, 2}, {10, 2}, {11, 4}};
    static const Note row[] = {{0, 3}, {0, 3}, {0, 2}, {1, 1}, {2, 3}, {2, 2}, {1, 1}, {2, 2}, {3, 1}, {4, 6}, {7, 1}, {7, 1}, {7, 1}, {4, 1}, {4, 1}, {4, 1}, {2, 1}, {2, 1}, {2, 1}, {0, 1}, {0, 1}, {0, 1}, {4, 2}, {3, 1}, {2, 2}, {1, 1}, {0, 6}};
    static const Note bells[] = {{2, 2}, {2, 2}, {2, 4}, {2, 2}, {2, 2}, {2, 4}, {2, 2}, {4, 2}, {0, 3}, {1, 1}, {2, 8}};
    static const Note lune[] = {{7, 2}, {7, 2}, {7, 2}, {8, 2}, {9, 4}, {8, 4}, {7, 2}, {9, 2}, {8, 2}, {8, 2}, {7, 8}};
    static const Note bday[] = {{4, 1}, {4, 1}, {5, 2}, {4, 2}, {7, 2}, {6, 4}, {4, 1}, {4, 1}, {5, 2}, {4, 2}, {8, 2}, {7, 4}};
    switch (which) {
      case 0: n = sizeof(twinkle) / sizeof(Note); return twinkle;
      case 1: n = sizeof(ode) / sizeof(Note); return ode;
      case 2: n = sizeof(mary) / sizeof(Note); return mary;
      case 3: n = sizeof(jacques) / sizeof(Note); return jacques;
      case 4: n = sizeof(row) / sizeof(Note); return row;
      case 5: n = sizeof(bells) / sizeof(Note); return bells;
      case 6: n = sizeof(lune) / sizeof(Note); return lune;
      default: n = sizeof(bday) / sizeof(Note); return bday;
    }
  }
  static constexpr float HALF_BEAT = 0.2f;  // s
  static float tuneLen(int which) {
    int n;
    const Note* s = tune(which, n);
    float L = 0;
    for (int i = 0; i < n; i++) L += s[i].half * HALF_BEAT;
    return L;
  }
  int pianoTune() const { return sc.iv[0] % 4; }       // piano: twinkle, ode, mary, jacques
  int singTune() const { return 4 + sc.iv[0] % 3; }    // sing: row, bells, lune
  // the note playing at time u after the tune started (-1 = none); its start in *at
  static int noteAt(int which, float u, float* at = nullptr, int* idx = nullptr) {
    int n;
    const Note* s = tune(which, n);
    float t0 = 0;
    for (int i = 0; i < n; i++) {
      const float d = s[i].half * HALF_BEAT;
      if (u >= t0 && u < t0 + d) { if (at) *at = t0; if (idx) *idx = i; return s[i].key; }
      t0 += d;
    }
    return -1;
  }
  void playTune(int which, float vol) {
    static const char* const files[8] = {"piano_twinkle", "piano_ode", "piano_mary", "piano_jacques", "sing_row", "sing_bells", "sing_lune", "sing_bday"};
    int n;
    const Note* s = tune(which, n);
    char b[320];
    int o = snprintf(b, sizeof(b), "w:%s|v%.2f", files[which & 7], vol);
    for (int i = 0; i < n && o < (int)sizeof(b) - 20; i++) {
      const int ms = (int)(s[i].half * HALF_BEAT * 1000);
      o += snprintf(b + o, sizeof(b) - o, " %d:%d 0:%d", (int)keyHz(s[i].key), ms - 30, 30);
    }
    sfx(b);
  }

  float sceneDur(int id) const {
    switch (id) {
      case SC_FLY: return 17;  // (each take ends itself: finishIn)
      case SC_BOOK: return 24;  // (ends itself: finishIn)
      case SC_DOT: return 16;
      case SC_GLIDER: return 12;
      case SC_ANT: return 13;
      case SC_PILLOW: return 20;
      case SC_MUG: return 14;
      case SC_HELLO: return 8;
      case SC_CRUMB: return 18;
      case SC_CLOCK: return 18;
      case SC_DICE: return 8.5f;
      case SC_RPS: return 9;
      case SC_GIFT: return 10;
      case SC_PIANO: return 1.6f + tuneLen(pianoTune()) + 3.2f;
      case SC_SNACK: return 18;
      case SC_BUBBLES: return 16;  // (ends itself: finishIn)
      case SC_BUTTERFLY: return 18;  // (ends itself: finishIn)
      case SC_DREAM: return 17;
      case SC_STARS: return 16;
      case SC_TEA: return 14;
      case SC_GAME: return 15;
      case SC_HEADPHONES: return 1.2f + tuneLen(singTune()) + 2.4f;  // (the song, then a moment)
      case SC_SNOW: return 14;
      case SC_PAINT: return 14;
      case SC_PEEKABOO: return 12;
      case SC_FEATHER: return 13;
      case SC_BALLOON: return 13;
      case SC_COIN: return 11;
      case SC_HICCUPS: return 12;
      case SC_IDEA: return 13;
      case SC_PLANE: return 12.5f;
      case SC_LADYBUG: return 13;
      case SC_SING: return 1.6f + tuneLen(singTune()) + 4.0f;
      case SC_CAKE: return 16;
      case SC_RAIN: return 19;
      case SC_JUGGLE: return 14;
      case SC_GHOST: return 12;
      case SC_MOVIE: return 15;
      case SC_SELFIE: return 11;
      case SC_LEAF: return 9;
      case SC_BIRD: return 4.5f;
      case SC_BEE: return 8;
      case SC_FIREFLY: return 6;
      case SC_SPIDER: return 6;
      case SC_COMET: return 4;
      case SC_HEART: return 4.5f;
      case SC_NOTES: return 5;
      case SC_SNEEZE: return 4.5f;
      case SC_YAWN: return 4.5f;
      case SC_LOOKOFF: return 4;
      case SC_SNAIL: return 7;
      case SC_SEEDS: return 6;
      case SC_WINK: return 3.5f;
    }
    if ((id >= SC_KITE && id <= SC_ICECREAM) || (id >= SC_DANDELION && id <= SC_PAPERBALL)) return sceneDur2(id);
    if (id >= SC_O_FIRST) return 6;  // (overlays loop until the emote ends)
    return 10;
  }

  // ---- helpers ----
  static float ease(float x) { x = clampf(x, 0, 1); return x * x * (3 - 2 * x); }
  static float fracf(float x) { return x - floorf(x); }
  static float lerpf(float a, float b, float k) { return a + (b - a) * k; }
  float seg(float a, float b) const { return ease((sc.u - a) / (b - a)); }        // 0 -> 1 between two times
  bool at(float x) const { return sc.lastU < x && sc.u >= x; }                     // once, when u passes x
  float since(float x) const { return sc.u - x; }
  // a quick kick that dies away (hops, jolts): 0 before x, 1 at x + rise, then decays
  float kick(float x, float rise = 0.07f, float decay = 9) const {
    const float d = sc.u - x;
    if (d < 0) return 0;
    if (d < rise) return d / rise;
    return expf(-(d - rise) * decay);
  }
  // A beat's expression: generic ones come from a pool of similar faces (random strength too),
  // are now and then left out (the living face fills in), and sometimes the robot says something
  static const char* poolFor(const char* e) {
    static const char* const P[][2] = {
      {"curious", "curious|tilted|suspicious|thinking"}, {"happy", "happy|joy|content|giddy|ehehe|playful"},
      {"amazed", "amazed|awe|starstruck|surprised"}, {"surprised", "surprised|startled|amazed"},
      {"giggle", "giggle|ehehe|laugh|playful"}, {"laugh", "laugh|giggle|tears_of_joy"},
      {"content", "content|cozy|blissful|peaceful|calm"}, {"proud", "proud|smug|confident|triumphant"},
      {"sad", "sad|pout|gloomy|wistful"}, {"scared", "scared|nervous|terrified|anxious"},
      {"annoyed", "annoyed|irritated|grumpy|unimpressed"}, {"focus", "focus|determined|thinking"},
      {"adoring", "adoring|lovestruck|love"}, {"wistful", "wistful|nostalgic|calm"}, {"excited", "excited|hyper|joy"},
      {"sheepish", "sheepish|embarrassed|bashful|awkward"}, {"startled", "startled|flinch|shocked"},
      {"relieved", "relieved|calm|content"}, {"smug", "smug|cocky|proud"}, {"awe", "awe|amazed|starstruck"},
    };
    for (const auto& p : P) if (!strcmp(e, p[0])) return p[1];
    return nullptr;
  }
  static const char* voiceFor(const char* e) {
    static const char* const V[][2] = {
      {"happy", "v_yay"}, {"joy", "v_yay"}, {"giddy", "v_chat1"}, {"playful", "v_chat2"}, {"ehehe", "v_hehe"}, {"giggle", "v_hehe"},
      {"laugh", "v_hehe"}, {"amazed", "v_wow"}, {"awe", "v_ooh"}, {"starstruck", "v_ooh"}, {"surprised", "v_wow"},
      {"startled", "v_eep"}, {"curious", "v_hmm"}, {"thinking", "v_hmm"}, {"tilted", "v_chat_q"}, {"sad", "v_aww"},
      {"gloomy", "v_chat_sad"}, {"scared", "v_eep"}, {"nervous", "v_uhoh"}, {"annoyed", "v_uhuh"}, {"grumpy", "v_grr"},
      {"proud", "v_tada"}, {"triumphant", "v_tada"}, {"adoring", "v_ooh"}, {"love", "v_ooh"}, {"sheepish", "v_hehe"},
      {"excited", "v_wow"}, {"content", "v_chat3"}, {"yum", "v_yum"}, {"sleepy", "v_yawn"}, {"yawn", "v_yawn"},
    };
    for (const auto& v : V) if (!strcmp(e, v[0])) return v[1];
    return nullptr;
  }
  void say(const char* e) {
    if (sc.overlay) return;
    const char* pool = poolFor(e);
    char pick[32];
    strncpy(pick, e, sizeof(pick) - 1);
    pick[sizeof(pick) - 1] = 0;
    if (pool) {
      if (chance(0.12f)) return;  // a beat left out: the living face fills it
      int n = 1;
      for (const char* c = pool; *c; c++) n += *c == '|';
      int k = (int)(rv() * n) % n;
      const char* c = pool;
      while (k-- > 0) c = strchr(c, '|') + 1;
      const char* end = strchr(c, '|');
      const size_t L = min(end ? (size_t)(end - c) : strlen(c), sizeof(pick) - 1);
      memcpy(pick, c, L);
      pick[L] = 0;
    }
    const char* v = voiceFor(pick);
    if (v && !sfxBuf[0] && chance(0.3f)) { char b[24]; snprintf(b, sizeof(b), "w:%s|", v); sfx(b); }
    if (pool && chance(0.3f)) strncat(pick, chance(0.5f) ? "_mild" : "_max", sizeof(pick) - strlen(pick) - 1);
    set(pick);
  }
  void slook(float x, float y, float hold) { if (!sc.overlay) lookAt(sc.mir < 0 ? -x : x, y, hold); }
  void sfx(const char* s) { if (sc.overlay) return; strncpy(sfxBuf, s, sizeof(sfxBuf) - 1); sfxBuf[sizeof(sfxBuf) - 1] = 0; }
  void lookPt(float x, float y) { slook(clampf((x - W / 2) / 130.0f, -1.2f, 1.2f), clampf((y - sc.fy) / 90.0f, -1.2f, 1.2f), 0.3f); }
  void headTo(float x, float y) {
    if (sc.overlay) return;
    headOn = true;
    headX = clampf(sc.mir < 0 ? -x : x, -1, 1);
    headY = clampf(y, -1, 1);
  }
  void headPt(float x, float y, float amt) { headTo((x - W / 2) / (W / 2) * amt, (y - sc.fy) / 120.0f * amt); }
  void faceShift(float dy) { if (!sc.overlay) shown.y += dy; }
  static uint16_t cc(float r, float g, float b) { return to565(C(r, g, b)); }
  uint16_t fadeTo(const RGB& c, float a) { return to565(mixc(lidCol(), c, clampf(a, 0, 1))); }
  RGB inkish() const { return lightBg() ? C(70, 60, 80) : C(235, 235, 245); }  // a line colour that shows on this background

  // ---- 3D props: shaded shapes (props3d on), else the flat ones ----
  // the light comes from the top left: a darker body (shadow side, bottom right), the colour, a lit patch
  // and a small shine
  void ball(float x, float y, float r, const RGB& c) {
    Canvas& cv = *k.cv;
    if (!props3d || r < 3) { cv.circle(x, y, r, to565(c)); return; }
    cv.circle(x, y, r, to565(mulc(c, 0.58f)));
    cv.circle(x - r * 0.08f, y - r * 0.08f, r * 0.9f, to565(c));
    cv.circle(x - r * 0.22f, y - r * 0.24f, r * 0.58f, to565(mixc(c, C(255, 255, 255), 0.16f)));
    cv.circle(x - r * 0.32f, y - r * 0.34f, r * 0.32f, to565(mixc(c, C(255, 255, 255), 0.32f)));
    cv.ellipse(x - r * 0.4f, y - r * 0.44f, r * 0.15f, r * 0.11f, to565(mixc(c, C(255, 255, 255), 0.8f)));
  }
  void oval(float x, float y, float rx, float ry, const RGB& c) {
    Canvas& cv = *k.cv;
    if (!props3d || rx < 3) { cv.ellipse(x, y, rx, ry, to565(c)); return; }
    cv.ellipse(x, y, rx, ry, to565(mulc(c, 0.58f)));
    cv.ellipse(x - rx * 0.08f, y - ry * 0.08f, rx * 0.9f, ry * 0.9f, to565(c));
    cv.ellipse(x - rx * 0.22f, y - ry * 0.24f, rx * 0.58f, ry * 0.58f, to565(mixc(c, C(255, 255, 255), 0.16f)));
    cv.ellipse(x - rx * 0.32f, y - ry * 0.36f, rx * 0.3f, ry * 0.3f, to565(mixc(c, C(255, 255, 255), 0.32f)));
    cv.ellipse(x - rx * 0.4f, y - ry * 0.46f, rx * 0.14f, ry * 0.11f, to565(mixc(c, C(255, 255, 255), 0.8f)));
  }
  // a block (a cake tier, a card): a lit top band, a shaded bottom band
  void box(float x, float y, float w, float h, float rad, const RGB& c) {
    Canvas& cv = *k.cv;
    if (!props3d) { cv.rrect(x, y, w, h, rad, to565(c)); return; }
    const float lum = (c.r + c.g + c.b) / 765.0f;  // light colours: a gentler shade (white went grey)
    cv.rrect(x, y, w, h, rad, to565(mulc(c, lum > 0.85f ? 0.86f : 0.7f)));
    cv.rrect(x, y, w, h * 0.8f, rad, to565(c));
    cv.rrect(x + rad * 0.5f, y + 2, w - rad, fmaxf(2, h * 0.16f), fminf(rad, h * 0.08f), to565(mixc(c, C(255, 255, 255), 0.35f)));
  }
  static RGB unpack(uint16_t c) { return C(((c >> 11) & 31) * 255.0f / 31, ((c >> 5) & 63) * 255.0f / 63, (c & 31) * 255.0f / 31); }

  // ---- little drawings shared by the scenes ----
  void noteGlyph(float x, float y, float s, uint16_t c) {
    k.cv->ellipse(x, y, s * 0.55f, s * 0.42f, c);
    k.cv->lineFast(x + s * 0.48f, y, x + s * 0.48f, y - s * 1.7f, fmaxf(0.8f, s * 0.12f), c);
    k.cv->tri(x + s * 0.48f, y - s * 1.7f, x + s * 1.2f, y - s * 1.2f, x + s * 0.48f, y - s * 1.25f, c);
  }
  void cloud(float x, float y, float s, uint16_t c) {
    if (props3d) {  // a puffy cloud: a darker underside, the puffs lit from the top left
      const RGB cb = unpack(c), dk = mulc(cb, 0.7f), hi = mixc(cb, C(255, 255, 255), 0.35f);
      k.cv->rrect(x - s * 1.3f, y + s * 0.08f, s * 2.6f, s * 0.75f, s * 0.37f, to565(dk));
      const float px[4] = {-0.9f, -0.3f, 0.45f, 1.0f}, py[4] = {0.15f, -0.3f, -0.15f, 0.2f}, pr[4] = {0.62f, 0.8f, 0.7f, 0.55f};
      for (int i = 0; i < 4; i++) k.cv->circle(x + s * px[i], y + s * py[i] + s * 0.06f, s * pr[i], to565(dk));
      for (int i = 0; i < 4; i++) k.cv->circle(x + s * px[i] - s * 0.04f, y + s * py[i] - s * 0.04f, s * pr[i] * 0.9f, c);
      k.cv->rrect(x - s * 1.2f, y - s * 0.02f, s * 2.4f, s * 0.6f, s * 0.3f, c);
      for (int i = 1; i < 3; i++) k.cv->circle(x + s * px[i] - s * pr[i] * 0.3f, y + s * py[i] - s * pr[i] * 0.35f, s * pr[i] * 0.35f, to565(hi));
      return;
    }
    k.cv->circle(x - s * 0.9f, y + s * 0.15f, s * 0.62f, c);
    k.cv->circle(x - s * 0.3f, y - s * 0.3f, s * 0.8f, c);
    k.cv->circle(x + s * 0.45f, y - s * 0.15f, s * 0.7f, c);
    k.cv->circle(x + s * 1.0f, y + s * 0.2f, s * 0.55f, c);
    k.cv->rrect(x - s * 1.3f, y, s * 2.6f, s * 0.75f, s * 0.37f, c);
  }
  void burst(float x, float y, float r0, float r1, int n, float th, uint16_t c, float rot = 0) {
    for (int i = 0; i < n; i++) {
      const float a = rot + i * 2 * PI_F / n;
      k.cv->lineFast(x + cosf(a) * r0, y + sinf(a) * r0, x + cosf(a) * r1, y + sinf(a) * r1, th, c);
    }
  }
  void paw(float x, float y, float r, const RGB& fur, const RGB& pad) {
    Canvas& cv = *k.cv;
    for (int i = -1; i <= 1; i++) cv.circle(x + i * r * 0.58f, y - r * 0.78f + fabsf((float)i) * r * 0.16f, r * 0.36f, to565(fur));
    cv.circle(x, y, r, to565(fur));
    cv.ellipse(x, y + r * 0.15f, r * 0.42f, r * 0.34f, to565(pad));
    for (int i = -1; i <= 1; i++) cv.circle(x + i * r * 0.5f, y - r * 0.55f + fabsf((float)i) * r * 0.12f, r * 0.14f, to565(pad));
  }
  // a feather: centre, angle (0 = pointing up), length
  void feather(float x, float y, float a, float L, const RGB& tip) {
    const float dx = sinf(a), dy = -cosf(a), nx = -dy, ny = dx;
    const RGB vane = C(248, 244, 255);
    for (int j = 1; j <= 9; j++) {
      const float s = -0.5f + j * 0.1f;  // along the spine
      const float px = x + dx * s * L, py = y + dy * s * L;
      const float w = L * 0.2f * sinf(PI_F * j / 10.0f);
      const RGB c = mixc(vane, tip, clampf((j - 5) / 5.0f, 0, 1));
      for (int sd = -1; sd <= 1; sd += 2)
        k.cv->lineFast(px, py, px + nx * w * sd + dx * w * 0.6f, py + ny * w * sd + dy * w * 0.6f, 1.3f, to565(c));
    }
    k.cv->lineFast(x - dx * L * 0.62f, y - dy * L * 0.62f, x + dx * L * 0.5f, y + dy * L * 0.5f, 1.0f, cc(210, 205, 220));
  }
  void coin(float x, float y, float w, float r) {
    k.cv->ellipse(x, y, fmaxf(1, w), r, cc(214, 150, 20));
    k.cv->ellipse(x, y, fmaxf(0.8f, w * 0.78f), r * 0.78f, cc(255, 205, 50));
    if (w > r * 0.4f) k.cv->ellipse(x - w * 0.25f, y - r * 0.3f, w * 0.18f, r * 0.2f, cc(255, 245, 190));
  }

  // ---- beat helpers ----
  // the take's variants, weighted (beatWeight: context), never the last take's combination
  void pickBeats(uint8_t nMid, uint8_t nTwist, uint8_t nEnd) {
    const int id = sc.id;
    auto pick = [&](int kind, uint8_t n) -> uint8_t {
      if (n <= 1) return 0;
      float w[8], sum = 0;
      for (int i = 0; i < n && i < 8; i++) { w[i] = beatWeight(id, kind, i); sum += w[i]; }
      float r = rv() * sum;
      for (int i = 0; i < n && i < 8; i++) { if ((r -= w[i]) <= 0) return (uint8_t)i; }
      return (uint8_t)(n - 1);
    };
    for (int tries = 0; tries < 12; tries++) {
      sc.mid = pick(0, nMid);
      sc.twist = pick(1, nTwist);
      sc.end = pick(2, nEnd);
      const uint8_t combo = (uint8_t)(1 + sc.mid + 8 * sc.twist + 32 * sc.end);
      if (combo != lastCombo[id] || nMid * nTwist * nEnd <= 1) { lastCombo[id] = combo; break; }
    }
    if (forceMid >= 0) sc.mid = (uint8_t)forceMid;
    if (forceTwist >= 0) sc.twist = (uint8_t)forceTwist;
    if (forceEnd >= 0) sc.end = (uint8_t)forceEnd;
    forceMid = forceTwist = forceEnd = -1;
  }
  float bt(int i, float base) const { return base * (1 + sc.jit[i & 7]); }        // a beat's time this take
  bool atb(int i, float base) const { return at(bt(i, base)); }
  float evT() const { return sc.evId ? sc.u - sc.evU : -1; }                       // seconds since the event
  bool evAt(float after) const { return sc.evId && at(sc.evU + after); }
  void finishIn(float s) { if (sc.dur > sc.u + s) sc.dur = sc.u + s; }            // end the take early
  // context leanings per scene, beat kind (0 middle, 1 twist, 2 ending) and variant
  float beatWeight(int id, int kind, int i) const {
    const bool night = hourNow >= 22 || (hourNow >= 0 && hourNow < 6), lunch = hourNow >= 12 && hourNow < 14;
    if (id == SC_BUTTERFLY && kind == 2 && i == 1 && night) return 3;   // at night it dozes off with it
    if (id == SC_LEAF && kind == 2 && i == 1 && weatherNow == 3) return 2;  // wet and windy: blown off
    if (id == SC_RAIN && kind == 1 && i == 1 && weatherNow == 3) return 2;   // it really rains: the umbrella
    if (id == SC_RAIN && kind == 1 && weatherNow == 4) return i == 2 ? 1 : 0;  // a storm outside: always thunder
    if (id == SC_RAIN && kind == 2 && i == 2 && night) return 3;             // night rain: cozy and sleepy
    if (id == SC_BOOK && kind == 2 && i == 0 && night) return 3;             // reading at night: dozes off on it
    if (id == SC_SNACK && kind == 0 && lunch && (i == 2 || i == 3)) return 2; // lunch: fruit
    if (id == SC_PILLOW && kind == 2 && i == 0 && night) return 3;           // night: it dozes off
    return beatWeight2(id, kind, i);
  }
  // which scenes have beats, and how many of each
  void setupBeats(int id) {
    sc.mid = sc.twist = sc.end = 0;
    sc.evId = 0;
    switch (id) {
      case SC_FLY: pickBeats(3, 3, 3); break;
      case SC_BEE: pickBeats(3, 2, 3); break;
      case SC_BUTTERFLY: pickBeats(3, 1, 3); break;
      case SC_LEAF: pickBeats(3, 2, 3); break;
      case SC_BUBBLES: pickBeats(3, 1, 3); break;
      case SC_RAIN: pickBeats(2, 3, 3); break;
      case SC_BOOK: pickBeats(3, 2, 3); break;
      case SC_SNACK: pickBeats(4, 3, 3); break;
      case SC_DOT: pickBeats(3, 1, 3); break;
      case SC_GLIDER: pickBeats(3, 1, 3); break;
      case SC_ANT: pickBeats(2, 1, 2); break;
      case SC_PILLOW: pickBeats(3, 1, 3); break;
      case SC_MUG: pickBeats(3, 1, 3); break;
      case SC_HELLO: pickBeats(3, 1, 3); break;
      case SC_CRUMB: pickBeats(3, 2, 3); break;
      case SC_CLOCK: pickBeats(3, 1, 3); break;
      default: setupBeats2(id); break;
    }
  }

  // The scenes' timelines (called every step, after the pose is worked out)
  void sceneStep() {
    if (!sc.id) return;
    sc.u = (t - sc.t0) * sc.speed;
    if (sc.u >= sc.dur && sc.overlay) {  // an emote's props: again from the start until the emote ends
      sc.t0 = t;
      sc.u = 0;
      sc.lastU = -1;
    }
    if (sc.u >= sc.dur) {
      const int next = chainFor(sc.id);
      stopScene();
      if (next && chance(0.25f)) play(sceneNameAt(next));
      return;
    }
    const float u = sc.u;
    switch (sc.id) {
      case SC_FLY: {
        float x, y;
        bool sit;
        flyPos(x, y, &sit);
        const bool fled = sc.evId == 1 || sc.evId == 2;
        if (!fled || evT() < 0.6f) { lookPt(x, y); headPt(x, y, 0.5f); } else headTo(0, 0);
        if (at(0.3f)) say("curious");
        if (atb(4, 1.2f) || atb(5, 4.4f) || atb(6, 6.9f)) sfx("w:fly_buzz|v0.3 z:900");
        if (fled) {  // the tap / bang scared it off
          if (evAt(0.01f)) { say("startled"); sfx("w:snap|v0.4 n:50"); }
          if (evAt(1.2f)) say(sc.evId == 1 ? "smug" : "relieved");
          if (evAt(2.4f)) say("happy");
          if (evT() > 0.1f) finishIn(3.0f);
          break;
        }
        if (sc.mid == 0) { if (atb(1, 3.6f)) say("annoyed"); if (atb(2, 6.2f)) say("irritated"); }
        if (sc.mid == 1) {
          if (sit) shown.cross = fmaxf(shown.cross, 0.9f);
          if (at(bt(0, 4))) say("suspicious");
          if (at(bt(0, 4) + 2.5f)) say("giggle");
        }
        if (sc.mid == 2) { if (at(bt(0, 3) + 1.4f)) say("dizzy"); if (at(bt(1, 7.5f))) say("woozy"); }
        if (sc.twist == 1 && atb(7, 6.5f)) say("annoyed");  // a second fly turns up
        if (sc.twist == 2 && u > bt(2, 7) && u < bt(2, 7) + 1) shown.cross = fmaxf(shown.cross, 0.8f);
        const float E = bt(3, 9.5f);
        if (sc.end == 2) {
          if (at(E)) say("relieved");
          if (at(E + 1.5f)) say("happy");
          if (u > E + 0.1f) finishIn(3.0f);
        } else {
          if (sit) shown.cross = fmaxf(shown.cross, 0.9f * seg(E, E + 0.4f));
          if (at(E)) say("suspicious");
          if (sc.end == 0) {
            if (at(E + 0.6f)) { say("startled"); sfx("w:snap|v0.4 n:50 0:40 1200~1900:70"); }
            if (at(E + 1.5f)) say("smug");
          } else {
            if (at(E + 1.2f)) { say("sneeze"); sfx("w:achoo|v0.6 n:280"); }
            faceShift(16 * kick(E + 1.2f, 0.06f, 7));
            if (at(E + 2.3f)) say("giggle");
          }
          if (at(E + 3)) say("happy");
          if (u > E + 0.1f) finishIn(4.2f);
        }
        break;
      }
      case SC_CRUMB: {  // Crumb the bird: hops, steals a crumb, naps; flies off, sings with Oreo, or stays
        float x, y, flap;
        int face, st;
        const bool on = crumbPos(x, y, flap, face, st);
        if (on) { lookPt(x, y); headPt(x, y, 0.35f); } else headTo(0, 0);
        if (at(0.3f)) say(crumbFear > 0 ? "curious" : crumbFriend > 1 ? "excited" : "happy");
        if (crumbFear > 0 && at(2.2f)) say("hopeful");
        if (atb(4, 1.2f)) sfx("w:tiny_chirp|v0.2 1760:45 0:45 2093:70");
        if (sc.mid == 1) { if (at(bt(0, 4.5f) + 0.8f)) { say("pout"); sfx("w:tiny_chirp|v0.25 2093:40 0:30 2349:60"); } if (at(bt(0, 4.5f) + 2.4f)) say("giggle"); }
        if (sc.mid == 2 && at(bt(0, 5) + 0.6f)) say("adoring");
        if (sc.twist == 1 && atb(5, 3.5f)) { say("singing"); sfx("w:tiny_chirp|v0.25 1568:90 1760:90 2093:90 1760:90 2349:160"); }
        if (sc.evId == 3) {
          if (evAt(0.01f)) { say("blissful"); crumbFriend = crumbFriend < 3 ? crumbFriend + 1 : 3; if (crumbFear > 0) crumbFear--; }
          if (evAt(0.9f)) sfx("w:tiny_chirp|v0.25 2093:60 2637:90");
          if (evT() > 0.1f) finishIn(3.4f);
          break;
        }
        if (sc.evId == 1) {
          if (evAt(0.01f)) { say("sheepish"); crumbFear = crumbFear < 3 ? crumbFear + 1 : 3; }
          if (evAt(1.2f)) say("sad");
          if (evT() > 0.1f) finishIn(2.6f);
          break;
        }
        const float E = bt(3, 9);
        if (sc.end == 0) { if (at(E)) say("waving"); if (at(E + 2.2f)) say("wistful"); if (u > E + 0.1f) finishIn(3.6f); }
        if (sc.end == 1) {
          if (at(E)) { say("singing"); sfx("w:tiny_chirp|v0.25 1568:120 0:60 1760:120 0:60 2093:200"); }
          if (at(E + 1.6f)) sfx("w:tiny_chirp|v0.25 2093:120 0:60 2349:120 0:60 2637:200");
          if (at(E + 3.2f)) say("happy");
          if (u > E + 0.1f) finishIn(4.4f);
        }
        if (sc.end == 2) { if (at(E + 1.3f)) say("content"); if (at(E + 3)) say("sleepy"); if (u > E + 0.1f) finishIn(5.0f); }
        if (u > E + 0.1f && sc.evId == 0) { if (crumbFear > 0 && at(E + 0.2f)) crumbFear--; if (sc.end != 0 && at(E + 0.2f) && crumbFriend < 3) crumbFriend++; }
        break;
      }
      case SC_CLOCK: {  // the real time: Crumb on a digit, a rolling digit, a nudge; a tick, a nap in the 0, a line-up
        const float in = seg(0.2f, 1.2f), E = bt(2, 9);
        const int sit = clockPick(7, 1), roll = clockPick(8, 3), nap = clockPick(0, 2);
        if (u < 1.4f) slook(0, 0.8f, 0.3f);
        faceShift(-24 * in * (1 - seg(sc.dur - 0.8f, sc.dur)));  // the face moves up: the digits are below it
        if (at(0.3f)) say("curious");
        if (at(1.4f)) say(minuteNow == 0 ? "excited" : "focus");
        if (sc.mid == 0) { lookPt(clockX(sit) + 18, 140); if (at(bt(0, 2.5f) + 1)) say("giggle"); }
        if (sc.mid == 1) {
          const float r0 = bt(0, 3), e = seg(r0, r0 + 1.4f) * (1 - seg(r0 + 3.2f, r0 + 4.6f));
          lookPt(clockX(roll) + 18 + 130 * e * sc.mir, 190);
          if (at(r0 + 0.4f)) say("surprised");
          if (at(r0 + 2.4f)) say("determined");
        }
        if (sc.mid == 2) {
          const float n0 = bt(0, 3);
          faceShift(26 * kick(n0, 0.15f, 5));
          if (at(n0)) { say("cheeky"); sfx("w:bonk|v0.3 330~220:80"); }
          if (at(n0 + 1)) say("giggle");
        }
        if (sc.evId == 3) {  // a pat: the digits jump for joy
          if (evAt(0.01f)) say("happy");
          if (evT() > 0.1f) finishIn(2.6f);
          break;
        }
        if (sc.end == 0) { if (at(E)) say("amazed"); if (at(E + 0.6f)) sfx("w:tick|v0.3 1400:30 0:200 1100:30"); }
        if (sc.end == 1) { lookPt(clockX(nap) + 18, 175); if (at(E + 1.2f)) say("adoring"); if (at(E + 2.6f)) say("sleepy"); }
        if (sc.end == 2) { if (at(E)) say("proud"); if (at(E + 1.6f)) say("happy"); }
        (void)in;
        if (u > E + 0.1f) finishIn(sc.end == 1 ? 4.6f : 3.4f);
        break;
      }
      case SC_DOT: {  // a laser dot: chased, ambushed or ignored; caught, gone, or many
        float x, y;
        const bool on = dotPos(x, y);
        const float E = bt(2, 10);
        if (sc.mid == 1 && u < bt(0, 6)) { slook(-0.6f * sc.mir, -0.3f, 0.3f); headTo(-0.2f * sc.mir, 0); }  // pretends not to look...
        else if (sc.mid == 2 && u < E - 1) { if (fracf(u / 2.6f) < 0.3f && on) lookPt(x, y); else slook(0.5f, -0.6f, 0.3f); }
        else if (on) { lookPt(x, y); headPt(x, y, 0.5f); }
        if (at(0.5f)) say(sc.mid == 2 ? "bored" : "excited");
        if (sc.mid == 0) for (int q = 0; q < 2; q++) if (atb(q, 4.0f + q * 3)) { say("determined"); faceShift(12); }
        if (sc.mid == 0) faceShift(10 * (kick(bt(0, 4)) + kick(bt(1, 7))));
        if (sc.mid == 1) { if (atb(3, 3)) say("sneaky"); if (at(bt(0, 6))) { say("excited"); sfx("w:boing|v0.3 300~700:120"); } faceShift(14 * kick(bt(0, 6))); }
        if (sc.mid == 2) { if (atb(3, 4)) say("eye_roll"); if (atb(4, 7)) say("side_eye"); }
        if (sc.end == 0) { if (at(E + 0.6f)) { say("triumphant"); sfx("w:snap|v0.4 n:50"); } if (at(E + 2)) say("proud"); }
        if (sc.end == 1) { if (at(E + 0.3f)) say("confused"); if (at(E + 1.8f)) say("thinking"); }
        if (sc.end == 2) { if (at(E + 0.4f)) say("overwhelmed"); if (at(E + 1.8f)) say("dizzy"); }
        if (u > E + 0.1f) finishIn(3.2f);
        break;
      }
      case SC_GLIDER: {  // a paper glider: loops, glides or stalls; then bonks the head, lands, or flies off
        float x, y, a;
        gliderPos(x, y, a);
        const float E = bt(2, 4.6f);
        if (x > -20 && x < 340) { lookPt(x, y); headPt(x, y, 0.4f); }
        if (at(0.4f)) say("amazed");
        if (sc.mid == 0 && at(bt(0, 2.4f) + 0.6f)) say("starstruck");
        if (sc.mid == 2 && at(bt(0, 2.6f) + 0.7f)) say("worried");
        if (sc.end == 0) { if (at(E + 0.7f)) { say("dizzy"); sfx("w:bonk|v0.4 220~120:90"); faceShift(8); } if (at(E + 2.2f)) say("giggle"); }
        if (sc.end == 1) { if (at(E + 1.6f)) say("happy"); if (at(E + 2.8f)) say("proud"); }
        if (sc.end == 2) { if (at(E + 0.8f)) say("wistful"); }
        if (u > E + 0.1f) finishIn(sc.end == 1 ? 4.0f : 3.0f);
        break;
      }
      case SC_ANT: {  // an ant walks across the face: sneezed away, or it waves and leaves; a tap: it jumps off
        float x, y;
        const bool on = antPos(x, y);
        if (on) { lookPt(x, y); shown.cross = fmaxf(shown.cross, x > 120 && x < 200 ? 0.8f : 0); }
        if (at(0.6f)) say("curious");
        if (atb(3, 3)) say("suspicious");
        if (sc.evId == 1) {
          if (evAt(0.01f)) say("startled");
          if (evAt(1.0f)) say("giggle");
          if (evT() > 0.1f) finishIn(2.4f);
          break;
        }
        const float E = bt(2, 5.5f);
        if (sc.end == 0) {
          if (at(E)) say("sneeze_ah");
          if (at(E + 0.5f)) say("sneeze_ahh");
          if (at(E + 1.3f)) { say("sneeze"); sfx("w:achoo|v0.6 n:280"); }
          faceShift(16 * kick(E + 1.3f, 0.06f, 7));
          if (at(E + 2.4f)) say("sheepish");
          if (u > E + 0.1f) finishIn(3.6f);
        } else {
          if (at(E)) say("amazed");  // it waves its tiny arm
          if (at(E + 1.2f)) say("waving");
          if (at(E + 2.6f)) say("happy");
        }
        break;
      }
      case SC_PILLOW: {  // a pillow: fluffed, flopped on, circled; dozes, refuses, hugs; a bang: jolts awake
        const float in = seg(0.3f, 1.3f), E = bt(2, 7.5f);
        const bool bang = sc.evId == 2;
        if (at(0.4f)) say("sleepy");
        if (sc.mid == 0) { if (atb(0, 2.0f) || atb(1, 3.0f)) { say("determined"); sfx("w:pat|v0.25 n:40"); } }
        if (sc.mid == 1 && atb(0, 2.4f)) { say("exhausted"); sfx("w:flop|v0.3 n:120 140~90:120"); }
        if (sc.mid == 2) headTo(0.25f * sinf(u * 2.2f) * (1 - seg(E - 1, E)), 0.1f);
        faceShift(in * (sc.mid == 1 ? 24 : 14) * (1 - (bang ? seg(sc.evU, sc.evU + 0.3f) : 0)));
        if (bang) {
          if (evAt(0.01f)) { say("startled"); sfx("w:boing|v0.4 300~900:90"); }
          if (evAt(1.2f)) say("groggy");
          if (evT() > 0.1f) finishIn(2.8f);
          break;
        }
        if (sc.end == 0) {
          if (at(E)) say("sleeping");
          if (at(E + 1.5f) || at(E + 4.5f)) sfx("w:snore|v0.14 120~85:800");
          if (u > E + 0.1f) finishIn(7.0f);
        } else if (sc.end == 1) {
          if (at(E)) say("sassy");
          if (at(E + 1.4f)) say("smug");
          if (u > E + 0.1f) finishIn(2.6f);
        } else {
          if (at(E)) say("adoring");
          if (at(E + 1.6f)) say("cozy");
          if (u > E + 0.1f) finishIn(3.4f);
        }
        break;
      }
      case SC_MUG: {  // a warm mug: sip, blow, sniff; too hot, ahh, or a steam heart
        const float E = bt(2, 6.5f), lift = sc.mid == 0 ? seg(bt(0, 3), bt(0, 3) + 0.6f) * (1 - seg(E - 0.4f, E)) : 0;
        slook(0, 0.7f - 0.6f * lift, 0.3f);
        headTo(0, 0.2f);
        if (at(0.4f)) say("cozy");
        if (sc.mid == 0 && at(bt(0, 3) + 0.6f)) say("munching");
        if (sc.mid == 1) { if (atb(0, 2.4f) || atb(1, 3.6f)) { say("puffed"); sfx("w:blow|v0.2 n:300"); } }
        if (sc.mid == 2 && atb(0, 2.6f)) say("dreamy");
        if (sc.end == 0) { if (at(E)) { say("tongue"); sfx("w:hot|v0.3 900~600:120"); } if (at(E + 1.6f)) say("sheepish"); }
        if (sc.end == 1) { if (at(E)) say("blissful"); if (at(E + 1.8f)) say("content"); }
        if (sc.end == 2) { if (at(E + 0.6f)) say("adoring"); if (at(E + 2.2f)) say("happy"); }
        if (u > E + 0.1f) finishIn(3.4f);
        break;
      }
      case SC_HELLO: {  // you're here: a wave, a peek, a big smile; then a wink, a blush, or a heart
        const float E = bt(2, 3.2f);
        slook(0, -0.1f, 0.3f);
        if (sc.mid == 1) faceShift(70 * (1 - seg(0.3f, 1.4f)));  // peeks up from below
        if (at(0.3f)) say(sc.mid == 2 ? "excited" : sc.mid == 1 ? "peek" : "happy");
        if (sc.mid == 0 && at(0.5f)) sfx("w:v_chat1|700~900:120");
        if (at(E)) say(sc.end == 0 ? "wink_left" : sc.end == 1 ? "shy" : "lovestruck");
        if (u > E + 0.1f) finishIn(2.6f);
        break;
      }
      case SC_BOOK: {  // a book: reads / flicks through / upside down; a bookworm; asleep, laughing or proud
        const bool arrived = sc.evId == 4;
        const float E = arrived ? sc.evU : bt(2, 14.4f);
        const bool sleeps = !arrived && sc.end == 0;
        const float up = seg(0, 1.2f) * (sleeps ? 1 : 1 - seg(E, E + 1));
        faceShift(-16 * up);
        headTo(0, sleeps && u > E ? 0.45f : 0.35f * up);
        if (u > 1.5f && u < E - 0.2f) {
          const float per = sc.mid == 1 ? 0.9f : 1.8f;  // flicking through: twice as fast
          const float line = fracf((u - 1.5f) / per);
          const float lx = line < 0.85f ? -0.55f + 1.1f * line / 0.85f : 0.55f - 1.1f * (line - 0.85f) / 0.15f;
          const int ln = (int)((u - 1.5f) / per) % 4;
          slook(lx, 0.8f + ln * 0.08f, 0.3f);
        }
        if (arrived && u > E && u < E + 1.6f) slook(0, -0.5f, 0.3f);  // looks up at you
        if (at(0.2f)) say("curious");
        if (at(1.5f)) say(sc.mid == 2 ? "confused" : "focus");
        if (sc.mid == 2) { if (atb(0, 4.6f)) say("embarrassed"); if (at(bt(0, 4.6f) + 1.2f)) say("focus"); }
        if (sc.mid == 1) { if (atb(3, 4)) say("excited"); }
        else if (atb(3, 7)) say(sc.v[1] < 0.5f ? "amazed" : "shocked");
        if (sc.twist == 1) { const float W = bt(1, 8); if (at(W)) say("startled"); if (at(W + 1.2f)) say("giggle"); if (at(W + 2.6f)) say("focus"); }
        if (atb(4, 5.2f) || atb(5, 9.6f) || atb(6, 13.2f) || (sc.mid == 1 && fracf((u - 2) / 1.15f) < 0.03f && u > 2 && u < 13))
          if (u < E) sfx("w:page|v0.2 n:110");
        if (arrived) {
          if (evAt(0.01f)) say("happy");
          if (evAt(1.0f)) say("adoring");
          if (evT() > 0.1f) finishIn(2.6f);
          break;
        }
        if (sleeps) {
          if (at(E)) say("sleepy");
          if (at(E + 1.4f)) say("sleeping");
          if (at(E + 2.4f) || at(E + 5)) sfx("w:snore|v0.14 120~85:800");
          if (u > E + 0.1f) finishIn(6.5f);
        } else {
          if (at(E - 0.3f)) say(sc.end == 1 ? "laugh" : "proud");
          if (at(E + 1.2f)) say("happy");
          if (u > E + 0.1f) finishIn(2.4f);
        }
        break;
      }
      case SC_PIANO: {
        const float up = seg(0, 1.0f) * (1 - seg(sc.dur - 1.2f, sc.dur - 0.3f));
        faceShift(-14 * up);
        const float tl = tuneLen(pianoTune());
        if (at(0.2f)) say("curious");
        if (at(1.2f)) say("focus");
        if (at(1.6f)) playTune(pianoTune(), 0.45f);
        if (at(1.6f + tl * 0.45f)) say("vibing");
        float nt = 0;
        const int key = u > 1.6f ? noteAt(pianoTune(), u - 1.6f, &nt) : -1;
        if (key >= 0) {
          const float kx = 20 + key * 20 + 10;
          lookPt(kx, 215);
          headTo((kx - W / 2) / (W / 2) * 0.35f, 0.3f);
          faceShift(3 * kick(1.6f + nt, 0.05f, 12));
        } else {
          headTo(0, 0.3f * up);
        }
        if (at(1.6f + tl + 0.3f)) { say("proud"); sfx("w:piano_finale|v0.4 523:110 659:110 784:110 1047:360"); }
        break;
      }
      case SC_SNACK: {  // a snack: cookie / donut / apple / watermelon; three bites or one gulp; then an ending
        float x, y;
        snackPos(x, y);
        const float E = snackEndT();
        if (u < E - 1.2f) { lookPt(x, y); headPt(x, y, 0.35f); }
        else headTo(0, 0);
        if (at(0.4f)) say("curious");
        if (atb(5, 1.5f)) say("hungry");
        if (atb(6, 2.5f)) say("drooling");
        for (int b = 0; b < snackBiteN(); b++)
          if (at(snackBiteT(b))) { say("munching"); sfx(sc.twist == 1 ? "w:crunch|v0.5 n:80 0:40 n:60" : "w:crunch|v0.4 n:45 0:60 n:40 0:70 n:35 0:60 n:30"); }
        if (sc.twist == 1 && at(snackBiteT(0) + 0.6f)) say("puffed");
        if (sc.twist == 2) {  // Crumb swoops in for the last bite
          const float c0 = snackBiteT(1) + 0.9f;
          if (at(c0)) say("surprised");
          if (at(c0 + 0.9f)) { say("pout"); sfx("w:tiny_chirp|v0.25 2093:40 0:30 2349:60"); }
          if (at(c0 + 2.2f)) say("giggle");
        }
        if (sc.evId == 3) {  // a pat: it shares a crumb with you
          if (evAt(0.01f)) say("adoring");
          if (evAt(1.3f)) say("happy");
          if (evT() > 0.1f) finishIn(2.6f);
          break;
        }
        if (at(E)) say("yum");
        if (sc.end == 0) { if (at(E + 0.9f)) say("sheepish"); if (at(E + 2.2f)) say("giggle"); }
        if (sc.end == 1) {
          for (int h = 0; h < 3; h++) if (at(E + 0.8f + h * 0.9f)) { say("hiccup"); sfx("w:hiccup|v0.3 700~1100:60"); faceShift(-6); }
        }
        if (sc.end == 2) { if (at(E + 1.0f)) say(sc.v[3] < 0.5f ? "blissful" : "content"); if (at(E + 2.6f)) say("sleepy"); }
        if (u > E + 0.1f) finishIn(sc.end == 1 ? 4.0f : 3.4f);
        break;
      }
      case SC_BUBBLES: {
        int tgt = -1;
        float best = 1e9f;
        const int n = bubbleN();
        for (int i = 0; i < n; i++) {
          float x, y, r, tp;
          bubble(i, x, y, r, tp);
          if (u < tp && tp < best && u > tp - 6 && y < 250) { best = tp; tgt = i; }
          if (at(tp)) { sfx(sc.mid == 2 ? "w:bubble_pop|v0.5 900~1600:90" : "w:bubble_pop|v0.3 1700~2600:45"); if (sc.end != 2 && sc.evId != 1) say(i % 3 == 0 ? "giggle" : i % 3 == 1 ? "surprised" : "happy"); }
        }
        if (tgt < 0 && sc.end == 1) {  // floating away: watch the highest one go
          float hy = 1e9f;
          for (int i = 0; i < n; i++) { float x, y, r, tp; bubble(i, x, y, r, tp); if (y > -30 && y < hy) { hy = y; tgt = i; } }
        }
        if (tgt >= 0) {
          float x, y, r, tp;
          bubble(tgt, x, y, r, tp);
          lookPt(x, y);
          headPt(x, y, 0.4f);
        } else headTo(0, 0);
        if (at(0.3f)) say(sc.mid == 2 ? "awe" : "amazed");
        const float E = bt(2, 8.5f);
        if (sc.evId == 1) {
          if (evAt(0.05f)) say("startled");
          if (evAt(0.9f)) say("laugh");
          if (evT() > 0.1f) finishIn(2.5f);
          break;
        }
        if (sc.end == 0) { if (at(E + 0.4f)) say("surprised"); if (at(E + 1.2f)) say("giggle"); }
        if (sc.end == 1) { if (at(E + 1)) say("wistful"); if (at(E + 3)) say("content"); }
        if (sc.end == 2) { if (at(E)) say("excited"); if (at(E + 1.5f)) say("laugh"); }
        if (u > E + 0.1f) finishIn(sc.end == 1 ? 4.5f : 3.0f);
        break;
      }
      case SC_BUTTERFLY: {
        float x, y;
        int where;
        butterflyPos(x, y, &where);
        const bool patted = sc.evId == 3;
        const float E = patted ? sc.evU : bt(3, 9.2f);
        const int end = patted ? 2 : sc.end;
        if (where == 1) { slook(0, -1.2f, 0.3f); headTo(0, -0.5f); shown.cross = fmaxf(shown.cross, 0.4f); }
        else if (where == 2) { slook(0, 0.6f, 0.3f); headTo(0, 0.1f); shown.cross = fmaxf(shown.cross, 0.95f); }
        else if (x > -20 && x < 340) { lookPt(x, y); headPt(x, y, 0.45f); }
        else headTo(0, 0);
        if (at(0.3f)) say("amazed");
        if (atb(1, 3)) say(sc.mid == 2 ? "giggle" : "happy");
        if (sc.mid == 1 && at(bt(0, 5.2f) + 1)) { say("adoring"); sfx("w:chime_up|v0.25 1568:90 2093:170"); }
        if (patted && evAt(0.01f)) say("surprised");
        if (at(E + 1.0f)) {
          if (end == 0) { say("wistful"); sfx("w:chime_down|v0.2 2093:70 1568:90"); }
          if (end == 1) { say("sleepy"); sfx("w:chime_up|v0.2 1568:90 2093:170"); }
          if (end == 2) say("shy");
        }
        if (end == 1 && at(E + 3)) say("sleeping");
        if (end == 2 && at(E + 2.6f)) say("blissful");
        if (end == 0 && at(E + 2.4f)) say("content");
        if (u > E + 0.1f) finishIn(end == 1 ? 6.0f : 4.0f);
        break;
      }
      case SC_DREAM: {
        if (at(0.2f)) say("sleepy");
        if (at(1.8f)) say("sleeping");
        if (at(3.2f) || at(6.8f) || at(10.2f)) sfx("w:snore|v0.14 120~85:800");
        headTo(0, u < 12.5f ? 0.3f * seg(1, 3) : 0.3f * (1 - seg(12.5f, 13.5f)));
        if (at(12.5f)) { say("startled"); sfx("w:dream_pop|v0.3 900~300:90"); }
        if (at(13.8f)) say("groggy");
        if (at(15.2f)) say("yawn");
        break;
      }
      case SC_STARS: {
        const float up = seg(0, 1.8f) * (1 - seg(13, 14.8f));
        headTo(0.15f * sinf(u * 0.4f) * up, -0.6f * up);
        if (u < 13.5f) {
          if (u > 5.5f && u < 6.4f) {
            const float s = (u - 5.5f) / 0.8f;
            lookPt(lerpf(300, 80, s), lerpf(10, 70, s));
          } else if (u < 7.4f || u > 10.5f) {
            slook(0.5f * sinf(u * 0.45f), -0.95f, 0.3f);
          }
        }
        if (at(0.3f)) say("awe");
        if (at(5.5f)) sfx("w:shooting_star|v0.22 2637:60 2349:60 2093:60 1760:160");
        if (at(5.7f)) say("starstruck");
        if (at(7.4f)) { say("peaceful"); sfx("w:wish|v0.16 1047:220 1319:220 1568:420"); }
        if (at(10.5f)) say("giddy");
        if (at(13)) say("content");
        break;
      }
      case SC_TEA: {
        float y;
        teaPos(y);
        lookPt(160, y - 10);
        headTo(0, 0.3f * seg(0, 1.2f) * (1 - seg(12.6f, 13.6f)));
        shown.blush = fmaxf(shown.blush, 0.55f * seg(4, 6) * (1 - seg(12, 13.5f)));
        if (at(0.3f)) say("cozy");
        if (at(4.0f) || at(9.0f)) say("blissful");
        if (at(4.3f) || at(9.3f)) sfx("w:sip|v0.2 n:260");
        if (at(6.6f)) { say("relieved"); sfx("w:ahh|v0.16 520~330:480"); }
        if (at(8)) say("content");
        if (at(11.4f)) say("peaceful");
        if (at(12.8f)) say("happy");
        break;
      }
      case SC_GAME: {
        const float up = seg(0, 1.2f) * (1 - seg(13.4f, 14.4f));
        faceShift(-14 * up);
        headTo(0, 0.4f * up);  // (a +-1 deg wobble only made the pan servo jitter)
        if (u > 1 && u < 13) slook(-0.1f + 0.15f * sinf(u * 6.3f) * sinf(u * 2.1f), 1.0f, 0.3f);
        const bool win = sc.v[5] < 0.6f;
        for (int i = 0; i < 8; i++) {
          const float jt = 2 + i * 1.1f + 0.97f;
          if (at(jt) && (win || i < 7)) sfx("w:jump|v0.22 880~1320:60");
        }
        if (at(0.3f)) say("curious");
        if (at(2)) say("determined");
        if (at(6.5f)) say("focus");
        if (at(11)) {
          say(win ? "triumphant" : "frustrated");
          sfx(win ? "w:win|v0.35 523:90 659:90 784:90 1047:280" : "w:lose|v0.35 392:170 330:170 262:360");
        }
        if (at(12.4f)) say(win ? "celebrate" : "sulking");
        if (at(13.8f)) say(win ? "happy" : "determined");
        break;
      }
      case SC_HEADPHONES: {
        const float on = seg(0, 1.0f) * (1 - seg(sc.dur - 1.4f, sc.dur - 0.4f));
        const float beat = u * 1.9f;
        // a slow, clear nod to every other beat (a +-2.6 deg bob at 1.9 Hz made the servos jitter)
        headTo(0.28f * sinf(beat * PI_F * 0.5f) * on, (0.12f + 0.2f * sinf(beat * PI_F)) * on);
        faceShift(4 * sinf(beat * 2 * PI_F) * on);
        shown.tilt += 6 * sinf(beat * PI_F * 0.5f) * on;
        if (at(0.3f)) say("curious");
        // the music starts when the headphones are on
        if (at(1.2f)) { say("vibing"); playTune(singTune(), 0.3f); }
        if (at(5)) say("blissful");
        if (at(8)) say("singing");
        if (at(sc.dur - 3.0f)) say("vibing");
        if (at(sc.dur - 1.0f)) say("happy");
        break;
      }
      case SC_SNOW: {
        float x, y;
        flakePos(x, y);
        if (u < 3.5f) { slook(0.5f * sinf(u * 1.3f), -0.9f, 0.3f); headTo(0.1f * sinf(u), -0.4f * seg(0, 1)); }
        else if (u < 7.1f) { lookPt(x, y); headPt(x, y, 0.4f); shown.cross = fmaxf(shown.cross, 0.8f * seg(5.8f, 7)); }
        else headTo(0, 0);
        if (at(0.3f)) say("amazed");
        if (at(3.5f)) say("curious");
        if (at(5.4f)) say("tongue");
        if (at(7.1f)) { say("freezing"); sfx("w:tinkle|v0.25 2637:50 3136:100"); }
        if (at(9.2f)) say("giggle");
        if (at(11.4f)) say("happy");
        break;
      }
      case SC_PAINT: {
        float x, y;
        brushPos(x, y);
        if (u < 9.5f) { lookPt(x, y); headPt(x, y, 0.35f); }
        else { slook(0.6f * sinf(u * 0.8f), -0.7f, 0.3f); headTo(0, -0.2f); }
        if (at(0.3f)) say("curious");
        if (at(1.5f)) say("focus");
        if (at(5)) say("happy");
        if (at(9.5f)) { say("proud"); sfx("w:sparkle|v0.3 1047:80 1319:80 1568:80 2093:260"); }
        if (at(12)) say("content");
        break;
      }
      case SC_PEEKABOO: {
        const float covers[3] = {0.8f, 4.3f, 7.4f}, holds[3] = {1.5f, 1.1f, 0.7f};
        for (int i = 0; i < 3; i++) {
          if (at(covers[i])) say("giggle");
          const float op = covers[i] + 0.35f + holds[i];
          if (at(op)) { say("surprised"); sfx("w:boo|v0.35 500~1150:120"); }
          if (at(op + 0.45f)) say(i == 2 ? "laugh" : "joy");
          headTo(0, -0.28f * kick(op, 0.08f, 5));
        }
        if (at(11)) say("happy");
        break;
      }
      case SC_FEATHER: {
        float x, y, a;
        featherPos(x, y, a);
        if (u < 8.3f) { lookPt(x, y); headPt(x, y, 0.35f); }
        else headTo(0, 0.45f * kick(8.3f, 0.06f, 6));
        faceShift(18 * kick(8.3f, 0.06f, 7));
        if (at(0.3f)) say("curious");
        if (at(4.2f)) { say("sneeze_ah"); sfx("w:ah1|v0.18 400~560:280"); }
        if (at(5.6f)) { say("sneeze_ahh"); sfx("w:ah2|v0.22 450~660:300"); }
        if (at(7.0f)) { say("sneeze_ahhh"); sfx("w:ah3|v0.26 500~780:340"); }
        if (at(8.2f)) { say("sneeze"); sfx("w:achoo|v0.6 n:280"); }
        if (at(9.2f)) say("sneeze_after");
        if (at(10.4f)) say("sheepish");
        if (at(11.2f)) say("giggle");
        break;
      }
      case SC_BALLOON: {
        float x, y;
        balloonPos(x, y);
        if (u < 7) { lookPt(x, y); headPt(x, y, 0.5f); }
        else headTo(0, 0.2f * kick(7, 0.05f, 4));
        if (at(0.5f)) say("happy");
        if (at(3.5f)) say("amazed");
        if (sc.iv[2] % 2) {  // this time it floats away instead of popping
          if (u > 6.8f) { slook(0.1f * sinf(u), -1.1f, 0.3f); headTo(0, -0.5f * (1 - seg(10, 11.5f))); }
          if (at(7.2f)) say("wistful");
          if (at(9.8f)) say("content");
          break;
        }
        if (at(7)) { say("startled"); sfx("w:balloon_pop|v0.6 n:45"); }
        if (at(7.6f)) say("scared");
        if (at(9.3f)) { say("laugh"); sfx("w:giggle|v0.25 784:80 880:80 784:80 988:170"); }
        if (at(11.4f)) say("happy");
        break;
      }
      case SC_COIN: {
        float x, y, w;
        coinPos(x, y, w);
        if (u < 6) { lookPt(x, y); headPt(x, y, 0.5f); }
        else headTo(0, 0);
        if (at(0.3f)) say("curious");
        if (at(2.6f)) say("money");
        if (at(4.8f)) sfx("w:coin|v0.35 988:90 1319:420");
        if (at(5.9f)) say("triumphant");
        if (at(7.8f)) say("smug");
        if (at(9.6f)) say("happy");
        break;
      }
      case SC_HICCUPS: {
        const float hics[5] = {1.2f, 3.4f, 5.0f, 6.1f, 8.2f};
        float j = 0;
        for (float h : hics) {
          j = fmaxf(j, kick(h, 0.06f, 8));
          if (at(h)) sfx("w:hic|v0.3 600~1150:70");
        }
        faceShift(-16 * j);
        headTo(0, -0.3f * j);
        if (at(1.2f)) say("startled");
        if (at(2.2f)) say("embarrassed");
        if (at(3.4f) || at(6.1f)) say("hiccup");
        if (at(5.0f)) say("annoyed");
        if (at(7.0f)) say("frustrated");
        if (at(8.2f)) say("shocked");
        if (at(9.5f)) { say("relieved"); sfx("w:sigh|v0.16 440~300:500"); }
        if (at(11)) say("happy");
        break;
      }
      case SC_IDEA: {
        if (u < 7.3f) { slook(-0.55f + 0.25f * sinf(u * 0.9f), -0.75f, 0.3f); headTo(-0.2f, -0.25f); }
        else if (u < 11) { slook(0, -1.15f, 0.3f); headTo(0, -0.4f); }
        else headTo(0, 0);
        if (at(0.2f)) say("thinking");
        if (at(3.5f)) say("confused");
        if (at(5.5f)) say("thinking");
        if (at(7.3f)) { say("idea"); sfx("w:ding|v0.35 1319:90 1760:340"); }
        if (at(8.8f)) say("excited");
        if (at(11)) say("proud");
        break;
      }
      case SC_PLANE: {
        float x, y, a;
        planePos(x, y, a);
        if (x > -30 && x < 350) { lookPt(x, y); headPt(x, y, 0.5f); }
        else headTo(0, 0);
        if (at(0.3f)) say("curious");
        if (at(2.4f)) { say("amazed"); sfx("w:whoosh|v0.18 n:420"); }
        if (at(5)) say("happy");
        if (at(9.6f)) { say("surprised"); sfx("w:crumple|v0.3 n:70"); }
        if (at(10.3f)) say("giggle");
        if (at(11.6f)) say("content");
        break;
      }
      case SC_LADYBUG: {
        float x, y, a;
        bool fly;
        bugPos(x, y, a, fly);
        if (u < 11) { lookPt(x, y); headPt(x, y, 0.5f); }
        else headTo(0, 0);
        if (at(0.3f)) say("curious");
        if (at(3)) say("adoring");
        if (at(8.5f)) say("amazed");
        if (at(9.5f)) { say("happy"); sfx("w:tiny_chirp|v0.22 1760:45 0:45 2093:70"); }
        if (at(11.5f)) say("content");
        break;
      }
      case SC_SING: {
        const float tl = tuneLen(singTune());
        const float up = seg(0, 1.0f) * (1 - seg(sc.dur - 1.2f, sc.dur - 0.3f));
        faceShift(-10 * up);
        if (at(0.3f)) say("happy");
        if (at(1.6f)) { say("singing"); playTune(singTune(), 0.45f); }
        float nt = 0;
        const int key = u > 1.6f && u < 1.6f + tl ? noteAt(singTune(), u - 1.6f, &nt) : -1;
        if (key >= 0) {
          headTo(0.3f * sinf(u * 1.6f), -0.1f - key * 0.02f);
          faceShift(-2 * kick(1.6f + nt, 0.05f, 10));
        } else {
          const float bow = since(1.6f + tl + 0.6f);
          headTo(0, bow > 0 && bow < 1.6f ? 0.5f * sinf(fminf(1, bow / 1.6f) * 2 * PI_F) * (bow < 0.8f ? 1 : 0.6f) : 0);
        }
        if (at(1.6f + tl + 0.2f)) say("proud");
        if (at(1.6f + tl + 2.2f)) say("grateful");
        break;
      }
      case SC_CAKE: {
        const float up = seg(0, 1.3f) * (1 - seg(14.6f, 15.6f));
        faceShift(-14 * up);
        const float tl = tuneLen(7);
        if (u < 9) lookPt(160, 200);
        if (at(0.3f)) say("amazed");
        if (at(1.4f)) say("starstruck");
        if (at(1.9f)) { say("singing"); playTune(7, 0.45f); }
        if (u > 1.9f && u < 1.9f + tl) headTo(0.28f * sinf(u * 2.2f), 0.15f);
        else if (u < 14) headTo(0, u > 9 && u < 9.8f ? 0.3f : 0.1f);
        else headTo(0, 0);
        if (at(9)) { say("whistle"); sfx("w:blow|v0.3 n:500"); }
        if (at(10.1f)) { say("party"); sfx("w:party|v0.35 784:100 988:100 1175:100 1568:340"); }
        if (at(13.2f)) say("happy");
        break;
      }
      case SC_RAIN: {  // a little rain cloud: drizzle or downpour; an umbrella or thunder; then sun, a splash, or cozy
        const float U0 = bt(1, 5), E = bt(2, 11);
        const bool umb = sc.twist == 1, thunder = sc.twist == 2;
        if (u < U0) { slook(0.1f * sinf(u * 2), -0.9f, 0.3f); headTo(0, u < 3.5f ? -0.3f : 0.25f); }
        else if (u < E) { slook(0.3f * sinf(u * 0.8f), -0.7f, 0.3f); headTo(0, -0.2f); }
        else if (sc.end == 0) { slook(0.8f, -0.8f, 0.3f); headTo(0.3f, -0.2f); }
        else if (sc.end == 1) { slook(0.2f, 1.0f, 0.3f); headTo(0, 0.35f); }
        else { slook(0, 0.3f, 0.3f); headTo(0, 0.25f); }
        if (at(0.2f)) say("curious");
        if (at(1.5f)) sfx(sc.mid == 1 ? "w:rain|v0.4 n:900" : "w:rain|");
        if (atb(0, 2)) say(sc.mid == 1 ? "shocked" : "gloomy");
        if (atb(3, 3.5f)) say("sad");
        if (umb) {
          if (at(U0)) { sfx("w:umbrella|v0.3 400~900:130"); say("surprised"); }
          if (at(U0 + 0.6f)) say("relieved");
          if (at(U0 + 2.2f)) say("smug");
        }
        if (thunder) {
          if (at(U0)) { sfx("w:thunder|v0.6 n:500 90~50:600"); say("startled"); }
          if (at(U0 + 0.9f)) say("scared");
          if (at(U0 + 3)) say("nervous");
        }
        if (sc.end == 0) { if (at(E)) say("happy"); if (at(E + 1.6f)) say("sunny"); }
        if (sc.end == 1) { if (at(E + 0.6f)) { say("giggle"); sfx("w:splash|v0.4 n:150 600~200:120"); } if (at(E + 2)) say("playful"); }
        if (sc.end == 2) { if (at(E)) say("cozy"); if (at(E + 1.8f)) say("sleepy"); if (at(E + 3.4f)) say("yawn"); }
        if (u > E + 0.1f) finishIn(sc.end == 2 ? 5.0f : 3.4f);
        break;
      }
      case SC_JUGGLE: {
        const float dropT = jugDropT();
        int hi = 0;
        float hy = 1e9f;
        for (int i = 0; i < 3; i++) {
          float x, y;
          ballPos(i, x, y);
          if (y < hy) { hy = y; hi = i; }
        }
        if (u < dropT) {
          float x, y;
          ballPos(hi, x, y);
          lookPt(x, y);
          headPt(x, y, 0.3f);
        } else if (u < dropT + 1.2f) {
          float x, y;
          ballPos(2, x, y);
          lookPt(x, fminf(y, 235));
          headTo(0.2f, 0.4f);
        } else headTo(0, 0);
        if (at(0.3f)) say("focus");
        if (at(3.5f)) say("determined");
        if (at(6)) say("confident");
        if (at(dropT)) { say("shocked"); sfx("w:uh_oh|v0.3 700~200:320"); }
        if (at(dropT + 1.2f)) say("sheepish");
        if (at(dropT + 2.4f)) say("giggle");
        break;
      }
      case SC_GHOST: {
        float x, y, s, a;
        ghostPos(x, y, s, a);
        if (u < 2.6f || (u > 4.5f && u < 10)) { lookPt(x, y); headPt(x, y, 0.45f); }
        else if (u < 4.5f) { slook(-0.7f, 0.3f, 0.3f); headTo(-0.45f, 0.15f); }
        else headTo(0, 0);
        if (at(0.5f)) say("curious");
        if (at(2.6f)) { say("terrified"); sfx("w:ghost_boo|v0.35 320~150:380"); }
        if (at(4.5f)) say("confused");
        if (at(5.8f)) say("happy");
        if (at(8)) say("giggle");
        if (at(10.2f)) say("wistful");
        if (at(11.3f)) say("content");
        break;
      }
      case SC_MOVIE: {
        // the film's light on the eyes: bluish, a warm scene, a scary red flash, a sad blue
        const float fl = 0.5f + 0.5f * sinf(u * 9) * sinf(u * 3.7f);
        RGB lc = mixc(C(150, 190, 255), C(255, 255, 255), fl);
        if (u > 6.3f && u < 7.8f) lc = mixc(C(255, 60, 40), C(40, 20, 30), fl);
        else if (u > 10.4f && u < 12.4f) lc = C(90, 140, 255);
        const float ta = 0.35f * seg(0.5f, 1.5f) * (1 - seg(13.8f, 14.8f));
        shown.tr = shown.tr + (lc.r - shown.tr) * ta;
        shown.tg = shown.tg + (lc.g - shown.tg) * ta;
        shown.tb = shown.tb + (lc.b - shown.tb) * ta;
        shown.tint = fmaxf(shown.tint, ta);
        float px, py;
        if (popcornPos(px, py)) { lookPt(px, py); headPt(px, py, 0.3f); }
        else { slook(0.05f * sinf(u * 1.3f), -0.1f, 0.3f); headTo(0, u > 6.3f && u < 7.2f ? -0.2f : 0); }
        const float tosses[4] = {2.5f, 4.0f, 9.0f, 11.5f};
        for (float tt : tosses) if (at(tt + 0.5f)) sfx("w:munch|v0.3 n:35 0:50 n:30");
        if (at(0.3f)) say("curious");
        if (at(1.5f)) say("content");
        if (at(3.0f) || at(4.5f) || at(9.5f)) say("munching");
        if (at(5.2f)) say("focus");
        if (at(6.3f)) { say("scared"); sfx("w:scary|v0.3 200~110:320"); }
        if (at(7.8f)) say("laugh");
        if (at(10.4f)) say("sad_cry");
        if (at(12.4f)) say("tears_of_joy");
        if (at(13.8f)) say("happy");
        break;
      }
      case SC_LEAF: {  // a leaf (or two) drifts down - lands on the head, is blown off, or munched
        float x, y;
        int where;
        leafPos(x, y, 0, &where);
        if (where == 1) { slook(0, -1.2f, 0.3f); headTo(0, -0.4f); }
        else if (where == 2) { slook(0, 0.7f, 0.3f); shown.cross = fmaxf(shown.cross, 0.6f); }
        else { lookPt(x, y); headPt(x, y, 0.35f); }
        if (at(0.3f)) say(weatherNow == 3 ? "wistful" : "curious");
        if (sc.twist == 1 && at(1.6f)) say("amazed");
        const float E = bt(1, 3.4f) / (weatherNow == 3 ? 1.4f : 1.0f);
        if (at(E + 0.8f)) say(sc.end == 0 ? "content" : sc.end == 1 ? "surprised" : "munching");
        if (sc.end == 2 && at(E + 2.0f)) say("giggle");
        if (sc.end == 1 && at(E + 1.6f)) say("wistful");
        if (u > E + 0.1f) finishIn(2.8f);
        break;
      }
      case SC_BIRD: {  // a little bird flaps across the top
        const float x = lerpf(-30, 350, u / 4.2f), y = 40 + 12 * sinf(u * 2.5f);
        if (x > -20 && x < 340) { lookPt(x, y); headPt(x, y, 0.45f); }
        if (at(0.4f)) { say("amazed"); sfx("w:tiny_chirp|v0.2 1760:45 0:45 2093:70"); }
        if (at(2.6f)) say("happy");
        break;
      }
      case SC_BEE: {  // a bee: quicker than the fly - its own path, a close buzz, then off or a rest
        float x, y;
        bool rest;
        beePos(x, y, &rest);
        lookPt(x, y);
        headPt(x, y, rest ? 0.2f : 0.4f);
        if (at(0.2f)) { say("startled"); sfx("w:fly_buzz|v0.3 z:900"); }
        if (sc.evId == 2) {
          if (evAt(0.01f)) say("startled");
          if (evAt(1.0f)) say("relieved");
          if (evT() > 0.1f) finishIn(2.0f);
          break;
        }
        if (atb(0, 2)) say(sc.mid == 1 ? "suspicious" : sc.v[3] < 0.5f ? "scared" : "curious");
        if (sc.twist == 1 && at(bt(1, 2.3f))) { say("startled"); sfx("w:fly_buzz|v0.5 z:600"); }
        const float E = bt(2, 3.8f);
        if (sc.end == 1) {
          if (at(E + 0.8f)) say("adoring");
          if (at(E + 2.6f)) say("content");
          if (u > E + 0.1f) finishIn(3.8f);
        } else {
          if (at(E + 0.4f)) say("relieved");
          if (u > E + 0.1f) finishIn(1.4f);
        }
        break;
      }
      case SC_FIREFLY: {  // a glowing firefly wanders and blinks
        const float x = 160 + 120 * sinf(u * 0.7f + sc.v[0] * 6), y = 80 + 50 * sinf(u * 1.1f);
        lookPt(x, y);
        headPt(x, y, 0.35f);
        if (at(0.4f)) say("awe");
        if (at(3.5f)) say(sc.v[3] < 0.5f ? "adoring" : "content");
        break;
      }
      case SC_SPIDER: {  // a spider drops down on its thread, bounces, climbs back up
        const float y = spiderY();
        lookPt(230, y);
        headPt(230, y, 0.3f);
        if (at(1.0f)) say(sc.v[3] < 0.5f ? "startled" : "curious");
        if (at(2.6f)) say(sc.v[3] < 0.5f ? "scared" : "giggle");
        if (at(4.6f)) say("relieved");
        break;
      }
      case SC_COMET: {  // a comet streaks across
        const float e = seg(0.5f, 2.2f);
        lookPt(lerpf(20, 300, e), lerpf(20, 70, e));
        headTo(lerpf(-0.3f, 0.3f, e), -0.3f);
        if (at(0.6f)) sfx("w:shooting_star|v0.22 2637:60 2349:60 2093:60");
        if (at(0.8f)) say("amazed");
        if (at(2.6f)) say("starstruck");
        break;
      }
      case SC_HEART: {  // a heart floats up and bursts into sparkles
        const float y = lerpf(260, 60, seg(0, 3));
        lookPt(160 + 20 * sinf(u * 2), y);
        if (at(0.3f)) say("adoring");
        if (at(3.0f)) sfx("w:sparkle|v0.25 1568:80 2093:160");
        if (at(3.2f)) say("happy");
        break;
      }
      case SC_NOTES: {  // notes drift by and it bobs along
        headTo(0.22f * sinf(u * 3), 0.18f * sinf(u * 3));  // (a slow sway: a fast little bob jittered)
        faceShift(3 * sinf(u * 6));
        if (at(0.2f)) say("vibing");
        if (at(0.4f)) sfx("w:hum|v0.22 523:230 587:230 659:230 784:460");
        if (at(4.2f)) say("happy");
        break;
      }
      case SC_SNEEZE: {  // a sneeze out of nowhere
        if (at(0.3f)) { say("sneeze_ah"); sfx("w:ah1|v0.18 400~560:280"); }
        if (at(1.1f)) { say("sneeze_ahh"); sfx("w:ah2|v0.22 450~660:300"); }
        if (at(1.9f)) { say("sneeze_ahhh"); sfx("w:ah3|v0.26 500~780:340"); }
        if (at(2.7f)) { say("sneeze"); sfx("w:achoo|v0.6 n:280"); }
        headTo(0, u < 2.7f ? -0.3f * seg(0.3f, 2.5f) : 0.4f * kick(2.7f, 0.06f, 6));
        faceShift(16 * kick(2.7f, 0.06f, 7));
        if (at(3.7f)) say(sc.v[3] < 0.5f ? "sneeze_after" : "sheepish");
        break;
      }
      case SC_YAWN: {  // a big yawn and a stretch of the neck
        if (at(0.2f)) { say("yawn"); sfx("w:v_yawn|"); }
        headTo(0.25f * sinf(seg(0.3f, 3.5f) * 2 * PI_F), -0.45f * sinf(seg(0.3f, 3.5f) * PI_F));
        if (at(3.2f)) say(sc.v[3] < 0.5f ? "content" : "sleepy");
        break;
      }
      case SC_LOOKOFF: {  // something off to one side catches its eye
        const float side = sc.v[1] < 0.5f ? -1 : 1;
        if (u < 3) { slook(side * 1.1f, -0.2f + 0.3f * sc.v[2], 0.3f); headTo(side * 0.55f * seg(0.2f, 0.8f), -0.1f); }
        else headTo(0, 0);
        if (at(0.3f)) { say("curious"); sfx("w:v_hmm|"); }
        if (at(1.8f)) say(sc.v[3] < 0.33f ? "suspicious" : sc.v[3] < 0.66f ? "amazed" : "giggle");
        if (at(3.1f)) say("happy");
        break;
      }
      case SC_SNAIL: {  // a snail inches along the bottom
        const float x = lerpf(-20, 340, u / 7.0f);
        lookPt(x, 225);
        headPt(x, 225, 0.3f);
        if (at(0.5f)) say("curious");
        if (at(2.5f)) say("adoring");
        if (at(5.5f)) say("content");
        break;
      }
      case SC_SEEDS: {  // dandelion seeds drift past
        slook(0.6f * sinf(u * 0.8f), -0.4f + 0.3f * sinf(u * 0.5f), 0.3f);
        headTo(0.2f * sinf(u * 0.8f), -0.15f);
        if (at(0.3f)) say("awe");
        if (at(3.5f)) say(sc.v[3] < 0.5f ? "peaceful" : "wistful");
        break;
      }
      case SC_DICE: {  // a die tumbles in, bounces, settles: a six is a triumph, a one a shock
        const int pip = 1 + (int)(sc.v[0] * 5.999f);
        slook(0.6f, 0.65f - 0.5f * (1 - seg(0.5f, 2.4f)), 0.3f);
        headTo(0.25f, 0.05f);
        if (at(0.3f)) say("curious");
        if (at(1.05f) || at(1.6f) || at(2.05f)) sfx("w:tick|v0.35 1300:20");
        if (at(2.5f)) {
          if (pip == 6) { say("proud"); sfx("w:v_yay|660~990:120 0:40 990~1320:200"); }
          else if (pip == 1) { say("startled"); sfx("w:v_uhoh|500~300:200"); }
          else { say(sc.v[1] < 0.5f ? "happy" : "cheeky"); sfx("w:v_hehe|"); }
        }
        if (at(5.5f)) say("happy");
        break;
      }
      case SC_RPS: {  // rock, paper, scissors: three shakes, its pick, and how it feels about it
        slook(0.6f, 0.6f, 0.3f);
        headTo(0.2f, 0.08f + 0.05f * sinf(u * 9) * (u > 1 && u < 3.4f ? 1 : 0));
        if (at(0.4f)) say("cheeky");
        if (at(1.2f) || at(2.0f) || at(2.8f)) sfx("w:beep|v0.3 700:70");
        if (at(3.5f)) sfx("w:beep_hi|v0.35 1100:140");
        if (at(4.3f)) say(sc.v[2] < 0.4f ? "proud" : sc.v[2] < 0.7f ? "silly" : "smug");
        if (at(4.4f) && sc.v[2] < 0.4f) sfx("w:v_yay|660~990:120 0:40 990~1320:200");
        if (at(7.2f)) say("happy");
        break;
      }
      case SC_GIFT: {  // a present: it wobbles, Oreo waits... pop! sparkles and confetti
        slook(0.6f, 0.6f, 0.3f);
        headTo(0.15f, 0.1f);
        if (at(0.4f)) say("curious");
        if (at(1.8f) || at(2.5f) || at(3.1f)) sfx("w:tick|v0.3 900:30");
        if (at(3.7f)) { say("excited"); sfx("w:v_yay|n:40 0:20 880~1320:160 0:40 1320~1760:220"); }
        if (at(6.2f)) say("happy");
        if (at(8.0f)) say("proud");
        break;
      }
      case SC_WINK: {  // a wink at you, a sparkle
        slook(0, 0, 0.3f);
        if (at(0.3f)) say(sc.v[1] < 0.5f ? "wink_left" : "wink_right");
        if (at(0.5f)) sfx("w:v_hehe|");
        if (at(2.2f)) say("happy");
        break;
      }
      case SC_SELFIE: {
        static const char* const poses[7] = {"wink_left", "cool", "cheeky", "kiss", "silly", "playful", "smug"};
        if (at(0.3f)) say("happy");
        if (at(2.0f)) say(poses[sc.iv[1] % 7]);
        if (u > 1 && u < 4.4f) slook(0, 0, 0.3f);  // into the camera
        if (at(2.0f) || at(2.8f)) sfx("w:beep|v0.3 880:80");
        if (at(3.6f)) sfx("w:beep_hi|v0.3 1320:160");
        if (at(4.4f)) sfx("w:shutter|v0.45 n:25 0:40 n:40");
        if (at(4.6f)) say("startled");
        if (at(5.6f)) say("happy");
        if (at(7)) say("proud");
        headTo(0.1f * sinf(u), -0.1f);
        break;
      }
      default: sceneStep2(u); break;  // face_scenes_more.h
    }
    sc.lastU = u;
  }

  // ---- positions over time (shared by the timeline and the drawing) ----
  // A leaf this take (k = 0, or 1 for the second leaf). where: 0 falling, 1 on the head, 2 at the mouth
  void leafPos(float& x, float& y, int k = 0, int* where = nullptr) const {
    const float wet = weatherNow == 3 ? 1.4f : 1.0f;  // a wet leaf falls faster
    const float u = sc.u - k * 1.3f;
    y = -20 + u * 45 * wet;
    x = 120 + 90 * sinf(u * 1.3f + sc.v[0] * 6 + k * 2) + 20 * u;
    if (sc.mid == 1) x += 25 * sinf(u * 6);                         // spins: a wider wobble
    if (sc.mid == 2) x += 110 * sc.mir * seg(1.2f, 2.0f) * (1 - seg(2.6f, 3.6f));  // a gust sweeps it
    int w = 0;
    const float E = bt(1, 3.4f) / wet;
    if (k == 0 && u > E) {
      if (sc.end == 0) {  // lands on the head
        const float s2 = seg(E, E + 0.8f);
        x = lerpf(x, 150, s2);
        y = lerpf(y, 18, s2);
        if (s2 > 0.95f) w = 1;
      } else if (sc.end == 1) {  // blown off
        const float e = clampf((u - E) / 1.0f, 0, 1);
        x += 300 * sc.mir * e * e;
        y -= 40 * e;
      } else {  // to the mouth: munched (pretend)
        const float s2 = seg(E, E + 0.7f);
        x = lerpf(x, 160, s2);
        y = lerpf(y, sc.fy + 70, s2);
        if (s2 > 0.95f) w = 2;
      }
    }
    if (where) *where = w;
  }
  float spiderY() const {
    const float u = sc.u;
    if (u < 1.2f) return lerpf(-20, 120, ease(u / 1.2f));
    if (u < 4.2f) return 120 + 14 * sinf((u - 1.2f) * 6) * expf(-(u - 1.2f) * 0.8f);
    return lerpf(120, -30, seg(4.2f, 5.6f));
  }
  // The fly: where it is this take (middle, ending, a tap / loud event: it flees). sit = on the nose
  void flyPos(float& x, float& y, bool* sitOut = nullptr) const {
    const float u = sc.u, a = sc.v[0] * 6.28f;
    float wx = 160 + 125 * sinf(u * 1.1f + a) * cosf(u * 0.43f), wy = 85 + 55 * sinf(u * 1.9f + a * 0.7f) + 6 * sinf(u * 23);
    const float in = ease(u / 1.5f);
    x = lerpf(-30, wx, in);
    y = lerpf(50, wy, in);
    const float nx = 160 + 3 * sinf(u * 5), ny = sc.fy + 46 + 2 * sinf(u * 7);
    bool sit = false;
    if (sc.mid == 1) {  // lands on the nose for a while
      const float l0 = bt(0, 4), l1 = l0 + 2.5f, k = seg(l0 - 0.6f, l0) * (1 - seg(l1, l1 + 0.6f));
      x = lerpf(x, nx, k);
      y = lerpf(y, ny, k);
      sit = u > l0 && u < l1;
    } else if (sc.mid == 2) {  // circles the head
      const float c0 = bt(0, 3), c1 = bt(1, 7.5f), k = seg(c0 - 0.5f, c0) * (1 - seg(c1, c1 + 0.5f));
      const float an = u * 2.4f * sc.mir;
      x = lerpf(x, 160 + 105 * cosf(an), k);
      y = lerpf(y, sc.fy + 15 + 70 * sinf(an), k);
    }
    if (sc.evId == 1 || sc.evId == 2) {  // a tap / a bang: it flees, fast
      const float e = clampf((u - sc.evU) / 0.8f, 0, 1);
      x = lerpf(x, x < 160 ? -60 : 380, e * e);
      y = lerpf(y, -50, e);
      if (sitOut) *sitOut = false;
      return;
    }
    const float E = bt(3, 9.5f);
    if (sc.end == 2) {  // it leaves by itself
      const float e = clampf((u - E) / 1.2f, 0, 1);
      x = lerpf(x, sc.mir > 0 ? 380 : -60, e * e);
      y = lerpf(y, -40, e);
    } else {  // lands on the nose, then: 0 snapped at, 1 sneezed away
      const float land = seg(E - 0.7f, E), off = E + (sc.end == 0 ? 0.6f : 1.2f);
      x = lerpf(x, nx, land);
      y = lerpf(y, ny, land);
      if (u > E && u < off) sit = true;
      if (u >= off) {
        const float e = clampf((u - off) / 0.9f, 0, 1);
        x = lerpf(nx, 380, e * e);
        y = lerpf(ny, -40, e) + (sc.end == 1 ? 30 * sinf(e * 14) : 0);
      }
    }
    if (sitOut) *sitOut = sit;
  }
  void beePos(float& x, float& y, bool* restOut = nullptr) const {
    const float u = sc.u;
    x = 160 + 110 * sinf(u * 2.1f + sc.v[0] * 6);
    y = 95 + 45 * sinf(u * 4.2f);
    bool rest = false;
    if (sc.mid == 1) {  // hovers at one eye
      const float k = seg(0.8f, 1.3f) * (1 - seg(bt(0, 3), bt(0, 3) + 0.5f));
      x = lerpf(x, 160 + 55 * sc.mir + 6 * sinf(u * 9), k);
      y = lerpf(y, sc.fy - 10 + 5 * sinf(u * 11), k);
    } else if (sc.mid == 2) {  // zig-zags close
      x = 160 + 70 * sinf(u * 5.5f);
      y = sc.fy + 10 + 30 * sinf(u * 2.2f);
    }
    if (sc.twist == 1) {  // buzzes right up to the face
      const float b0 = bt(1, 2.3f), k = seg(b0, b0 + 0.3f) * (1 - seg(b0 + 0.8f, b0 + 1.1f));
      x = lerpf(x, 160, k);
      y = lerpf(y, sc.fy + 25, k);
    }
    if (sc.evId == 2) {  // a bang: it bolts
      const float e = clampf((u - sc.evU) / 0.5f, 0, 1);
      x = lerpf(x, x < 160 ? -60 : 380, e * e);
      y = lerpf(y, -40, e);
      if (restOut) *restOut = false;
      return;
    }
    const float E = bt(2, 3.8f);
    if (sc.end == 1) {  // lands on top of the head and rests
      const float k = seg(E, E + 0.7f);
      x = lerpf(x, 160 + 30 * sc.mir, k);
      y = lerpf(y, sc.fy - 78, k);
      rest = u > E + 0.7f;
    } else {
      const float e = seg(E, E + 1.0f);
      x = lerpf(x, (sc.end == 0) == (sc.mir > 0) ? 380 : -60, e);
    }
    if (restOut) *restOut = rest;
  }
  int snackBiteN() const { return sc.twist == 1 ? 1 : sc.twist == 2 ? 2 : 3; }  // three bites, one gulp, or two (Crumb takes the last)
  float snackBiteT(int b) const { return sc.twist == 1 ? bt(1, 3.6f) : bt(b + 1, 3.6f + b * 2.0f); }
  float snackEndT() const { return snackBiteT(snackBiteN() - 1) + 1.4f; }
  void snackPos(float& x, float& y) const {
    const float u = sc.u;
    const float e = clampf(u / 1.6f, 0, 1);
    const float back = 1 + 2.4f * (e - 1) * (e - 1) * (e - 1) + 1.4f * (e - 1) * (e - 1);  // ease out with a little overshoot
    x = lerpf(250, 160, ease(e));
    y = lerpf(-40, 205, back);
    for (int b = 0; b < snackBiteN(); b++) {
      const float tb = snackBiteT(b);
      y -= (sc.twist == 1 ? 34 : 22) * seg(tb - 0.45f, tb) * (1 - seg(tb + 0.05f, tb + 0.5f));
    }
  }
  // ---- the weather, faint, behind the face (desk mode: ambientOn) ----
  void drawAmbient() {
    if (!ambientOn || weatherNow <= 0 || sc.id == SC_RAIN || sc.id == SC_STARS || sc.id == SC_SNOW) return;
    Canvas& cv = *k.cv;
    const bool night = hourNow >= 19 || (hourNow >= 0 && hourNow < 6);
    switch (weatherNow) {
      case 1:  // clear
        if (night) {
          cv.circleFast(282, 30, 13, fadeTo(C(240, 235, 200), 0.55f));
          cv.circleFast(289, 25, 12, to565(lidCol()));  // (a crescent)
          for (int i = 0; i < 3; i++) {
            const float b = 0.5f + 0.5f * sinf(t * (1.3f + i * 0.4f) + i * 2);
            star4(40 + i * 95, 22 + (i % 2) * 14, 2.5f + b * 1.5f, 0.3f, fadeTo(C(220, 225, 255), 0.25f + 0.35f * b));
          }
        } else {
          cv.circleFast(296, 18, 22, fadeTo(C(255, 210, 90), 0.18f));
          cv.circleFast(296, 18, 13, fadeTo(C(255, 220, 110), 0.35f));
          burst(296, 18, 17, 25, 8, 1.2f, fadeTo(C(255, 210, 90), 0.3f), t * 0.2f);
        }
        break;
      case 2:  // cloudy: two clouds drifting
      case 4:  // a storm: dark ones (and rain below)
        for (int i = 0; i < 2; i++) {
          const float x = fmodf(t * (5 + i * 2) + i * 190, 420) - 50, y = 26 + i * 14;
          cloud(x, y, 15 - i * 3, fadeTo(weatherNow == 4 ? C(110, 110, 125) : C(170, 175, 190), 0.35f));
        }
        if (weatherNow == 2) break;
        // fall through: the storm's rain
      case 3:  // rain: faint streaks
        for (int i = 0; i < (weatherNow == 4 ? 18 : 12); i++) {
          const float x = fracf(i * 0.377f) * 340 - 10, y = fmodf(t * (weatherNow == 4 ? 220 : 160) + i * 41, 270) - 20;
          cv.lineFast(x, y, x - 3, y + 9, 0.6f, fadeTo(C(140, 190, 255), weatherNow == 4 ? 0.5f : 0.4f));
        }
        if (weatherNow == 4 && fmodf(t, 23) < 0.12f) cv.rrect(0, 0, W, H, 0, fadeTo(C(255, 255, 240), 0.22f));  // a rare flash
        break;
      case 5:  // fog: two pale bands drifting
        for (int i = 0; i < 2; i++) cv.ellipse(160 + 60 * sinf(t * 0.1f + i * 2), 70 + i * 110, 230, 18, fadeTo(C(200, 205, 215), 0.12f));
        break;
      case 6:  // snow: slow flakes
        for (int i = 0; i < 14; i++) {
          const float x = fracf(i * 0.618f) * 320 + 10 * sinf(t * 0.8f + i), y = fmodf(t * 22 + i * 37, 260) - 10;
          cv.circleFast(x, y, 1.6f, fadeTo(C(240, 245, 255), 0.5f));
        }
        break;
    }
  }
  // ---- StandBy: a tiny 5x7 font (capitals, digits, a few signs) ----
  static const uint8_t* glyph5(char ch) {
    static const uint8_t F[][5] = {
      {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
      {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
      {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},  // 0-9
      {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
      {0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},
      {0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
      {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
      {0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},
      {0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
      {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},  // A-Z
      {0x00,0x36,0x36,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x00,0x50,0x30,0x00,0x00},
      {0x20,0x10,0x08,0x04,0x02},{0x00,0x06,0x09,0x09,0x06},{0,0,0,0,0}};  // : - . , / degree space
    if (ch >= '0' && ch <= '9') return F[ch - '0'];
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch >= 'A' && ch <= 'Z') return F[10 + ch - 'A'];
    switch (ch) { case ':': return F[36]; case '-': return F[37]; case '.': return F[38]; case ',': return F[39];
                  case '/': return F[40]; case '*': return F[41]; default: return F[42]; }  // ('*' draws a degree sign)
  }
  float textW(const char* s2, float px) const { return strlen(s2) * 6 * px - px; }
  // align: 0 left, 1 centre, 2 right
  void drawText(const char* s2, float x, float y, float px, uint16_t c, int align = 0) {
    if (align) x -= textW(s2, px) * (align == 1 ? 0.5f : 1.0f);
    for (; *s2; s2++, x += 6 * px) {
      const uint8_t* g = glyph5(*s2);
      for (int col = 0; col < 5; col++)
        for (int row = 0; row < 7; row++)
          if (g[col] >> row & 1) k.cv->rrect(x + col * px, y + row * px, px * 0.92f, px * 0.92f, px * 0.3f, c);
    }
  }
  // ---- StandBy pages ----
  RGB sbMain() const { return sbNight ? C(160, 22, 22) : C(240, 234, 222); }
  RGB sbSoft() const { return sbNight ? C(95, 14, 14) : C(140, 136, 130); }
  RGB sbAcc(const RGB& day) const { return sbNight ? C(190, 30, 25) : day; }
  void sbClock(float ox, float oy) {
    const uint16_t c = to565(sbMain());
    const float S = 1.7f, y = 46 + oy;
    for (int i = 0; i < 4; i++) drawDigit(clockDigit(i), 14 + ox + i * 70 + (i >= 2 ? 22 : 0), y, 1, c, S);
    if (fracf(t) < 0.55f) {  // the colon blinks with the seconds
      k.cv->circle(160 + ox, y + 34, 5.5f, c);
      k.cv->circle(160 + ox, y + 72, 5.5f, c);
    }
    char line[24];
    static const char* const WD[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char* const MO[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    if (wdayNow >= 0 && monthNow >= 1) snprintf(line, sizeof(line), "%s %d %s", WD[wdayNow % 7], dayNow, MO[(monthNow - 1) % 12]);
    else strcpy(line, "");
    drawText(line, 160 + ox, 186 + oy, 3, to565(sbSoft()), 1);
  }
  void sbCalendar(float ox, float oy) {
    static const char* const WDL[7] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
    static const char* const MO[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    if (dayNow < 1 || monthNow < 1) { drawText("NO DATE YET", 160 + ox, 110 + oy, 3, to565(sbSoft()), 1); return; }
    const RGB red = sbAcc(C(255, 90, 80));
    drawText(WDL[wdayNow % 7], 22 + ox, 34 + oy, 2.5f, to565(red));
    drawDigit(dayNow / 10, 18 + ox, 68 + oy, 1, to565(sbMain()), 1.45f);
    drawDigit(dayNow % 10, 76 + ox, 68 + oy, 1, to565(sbMain()), 1.45f);
    char my[12];
    snprintf(my, sizeof(my), "%s %d", MO[(monthNow - 1) % 12], yearNow);
    drawText(my, 22 + ox, 176 + oy, 2.5f, to565(sbSoft()));
    sbCalendarGrid(ox, oy);
  }
  void sbCalendarGrid(float ox, float oy) {  // the month: S M T W T F S, today ringed
    if (dayNow < 1 || monthNow < 1) return;
    const RGB red = sbAcc(C(255, 90, 80));
    static const char WL[7] = {'S', 'M', 'T', 'W', 'T', 'F', 'S'};
    const float gx = 166 + ox, gy = 36 + oy, cw = 21, rh = 21;
    for (int i = 0; i < 7; i++) { const char ch[2] = {WL[i], 0}; drawText(ch, gx + i * cw + 3, gy, 1.6f, to565(i == 0 ? red : sbSoft())); }
    static const int DIM[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int dim = DIM[(monthNow - 1) % 12];
    if (monthNow == 2 && yearNow % 4 == 0 && (yearNow % 100 != 0 || yearNow % 400 == 0)) dim = 29;
    const int first = ((wdayNow - (dayNow - 1) % 7) % 7 + 7) % 7;
    for (int d = 1; d <= dim; d++) {
      const int cell = first + d - 1, col = cell % 7, row = cell / 7;
      const float x = gx + col * cw, y = gy + 22 + row * rh;
      char n[3];
      snprintf(n, sizeof(n), "%d", d);
      if (d == dayNow) k.cv->circle(x + 9, y + 5, 10, to565(red));
      drawText(n, x + 9, y + 1, 1.3f, to565(d == dayNow ? (sbNight ? C(20, 0, 0) : C(255, 255, 255)) : sbMain()), 1);
    }
  }
  void sbWeather(float ox, float oy) {
    const float ix = 86 + ox, iy = 104 + oy;
    const bool night = sbNight || hourNow >= 19 || (hourNow >= 0 && hourNow < 6);
    const RGB sun = sbAcc(C(255, 200, 60)), cl = sbNight ? C(110, 18, 18) : C(200, 205, 220), dropC = sbAcc(C(120, 180, 255));
    switch (weatherNow) {
      case 1:
        if (night) { k.cv->circle(ix, iy, 34, to565(sbAcc(C(240, 232, 190)))); k.cv->circle(ix + 15, iy - 10, 30, cc(0, 0, 0)); }
        else { burst(ix, iy, 42, 56, 10, 3, to565(sun), t * 0.15f); k.cv->circle(ix, iy, 32, to565(sun)); }
        break;
      case 2: cloud(ix - 12, iy - 14, 22, to565(mulc(cl, 0.8f))); cloud(ix + 8, iy + 4, 28, to565(cl)); break;
      case 3: case 4:
        cloud(ix, iy - 14, 28, to565(weatherNow == 4 ? mulc(cl, 0.6f) : cl));
        for (int i = 0; i < 5; i++) {
          const float x = ix - 34 + i * 17, y = iy + 22 + fmodf(t * 50 + i * 13, 34);
          k.cv->lineFast(x, y, x - 4, y + 10, 1.4f, to565(dropC));
        }
        if (weatherNow == 4) { k.cv->tri(ix + 4, iy + 6, ix - 12, iy + 40, ix, iy + 34, to565(sun)); k.cv->tri(ix, iy + 30, ix + 14, iy + 30, ix - 6, iy + 62, to565(sun)); }
        break;
      case 5: for (int i = 0; i < 3; i++) k.cv->rrect(ix - 46 + i * 8, iy - 24 + i * 22, 80, 10, 5, to565(cl)); break;
      case 6:
        cloud(ix, iy - 14, 28, to565(cl));
        for (int i = 0; i < 6; i++) k.cv->circle(ix - 36 + i * 14, iy + 26 + fmodf(t * 18 + i * 9, 34), 3, to565(sbMain()));
        break;
      default: drawText("?", ix, iy - 20, 6, to565(sbSoft()), 1); break;
    }
    // the temperature, big
    const uint16_t c = to565(sbMain());
    if (tempNow > -90) {
      const int tv = (int)lroundf(tempNow), a = tv < 0 ? -tv : tv;
      if (tv < 0) k.cv->rrect(170 + ox, 92 + oy, 16, 7, 3.5f, c);
      if (a >= 10) drawDigit(a / 10, 190 + ox, 52 + oy, 1, c, 1.25f);
      drawDigit(a % 10, 240 + ox, 52 + oy, 1, c, 1.25f);
      ring(296 + ox, 60 + oy, 7, 3, c);
    }
    static const char* const WN[7] = {"", "CLEAR", "CLOUDY", "RAIN", "STORM", "FOG", "SNOW"};
    drawText(weatherNow > 0 && weatherNow < 7 ? WN[weatherNow] : "NO WEATHER", 236 + ox, 148 + oy, 3, to565(sbSoft()), 1);
    if (cityNow[0]) drawText(cityNow, 160 + ox, 200 + oy, 2, to565(sbSoft()), 1);
  }
  void sbAnalog(float ox, float oy) {  // a round clock face: ticks, hour and minute hands, a ticking seconds dot
    const float cx = 160 + ox, cy = 120 + oy, R = 100;
    const RGB m = sbMain(), soft = sbSoft(), acc = sbAcc(C(255, 120, 60));
    ring(cx, cy, R, 3, to565(soft));
    for (int i = 0; i < 12; i++) {
      const float a = i * PI_F / 6, l = i % 3 == 0 ? 16 : 8;
      k.cv->lineFast(cx + sinf(a) * (R - 8), cy - cosf(a) * (R - 8), cx + sinf(a) * (R - 8 - l), cy - cosf(a) * (R - 8 - l), i % 3 == 0 ? 2.6f : 1.4f, to565(i % 3 == 0 ? m : soft));
    }
    if (hourNow >= 0 && minuteNow >= 0) {
      const float mi = minuteNow, ha = ((hourNow % 12) + mi / 60) * PI_F / 6, ma = mi * PI_F / 30;
      k.cv->line(cx, cy, cx + sinf(ha) * R * 0.5f, cy - cosf(ha) * R * 0.5f, 5, to565(m));
      k.cv->line(cx, cy, cx + sinf(ma) * R * 0.78f, cy - cosf(ma) * R * 0.78f, 3.5f, to565(m));
    }
    const float sa = fmodf(t, 60) * PI_F / 30;  // (the seconds: a dot going round)
    k.cv->circle(cx + sinf(sa) * (R - 22), cy - cosf(sa) * (R - 22), 3.5f, to565(acc));
    k.cv->circle(cx, cy, 6, to565(acc));
  }
  int flipLast = -1;
  float flipT = -9;
  void sbFlip(float ox, float oy) {  // two flip cards: HH and MM, the minute card flips when it changes
    const int mm = minuteNow;
    if (mm != flipLast) { if (flipLast >= 0) flipT = t; flipLast = mm; }
    const float f = clampf((t - flipT) / 0.6f, 0, 1);
    const RGB card = sbNight ? C(40, 6, 6) : C(38, 38, 44), m = sbMain();
    for (int c = 0; c < 2; c++) {
      const float x = 14 + ox + c * 152, y = 34 + oy, w = 140, h = 150;
      k.cv->rrect(x, y, w, h, 16, to565(card));
      const float sq = c == 1 && f < 1 ? fabsf(cosf(f * PI_F)) : 1;  // the flip
      drawDigit(clockDigit(c * 2), x + 14, y + 30, fmaxf(0.08f, sq), to565(m), 1.45f);
      drawDigit(clockDigit(c * 2 + 1), x + 74, y + 30, fmaxf(0.08f, sq), to565(m), 1.45f);
      k.cv->rrect(x, y + h / 2 - 1.5f, w, 3, 0, cc(0, 0, 0));  // the split between the halves
    }
    static const char* const WD[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    if (wdayNow >= 0) { char l[12]; snprintf(l, sizeof(l), "%s %d", WD[wdayNow % 7], dayNow); drawText(l, 160 + ox, 204 + oy, 2.5f, to565(sbSoft()), 1); }
  }
  void sbSplit(float ox, float oy) {  // the time on the left, the month on the right
    const uint16_t c = to565(sbMain());
    for (int i = 0; i < 4; i++) drawDigit(clockDigit(i), 10 + ox + i * 36 + (i >= 2 ? 10 : 0), 60 + oy, 1, c, 0.85f);
    if (fracf(t) < 0.55f) { k.cv->circle(80 + ox, 80 + oy, 3, c); k.cv->circle(80 + ox, 100 + oy, 3, c); }
    static const char* const WDL[7] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
    if (wdayNow >= 0) drawText(WDL[wdayNow % 7], 14 + ox, 140 + oy, 2.2f, to565(sbAcc(C(255, 90, 80))));
    if (tempNow > -90) { char l[12]; snprintf(l, sizeof(l), "%d*", (int)lroundf(tempNow)); drawText(l, 14 + ox, 170 + oy, 2.2f, to565(sbSoft())); }
    sbCalendarGrid(ox, oy);
  }
  // ---- StandBy: more styles ----
  RGB hueRGB(float h) const {  // h 0..1 -> a bright colour
    h = fracf(h) * 6;
    const int i = (int)h;
    const float f = h - i;
    float r = 0, g = 0, b = 0;
    switch (i) { case 0: r = 1; g = f; break; case 1: r = 1 - f; g = 1; break; case 2: g = 1; b = f; break;
                 case 3: g = 1 - f; b = 1; break; case 4: r = f; b = 1; break; default: r = 1; b = 1 - f; break; }
    return C((uint8_t)(70 + 185 * r), (uint8_t)(70 + 185 * g), (uint8_t)(70 + 185 * b));
  }
  void sbTimeText(char* out, int n, int addMin = 0) const {
    if (hourNow < 0 || minuteNow < 0) { snprintf(out, n, "--:--"); return; }
    int m = ((hourNow * 60 + minuteNow + addMin) % 1440 + 1440) % 1440;
    snprintf(out, n, "%02d:%02d", m / 60, m % 60);
  }
  void sbDateLine(char* out, int n) const {
    static const char* const WD[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char* const MO[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    if (wdayNow >= 0 && monthNow >= 1) snprintf(out, n, "%s %d %s", WD[wdayNow % 7], dayNow, MO[(monthNow - 1) % 12]);
    else out[0] = 0;
  }
  void sbMinimal(float ox, float oy) {  // a small time drifting slowly about the screen
    char tt[8], d[20];
    sbTimeText(tt, sizeof(tt));
    sbDateLine(d, sizeof(d));
    const float x = 160 + 90 * sinf(t / 37), y = 110 + 60 * sinf(t / 53);
    drawText(tt, x + ox, y + oy, 4, to565(sbMain()), 1);
    drawText(d, x + ox, y + 38 + oy, 2, to565(sbSoft()), 1);
  }
  void sbWords(float ox, float oy) {  // IT IS TEN PAST TWO
    static const char* const HN[12] = {"TWELVE", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE", "TEN", "ELEVEN"};
    static const char* const MP[12] = {"OCLOCK", "FIVE PAST", "TEN PAST", "QUARTER PAST", "TWENTY PAST", "TWENTY FIVE PAST", "HALF PAST",
                                       "TWENTY FIVE TO", "TWENTY TO", "QUARTER TO", "TEN TO", "FIVE TO"};
    if (hourNow < 0) { drawText("NO TIME YET", 160 + ox, 110 + oy, 3, to565(sbSoft()), 1); return; }
    const int f = ((minuteNow + 2) / 5) % 12, h = (hourNow + (((minuteNow + 2) / 5) > 6 ? 1 : 0) + (minuteNow >= 58 ? 0 : 0)) % 12;
    const RGB acc = sbAcc(C(255, 170, 70));
    drawText("IT IS", 160 + ox, 36 + oy, 3, to565(sbSoft()), 1);
    if (f == 0) {
      drawText(HN[h], 160 + ox, 84 + oy, 6, to565(acc), 1);
      drawText("OCLOCK", 160 + ox, 150 + oy, 4, to565(sbMain()), 1);
    } else {
      drawText(MP[f], 160 + ox, 84 + oy, textW(MP[f], 4) > 300 ? 3 : 4, to565(sbMain()), 1);
      drawText(HN[h], 160 + ox, 140 + oy, 6, to565(acc), 1);
    }
  }
  void sbBinary(float ox, float oy) {  // H H M M as columns of lit dots (BCD)
    static const int BITS[4] = {2, 4, 3, 4};
    for (int c = 0; c < 4; c++) {
      const int d = clockDigit(c);
      const float x = 70 + ox + c * 56 + (c >= 2 ? 12 : 0);
      for (int b = 0; b < 4; b++) {
        if (b >= BITS[c]) continue;
        const bool on = d >= 0 && (d >> b & 1);
        k.cv->circle(x, 170 + oy - b * 40, 14, to565(on ? sbAcc(hueRGB(c * 0.2f + 0.05f)) : (sbNight ? C(30, 5, 5) : C(40, 40, 46))));
      }
      char n[2] = {(char)('0' + (d < 0 ? 0 : d)), 0};
      drawText(n, x, 200 + oy, 2.5f, to565(sbSoft()), 1);
    }
  }
  void arcDots(float cx, float cy, float r, float frac, float th, uint16_t on, uint16_t off) {
    for (int i = 0; i < 90; i++) {
      const float a = i * PI_F * 2 / 90 - PI_F / 2;
      k.cv->circleFast(cx + cosf(a) * r, cy + sinf(a) * r, th, i < frac * 90 ? on : off);
    }
  }
  void sbRings(float ox, float oy) {  // the hour, the minute, the second: three rings filling up
    const float cx = 160 + ox, cy = 120 + oy;
    const uint16_t off = to565(sbNight ? C(28, 4, 4) : C(36, 36, 42));
    arcDots(cx, cy, 100, hourNow < 0 ? 0 : (hourNow + minuteNow / 60.0f) / 24, 6, to565(sbAcc(C(255, 60, 110))), off);
    arcDots(cx, cy, 80, minuteNow < 0 ? 0 : minuteNow / 60.0f, 6, to565(sbAcc(C(150, 255, 60))), off);
    arcDots(cx, cy, 60, fmodf(t, 60) / 60, 6, to565(sbAcc(C(60, 220, 255))), off);
    char tt[8];
    sbTimeText(tt, sizeof(tt));
    drawText(tt, cx, cy - 10, 3.5f, to565(sbMain()), 1);
  }
  void sbDayBar(float ox, float oy) {  // the time, and how much of the day has gone
    const uint16_t c = to565(sbMain());
    for (int i = 0; i < 4; i++) drawDigit(clockDigit(i), 40 + ox + i * 56 + (i >= 2 ? 16 : 0), 30 + oy, 1, c, 1.25f);
    if (fracf(t) < 0.55f) { k.cv->circle(160 + ox, 58 + oy, 4, c); k.cv->circle(160 + ox, 88 + oy, 4, c); }
    const float f = hourNow < 0 ? 0 : (hourNow * 60 + minuteNow) / 1440.0f;
    k.cv->rrect(30 + ox, 150 + oy, 260, 16, 8, to565(sbNight ? C(28, 4, 4) : C(40, 40, 46)));
    k.cv->rrect(30 + ox, 150 + oy, fmaxf(16, 260 * f), 16, 8, to565(sbAcc(hueRGB(0.08f + f * 0.6f))));
    char l[24];
    snprintf(l, sizeof(l), "%d PERCENT OF THE DAY", (int)(f * 100));
    drawText(l, 160 + ox, 182 + oy, 2.2f, to565(sbSoft()), 1);
  }
  void sbBigDate(float ox, float oy) {
    static const char* const WDL[7] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
    static const char* const MO[12] = {"JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};
    if (dayNow < 1) { drawText("NO DATE YET", 160 + ox, 110 + oy, 3, to565(sbSoft()), 1); return; }
    drawText(WDL[wdayNow % 7], 160 + ox, 22 + oy, 3, to565(sbAcc(C(255, 90, 80))), 1);
    drawDigit(dayNow / 10, 84 + ox, 50 + oy, 1, to565(sbMain()), 1.85f);
    drawDigit(dayNow % 10, 166 + ox, 50 + oy, 1, to565(sbMain()), 1.85f);
    drawText(MO[(monthNow - 1) % 12], 160 + ox, 186 + oy, 3, to565(sbSoft()), 1);
  }
  void sbNightSky(float ox, float oy) {  // stars, the moon, now and then a shooting star; the time small
    for (int i = 0; i < 40; i++) {
      const float x = fracf(i * 0.6180339f) * 320, y = fracf(i * 0.4142135f) * 200;
      const float b = 0.5f + 0.5f * sinf(t * (0.6f + fracf(i * 0.37f)) + i);
      k.cv->circleFast(x + ox * 0.3f, y + oy * 0.3f, 0.8f + 0.9f * b * fracf(i * 0.73f), fadeTo(sbNight ? C(200, 40, 40) : C(230, 235, 255), 0.25f + 0.6f * b));
    }
    k.cv->circle(250 + ox, 60 + oy, 24, to565(sbAcc(C(245, 238, 205))));
    k.cv->circle(262 + ox, 52 + oy, 22, cc(0, 0, 0));
    const float sp = fmodf(t, 19);
    if (sp < 0.9f) {
      const float e = sp / 0.9f, x = 40 + 200 * e, y = 30 + 70 * e;
      k.cv->lineFast(x, y, x - 30, y - 10, 1.2f, fadeTo(C(255, 255, 255), 1 - e));
    }
    char tt[8];
    sbTimeText(tt, sizeof(tt));
    drawText(tt, 160 + ox, 196 + oy, 3.5f, to565(sbMain()), 1);
  }
  void sbNeon(float ox, float oy) {  // glowing digits
    for (int i = 0; i < 4; i++) {
      const RGB c = sbNight ? C(200, 30, 30) : (i < 2 ? C(255, 60, 200) : C(60, 230, 255));
      const float x = 22 + ox + i * 70 + (i >= 2 ? 16 : 0), y = 56 + oy;
      for (int g = 3; g >= 1; g--)
        for (int q = 0; q < 4; q++) drawDigit(clockDigit(i), x + (q == 0 ? -g : q == 1 ? g : 0), y + (q == 2 ? -g : q == 3 ? g : 0), 1, fadeTo(c, 0.12f), 1.55f);
      drawDigit(clockDigit(i), x, y, 1, to565(mixc(c, C(255, 255, 255), 0.45f)), 1.55f);
    }
    if (fracf(t) < 0.55f) { k.cv->circle(160 + ox, 86 + oy, 5, to565(sbMain())); k.cv->circle(160 + ox, 120 + oy, 5, to565(sbMain())); }
  }
  void sbPixel(float ox, float oy) {
    char tt[8], d[20];
    sbTimeText(tt, sizeof(tt));
    sbDateLine(d, sizeof(d));
    drawText(tt, 160 + ox, 66 + oy, 10, to565(sbAcc(C(120, 255, 140))), 1);
    drawText(d, 160 + ox, 172 + oy, 3, to565(sbSoft()), 1);
  }
  void sbWorld(float ox, float oy) {  // here, and three fixed zones (the robot's clock: its timezone setting)
    static const char* const NM[4] = {"HERE", "UTC", "DUBAI", "TOKYO"};
    const int here = ::tzOffsetSec / 60;  // (main.cpp: the clock's timezone)
    const int OFF[4] = {0, 0 - here, 240 - here, 540 - here};  // minutes from local time (UTC, UTC+4, UTC+9)
    for (int i = 0; i < 4; i++) {
      char tt[8];
      sbTimeText(tt, sizeof(tt), OFF[i]);
      const float y = 30 + oy + i * 48;
      drawText(i == 0 && cityNow[0] ? cityNow : NM[i], 24 + ox, y + 6, 2.2f, to565(i == 0 ? sbAcc(C(255, 170, 70)) : sbSoft()));
      drawText(tt, 296 + ox, y, 4, to565(sbMain()), 2);
    }
  }
  void sbCrumbClock(float ox, float oy) {  // the big clock, Crumb hopping from digit to digit
    const uint16_t c = to565(sbMain());
    for (int i = 0; i < 4; i++) drawDigit(clockDigit(i), 22 + ox + i * 70 + (i >= 2 ? 16 : 0), 80 + oy, 1, c, 1.5f);
    if (fracf(t) < 0.55f) { k.cv->circle(160 + ox, 110 + oy, 5, c); k.cv->circle(160 + ox, 146 + oy, 5, c); }
    const float hp = fmodf(t / 1.6f, 4), seg2 = floorf(hp), e = hp - seg2;
    const int a = (int)seg2, b = (a + 1) % 4;
    auto dx = [&](int i) { return 22 + ox + i * 70 + (i >= 2 ? 16 : 0) + 27; };
    const float x = b == 0 ? dx(a) + (dx(a) - dx(a)) : lerpf(dx(a), dx(b), ease(e)), y = 66 + oy - 34 * sinf(e * PI_F) * (b == 0 ? 0 : 1);
    drawCrumb(b == 0 ? dx(a) : x, y, e < 0.9f && b != 0 ? fabsf(sinf(t * 18)) : 0, 1, false);
  }
  void sbRainbow(float ox, float oy) {
    for (int i = 0; i < 4; i++)
      drawDigit(clockDigit(i), 14 + ox + i * 70 + (i >= 2 ? 22 : 0), 56 + oy, 1, to565(sbAcc(hueRGB(t * 0.03f + i * 0.12f))), 1.7f);
    if (fracf(t) < 0.55f) { k.cv->circle(160 + ox, 90 + oy, 5.5f, to565(sbMain())); k.cv->circle(160 + ox, 128 + oy, 5.5f, to565(sbMain())); }
    char d[20];
    sbDateLine(d, sizeof(d));
    drawText(d, 160 + ox, 196 + oy, 3, to565(sbSoft()), 1);
  }
  void sbEyes(float ox, float oy) {  // Oreo's eyes tell the time: the left looks at the hour, the right at the minute
    const float blink = fmodf(t, 6.5f) < 0.16f ? 0.15f : 1;
    const float ha = hourNow < 0 ? 0 : ((hourNow % 12) + minuteNow / 60.0f) * PI_F / 6, ma = minuteNow < 0 ? 0 : minuteNow * PI_F / 30;
    const float ang[2] = {ha, ma};
    for (int e = 0; e < 2; e++) {
      const float cx = 92 + e * 136 + ox, cy = 112 + oy;
      k.cv->ellipse(cx, cy, 56, 56 * blink, to565(sbNight ? C(120, 18, 18) : C(240, 240, 245)));
      if (blink > 0.5f) {
        const float px = cx + sinf(ang[e]) * 30, py = cy - cosf(ang[e]) * 30;
        k.cv->circle(px, py, 21, to565(sbNight ? C(30, 2, 2) : C(25, 30, 60)));
        k.cv->circleFast(px - 7, py - 7, 6, to565(sbNight ? C(170, 40, 40) : C(255, 255, 255)));
      }
    }
    char tt[8];
    sbTimeText(tt, sizeof(tt));
    drawText(tt, 160 + ox, 196 + oy, 3, to565(sbSoft()), 1);
  }
  void sbOreo(float ox, float oy) {  // a little Oreo dozing, and the time
    const RGB c = sbMain();
    const float cx = 110 + ox, cy = 104 + oy, br = 1 + 0.04f * sinf(t * 1.6f);
    for (int e = -1; e <= 1; e += 2)  // closed eyes: little arcs
      for (int q = 0; q < 6; q++) {
        const float a0 = 0.35f + q * 0.4f, a1 = a0 + 0.4f;
        k.cv->lineFast(cx + e * 34 + 18 * cosf(a0), cy + 8 * sinf(a0) * br, cx + e * 34 + 18 * cosf(a1), cy + 8 * sinf(a1) * br, 2.6f, to565(c));
      }
    k.cv->ellipse(cx, cy + 34, 6, 3 * br, to565(sbSoft()));
    for (int z = 0; z < 3; z++) {  // Z's drifting up
      const float zz = fracf(t * 0.18f + z / 3.0f);
      const char zs[2] = {'Z', 0};
      drawText(zs, cx + 60 + 34 * zz, cy - 26 - 60 * zz, 2 + 2 * (1 - zz), fadeTo(sbMain(), 1 - zz), 1);
    }
    const uint16_t tc = to565(sbSoft());
    for (int i = 0; i < 4; i++) drawDigit(clockDigit(i), 196 + ox + i * 26 + (i >= 2 ? 10 : 0), 168 + oy, 1, tc, 0.62f);
    k.cv->circle(249 + ox, 180 + oy, 2.2f, tc);
    k.cv->circle(249 + ox, 196 + oy, 2.2f, tc);
  }
 public:
  // the whole StandBy screen: pages turn every 45 s with a slide; a slow drift (no pixel sits still)
  void drawStandby(Canvas& out) {
    k.cv = &out;
    const bool autoNight = sbNight;  // (desk mode: night or a dark room)
    sbNight = sbNightMode == 2 || (sbNightMode == 0 && autoNight);
    out.clear(0);
    const float ox = 3 * sinf(t / 50), oy = 2 * cosf(t / 70);
    const float per = 45, ph = fmodf(t, per), slide = ph > per - 0.7f ? ease((ph - (per - 0.7f)) / 0.7f) : 0;
    if (sbStyle != 0) {  // one fixed style
      switch (sbStyle) {
        case 1: sbClock(ox, oy); break;
        case 2: sbAnalog(ox, oy); break;
        case 3: sbFlip(ox, oy); break;
        case 4: sbSplit(ox, oy); break;
        case 5: sbWeather(ox, oy); break;
        case 6: sbOreo(ox, oy); break;
        case 7: sbMinimal(ox, oy); break;
        case 8: sbWords(ox, oy); break;
        case 9: sbBinary(ox, oy); break;
        case 10: sbRings(ox, oy); break;
        case 11: sbDayBar(ox, oy); break;
        case 12: sbBigDate(ox, oy); break;
        case 13: sbNightSky(ox, oy); break;
        case 14: sbNeon(ox, oy); break;
        case 15: sbPixel(ox, oy); break;
        case 16: sbWorld(ox, oy); break;
        case 17: sbCrumbClock(ox, oy); break;
        case 18: sbRainbow(ox, oy); break;
        default: sbEyes(ox, oy); break;
      }
      sbNight = autoNight;
      return;
    }
    const int pg = (int)(t / per) % 4;
    auto page = [&](int i, float dx) {
      switch (i) {
        case 0: sbClock(ox + dx, oy); break;
        case 1: sbCalendar(ox + dx, oy); break;
        case 2: sbWeather(ox + dx, oy); break;
        default: sbOreo(ox + dx, oy); break;
      }
    };
    page(pg, -W * slide);
    if (slide > 0) page((pg + 1) % 4, W * (1 - slide));
    sbNight = autoNight;
  }
 private:
  // ---- Crumb, the little cookie-crumb bird ----
  void drawCrumb(float x, float y, float flap, int face, bool sleep, float z = 1) {
    Canvas& cv = *k.cv;
    const RGB body = C(190, 132, 78), belly = C(232, 190, 140), dark = C(110, 70, 40);
    const float r = 11 * z;
    // wings: up / down with flap (0 folded .. 1 up)
    cv.ellipse(x - 7 * z * face, y - 2 * z - flap * 8 * z, 8 * z, (3 + 4 * flap) * z, to565(mulc(body, 0.85f)));
    cv.circle(x, y, r, to565(body));
    cv.ellipse(x + 2 * z * face, y + 4 * z, 6 * z, 5 * z, to565(belly));
    cv.circleFast(x - 5 * z, y - 4 * z, 1.4f * z, to565(dark));  // specks: it's a cookie crumb
    cv.circleFast(x + 1 * z, y - 7 * z, 1.1f * z, to565(dark));
    cv.circleFast(x - 7 * z, y + 3 * z, 1.2f * z, to565(dark));
    const float ex = x + 5 * z * face, ey = y - 3 * z;
    if (sleep) cv.lineFast(ex - 2 * z, ey, ex + 2 * z, ey, 0.7f * z, to565(dark));
    else cv.circleFast(ex, ey, 1.7f * z, cc(30, 25, 25));
    cv.tri(x + 10 * z * face, y - 2 * z, x + 16 * z * face, y, x + 10 * z * face, y + 2 * z, cc(255, 160, 50));  // beak
    if (flap < 0.05f) {  // perched: little feet
      cv.lineFast(x - 3 * z, y + r - 1, x - 4 * z, y + r + 4 * z, 0.6f, cc(230, 140, 60));
      cv.lineFast(x + 3 * z, y + r - 1, x + 4 * z, y + r + 4 * z, 0.6f, cc(230, 140, 60));
    }
  }
  // Crumb this take. state: 0 flying, 1 hopping / perched, 2 asleep. face: +1 right, -1 left. false = off screen
  bool crumbPos(float& x, float& y, float& flap, int& face, int& state) const {
    const float u = sc.u;
    const bool cautious = crumbFear > 0;
    const float in0 = cautious ? 2.2f : 0;  // a scared bird peeks in from the edge first
    const float sx = sc.mir > 0 ? -20 : 340;
    face = sc.mir > 0 ? 1 : -1;
    state = 0;
    flap = fabsf(sinf(t * 18));
    float tx = 160 + 40 * sc.mir, ty = 225;  // the hop spot on the "floor"
    if (sc.mid == 2) { tx = 160; ty = sc.fy - 80; }
    if (cautious && u < in0) {  // only its head at the edge, bobbing
      x = sc.mir > 0 ? 8 : 312;
      y = 130 + 4 * sinf(u * 6);
      flap = 0;
      state = 1;
      return true;
    }
    const float k2 = seg(in0, in0 + 1.6f);
    x = lerpf(sx, tx, k2);
    y = lerpf(60, ty, k2) - 50 * sinf(k2 * 3.14f);
    if (k2 >= 1) {
      flap = 0;
      state = 1;
      if (sc.mid == 0) {  // hops about
        const float hu = u - in0 - 1.6f, hop = fabsf(sinf(hu * 3.2f));
        x = tx + 50 * sinf(hu * 0.8f);
        y = ty - 16 * hop;
        face = cosf(hu * 0.8f) > 0 ? 1 : -1;
      } else if (sc.mid == 1) {  // steals a crumb from the mouth, back to its spot
        const float s0 = bt(0, 4.5f), a = seg(s0, s0 + 0.8f), b = seg(s0 + 1.4f, s0 + 2.2f);
        x = lerpf(lerpf(tx, 160 + 18 * sc.mir, a), tx, b);
        y = lerpf(lerpf(ty, sc.fy + 66, a), ty, b);
        if (a > 0 && b < 1) { flap = fabsf(sinf(t * 18)); state = 0; }
      } else if (u > bt(0, 5)) state = 2;  // naps on the head
    }
    if (sc.evId == 3) {  // a pat: it nuzzles the cheek
      const float e = seg(sc.evU, sc.evU + 0.8f);
      x = lerpf(x, 160 + 64 * sc.mir, e);
      y = lerpf(y, sc.fy + 22, e);
      state = e >= 1 ? 1 : 0;
      face = -sc.mir;
      return true;
    }
    if (sc.evId == 1) {  // a tap: off it goes, fast
      const float e = clampf((u - sc.evU) / 0.6f, 0, 1);
      x = lerpf(x, sc.mir > 0 ? -40 : 360, e * e);
      y = lerpf(y, 20, e);
      flap = fabsf(sinf(t * 26));
      face = sc.mir > 0 ? -1 : 1;
      return e < 1;
    }
    const float E = bt(3, 9);
    if (sc.end == 0 && u > E + 1.5f) {  // flies off (a wing waved first)
      const float e = clampf((u - E - 1.5f) / 1.2f, 0, 1);
      x = lerpf(x, sc.mir > 0 ? 360 : -40, e);
      y = lerpf(y, 30, e) - 30 * sinf(e * 3.14f);
      flap = fabsf(sinf(t * 18));
      state = 0;
      return e < 1;
    }
    if (sc.end == 0 && u > E && u < E + 1.5f) flap = 0.5f + 0.5f * sinf(t * 10);  // waves bye
    if (sc.end == 2 && u > E) {  // stays and dozes off (on the head if it wasn't there)
      const float e = seg(E, E + 1.2f);
      if (sc.mid != 2) { x = lerpf(x, 160, e); y = lerpf(y, sc.fy - 80, e); }
      if (e >= 1) state = 2;
    }
    return true;
  }
  // ---- the clock ----
  // digit i of the time (0..3: H H M M), -1 when unknown; plusMin: the minute after
  int clockDigit(int i, int plusMin = 0) const {
    if (hourNow < 0 || minuteNow < 0) return -1;
    int h = hourNow, m = minuteNow + plusMin;
    if (m >= 60) { m -= 60; h = (h + 1) % 24; }
    const int v[4] = {h / 10, h % 10, m / 10, m % 10};
    return v[i & 3];
  }
  float clockX(int i) const { return 70 + i * 46 + (i >= 2 ? 14 : 0); }  // a digit's left edge
  // which digit to play with: the first one matching want (e.g. 7 or 0), else fallback
  int clockPick(int want, int fallback) const {
    for (int i = 0; i < 4; i++) if (clockDigit(i) == want) return i;
    return fallback;
  }
  void drawDigit(int d, float x, float y, float sq, uint16_t c, float S = 1) {  // rounded 7-segment, 36 x 62 (x S)
    const float w = 36 * S, H = 62 * S, th = 7 * S, h = H * sq, yo = y + H * (1 - sq), i4 = 4 * S, i3 = 3 * S;
    if (d < 0) { k.cv->rrect(x + 6 * S, y + 28 * S, 24 * S, th, th / 2, c); return; }
    static const uint8_t SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};  // gfedcba
    const uint8_t m = SEG[d % 10];
    if (m & 1) k.cv->rrect(x + i4, yo, w - 2 * i4, th, th / 2, c);                   // a
    if (m & 2) k.cv->rrect(x + w - th, yo + i3, th, h / 2 - i3, th / 2, c);         // b
    if (m & 4) k.cv->rrect(x + w - th, yo + h / 2, th, h / 2 - i3, th / 2, c);      // c
    if (m & 8) k.cv->rrect(x + i4, yo + h - th, w - 2 * i4, th, th / 2, c);         // d
    if (m & 16) k.cv->rrect(x, yo + h / 2, th, h / 2 - i3, th / 2, c);              // e
    if (m & 32) k.cv->rrect(x, yo + i3, th, h / 2 - i3, th / 2, c);                 // f
    if (m & 64) k.cv->rrect(x + i4, yo + h / 2 - th / 2, w - 2 * i4, th, th / 2, c); // g
  }
  // ---- the desk scenes ----
  // the laser dot: where it is (k: 0 the dot, 1/2 its copies when it multiplies); false when gone
  bool dotPos(float& x, float& y, int k = 0) const {
    const float u = sc.u, a = sc.v[0] * 6.28f;
    x = 160 + 130 * sinf(u * 1.3f + a) * cosf(u * 0.37f + k);
    y = 175 + 45 * sinf(u * 2.1f + a + k * 2);
    if (fmodf(u, 3.1f) < 0.6f) { x = 160 + 130 * sinf(floorf(u / 3.1f) * 3.1f * 1.3f + a); }  // pauses (to be pounced on)
    const float E = bt(2, 10);
    if (sc.end == 0 && u > E) { const float k2 = seg(E, E + 0.6f); x = lerpf(x, 160, k2); y = lerpf(y, sc.fy + 62, k2); return u < E + 0.9f; }
    if (sc.end == 1 && u > E + 0.3f) return false;
    if (k > 0) {
      if (sc.end != 2 || u < E) return false;
      const float s2 = seg(E, E + 0.5f);
      x = lerpf(160, x, s2);
      y = lerpf(175, y, s2);
    }
    return u > 0.4f;
  }
  // the paper glider: position and heading
  void gliderPos(float& x, float& y, float& ang) const {
    const float u = sc.u;
    x = -40 + u * 70;
    y = 60 + 15 * sinf(u * 1.2f);
    if (sc.mid == 0) {  // a loop-the-loop
      const float l0 = bt(0, 2.4f), e = clampf((u - l0) / 1.6f, 0, 1);
      if (e > 0 && e < 1) { x = -40 + l0 * 70 + 45 * sinf(e * 6.28f); y = 60 - 45 * (1 - cosf(e * 6.28f)) * 0.5f - 0 * e; }
      if (e >= 1) x = -40 + (u - 1.6f) * 70;
    } else if (sc.mid == 2) {  // stalls: slows, nose up, dips
      const float s0 = bt(0, 2.6f), k = seg(s0, s0 + 0.8f) * (1 - seg(s0 + 1.4f, s0 + 2.0f));
      x -= 40 * k;
      y += 30 * k * sinf((u - s0) * 3);
    }
    const float E = bt(2, 4.6f);
    if (u > E) {
      if (sc.end == 0) { const float k2 = seg(E, E + 0.7f); x = lerpf(x, 160, k2); y = lerpf(y, sc.fy - 62, k2); }      // bonk
      else if (sc.end == 1) { const float k2 = seg(E, E + 1.6f); x = lerpf(x, 230, k2); y = lerpf(y, 226, k2); }   // a soft landing
    }
    const float dx = 70, dy = 15 * 1.2f * cosf(u * 1.2f);
    ang = atan2f(dy, dx);
    if (sc.end == 1 && u > E + 1.6f) ang = 0;
  }
  // the ant walking over the face
  bool antPos(float& x, float& y) const {
    const float u = sc.u;
    x = lerpf(-20, 340, u / 11.0f);
    y = sc.fy + 72 - 30 * sinf(clampf(u / 11.0f, 0, 1) * 3.14f);  // up over the nose and down
    if (sc.mid == 1) y += 12 * sinf(u * 5);  // zig-zag
    if (sc.evId == 1) {  // a tap: it jumps off
      const float e = clampf((u - sc.evU) / 0.7f, 0, 1);
      y -= 120 * e * (1.6f - e);
      x += 60 * e;
      return e < 1;
    }
    const float E = bt(2, 5.5f);
    if (sc.end == 0 && u > E + 1.3f) {  // blown away by the sneeze
      const float e = clampf((u - E - 1.3f) / 0.8f, 0, 1);
      x += 260 * e * e;
      y -= 90 * e;
      return e < 1;
    }
    if (sc.end == 0 && u > E) { x = lerpf(-20, 340, E / 11.0f); y = sc.fy + 72 - 30 * sinf(E / 11.0f * 3.14f); }  // waits on the nose
    return x < 340;
  }
  // How many bubbles this take: a few big ones, many small ones, or one giant
  int bubbleN() const { return sc.mid == 0 ? 4 : sc.mid == 1 ? 7 : 1; }
  // Bubble i: where, how big, and when it pops (1e9: never - it floats away)
  void bubble(int i, float& x, float& y, float& r, float& tp) const {
    const float born = sc.mid == 2 ? 0.6f : 0.3f + i * (sc.mid == 0 ? 2.2f : 1.45f);
    const float f = fracf(sc.v[i % 8] * 7.3f + i * 0.37f);
    float speed = 38 + 22 * f, popY = 35 + 60 * fracf(f * 3.1f);
    r = sc.mid == 0 ? 14 + 6 * fracf(f * 5.7f) : sc.mid == 1 ? 6 + 5 * fracf(f * 5.7f) : 34;
    if (sc.mid == 2) speed = 22;
    x = 50 + 220 * fracf(f * 1.9f + i * 0.29f) + 12 * sinf((sc.u - born) * 2 + i);
    if (sc.mid == 2) x = 160 + 40 * sinf((sc.u - born) * 0.9f);
    y = 255 - (sc.u - born) * speed;
    tp = born + (255 - popY) / speed;
    const float E = bt(2, 8.5f);
    const int n = bubbleN();
    if (sc.end == 0 && i == n - 1) {  // the last one drifts to the nose and pops there
      const float k = seg(E - 1.2f, E);
      x = lerpf(x, 160, k);
      y = lerpf(y, sc.fy + 46, k);
      tp = E + 0.4f;
    } else if (sc.end == 1) {
      tp = 1e9f;  // they all float away, up and off
    } else if (sc.end == 2) {
      tp = fminf(tp, E + i * 0.18f);  // a quick chain: pop pop pop
    }
    if (sc.evId == 1) tp = fminf(tp, sc.evU + 0.05f * (i % 3));  // a tap: all pop at once
  }
  // The butterfly this take. where: 0 flying, 1 on the head, 2 on the nose
  void butterflyPos(float& x, float& y, int* where = nullptr) const {
    const float u = sc.u, a = sc.v[0] * 6.28f;
    const float wx = 160 + 120 * sinf(u * 0.9f + a), wy = 90 + 50 * sinf(u * 1.7f) + 10 * sinf(u * 6);
    const float in = ease(u / 1.5f);
    x = lerpf(-30, wx, in);
    y = lerpf(150, wy, in);
    int w = 0;
    if (sc.mid == 1) {  // a rest on the head mid-way
      const float l0 = bt(0, 5.2f), k = seg(l0, l0 + 1) * (1 - seg(l0 + 3, l0 + 3.8f));
      x = lerpf(x, 160, k);
      y = lerpf(y, 22, k);
      if (k > 0.95f) w = 1;
    } else if (sc.mid == 2) {  // dances in front of the eyes
      const float d0 = bt(0, 3.5f), k = seg(d0, d0 + 0.8f) * (1 - seg(d0 + 4, d0 + 4.8f));
      x = lerpf(x, 160 + 70 * sinf(u * 2.6f), k);
      y = lerpf(y, sc.fy - 5 + 18 * sinf(u * 5.2f), k);
    }
    const bool patted = sc.evId == 3;
    const float E = patted ? sc.evU : bt(3, 9.2f);
    const int end = patted ? 2 : sc.end;
    if (u > E) {
      if (end == 0) {  // flies off
        const float e = clampf((u - E) / 1.4f, 0, 1);
        x = lerpf(x, 360, e) + 20 * sinf(e * 9);
        y = lerpf(y, -40, e) + 12 * sinf(e * 14);
      } else {  // settles: 1 on the head, 2 on the nose
        const float k = seg(E, E + 1.0f);
        x = lerpf(x, 160, k);
        y = lerpf(y, end == 1 ? 22 : sc.fy + 44, k);
        if (k > 0.95f) w = end;
      }
    }
    if (where) *where = w;
  }
  void teaPos(float& y) const {
    const float u = sc.u;
    y = lerpf(270, 202, seg(0, 1.2f)) + 70 * seg(12.6f, 13.6f);
    y -= 16 * seg(3.8f, 4.4f) * (1 - seg(6, 6.6f));
    y -= 16 * seg(8.8f, 9.4f) * (1 - seg(10.8f, 11.4f));
    (void)u;
  }
  void flakePos(float& x, float& y) const {
    const float u = sc.u;
    const float e = clampf((u - 3.5f) / 3.6f, 0, 1);
    x = lerpf(215, 160, ease(e)) + 18 * sinf(u * 2.1f) * (1 - e);
    y = lerpf(-10, sc.fy + 52, e);
  }
  // the painted rainbow: an arch across the bottom of the screen, under the mouth (centre 160, RB_Y;
  // outer radius RB_R; flattened by RB_S)
  static constexpr float RB_Y = 300, RB_R = 140, RB_S = 0.75f;
  void brushPos(float& x, float& y) const {
    const float p = seg(1.5f, 8.5f);
    const float a = PI_F + p * PI_F, r = RB_R - 4;
    const float ax = 160 + cosf(a) * r, ay = RB_Y + sinf(a) * r * RB_S;
    const float in = seg(0, 1.5f), out = seg(8.7f, 9.7f);
    x = lerpf(lerpf(300, ax, in), 360, out);
    y = lerpf(lerpf(260, ay, in), 280, out);
  }
  void featherPos(float& x, float& y, float& a) const {
    const float u = sc.u;
    const float fx = 160 + 70 * sinf(u * 1.6f), fy = -30 + u * 32;
    const float tx = 160 + 16 * sinf(u * 9), ty = sc.fy + 44 + 5 * sinf(u * 13);
    const float b = seg(3.2f, 4.2f);
    x = lerpf(fx, tx, b);
    y = lerpf(fy, ty, b);
    a = lerpf(0.5f * cosf(u * 1.6f), 0.3f * sinf(u * 9), b) + 1.2f;
    if (u > 8.3f) {
      const float e = clampf((u - 8.3f) / 0.8f, 0, 1);
      x = lerpf(tx, -60, e);
      y = lerpf(ty, -60, e * (2 - e));
      a += (u - 8.3f) * 12;
    }
  }
  void balloonPos(float& x, float& y) const {
    const float e = seg(0.4f, 6.8f);
    x = lerpf(70, 175, e) + 18 * sinf(sc.u * 1.3f);
    y = lerpf(300, 42, e);
    if (sc.iv[2] % 2) y -= 140 * seg(6.8f, 10.5f);  // floating away
  }
  void coinPos(float& x, float& y, float& w) const {
    const float u = sc.u;
    x = 262;
    y = 214;
    w = 13;
    if (u > 4.8f) {
      const float e = clampf((u - 4.8f) / 0.9f, 0, 1);
      x = lerpf(262, 160, e);
      y = lerpf(214, 30, e) - 120 * sinf(e * PI_F) * 0.6f;
      w = 13 * fabsf(cosf((u - 4.8f) * 14));
    }
  }
  void planePos(float& x, float& y, float& a) const {
    auto pos = [&](float uu, float& px, float& py) {
      if (uu < 8.3f) {
        const float s = (uu - 0.3f) / 7.2f;
        const float th = 2 * PI_F * clampf((s - 0.36f) / 0.3f, 0, 1), R = 42;
        px = -40 + 400 * s + R * sinf(th);
        py = 104 + 14 * sinf(s * 6) - R * (1 - cosf(th));
      } else {  // back from the right, a nose dive into the bottom corner
        const float e = clampf((uu - 8.3f) / 1.3f, 0, 1);
        px = lerpf(350, 70, e);
        py = lerpf(60, 232, e * e);
      }
    };
    pos(sc.u, x, y);
    float x2, y2;
    pos(sc.u + 0.03f, x2, y2);
    a = atan2f(y2 - y, x2 - x);
    if (sc.u > 9.6f) { x = 70; y = 232; a = 0.6f; }
  }
  void bugPos(float& x, float& y, float& a, bool& fly) const {
    const float u = sc.u;
    fly = false;
    if (u < 5.5f) { x = lerpf(-15, 304, u / 5.5f); y = 226; a = 0; }
    else if (u < 8.5f) { x = 304; y = lerpf(226, 70, (u - 5.5f) / 3); a = -PI_F / 2; }
    else if (u < 9.5f) { x = 304; y = 70; a = -PI_F / 2; }
    else {
      const float e = clampf((u - 9.5f) / 1.6f, 0, 1);
      x = lerpf(304, 200, e) + 14 * sinf(e * 10);
      y = lerpf(70, -30, e);
      a = -PI_F / 2;
      fly = true;
    }
  }
  float jugDropT() const { return 1.2f * (ceilf(9.6f / 1.2f + 1.0f / 6) - 1.0f / 6); }
  void ballPos(int i, float& x, float& y) const {
    const float dropT = jugDropT();
    const float uu = i == 2 ? sc.u : fminf(sc.u, dropT + 0.01f);
    if (i == 2 && sc.u > dropT) {  // the dropped one: off the arc, down past the bottom
      const float d = sc.u - dropT;
      x = 215 + d * 50;
      y = 205 + d * d * 520;
      return;
    }
    const float ph = fracf(uu / 1.2f + i / 3.0f);
    const float s = ph < 0.5f ? ph * 2 : ph * 2 - 1;
    x = ph < 0.5f ? lerpf(105, 215, s) : lerpf(215, 105, s);
    y = 205 - 175 * 4 * s * (1 - s);
  }
  void ghostPos(float& x, float& y, float& s, float& a) const {
    const float u = sc.u;
    s = 1;
    a = 0.85f * seg(0.4f, 1.0f) * (1 - seg(9.5f, 11));
    if (u < 2.6f) { x = lerpf(360, 290, seg(0.5f, 1.8f)); y = 120; }
    else if (u < 4.5f) { const float e = seg(2.6f, 2.85f); x = lerpf(290, 240, e); y = 112; s = 1 + 0.35f * e * (1 - seg(3.6f, 4.4f)); }
    else { x = 240 + 30 * sinf((u - 4.5f) * 1.2f); y = 104 + 12 * sinf((u - 4.5f) * 2.1f); }
  }
  bool popcornPos(float& x, float& y) const {
    const float tosses[4] = {2.5f, 4.0f, 9.0f, 11.5f};
    for (float tt : tosses) {
      const float e = (sc.u - tt) / 0.5f;
      if (e >= 0 && e < 1) {
        x = lerpf(250, 160, e);
        y = lerpf(190, sc.fy + 52, e) - 110 * sinf(e * PI_F);
        return true;
      }
    }
    return false;
  }

  // Drawing: back = behind the eyes (after the background), else on top of everything
  void drawScene(bool back) {
    if (!sc.id) return;
    Canvas* keep = k.cv;
    if (sc.mir < 0) {
      mirrorCv.o = keep;
      mirrorCv.preferFast = keep->preferFast;
      k.cv = &mirrorCv;
    }
    drawSceneInner(back);
    k.cv = keep;
  }
  void drawSceneInner(bool back) {
    if (!sc.id) return;
    Canvas& cv = *k.cv;
    const float u = sc.u;
    const bool light = lightBg();
    switch (sc.id) {
      case SC_FLY: {
        if (back) break;
        auto drawFly = [&](float x, float y, bool sit) {
          if (x < -30 || x > 350 || y < -30) return;
          const float fl = sit ? 0.25f + 0.1f * sinf(u * 5) : fabsf(sinf(t * 70));
          const uint16_t wing = to565(mixc(lidCol(), C(200, 225, 255), 0.6f));
          cv.ellipse(x - 5, y - 6 - fl * 3, 6, 3 + fl * 4, wing);
          cv.ellipse(x + 5, y - 6 - fl * 3, 6, 3 + fl * 4, wing);
          const RGB body = light ? C(45, 45, 55) : C(135, 140, 158);
          cv.ellipse(x, y, 8, 5.5f, to565(body));
          cv.circleFast(x + 7, y - 1, 4, to565(mulc(body, 0.8f)));
          cv.circleFast(x + 8.5f, y - 2.5f, 1.6f, cc(230, 60, 60));
          if (!sit) for (int i = 0; i < 2; i++) cv.lineFast(x - 14 - i * 5, y - 3 + i * 5, x - 19 - i * 5, y - 3 + i * 5, 0.8f, to565(mixc(lidCol(), body, 0.6f)));
        };
        float x, y;
        bool sit;
        flyPos(x, y, &sit);
        drawFly(x, y, sit);
        if (sc.twist == 1 && u > bt(7, 6) && u < bt(7, 6) + 4.5f) {  // a second fly, its own loop
          const float k = u - bt(7, 6);
          drawFly(160 - 140 * sc.mir * cosf(k * 1.4f), 60 + 40 * sinf(k * 2.3f), false);
        }
        break;
      }
      case SC_CRUMB: {
        if (back) break;
        float x, y, flap;
        int face, st;
        if (!crumbPos(x, y, flap, face, st)) break;
        drawCrumb(x, y, st ? 0 : flap, face, st == 2);
        if (st == 2) {
          const float zz = fracf(u * 0.4f);
          star4(x + 12 + 14 * zz, y - 14 - 26 * zz, 3 * (1 - zz) + 1, 0.3f, fadeTo(C(170, 190, 255), 1 - zz));
        }
        if (sc.mid == 1) {  // the stolen crumb in its beak
          const float s0 = bt(0, 4.5f);
          if (u > s0 + 0.8f && u < s0 + 6) cv.circleFast(x + 14 * face, y + 1, 2.2f, cc(205, 150, 85));
          else if (u < s0 + 0.8f) cv.circleFast(160 + 6, sc.fy + 70, 2.2f, cc(205, 150, 85));
        }
        if (sc.evId == 3 && u > sc.evU + 0.8f) heart(x - 14 * face, y - 20 - 8 * sinf(u * 3), 6, cc(255, 110, 150));
        break;
      }
      case SC_CLOCK: {
        if (back) break;
        const float in = seg(0.2f, 1.2f) * (1 - seg(sc.dur - 0.8f, sc.dur));
        if (in <= 0.01f) break;
        const float E = bt(2, 9);
        const int sit = clockPick(7, 1), roll = clockPick(8, 3), nap = clockPick(0, 2);
        const uint16_t dc = fadeTo(light ? C(70, 80, 110) : C(235, 225, 205), 0.9f * in);
        const float y0 = 168;
        for (int i = 0; i < 4; i++) {
          float x = clockX(i), y = y0, sq = 1;
          int d = clockDigit(i);
          if (sc.mid == 1 && i == roll) {  // it rolls away and comes back
            const float r0 = bt(0, 3), e = seg(r0, r0 + 1.4f) * (1 - seg(r0 + 3.2f, r0 + 4.6f));
            x += 130 * e * sc.mir;
            y += -8 * fabsf(sinf(e * 9));
          }
          if (sc.mid == 2) y -= 14 * kick(bt(0, 3) + 0.05f * i, 0.1f, 4) * fabsf(sinf((u - bt(0, 3)) * 9 + i));
          if (sc.evId == 3) y -= 18 * fabsf(sinf((u - sc.evU) * 8 + i)) * (1 - seg(sc.evU + 1, sc.evU + 2));
          if (sc.end == 2 && u > E) y -= 12 * fabsf(sinf((u - E) * 7 - i * 0.6f)) * (1 - seg(E + 1.6f, E + 2.4f));
          if (sc.end == 0 && i == 3 && u > E + 0.4f) {  // the minute ticks: flips to the next one
            const float f = clampf((u - E - 0.4f) / 0.5f, 0, 1);
            sq = fabsf(cosf(f * PI_F));
            if (f > 0.5f) d = clockDigit(3, 1);
            if (clockDigit(2, 1) != clockDigit(2) && f > 0.5f) {}  // (the tens follow below)
          }
          if (sc.end == 0 && i == 2 && u > E + 0.65f) d = clockDigit(2, 1);
          if (sc.end == 0 && i < 2 && u > E + 0.65f) d = clockDigit(i, 1);  // (xx:59 -> the hour turns too)
          drawDigit(d, x, y, fmaxf(0.05f, sq), dc);
        }
        // the colon blinks
        if (fracf(u) < 0.6f) { cv.circleFast(167, y0 + 20, 3.5f, dc); cv.circleFast(167, y0 + 44, 3.5f, dc); }
        if (sc.mid == 0 && u > bt(0, 2.5f) - 1.4f) {  // Crumb flies in and sits on top of a digit
          const float k2 = seg(bt(0, 2.5f) - 1.4f, bt(0, 2.5f));
          const float cx = lerpf(sc.mir > 0 ? -20 : 340, clockX(sit) + 18, k2), cy = lerpf(40, y0 - 12, k2) - 40 * sinf(k2 * 3.14f);
          if (!(sc.end == 1 && u > E)) drawCrumb(cx, cy, k2 < 1 ? fabsf(sinf(t * 18)) : 0, sc.mir > 0 ? 1 : -1, false);
        }
        if (sc.end == 1 && u > E) {  // Crumb curled up asleep in a 0 (or on a digit)
          const float k2 = seg(E, E + 1.0f);
          const float cx = lerpf(sc.mir > 0 ? -20 : 340, clockX(nap) + 18, k2), cy = lerpf(40, y0 + (clockDigit(nap) == 0 ? 31 : -12), k2);
          drawCrumb(cx, cy, k2 < 1 ? fabsf(sinf(t * 18)) : 0, 1, k2 >= 1, clockDigit(nap) == 0 ? 0.8f : 1);
          if (k2 >= 1) { const float zz = fracf(u * 0.4f); star4(cx + 10 + 12 * zz, cy - 12 - 22 * zz, 3 * (1 - zz) + 1, 0.3f, fadeTo(C(170, 190, 255), 1 - zz)); }
        }
        break;
      }
      case SC_DOT: {
        if (back) break;
        for (int k = 0; k < 3; k++) {
          float x, y;
          if (!dotPos(x, y, k)) continue;
          cv.circleFast(x, y, 9, fadeTo(C(255, 40, 40), 0.25f));
          cv.circleFast(x, y, 4, cc(255, 70, 60));
          cv.circleFast(x - 1, y - 1, 1.4f, cc(255, 210, 200));
        }
        break;
      }
      case SC_GLIDER: {
        if (back) break;
        float x, y, a;
        gliderPos(x, y, a);
        if (x < -40 || x > 360) break;
        const float c = cosf(a), s2 = sinf(a);
        auto P = [&](float px, float py, float& ox, float& oy) { ox = x + px * c - py * s2; oy = y + px * s2 + py * c; };
        float ax, ay, bx, by2, cx2, cy2, dx2, dy2;
        P(22, 0, ax, ay); P(-14, -11, bx, by2); P(-10, 0, cx2, cy2); P(-14, 9, dx2, dy2);
        const uint16_t paper = cc(245, 245, 240), shade = cc(200, 205, 215);
        cv.tri(ax, ay, bx, by2, cx2, cy2, paper);
        cv.tri(ax, ay, cx2, cy2, dx2, dy2, shade);
        cv.lineFast(ax, ay, cx2, cy2, 0.6f, cc(170, 175, 190));
        break;
      }
      case SC_ANT: {
        if (back) break;
        float x, y;
        if (!antPos(x, y)) break;
        const uint16_t ac = light ? cc(40, 30, 30) : cc(150, 110, 90);
        cv.circleFast(x - 6, y, 3.2f, ac);
        cv.circleFast(x, y, 2.4f, ac);
        cv.circleFast(x + 5, y - 1, 2.8f, ac);
        const float st = sinf(t * 18);
        for (int l = -1; l <= 1; l++) {
          cv.lineFast(x + l * 3, y, x + l * 3 - 3 + 2 * st * l, y + 5, 0.6f, ac);
          cv.lineFast(x + l * 3, y, x + l * 3 + 3 - 2 * st * l, y - 5, 0.6f, ac);
        }
        cv.lineFast(x + 7, y - 3, x + 10, y - 7, 0.5f, ac);
        if (sc.end == 1 && u > bt(2, 5.5f) && u < bt(2, 5.5f) + 2) cv.lineFast(x + 4, y - 3, x + 6 + 3 * sinf(t * 14), y - 10, 0.7f, ac);  // a tiny wave
        break;
      }
      case SC_PILLOW: {
        if (back) break;
        const float in = seg(0.3f, 1.3f), E = bt(2, 7.5f);
        float py = lerpf(270, 212, in), sq = 1;
        if (sc.mid == 0) sq = 1 + 0.12f * (kick(bt(0, 2.0f)) + kick(bt(1, 3.0f)));
        if (sc.mid == 1) sq = 1 + 0.2f * kick(bt(0, 2.4f));
        float px = 160;
        if (sc.evId == 2) { const float e = clampf((u - sc.evU) / 0.6f, 0, 1); py -= 160 * e; px += 90 * e; }
        if (sc.evId != 2 && sc.end == 1 && u > E) px += 260 * seg(E + 0.4f, E + 1.4f);  // pushed away
        const RGB pc = C(225, 232, 250);
        cv.rrect(px - 95 * sq, py - 22 / sq, 190 * sq, 48 / sq, 18, to565(pc));
        cv.rrect(px - 70 * sq, py - 4, 140 * sq, 2, 1, to565(mulc(pc, 0.85f)));
        for (int c2 = -1; c2 <= 1; c2 += 2) cv.circleFast(px + c2 * 92 * sq, py - 18 / sq, 4, to565(mulc(pc, 0.9f)));
        if (sc.evId != 2 && sc.end == 0 && u > E + 0.8f) {
          for (int z = 0; z < 3; z++) {
            const float zz = fracf((u - E) * 0.35f + z * 0.33f);
            star4(220 + 30 * zz, 70 - 50 * zz, 5 * (1 - zz) + 1, 0.3f, fadeTo(C(170, 190, 255), 1 - zz));
          }
        }
        break;
      }
      case SC_MUG: {
        if (back) break;
        const float E = bt(2, 6.5f), lift = sc.mid == 0 ? seg(bt(0, 3), bt(0, 3) + 0.6f) * (1 - seg(E - 0.4f, E)) : 0;
        const float mx = 160, my = lerpf(262, 206, seg(0.2f, 1.0f)) - 40 * lift;
        const RGB mc = (sc.iv[1] % 2) ? C(240, 120, 90) : C(110, 170, 240);
        cv.rrect(mx - 26, my - 22, 52, 48, 8, to565(mc));
        ring(mx + 30, my, 11, 4, to565(mulc(mc, 0.85f)));
        cv.rrect(mx - 22, my - 22, 44, 6, 3, to565(C(120, 75, 50)));  // the tea
        const bool heartEnd = sc.end == 2 && u > E;
        for (int w = 0; w < 3; w++) {  // steam curls (blown sideways while it blows on it)
          const float blow = sc.mid == 1 ? 40 * (kick(bt(0, 2.4f)) + kick(bt(1, 3.6f))) : 0;
          for (int q = 0; q < 6; q++) {
            const float h = fracf(u * 0.35f + q / 6.0f + w * 0.13f);
            const float sx = mx - 12 + w * 12 + 6 * sinf(h * 9 + w) + blow * h, sy = my - 26 - h * 55;
            if (!heartEnd) cv.circleFast(sx, sy, 2.4f * (1 - h) + 0.6f, fadeTo(C(230, 235, 245), 0.6f * (1 - h)));
          }
        }
        if (heartEnd) heart(mx, my - 50 - 20 * seg(E, E + 2), 12 * seg(E, E + 0.6f), fadeTo(C(240, 240, 250), 0.85f));
        break;
      }
      case SC_HELLO: {
        if (back) break;
        const float E = bt(2, 3.2f);
        if (sc.mid == 0 && u < E + 1) {  // a little paw waving at the side
          const float a = 0.5f * sinf(u * 9);
          const float px = 270, py = 180;
          const uint16_t pw = to565(mixc(lidCol(), C(255, 255, 255), 0.85f));
          cv.line(px, py + 30, px + 20 * sinf(a), py - 10 * cosf(a), 7, pw);
          cv.circle(px + 20 * sinf(a), py - 10 * cosf(a), 10, pw);
        }
        if (sc.end == 2 && u > E) heart(160, lerpf(sc.fy + 40, 30, seg(E, E + 1.8f)), 12 * (1 + 0.1f * sinf(t * 9)), cc(255, 90, 140));
        break;
      }
      case SC_BOOK: {
        if (back) break;
        const bool arrived = sc.evId == 4;
        const float E = arrived ? sc.evU : bt(2, 14.4f);
        const bool sleeps = !arrived && sc.end == 0;
        const float up = seg(0, 1.2f) * (sleeps ? 1 : 1 - seg(E, E + 1));
        if (up <= 0.01f) break;
        float by = 248 - 68 * up;
        if (sc.mid == 2) by += 50 * seg(bt(0, 4.6f), bt(0, 4.6f) + 0.35f) * (1 - seg(bt(0, 4.6f) + 0.6f, bt(0, 4.6f) + 1.1f));  // turns it round
        const RGB covers[4] = {C(200, 60, 70), C(60, 110, 200), C(60, 160, 110), C(150, 80, 190)};
        const RGB cov = covers[sc.iv[1] % 4], paper = C(250, 244, 228), ink = C(160, 150, 140);
        cv.rrect(58, by - 7, 204, 90, 9, to565(cov));
        cv.rrect(66, by, 92, 80, 4, to565(paper));
        cv.rrect(162, by, 92, 80, 4, to565(paper));
        cv.rrect(148, by, 10, 80, 0, to565(mixc(paper, C(0, 0, 0), 0.1f)));
        cv.rrect(162, by, 10, 80, 0, to565(mixc(paper, C(0, 0, 0), 0.1f)));
        cv.rrect(157, by - 3, 6, 84, 3, to565(mulc(cov, 0.7f)));
        for (int i = 0; i < 5; i++) {
          const float ly = by + 10 + i * 9;
          cv.rrect(74, ly, i == 4 ? 44 : 72, 2.5f, 1, to565(ink));
          if (i < 2) cv.rrect(170, ly, 74, 2.5f, 1, to565(ink));
        }
        // a little picture on the right page (a sun over hills)
        cv.circleFast(222, by + 36, 5, cc(255, 190, 60));
        cv.tri(178, by + 52, 202, by + 32, 226, by + 52, to565(C(120, 190, 120)));
        cv.tri(206, by + 52, 226, by + 38, 246, by + 52, to565(C(90, 160, 100)));
        float flips[12];
        int nf = 0;
        if (sc.mid == 1) for (float tt = 2; tt < 13 && nf < 12; tt += 1.15f) flips[nf++] = tt;  // flicking through
        else { flips[0] = bt(4, 5.2f); flips[1] = bt(5, 9.6f); flips[2] = bt(6, 13.2f); nf = 3; }
        for (int fi = 0; fi < nf; fi++) {
          const float tf = flips[fi];
          if (tf > E) continue;
          const float f = (u - tf) / 0.6f;
          if (f < 0 || f >= 1) continue;
          const float w = 92 * cosf(f * PI_F);
          const RGB pc = mixc(paper, C(200, 190, 175), sinf(f * PI_F) * 0.6f);
          if (w >= 0) cv.rrect(162, by - 6 * sinf(f * PI_F), w, 80, 4, to565(pc));
          else cv.rrect(158 + w, by - 6 * sinf(f * PI_F), -w, 80, 4, to565(pc));
        }
        if (sc.twist == 1) {  // a bookworm peeks out over the right page
          const float W = bt(1, 8), k = seg(W, W + 0.5f) * (1 - seg(W + 2.2f, W + 2.7f));
          if (k > 0.02f) {
            const RGB wc = C(130, 210, 90);
            for (int q = 0; q < 4; q++) cv.circleFast(214 + 3 * sinf(u * 6 + q), by - k * (6 + q * 7), 5.5f - q * 0.5f, to565(wc));
            cv.circleFast(216, by - k * 30, 1.6f, cc(30, 30, 30));
            cv.circleFast(220, by - k * 30, 1.6f, cc(30, 30, 30));
          }
        }
        break;
      }
      case SC_PIANO: {
        if (back) break;
        const float up = seg(0, 1.0f) * (1 - seg(sc.dur - 1.2f, sc.dur - 0.3f));
        if (up <= 0.01f) break;
        const float ky = 248 - 62 * up;
        cv.rrect(10, ky - 12, 300, 90, 9, cc(70, 45, 100));
        cv.rrect(16, ky - 7, 288, 4, 2, cc(120, 90, 160));
        float nt = 0;
        const int key = u > 1.6f ? noteAt(pianoTune(), u - 1.6f, &nt) : -1;
        static const RGB lit[7] = {C(255, 120, 120), C(255, 180, 90), C(255, 230, 90), C(120, 230, 140), C(100, 190, 255), C(150, 140, 255), C(230, 130, 255)};
        for (int i = 0; i < 14; i++) {
          const bool on = i == key && u - 1.6f - nt < HALF_BEAT * 1.6f;
          cv.rrect(20 + i * 20 + 1, ky + (on ? 2 : 0), 18, 70, 3, to565(on ? lit[i % 7] : C(250, 250, 252)));
        }
        for (int i = 0; i < 13; i++) {
          const int m = i % 7;
          if (m == 2 || m == 6) continue;
          cv.rrect(20 + i * 20 + 14, ky, 12, 38, 2, cc(30, 28, 40));
        }
        // notes floating up from the last keys played
        int n;
        const Note* s = tune(pianoTune(), n);
        float t0 = 1.6f;
        for (int i = 0; i < n; i++) {
          const float age = u - t0;
          t0 += s[i].half * HALF_BEAT;
          if (age < 0 || age > 1.6f) continue;
          const float x = 20 + s[i].key * 20 + 10 + 10 * sinf(age * 4 + i), y = ky - 12 - age * 60;
          noteGlyph(x, y, 7, fadeTo(lit[s[i].key % 7], 1 - age / 1.6f));
        }
        break;
      }
      case SC_SNACK: {
        if (back) break;
        float x, y;
        snackPos(x, y);
        int bites = 0;
        for (int b = 0; b < snackBiteN(); b++) if (u >= snackBiteT(b)) bites++;
        if (sc.twist == 1 && bites) bites = 3;  // one gulp: all gone
        if (sc.twist == 2 && u > snackBiteT(1) + 0.9f) bites = 3;  // Crumb took the rest
        const int kind = sc.mid;
        const RGB crumbC = kind == 0 ? C(205, 150, 85) : kind == 1 ? C(255, 140, 185) : kind == 2 ? C(235, 60, 70) : C(235, 60, 75);
        if (bites < 3) {
          const float r = 26;
          if (kind == 0) {  // cookie
            ball(x, y, r, C(205, 150, 85));
            if (!props3d) cv.circle(x, y, r * 0.84f, cc(220, 168, 100));
            const float cp[5][2] = {{-0.4f, -0.3f}, {0.35f, -0.4f}, {0.1f, 0.2f}, {-0.3f, 0.45f}, {0.5f, 0.3f}};
            for (auto& c : cp) cv.circleFast(x + c[0] * r, y + c[1] * r, 3.2f, cc(95, 58, 36));
          } else if (kind == 1) {  // donut
            cv.circle(x, y, r, cc(215, 165, 100));
            cv.circle(x, y, r * 0.84f, cc(255, 140, 190));
            cv.circle(x, y, r * 0.32f, to565(lidCol()));
            const RGB sp[4] = {C(255, 255, 255), C(120, 220, 255), C(255, 230, 90), C(140, 240, 150)};
            for (int i = 0; i < 8; i++) {
              const float a = i * 0.785f + 0.3f, rr2 = r * (0.5f + 0.15f * (i & 1));
              cv.lineFast(x + cosf(a) * rr2, y + sinf(a) * rr2, x + cosf(a + 0.25f) * rr2, y + sinf(a + 0.25f) * rr2, 1.2f, to565(sp[i % 4]));
            }
          } else if (kind == 2) {  // apple
            if (props3d) {
              cv.circle(x + 8, y + 2, r * 0.8f, cc(160, 30, 40));
              ball(x, y + 2, r * 0.88f, C(225, 45, 60));
            } else {
              cv.circle(x - 8, y + 2, r * 0.8f, cc(225, 45, 60));
              cv.circle(x + 8, y + 2, r * 0.8f, cc(225, 45, 60));
              cv.ellipse(x - 9, y - 6, 5, 8, cc(255, 150, 150));
            }
            cv.lineFast(x, y - r * 0.6f, x + 3, y - r - 4, 1.8f, cc(110, 70, 40));
            cv.tri(x + 3, y - r - 2, x + 18, y - r - 10, x + 12, y - r + 2, cc(90, 200, 90));
          } else {  // a watermelon slice
            cv.tri(x - 30, y + 14, x + 30, y + 14, x, y - 26, cc(235, 60, 75));
            cv.rrect(x - 31, y + 12, 62, 4, 2, cc(235, 250, 215));
            cv.rrect(x - 33, y + 16, 66, 9, 4, cc(60, 170, 80));
            const float sd[5][2] = {{-12, 4}, {0, -8}, {12, 4}, {-4, 8}, {6, -1}};
            for (auto& q : sd) cv.ellipse(x + q[0], y + q[1], 2, 3, cc(35, 25, 25));
          }
          // bites out of the top edge
          const float ba[3] = {-1.2f, -1.9f, -0.6f};
          for (int b = 0; b < bites; b++) {
            const float bx = x + cosf(ba[b]) * r * 0.95f, byy = y + sinf(ba[b]) * r * 0.95f;
            for (int j = -1; j <= 1; j++) cv.circle(bx + j * 7 * cosf(ba[b] + 1.57f), byy + j * 7 * sinf(ba[b] + 1.57f), 7.5f, to565(lidCol()));
          }
        }
        const float E = snackEndT();
        if (sc.end == 0 && u > E && u < E + 3.4f) {  // crumbs everywhere: they settle at the bottom
          for (int i = 0; i < 14; i++) {
            const float a = clampf((u - E) / 0.8f, 0, 1);
            const float cx = 160 + (fracf(i * 0.618f + sc.v[2]) - 0.5f) * 220, cy = lerpf(sc.fy + 70, 228 + 6 * fracf(i * 0.37f), a * a);
            cv.circleFast(cx, cy, 1.6f + (i % 3) * 0.6f, to565(crumbC));
          }
        }
        if (sc.twist == 2) {  // Crumb: in, grabs the rest, off
          const float c0 = snackBiteT(1) + 0.9f;
          if (u > c0 - 0.8f && u < c0 + 2.4f) {
            const float a = seg(c0 - 0.8f, c0), b = seg(c0 + 0.6f, c0 + 2.4f);
            const float cx = lerpf(lerpf(sc.mir > 0 ? -20 : 340, x + 20, a), sc.mir > 0 ? 360 : -40, b);
            const float cy = lerpf(lerpf(30, y - 20, a), 20, b);
            drawCrumb(cx, cy, fabsf(sinf(t * 18)), (sc.mir > 0) == (b < 0.01f) ? 1 : -1, false);
            if (u > c0) cv.circleFast(cx + 12, cy + 2, 4, to565(crumbC));
          }
        }
        if (sc.evId == 3 && evT() > 0 && evT() < 1.4f) {  // the shared crumb comes toward you
          const float e = evT() / 1.4f;
          cv.circleFast(160, lerpf(sc.fy + 70, 222, e), 2 + 9 * e, to565(crumbC));
        }
        for (int b = 0; b < bites; b++) {  // crumbs
          const float age = u - snackBiteT(b < snackBiteN() ? b : 0);
          if (age > 1.2f) continue;
          for (int i = 0; i < 6; i++) {
            const float vx = (i - 2.5f) * 22, vy = -60 - 15 * (i % 3);
            cv.circleFast(x + vx * age, y - 22 + vy * age + 380 * age * age, 1.8f, to565(crumbC));
          }
        }
        break;
      }
      case SC_BUBBLES: {
        if (back) break;
        const RGB bc = light ? C(90, 150, 220) : C(170, 230, 255);
        const int n = bubbleN();
        for (int i = 0; i < n; i++) {
          float x, y, r, tp;
          bubble(i, x, y, r, tp);
          if (u < tp && y < 262 && y > -40) {
            ring(x, y, r, sc.mid == 2 ? 1.8f : 1.2f, to565(mixc(lidCol(), bc, 0.85f)));
            cv.ellipse(x - r * 0.4f, y - r * 0.4f, r * 0.2f, r * 0.14f, to565(mixc(lidCol(), C(255, 255, 255), 0.85f)));
            cv.circleFast(x + r * 0.45f, y + r * 0.2f, sc.mid == 2 ? 2.5f : 1, to565(mixc(lidCol(), C(255, 190, 240), 0.7f)));
          } else if (u >= tp && u < tp + 0.28f) {
            const float e = (u - tp) / 0.28f;
            burst(x, y, r * (0.8f + e), r * (1.3f + e * 1.2f), 8, 1.0f, to565(mixc(lidCol(), bc, 1 - e)));
          }
        }
        break;
      }
      case SC_BUTTERFLY: {
        if (back) break;
        float x, y;
        int where;
        butterflyPos(x, y, &where);
        if (x < -30 || x > 350 || y < -30) break;
        const bool sit = where != 0;
        const float f = sit ? 0.55f + 0.45f * sinf(u * 3) : fabsf(sinf(t * 16));
        static const RGB c1s[3] = {C(255, 150, 40), C(80, 160, 255), C(255, 110, 190)};
        static const RGB c2s[3] = {C(255, 215, 90), C(160, 225, 255), C(255, 195, 235)};
        const int pal = sc.iv[3] % 3;
        const uint16_t c1 = to565(c1s[pal]), c2 = to565(c2s[pal]);
        const float z = where == 2 ? 1.2f : 1.6f;  // (smaller on the nose)
        for (int sd = -1; sd <= 1; sd += 2) {
          cv.ellipse(x + sd * (9 * f + 3) * z, y - 5 * z, (9 * f + 2) * z, 9 * z, c1);
          cv.ellipse(x + sd * (6 * f + 2) * z, y + 6 * z, (6 * f + 2) * z, 6 * z, c2);
          cv.circleFast(x + sd * (9 * f + 3) * z, y - 6 * z, (2.2f * f + 0.5f) * z, to565(mixc(c1s[pal], C(255, 255, 255), 0.6f)));
        }
        const uint16_t dark = light ? cc(50, 40, 50) : cc(90, 80, 90);
        cv.ellipse(x, y + z, 2.4f * z, 9 * z, dark);
        cv.lineFast(x - z, y - 8 * z, x - 6 * z, y - 16 * z, 0.8f, dark);
        cv.lineFast(x + z, y - 8 * z, x + 6 * z, y - 16 * z, 0.8f, dark);
        break;
      }
      case SC_DREAM: {
        if (back) break;
        const float g = seg(3.2f, 4.2f) * (1 - seg(12.5f, 12.8f));
        const RGB fill = light ? C(255, 255, 255) : C(52, 60, 96), rim = light ? C(150, 160, 190) : C(175, 185, 235);
        const float cx = 238, cy = 54;
        const float dots[3][3] = {{188, 98, 3}, {202, 86, 5}, {216, 74, 7}};
        for (int i = 0; i < 3; i++) {
          const float a = seg(2.4f + i * 0.25f, 2.7f + i * 0.25f) * (1 - seg(12.5f, 12.8f));
          if (a > 0.02f) { cv.circle(dots[i][0], dots[i][1], dots[i][2] * a + 1.2f, to565(rim)); cv.circle(dots[i][0], dots[i][1], dots[i][2] * a, to565(fill)); }
        }
        if (g > 0.02f) {
          cloud(cx, cy, 34 * g + 1.5f, to565(rim));
          cloud(cx, cy, 34 * g, to565(fill));
          if (g > 0.9f) {
            const float gy = cy + 20;
            cv.rrect(cx - 42, gy, 84, 2, 1, cc(110, 200, 110));
            const uint16_t wood = cc(190, 140, 90);
            cv.rrect(cx - 7, gy - 16, 3, 16, 1, wood);
            cv.rrect(cx + 5, gy - 16, 3, 16, 1, wood);
            cv.rrect(cx - 10, gy - 12, 21, 2.5f, 1, wood);
            cv.rrect(cx - 10, gy - 6, 21, 2.5f, 1, wood);
            if (u > 4.5f && u < 12.4f) {
              const float p = fracf((u - 4.5f) / 2.0f);
              const float sx = cx - 36 + 72 * p, sy = gy - 6 - 26 * sinf(p * PI_F);
              const uint16_t wool = cc(250, 250, 250);
              for (int i = 0; i < 4; i++) cv.circleFast(sx - 4 + (i & 1) * 7, sy - 2 + (i >> 1) * 4, 4.2f, wool);
              cv.circleFast(sx + 7, sy - 2, 3, cc(60, 55, 60));
              cv.lineFast(sx - 3, sy + 5, sx - 4, sy + 9, 0.8f, cc(60, 55, 60));
              cv.lineFast(sx + 3, sy + 5, sx + 4, sy + 9, 0.8f, cc(60, 55, 60));
            }
          }
        }
        if (u >= 12.5f && u < 12.8f) burst(cx, cy, 30, 44, 10, 1.2f, to565(rim));
        break;
      }
      case SC_STARS: {
        if (!back) break;
        const float in = seg(0.3f, 2.2f) * (1 - seg(13.5f, 15));
        if (in <= 0.01f) break;
        const RGB sc1 = light ? C(230, 170, 40) : C(255, 250, 215);
        for (int i = 0; i < 18; i++) {
          const float x = 12 + fracf(i * 0.618f + sc.v[0]) * 296, y = 8 + fracf(i * 0.377f + sc.v[1]) * 70 + (fabsf(x - 160) > 110 ? fracf(i * 0.21f) * 60 : 0);
          const float tw = 0.6f + 0.4f * sinf(t * (2 + i % 3) + i);
          const float sz = (i % 4 == 0 ? 5.5f : 1.6f) * tw;
          const uint16_t c = to565(mixc(lidCol(), sc1, in * (0.5f + 0.5f * tw)));
          if (i % 4 == 0) star4(x, y, sz, 0.22f, c);
          else cv.circleFast(x, y, sz, c);
        }
        cv.circle(40, 32, 15, to565(mixc(lidCol(), light ? C(240, 200, 80) : C(255, 240, 180), in)));
        cv.circle(47, 26, 13.5f, to565(lidCol()));
        if (u > 5.5f && u < 6.5f) {
          const float s = (u - 5.5f) / 0.8f;
          const float hx = lerpf(300, 80, fminf(s, 1)), hy = lerpf(10, 70, fminf(s, 1));
          for (int i = 0; i < 5; i++) {
            const float a0 = i * 0.06f, a1 = a0 + 0.06f;
            const float x0 = hx + (300 - 80) * a0, y0 = hy - 60 * a0, x1 = hx + (300 - 80) * a1, y1 = hy - 60 * a1;
            cv.lineFast(x0, y0, x1, y1, 1.6f - i * 0.25f, to565(mixc(lidCol(), sc1, (1 - i / 5.0f) * (s < 1 ? 1 : 1 - (u - 6.3f) * 5))));
          }
          if (s < 1) star4(hx, hy, 6, 0.24f, to565(C(255, 255, 255)));
        }
        break;
      }
      case SC_TEA: {
        if (back) break;
        float y;
        teaPos(y);
        if (y > 262) break;
        static const RGB cups[3] = {C(150, 220, 200), C(255, 180, 200), C(170, 200, 255)};
        const RGB cup = cups[sc.iv[1] % 3];
        cv.ellipse(160, y + 26, 48, 8, cc(235, 235, 245));
        ring(197, y + 2, 10, 3.2f, to565(cup));
        cv.rrect(128, y - 18, 64, 44, 15, to565(cup));
        cv.ellipse(160, y - 16, 30, 6, to565(mulc(cup, 0.8f)));
        cv.ellipse(160, y - 15, 26, 4.5f, sc.iv[2] % 2 ? cc(150, 95, 55) : cc(130, 175, 70));
        heart(160, y + 3, 6, cc(255, 120, 150));
        for (int i = 0; i < 3; i++) {  // steam
          float xs[8], ys[8];
          for (int j = 0; j < 8; j++) {
            const float s = j / 7.0f;
            xs[j] = 146 + i * 14 + 5 * sinf(u * 3 + s * 5 + i * 2);
            ys[j] = y - 26 - s * 34 - fmodf(u * 12 + i * 7, 10);
          }
          polyline(xs, ys, 8, 1.3f, to565(mixc(lidCol(), light ? C(160, 160, 175) : C(230, 230, 240), 0.55f)), false);
        }
        break;
      }
      case SC_GAME: {
        if (back) break;
        const float up = seg(0, 1.2f) * (1 - seg(13.4f, 14.4f));
        if (up <= 0.01f) break;
        const float y0 = 252 - 88 * up;
        cv.rrect(92, y0, 136, 100, 16, cc(235, 70, 100));
        cv.rrect(106, y0 + 7, 108, 58, 6, cc(40, 40, 55));
        const float sx = 112, sy = y0 + 12, sw = 96, sh = 48;
        cv.rrect(sx, sy, sw, sh, 3, cc(150, 205, 120));
        const uint16_t px = cc(40, 70, 40);
        const bool win = sc.v[5] < 0.6f;
        const float gy = sy + sh - 8;
        cv.rrect(sx, gy + 4, sw, 1.5f, 0, px);
        float jump = 0;
        bool hit = false;
        for (int i = 0; i < 8; i++) {
          const float sp = 2 + i * 1.1f;
          const float ox = sx + sw - (u - sp) * 60;
          if (u >= sp && ox > sx - 6 && ox < sx + sw) cv.rrect(ox, gy - 4, 5, 8, 0, px);
          const float jt = sp + 0.97f, d = u - jt;
          if (d >= 0 && d < 0.45f && (win || i < 7)) jump = sinf(d / 0.45f * PI_F) * 16;
          if (!win && i == 7 && u > jt + 0.15f) hit = true;
        }
        if (!(hit && fmodf(u * 6, 1) < 0.5f)) cv.rrect(sx + 12, gy - 6 - jump, 7, 9, 1, px);
        if (u > 11 && win) star4(sx + sw / 2, sy + sh / 2 - 6, 9 * (0.7f + 0.3f * sinf(u * 8)), 0.25f, px);
        // D-pad and buttons
        const uint16_t dk = cc(60, 25, 40);
        cv.rrect(108, y0 + 72, 22, 7, 2, dk);
        cv.rrect(115, y0 + 65, 7, 22, 2, dk);
        cv.circle(198, y0 + 80, 6, cc(255, 220, 90));
        cv.circle(212, y0 + 72, 6, cc(110, 200, 255));
        break;
      }
      case SC_HEADPHONES: {
        if (back) break;
        const float on = seg(0, 1.0f) * (1 - seg(12.6f, 13.6f));
        if (on <= 0.01f) break;
        const float dy = -170 * (1 - on);
        static const RGB cups[3] = {C(255, 110, 170), C(90, 200, 255), C(170, 130, 255)};
        const RGB cup = cups[sc.iv[1] % 3];
        float xs[17], ys[17];
        for (int i = 0; i <= 16; i++) {
          const float a = PI_F + i * PI_F / 16;
          xs[i] = 160 + cosf(a) * 132;
          ys[i] = 140 + sinf(a) * 138 + dy;
        }
        polyline(xs, ys, 17, 5, to565(light ? C(70, 70, 85) : C(120, 120, 140)));
        polyline(xs, ys, 17, 1.5f, to565(mixc(cup, C(255, 255, 255), 0.4f)), false);
        for (int sd = -1; sd <= 1; sd += 2) {
          const float cx = 160 + sd * 136;
          cv.rrect(cx - 18, 108 + dy, 36, 66, 15, to565(cup));
          cv.rrect(cx - 18 + (sd < 0 ? 27 : 0), 116 + dy, 9, 50, 4, to565(mulc(cup, 0.6f)));
          cv.rrect(cx - 4 - sd * 4, 118 + dy, 4, 22, 2, to565(mixc(cup, C(255, 255, 255), 0.5f)));
        }
        for (int i = 0; i < 12; i++) {  // notes rising from the ear cups
          const float born = 1 + i * 0.6f, age = u - born;
          if (age < 0 || age > 2 || born > 12) continue;
          const float sd = i % 2 ? 1 : -1;
          noteGlyph(160 + sd * (120 - age * 14) + 6 * sinf(age * 5), 100 - age * 45 + dy, 6.5f, fadeTo(mixc(cup, C(255, 255, 255), 0.3f), 1 - age / 2));
        }
        break;
      }
      case SC_SNOW: {
        if (back) break;
        const float in = seg(0, 1) * (1 - seg(12, 13.5f));
        const RGB fc = light ? C(120, 165, 235) : C(235, 245, 255);
        for (int i = 0; i < 22; i++) {
          const float sp = 22 + 16 * fracf(i * 0.71f);
          const float y = -10 + fmodf(u * sp + i * 29, 260);
          const float x = fracf(i * 0.618f + sc.v[0]) * 320 + 10 * sinf(u * 1.2f + i);
          const uint16_t c = to565(mixc(lidCol(), fc, in * (0.55f + 0.45f * fracf(i * 0.33f))));
          if (i % 5 == 0) { const float s = 4; cv.lineFast(x - s, y, x + s, y, 0.7f, c); cv.lineFast(x - s * 0.5f, y - s * 0.87f, x + s * 0.5f, y + s * 0.87f, 0.7f, c); cv.lineFast(x - s * 0.5f, y + s * 0.87f, x + s * 0.5f, y - s * 0.87f, 0.7f, c); }
          else cv.circleFast(x, y, 1.3f + (i % 3) * 0.5f, c);
        }
        if (u > 3.5f && u < 7.7f) {
          float x, y;
          flakePos(x, y);
          const float s = 8 * (u < 7.1f ? 1 : 1 - (u - 7.1f) / 0.6f);
          const uint16_t c = to565(fc);
          for (int i = 0; i < 3; i++) {
            const float a = i * PI_F / 3 + u * 0.8f;
            cv.lineFast(x - cosf(a) * s, y - sinf(a) * s, x + cosf(a) * s, y + sinf(a) * s, 1.0f, c);
          }
          cv.circleFast(x, y, s * 0.25f, c);
        }
        break;
      }
      case SC_PAINT: {
        if (back) {  // the rainbow, painted on the wall behind the face
          const float p = seg(1.5f, 8.5f);
          if (p <= 0) break;
          static const RGB rb[6] = {C(255, 70, 70), C(255, 160, 50), C(255, 230, 70), C(90, 220, 110), C(80, 160, 255), C(170, 100, 255)};
          const int n = 2 + (int)(p * 22);
          for (int j = 0; j < 6; j++) {
            float xs[25], ys[25];
            const float r = RB_R - j * 7;
            for (int i = 0; i < n; i++) {
              const float a = PI_F + PI_F * p * i / (n - 1);
              xs[i] = 160 + cosf(a) * r;
              ys[i] = RB_Y + sinf(a) * r * RB_S;
            }
            polyline(xs, ys, n, 3.4f, to565(mixc(lidCol(), rb[j], 0.9f)), false);
          }
          if (u > 9.5f && u < 13)
            for (int i = 0; i < 5; i++) {
              const float a = PI_F + PI_F * (0.12f + i * 0.19f), tw = sinf(u * 6 + i * 1.7f);
              if (tw > 0.2f) star4(160 + cosf(a) * (RB_R - 18), RB_Y + sinf(a) * (RB_R - 18) * RB_S, 7 * tw, 0.22f, to565(C(255, 255, 255)));
            }
          break;
        }
        float x, y;
        brushPos(x, y);
        if (y > 270 || x > 340) break;
        cv.lineFast(x + 6, y + 8, x + 28, y + 38, 2.6f, cc(170, 110, 60));
        cv.circleFast(x + 5, y + 7, 3.6f, cc(190, 190, 200));
        const RGB tip = mixc(C(255, 90, 90), C(160, 110, 255), seg(1.5f, 8.5f));
        cv.circle(x, y, 5, to565(tip));
        break;
      }
      case SC_PEEKABOO: {
        if (back) break;
        const float covers[3] = {0.8f, 4.3f, 7.4f}, holds[3] = {1.5f, 1.1f, 0.7f};
        float c = 0;
        for (int i = 0; i < 3; i++) {
          const float a = covers[i], op = a + 0.35f + holds[i];
          if (u >= a && u < op + 0.25f) c = u < a + 0.35f ? ease((u - a) / 0.35f) : u < op ? 1 : 1 - ease((u - op) / 0.25f);
        }
        if (c <= 0.01f) break;
        const bool pet = k.s.head == H_CAT || k.s.head == H_DOG;
        const RGB fur = pet ? k.s.deco1 : light ? C(255, 225, 205) : C(255, 205, 220);
        const RGB pad = C(255, 140, 170);
        const float gap = k.s.gap * 0.5f;
        for (int sd = -1; sd <= 1; sd += 2) {
          const float x = 160 + sd * (gap + 40 * (1 - c)), y = lerpf(300, sc.fy + 6, c);
          paw(x, y, 46, fur, pad);
        }
        break;
      }
      case SC_FEATHER: {
        if (back) break;
        float x, y, a;
        featherPos(x, y, a);
        if (x < -50 || y < -50) break;
        static const RGB tips[3] = {C(255, 150, 200), C(130, 200, 255), C(190, 150, 255)};
        feather(x, y, a, 46, tips[sc.iv[1] % 3]);
        if (u > 8.3f && u < 8.8f) {  // "achoo!" puffs
          const float e = (u - 8.3f) / 0.5f;
          for (int i = 0; i < 5; i++) cv.circleFast(160 + (i - 2) * 16 * (1 + e), sc.fy + 60 - 30 * e - (i % 2) * 8, 6 * (1 - e) + 1, fadeTo(C(230, 235, 245), 1 - e));
        }
        break;
      }
      case SC_BALLOON: {
        if (back) break;
        float x, y;
        balloonPos(x, y);
        static const RGB bs[3] = {C(255, 70, 90), C(80, 170, 255), C(255, 190, 60)};
        const RGB bc = bs[sc.iv[1] % 3];
        if (u < 7 || (sc.iv[2] % 2 && y > -80)) {
          float xs[10], ys[10];
          for (int i = 0; i < 10; i++) { xs[i] = x + 4 * sinf(u * 3 + i * 0.8f) * i / 9; ys[i] = y + 30 + i * 7; }
          polyline(xs, ys, 10, 0.8f, to565(inkish()), false);
          oval(x, y, 22, 27, bc);
          cv.tri(x - 4, y + 30, x + 4, y + 30, x, y + 24, to565(mulc(bc, 0.8f)));
          if (!props3d) cv.ellipse(x - 8, y - 10, 5, 8, to565(mixc(bc, C(255, 255, 255), 0.55f)));
        } else if (u < 7.35f && !(sc.iv[2] % 2)) {
          const float e = (u - 7) / 0.35f;
          burst(x, y, 18 + 20 * e, 30 + 26 * e, 10, 1.4f, fadeTo(bc, 1 - e));
        }
        if (u > 7 && u < 8.5f && !(sc.iv[2] % 2)) {  // shreds and the string falling
          const float d = u - 7;
          for (int i = 0; i < 3; i++) cv.tri(x - 12 + i * 12, y + d * d * 300, x - 6 + i * 12, y + 4 + d * d * 300, x - 10 + i * 12, y + 7 + d * d * 300, to565(bc));
          cv.lineFast(x, y + 30 + d * d * 400, x + 3, y + 90 + d * d * 400, 0.8f, to565(inkish()));
        }
        break;
      }
      case SC_COIN: {
        if (back) break;
        float x, y, w;
        coinPos(x, y, w);
        if (u < 5.9f) {
          coin(x, y, w, 13);
          const float g = fracf(u / 1.2f);
          if (u < 4.8f && g < 0.35f) star4(x + 6, y - 7, 8 * sinf(g / 0.35f * PI_F), 0.22f, to565(C(255, 255, 230)));
        } else if (u < 6.4f) {
          const float e = (u - 5.9f) / 0.5f;
          for (int i = 0; i < 6; i++) {
            const float a = i * 1.047f + e;
            star4(160 + cosf(a) * 26 * e, 30 + sinf(a) * 26 * e, 6 * (1 - e), 0.24f, cc(255, 220, 90));
          }
        }
        break;
      }
      case SC_HICCUPS: {
        if (back) break;
        const float hics[5] = {1.2f, 3.4f, 5.0f, 6.1f, 8.2f};
        for (float h : hics) {
          const float age = u - h;
          if (age < 0 || age > 0.9f) continue;
          const float x = 195 + age * 50, y = sc.fy + 30 - age * 70, r = 6 + age * 6;
          const RGB bc = light ? C(120, 170, 230) : C(190, 230, 255);
          ring(x, y, r, 1.1f, fadeTo(bc, 1 - age / 0.9f));
          cv.circleFast(x - r * 0.35f, y - r * 0.35f, 1.3f, fadeTo(C(255, 255, 255), 1 - age / 0.9f));
        }
        break;
      }
      case SC_IDEA: {
        if (back) break;
        const float in = seg(0.8f, 2);
        const float suck = seg(6.8f, 7.3f);
        static const RGB cols[6] = {C(255, 120, 120), C(255, 200, 80), C(120, 220, 140), C(110, 180, 255), C(200, 140, 255), C(255, 150, 210)};
        if (suck < 1 && in > 0)
          for (int i = 0; i < 6; i++) {
            const float a = u * 0.6f + i * 2 * PI_F / 6;
            const float ox = 160 + cosf(a) * 135, oy = sc.fy - 10 + sinf(a) * 85;
            const float x = lerpf(ox, 160, suck), y = lerpf(oy, 20, suck), s = 8 * (1 - suck * 0.7f);
            const uint16_t c = fadeTo(cols[i], in * (1 - suck));
            switch (i) {
              case 0: cv.lineFast(x - s, y, x + s, y, 1.6f, c); cv.lineFast(x, y - s, x, y + s, 1.6f, c); break;           // +
              case 1: cv.lineFast(x - s, y, x + s, y, 1.6f, c); break;                                                      // -
              case 2: cv.lineFast(x - s * 0.7f, y - s * 0.7f, x + s * 0.7f, y + s * 0.7f, 1.6f, c); cv.lineFast(x - s * 0.7f, y + s * 0.7f, x + s * 0.7f, y - s * 0.7f, 1.6f, c); break;  // x
              case 3: cv.lineFast(x - s, y, x + s, y, 1.6f, c); cv.circleFast(x, y - s * 0.6f, 2, c); cv.circleFast(x, y + s * 0.6f, 2, c); break;  // divide
              case 4: cv.lineFast(x - s, y - s * 0.3f, x + s, y - s * 0.3f, 1.6f, c); cv.lineFast(x - s, y + s * 0.3f, x + s, y + s * 0.3f, 1.6f, c); break;  // =
              default: curve(x - s * 0.5f, y - s * 0.4f, x, y - s * 1.4f, x + s * 0.5f, y - s * 0.2f, 1.5f, c, 6, false); cv.lineFast(x + s * 0.5f, y - s * 0.2f, x, y + s * 0.3f, 1.5f, c); cv.circleFast(x, y + s * 0.8f, 1.8f, c); break;  // ?
            }
          }
        if (u > 7.2f) {
          const float pop = fminf(1, (u - 7.2f) / 0.25f), sz = 14 * (pop < 1 ? pop * 1.2f : 1 + 0.06f * sinf(u * 8));
          const float fade = 1 - seg(11.5f, 12.5f);
          if (fade > 0.02f) {
            burst(160, 20, sz + 5, sz + 11 + 2 * sinf(u * 8), 8, 1.3f, fadeTo(C(255, 230, 120), fade), 0.2f);
            cv.circle(160, 20, sz, fadeTo(C(255, 235, 110), fade));
            cv.circleFast(156, 16, sz * 0.3f, fadeTo(C(255, 255, 230), fade));
            cv.rrect(153, 20 + sz * 0.8f, 14, 8, 2, fadeTo(C(170, 170, 185), fade));
          }
        }
        break;
      }
      case SC_PLANE: {
        if (back) break;
        float x, y, a;
        planePos(x, y, a);
        if (x < -40 || x > 360) break;
        const float dx = cosf(a), dy = sinf(a), nx = -dy, ny = dx;
        const bool crashed = u > 9.6f;
        const float L = crashed ? 11 : 16;
        const RGB w = C(250, 250, 255), g = light ? C(170, 175, 195) : C(200, 205, 220);
        const float tx = x - dx * L, ty = y - dy * L;
        if (light) {  // a dark outline first: white paper on a light face
          const uint16_t o = cc(90, 90, 110);
          cv.tri(x + dx * (L + 2), y + dy * (L + 2), tx + nx * 13 - dx * 2, ty + ny * 13 - dy * 2, x - dx * 6, y - dy * 6, o);
          cv.tri(x + dx * (L + 2), y + dy * (L + 2), tx - nx * 9 - dx * 2, ty - ny * 9 - dy * 2, x - dx * 6, y - dy * 6, o);
        }
        cv.tri(x + dx * L, y + dy * L, tx + nx * 11, ty + ny * 11, x - dx * 4, y - dy * 4, to565(w));
        cv.tri(x + dx * L, y + dy * L, tx - nx * 7, ty - ny * 7, x - dx * 4, y - dy * 4, to565(g));
        cv.lineFast(x + dx * L, y + dy * L, tx, ty, 0.7f, to565(mixc(g, C(0, 0, 0), 0.2f)));
        if (crashed && u < 10.4f) burst(x, y, 12, 18, 5, 1, fadeTo(inkish(), 1 - (u - 9.6f) / 0.8f), -2.5f);
        break;
      }
      case SC_LADYBUG: {
        if (back) break;
        float x, y, a;
        bool fly;
        bugPos(x, y, a, fly);
        if (y < -20) break;
        const float z = 1.45f;  // size
        const float dx = cosf(a) * z, dy = sinf(a) * z, nx = -dy, ny = dx;
        const uint16_t leg = light ? cc(40, 40, 45) : cc(150, 150, 160);
        if (!fly) {
          const float st = sinf(u * 18) * 2.5f;
          for (int i = -1; i <= 1; i++)
            for (int sd = -1; sd <= 1; sd += 2) {
              const float bx = x + dx * i * 5, by = y + dy * i * 5;
              cv.lineFast(bx, by, bx + nx * sd * 13 + dx * (i * 3 + st * sd), by + ny * sd * 13 + dy * (i * 3 + st * sd), 0.7f, leg);
            }
        } else {
          const float f = fabsf(sinf(t * 20));
          for (int sd = -1; sd <= 1; sd += 2) cv.ellipse(x + sd * (8 + 6 * f) * z, y + 2, (7 + 5 * f) * z, 5 * z, fadeTo(C(220, 235, 255), 0.6f));
        }
        const float open = fly ? 5 : u > 8.5f && u < 9.5f ? 3 * seg(8.5f, 9.3f) : 0;
        const uint16_t red = cc(230, 40, 45), spot = cc(25, 20, 25);
        for (int sd = -1; sd <= 1; sd += 2) {
          const float ox = nx * sd * open, oy = ny * sd * open;
          cv.circle(x + ox, y + oy, 10 * z, red);
          cv.circleFast(x + ox + nx * sd * 4 - dx * 3, y + oy + ny * sd * 4 - dy * 3, 2.2f, spot);
          cv.circleFast(x + ox + nx * sd * 3 + dx * 4, y + oy + ny * sd * 3 + dy * 4, 1.8f, spot);
        }
        cv.lineFast(x - dx * 9, y - dy * 9, x + dx * 7, y + dy * 7, 0.7f, spot);
        cv.circle(x + dx * 10, y + dy * 10, 5 * z, light ? cc(30, 30, 35) : cc(70, 70, 82));
        cv.circleFast(x + dx * 12 + nx * 2, y + dy * 12 + ny * 2, 1.2f, cc(255, 255, 255));
        cv.circleFast(x + dx * 12 - nx * 2, y + dy * 12 - ny * 2, 1.2f, cc(255, 255, 255));
        break;
      }
      case SC_SING: {
        if (back) break;
        const float up = seg(0, 1.0f) * (1 - seg(sc.dur - 1.2f, sc.dur - 0.3f));
        if (up <= 0.01f) break;
        const float y = 272 - 66 * up;
        const uint16_t body = light ? cc(70, 70, 85) : cc(110, 110, 128);
        cv.rrect(153, y + 12, 14, 50, 5, body);
        cv.rrect(150, y + 10, 20, 7, 3, to565(light ? C(200, 170, 60) : C(230, 200, 90)));
        cv.circle(160, y, 16, cc(175, 175, 190));
        for (int i = -1; i <= 1; i++) {
          cv.lineFast(160 + i * 7, y - 13 + fabsf((float)i) * 3, 160 + i * 7, y + 13 - fabsf((float)i) * 3, 0.6f, cc(120, 120, 135));
          cv.lineFast(147 + fabsf((float)i) * 3, y + i * 7, 173 - fabsf((float)i) * 3, y + i * 7, 0.6f, cc(120, 120, 135));
        }
        cv.ellipse(154, y - 7, 4, 3, cc(235, 235, 245));
        int n;
        const Note* s = tune(singTune(), n);
        float t0 = 1.6f;
        static const RGB nc[4] = {C(255, 140, 170), C(255, 210, 90), C(120, 210, 255), C(170, 230, 140)};
        for (int i = 0; i < n; i++) {
          const float age = u - t0;
          t0 += s[i].half * HALF_BEAT;
          if (age < 0 || age > 1.8f) continue;
          const float sd = i % 2 ? 1 : -1;
          noteGlyph(160 + sd * (30 + age * 50), y - 20 - age * 60 - s[i].key * 2, 7, fadeTo(nc[i % 4], 1 - age / 1.8f));
        }
        break;
      }
      case SC_CAKE: {
        if (back) break;
        const float up = seg(0, 1.3f) * (1 - seg(14.6f, 15.6f));
        if (up <= 0.01f) break;
        const float cy = 250 - 60 * up;  // top of the bottom tier
        box(98, cy, 124, 44, 9, C(255, 180, 205));
        box(120, cy - 30, 80, 34, 9, C(255, 238, 244));
        for (int i = 0; i < 7; i++) cv.circleFast(106 + i * 18, cy + 3, 6, cc(255, 238, 244));   // icing drips
        for (int i = 0; i < 5; i++) cv.circleFast(128 + i * 16, cy - 28, 4.5f, cc(255, 150, 185));
        for (int i = 0; i < 3; i++) ball(125 + i * 35, cy + 20, 4.5f, C(230, 50, 70));   // cherries
        const float out = seg(9.0f, 9.6f);
        for (int i = 0; i < 3; i++) {
          const float x = 140 + i * 20;
          cv.rrect(x - 3, cy - 50, 6, 20, 2, to565(i == 1 ? C(120, 200, 255) : C(255, 220, 100)));
          if (out < 1) {
            const float fl = (1 - out) * (1 + 0.15f * sinf(t * 23 + i * 2));
            drop(x, cy - 55, 4.2f * fl, cc(255, 170, 40));
            drop(x, cy - 54, 2.4f * fl, cc(255, 245, 170));
          } else if (u < 11.5f) {  // smoke
            float xs[6], ys[6];
            const float a = fminf(1, (u - 9.6f) / 0.4f) * (1 - seg(10.8f, 11.5f));
            for (int j = 0; j < 6; j++) { xs[j] = x + 4 * sinf(u * 4 + j + i); ys[j] = cy - 54 - j * 5 - (u - 9.6f) * 10; }
            polyline(xs, ys, 6, 1.1f, fadeTo(C(170, 170, 180), a * 0.8f), false);
          }
        }
        if (u > 9 && u < 9.7f) {  // the blow
          const float e = (u - 9) / 0.7f;
          for (int i = -1; i <= 1; i++) curve(160 + i * 10, sc.fy + 40 + e * 10, 160 + i * 18 + 8, sc.fy + 60 + e * 16, 150 + i * 24, cy - 60 + e * 10, 1.2f, fadeTo(inkish(), 0.8f * (1 - e)), 6, false);
        }
        break;
      }
      case SC_RAIN: {
        if (back) break;
        const float U0 = bt(1, 5), E = bt(2, 11);
        const float ub = sc.twist == 1 ? seg(U0, U0 + 0.35f) * (1 - seg(E + 0.2f, E + 0.8f)) : 0;
        // with the umbrella open the cloud drifts off to one side, smaller and higher (else it sits right on the
        // canopy: the two stacked on each other), and goes behind it
        const float aside = sc.twist == 1 ? seg(U0 - 0.4f, U0 + 0.6f) * (1 - seg(E + 0.2f, E + 0.8f)) : 0;
        const float cx = lerpf(-80, 160, seg(0, 1.5f)) + (sc.end == 0 ? lerpf(0, 260, seg(E, E + 1.5f)) : 0) + 6 * sinf(u * 0.7f) + aside * 112;
        const float cy = 32 - aside * 10, cr = 26 - aside * 6;
        const float ucx = 160, ucy = 54, ur = 104 * ub;
        RGB cl = light ? C(125, 130, 148) : C(150, 156, 175);
        if (sc.mid == 1 || sc.twist == 2) cl = mulc(cl, 0.75f);  // a heavy, dark cloud
        cloud(cx, cy, cr, to565(cl));
        cv.rrect(cx - cr * 1.15f, cy + cr * 0.46f, cr * 2.3f, 4, 2, to565(mulc(cl, 0.8f)));
        const RGB dropC = light ? C(80, 140, 230) : C(140, 200, 255);
        const bool raining = u > 1.4f && (sc.end == 2 || u < E + 0.3f);
        const int nd = u > E && sc.end == 2 ? 6 : sc.mid == 1 ? 22 : 10;  // (cozy: a soft rain keeps on)
        if (raining)
          for (int i = 0; i < nd; i++) {
            const float x = cx - cr * 2.1f + fracf(i * 0.37f + sc.v[0]) * cr * 4.2f;
            float y = cy + 18 + fmodf(u * (sc.mid == 1 ? 240 : 150) + i * 37, 200);
            float stop = 250;
            if (ub > 0.5f && fabsf(x - ucx) < ur) {
              const float d = (x - ucx) / ur;
              stop = ucy - 0.55f * ur * sqrtf(fmaxf(0, 1 - d * d));
            }
            if (y < stop) cv.lineFast(x, y, x - 1, y + 8, 0.8f, to565(dropC));
            else if (y < stop + 14) cv.circleFast(x + (i % 2 ? 4 : -4) * (y - stop) / 14, stop - 2 - (y - stop) * 0.3f, 1.2f, to565(dropC));
          }
        if (sc.twist == 2 && u > U0 && u < U0 + 0.45f) {  // thunder: a bolt from the cloud
          const float a = 1 - (u - U0) / 0.45f;
          const uint16_t bc2 = fadeTo(C(255, 245, 160), a);
          float x0 = cx + 6, y0 = cy + cr * 0.6f;
          for (int k = 0; k < 4; k++) {
            const float x1 = x0 + ((k & 1) ? 12 : -10), y1 = y0 + 22;
            cv.lineFast(x0, y0, x1, y1, 2.4f, bc2);
            x0 = x1;
            y0 = y1;
          }
        }
        if (sc.end == 1 && u > E - 1) {  // a puddle grows; then the splash
          const float pw = 70 * seg(E - 1, E);
          cv.ellipse(160, 236, pw, 7 * seg(E - 1, E), to565(mulc(dropC, 0.8f)));
          if (u > E + 0.6f && u < E + 1.4f) {
            const float e = (u - E - 0.6f) / 0.8f;
            for (int k = 0; k < 6; k++) cv.circleFast(160 + (k - 2.5f) * 18 * e, 230 - 40 * e * (1 - e) * 4 * (0.6f + 0.1f * k), 2.2f, to565(dropC));
          }
        }
        if (ub > 0.02f) {
          static const RGB ucs[3][2] = {{C(255, 90, 100), C(255, 240, 240)}, {C(80, 200, 190), C(255, 230, 110)}, {C(160, 120, 255), C(255, 200, 230)}};
          const int pal = sc.iv[1] % 3;
          cv.lineFast(ucx, ucy - 0.55f * ur - 6, ucx, ucy + 12 * ub, 1.4f, to565(inkish()));
          for (int j = 0; j < 8; j++) {
            const float a0 = PI_F + j * PI_F / 8, a1 = a0 + PI_F / 8;
            // 3D: the panels on the right (away from the light) a little darker, a lit inner part on each
            const RGB pc = ucs[pal][j & 1];
            cv.tri(ucx, ucy - 2, ucx + cosf(a0) * ur, ucy + sinf(a0) * ur * 0.55f, ucx + cosf(a1) * ur, ucy + sinf(a1) * ur * 0.55f, to565(pc));
            if (props3d)  // 3D: a soft sheen over the middle of the dome
              cv.tri(ucx, ucy - 2, ucx + cosf(a0) * ur * 0.5f, ucy + sinf(a0) * ur * 0.27f, ucx + cosf(a1) * ur * 0.5f, ucy + sinf(a1) * ur * 0.27f,
                     to565(mixc(pc, C(255, 255, 255), 0.16f)));
          }
          for (int j = 0; j < 8; j++)  // the scalloped rim (3D: a shade darker - the dome curving away)
            cv.circleFast(ucx + cosf(PI_F + (j + 0.5f) * PI_F / 8) * ur, ucy + sinf(PI_F + (j + 0.5f) * PI_F / 8) * ur * 0.55f + 1, ur * 0.05f,
                          to565(props3d ? mulc(ucs[pal][j & 1], 0.8f) : ucs[pal][j & 1]));
        }
        if (sc.end == 0 && u > E + 1.4f) {  // the sun comes out
          const float s = seg(E + 1.4f, E + 2.2f);
          burst(40, 30, 16 * s, 24 * s, 8, 1.4f, cc(255, 200, 60), u);
          ball(40, 30, 12 * s + 0.5f, C(255, 210, 70));
        }
        break;
      }
      case SC_JUGGLE: {
        if (back) break;
        const float dropT = jugDropT();
        static const RGB bs[3] = {C(255, 80, 80), C(255, 210, 60), C(80, 160, 255)};
        const float in = seg(0.3f, 0.9f);
        for (int i = 0; i < 3; i++) {
          float x, y;
          ballPos(i, x, y);
          float a = in;
          if (i != 2 && u > dropT) a *= 1 - seg(dropT, dropT + 0.5f);
          if (a <= 0.02f || y > 260) continue;
          if (props3d && a > 0.98f) ball(x, y, 10, bs[i]);
          else {
            cv.circle(x, y, 10, fadeTo(bs[i], a));
            cv.circleFast(x - 3.5f, y - 3.5f, 2.5f, fadeTo(mixc(bs[i], C(255, 255, 255), 0.6f), a));
          }
        }
        break;
      }
      case SC_GHOST: {
        if (back) break;
        float x, y, s, a;
        ghostPos(x, y, s, a);
        if (a <= 0.02f) break;
        const uint16_t body = fadeTo(C(245, 245, 255), a), face = fadeTo(C(40, 40, 70), a);
        const float r = 26 * s;
        cv.circle(x, y, r, body);
        cv.rrect(x - r, y, 2 * r, r * 1.1f, 0, body);
        for (int i = -1; i <= 1; i++) cv.circle(x + i * r * 0.66f + 3 * sinf(u * 5 + i), y + r * 1.1f, r * 0.34f, body);
        if (u > 7.8f && u < 10) {  // waving
          const float w = sinf(u * 10) * 6;
          cv.circle(x - r - 4, y + 6 + w, r * 0.28f, body);
        } else {
          cv.circle(x - r + 2, y + 10, r * 0.26f, body);
        }
        const bool boo = u > 2.6f && u < 4.5f;
        cv.ellipse(x - r * 0.32f, y - r * 0.12f, r * 0.13f, r * (boo ? 0.24f : 0.19f), face);
        cv.ellipse(x + r * 0.32f, y - r * 0.12f, r * 0.13f, r * (boo ? 0.24f : 0.19f), face);
        if (boo) cv.ellipse(x, y + r * 0.35f, r * 0.2f, r * 0.24f, face);
        else if (u > 4.5f) curve(x - r * 0.25f, y + r * 0.25f, x, y + r * 0.45f, x + r * 0.25f, y + r * 0.25f, 1.6f, face, 6, false);
        if (u > 4.5f) {
          cv.ellipse(x - r * 0.55f, y + r * 0.15f, r * 0.15f, r * 0.08f, fadeTo(C(255, 170, 200), a));
          cv.ellipse(x + r * 0.55f, y + r * 0.15f, r * 0.15f, r * 0.08f, fadeTo(C(255, 170, 200), a));
        }
        break;
      }
      case SC_MOVIE: {
        if (back) break;
        const float in = seg(0, 1.0f) * (1 - seg(14, 14.8f));
        if (in <= 0.01f) break;
        const float by = 262 - 64 * in;
        const uint16_t pop = cc(255, 245, 200);
        for (int i = 0; i < 6; i++) cv.circleFast(232 + i * 7, by - 4 - (i % 2) * 5, 6, pop);
        cv.rrect(226, by, 50, 60, 6, cc(250, 250, 250));
        for (int i = 0; i < 3; i++) cv.rrect(230 + i * 16, by, 8, 60, 0, cc(230, 50, 60));
        float px, py;
        if (popcornPos(px, py)) cv.circleFast(px, py, 5, pop);
        break;
      }
      case SC_LEAF: {
        if (back) break;
        static const RGB lc[3] = {C(240, 150, 40), C(220, 80, 50), C(120, 200, 80)};
        for (int k = 0; k <= (sc.twist == 1 ? 1 : 0); k++) {
          float x, y;
          int where;
          leafPos(x, y, k, &where);
          if (y < -20 || y > 260 || x < -30 || x > 350) continue;
          const float a = where ? 0.3f : (sc.mid == 1 ? 3.0f * u : 0.6f * sinf(u * 2.2f + k));
          RGB c = lc[(sc.iv[1] + k) % 3];
          if (weatherNow == 3) c = mulc(c, 0.7f);  // wet
          const float dx = cosf(a), dy = sinf(a);
          const float sz = where == 2 ? 0.7f : 1.0f;
          cv.ellipse(x, y, 11 * sz * (sc.mid == 1 ? fabsf(dx) * 0.7f + 0.3f : 1), 6 * sz, to565(c));
          cv.lineFast(x - dx * 14 * sz, y - dy * 14 * sz, x + dx * 10 * sz, y + dy * 10 * sz, 0.8f, to565(mulc(c, 0.6f)));
        }
        break;
      }
      case SC_BIRD: {
        if (back) break;
        const float x = lerpf(-30, 350, u / 4.2f), y = 40 + 12 * sinf(u * 2.5f), f = sinf(t * 14) * 8;
        const uint16_t c = to565(inkish());
        cv.lineFast(x - 12, y - f, x, y + 2, 1.6f, c);
        cv.lineFast(x, y + 2, x + 12, y - f, 1.6f, c);
        cv.circleFast(x, y + 2, 2.5f, c);
        break;
      }
      case SC_BEE: {
        if (back) break;
        float x, y;
        bool rest;
        beePos(x, y, &rest);
        if (x < -30 || x > 350) break;
        const float fl = rest ? 0.2f : fabsf(sinf(t * 80));
        const uint16_t wing = to565(mixc(lidCol(), C(210, 235, 255), 0.6f));
        cv.ellipse(x - 3, y - 7 - fl * 2, 5, 3 + fl * 3, wing);
        cv.ellipse(x + 3, y - 7 - fl * 2, 5, 3 + fl * 3, wing);
        cv.ellipse(x, y, 8, 5.5f, cc(250, 200, 40));
        cv.rrect(x - 3, y - 5, 2.2f, 10, 0, cc(40, 30, 20));
        cv.rrect(x + 2, y - 5, 2.2f, 10, 0, cc(40, 30, 20));
        break;
      }
      case SC_FIREFLY: {
        if (back) break;
        const float x = 160 + 120 * sinf(u * 0.7f + sc.v[0] * 6), y = 80 + 50 * sinf(u * 1.1f);
        const float b = 0.5f + 0.5f * sinf(u * 3.3f);
        cv.circleFast(x, y, 9 * b + 2, fadeTo(C(200, 255, 90), 0.25f * b));
        cv.circleFast(x, y, 3.5f, fadeTo(C(230, 255, 140), 0.4f + 0.6f * b));
        break;
      }
      case SC_SPIDER: {
        if (back) break;
        const float y = spiderY();
        const uint16_t c = to565(inkish());
        cv.lineFast(230, 0, 230, y - 6, 0.6f, to565(mixc(lidCol(), inkish(), 0.6f)));
        cv.circle(230, y, 6, c);
        for (int i = 0; i < 4; i++)
          for (int sd = -1; sd <= 1; sd += 2) cv.lineFast(230, y, 230 + sd * 11, y - 5 + i * 3.5f + sinf(t * 8 + i) * 1.5f, 0.7f, c);
        cv.circleFast(228, y - 1.5f, 1.2f, cc(255, 255, 255));
        cv.circleFast(232, y - 1.5f, 1.2f, cc(255, 255, 255));
        break;
      }
      case SC_COMET: {
        if (back) break;
        const float e = seg(0.5f, 2.2f);
        if (e <= 0 || e >= 1) break;
        const float x = lerpf(20, 300, e), y = lerpf(20, 70, e);
        for (int i = 0; i < 6; i++) cv.circleFast(x - i * 9, y - i * 1.6f, 4 - i * 0.55f, fadeTo(C(255, 240, 200), 1 - i / 6.0f));
        star4(x, y, 7, 0.24f, cc(255, 255, 255));
        break;
      }
      case SC_HEART: {
        if (back) break;
        const float y = lerpf(260, 60, seg(0, 3));
        if (u < 3) heart(160 + 20 * sinf(u * 2), y, 11 * (1 + 0.08f * sinf(t * 8)), cc(255, 90, 140));
        else if (u < 3.6f) {
          const float e = (u - 3) / 0.6f;
          for (int i = 0; i < 6; i++) star4(160 + cosf(i * 1.05f) * 24 * e, 60 + sinf(i * 1.05f) * 24 * e, 6 * (1 - e), 0.24f, cc(255, 180, 210));
        }
        break;
      }
      case SC_NOTES: {
        if (back) break;
        static const RGB nc[4] = {C(255, 140, 170), C(255, 210, 90), C(120, 210, 255), C(170, 230, 140)};
        for (int i = 0; i < 5; i++) {
          const float age = u - i * 0.8f;
          if (age < 0 || age > 2.2f) continue;
          const float sd = i % 2 ? 1 : -1;
          noteGlyph(160 + sd * (60 + age * 40), 200 - age * 70, 7, fadeTo(nc[i % 4], 1 - age / 2.2f));
        }
        break;
      }
      case SC_SNAIL: {
        if (back) break;
        const float x = lerpf(-20, 340, u / 7.0f), y = 226;
        const float st = 1 + 0.08f * sinf(t * 5);
        cv.ellipse(x - 2, y + 4, 16 * st, 4, cc(180, 200, 120));
        ringE(x - 4, y - 4, 9, 9, 3, cc(210, 150, 80));
        cv.circle(x - 4, y - 4, 4, cc(170, 110, 60));
        cv.lineFast(x + 10, y, x + 14, y - 10, 0.8f, cc(180, 200, 120));
        cv.circleFast(x + 14, y - 10, 1.6f, cc(60, 60, 60));
        break;
      }
      case SC_SEEDS: {
        if (back) break;
        for (int i = 0; i < 6; i++) {
          const float x = fmodf(40 + i * 53 + u * (18 + i * 3), 360) - 20, y = 40 + 30 * sinf(u * 0.9f + i) + i * 12;
          const uint16_t c = fadeTo(C(245, 245, 250), 0.85f);
          for (int j = 0; j < 6; j++) cv.lineFast(x, y, x + cosf(j * 1.05f) * 5, y - 3 + sinf(j * 1.05f) * 5, 0.6f, c);
          cv.lineFast(x, y, x - 2, y + 8, 0.7f, c);
        }
        break;
      }
      case SC_O_WAVE: {  // a hand waving beside the face
        if (back) break;
        const float wx = 262, wy = sc.fy + 40, sw = 0.55f * sinf(t * 7);
        const RGB hc = light ? C(250, 205, 170) : C(255, 225, 200);
        const float ca = cosf(-1.57f + sw), sa = sinf(-1.57f + sw);
        const float px = wx + ca * 26, py = wy + sa * 26;  // palm centre
        cv.lineFast(wx, wy, px, py, 5, to565(hc));        // the arm
        cv.circle(px, py, 11, to565(hc));
        for (int i = -1; i <= 2; i++) {                   // fingers
          const float fa = -1.57f + sw + i * 0.32f;
          cv.lineFast(px + cosf(fa) * 8, py + sinf(fa) * 8, px + cosf(fa) * 19, py + sinf(fa) * 19, 3, to565(hc));
        }
        const float ta = -1.57f + sw - 1.1f;              // thumb
        cv.lineFast(px + cosf(ta) * 7, py + sinf(ta) * 7, px + cosf(ta) * 15, py + sinf(ta) * 15, 3, to565(hc));
        for (int i = 0; i < 2; i++) curve(px + 22 + i * 6, py - 14, px + 30 + i * 6, py, px + 22 + i * 6, py + 14, 1.2f, fadeTo(inkish(), 0.6f), 6, false);
        break;
      }
      case SC_O_SHAKE: {  // water droplets flying off, like a dog shaking itself dry
        if (back) break;
        const RGB dc = light ? C(90, 150, 230) : C(150, 210, 255);
        for (int i = 0; i < 12; i++) {
          const float a = i * 0.52f + sc.v[0] * 6, age = fracf(u * 1.4f + i * 0.13f);
          const float r = 70 + age * 90;
          const float x = 160 + cosf(a) * r * 1.3f, y = sc.fy + sinf(a) * r * 0.8f;
          drop(x, y, 3.5f * (1 - age * 0.5f), fadeTo(dc, 1 - age));
        }
        break;
      }
      case SC_O_SHRUG: {  // question marks floating up
        if (back) break;
        for (int i = 0; i < 3; i++) {
          const float age = fracf(u * 0.45f + i / 3.0f);
          const float x = (i == 1 ? 262 : 58) + (i == 2 ? 20 : 0) + 6 * sinf(u * 2 + i), y = sc.fy + 30 - age * 90, s = 10;
          const uint16_t c = fadeTo(inkish(), 1 - age);
          curve(x - s * 0.5f, y - s * 0.4f, x, y - s * 1.4f, x + s * 0.5f, y - s * 0.2f, 1.8f, c, 6, false);
          cv.lineFast(x + s * 0.5f, y - s * 0.2f, x, y + s * 0.3f, 1.8f, c);
          cv.circleFast(x, y + s * 0.85f, 2.2f, c);
        }
        break;
      }
      case SC_O_CRAB: {  // a little crab scuttling sideways at the bottom
        if (back) break;
        const float x = 160 + 110 * sinf(u * 0.9f), y = 222;
        const uint16_t rc = cc(240, 90, 70), dk = cc(40, 30, 30);
        for (int i = -1; i <= 1; i++)
          for (int sd = -1; sd <= 1; sd += 2) {
            const float st = sinf(t * 16 + i) * 2;
            cv.lineFast(x + sd * 8, y + i * 3, x + sd * 17, y + 6 + i * 3 + st, 1.2f, rc);
          }
        cv.ellipse(x, y, 13, 8, rc);
        for (int sd = -1; sd <= 1; sd += 2) {  // claws, snapping
          const float op = 0.35f + 0.25f * fabsf(sinf(t * 6 + sd));
          cv.lineFast(x + sd * 10, y - 4, x + sd * 17, y - 13, 1.8f, rc);
          cv.circle(x + sd * 19, y - 15, 5, rc);
          cv.tri(x + sd * 19, y - 15, x + sd * 26, y - 15 - 6 * op, x + sd * 26, y - 15 + 6 * op, to565(lidCol()));
        }
        cv.circleFast(x - 4, y - 9, 2, dk);
        cv.circleFast(x + 4, y - 9, 2, dk);
        break;
      }
      case SC_O_POINT: {  // an arrow pointing out to the side, pulsing
        if (back) break;
        const float k = 6 * sinf(t * 6), x = 262 + k, y = sc.fy;
        const uint16_t c = to565(light ? C(230, 120, 40) : C(255, 190, 90));
        cv.lineFast(x - 22, y, x + 14, y, 3, c);
        cv.tri(x + 26, y, x + 12, y - 10, x + 12, y + 10, c);
        break;
      }
      case SC_O_SWEAT: {  // sweat drops running down and effort lines (pushups)
        if (back) break;
        const RGB dc = light ? C(90, 150, 230) : C(150, 210, 255);
        for (int i = 0; i < 3; i++) {
          const float age = fracf(u * 0.8f + i * 0.33f);
          const float x = (i == 1 ? 250 : 70) + (i == 2 ? -14 : 0), y = sc.fy - 60 + age * 70;
          drop(x, y, 4, fadeTo(dc, 1 - age * 0.7f));
        }
        const float k = fracf(u * 2);
        for (int i = -1; i <= 1; i++) cv.lineFast(160 + i * 16, 22 - k * 4, 160 + i * 18, 12 - k * 4, 1.4f, fadeTo(inkish(), 0.7f * (1 - k)));
        break;
      }
      case SC_O_SPARKLE: {  // sparkles twinkling round the face (a bow, a cute moment)
        if (back) break;
        for (int i = 0; i < 7; i++) {
          const float x = 30 + fracf(i * 0.618f + sc.v[0]) * 260, y = 20 + fracf(i * 0.37f + sc.v[1]) * 190;
          const float tw = sinf(t * 3 + i * 1.7f);
          if (tw > 0.2f) star4(x, y, 7 * tw, 0.24f, to565(light ? C(240, 180, 50) : C(255, 245, 200)));
        }
        break;
      }
      case SC_DICE: {
        if (back) break;
        // falls in from the top-right, three bounces, a little squash on each
        const float x = 272;
        const float fall = seg(0.5f, 1.05f);
        float y = lerpf(-30, 198, fall);
        const float b1 = u > 1.05f && u < 1.6f ? sinf((u - 1.05f) / 0.55f * PI_F) * 26 : 0;
        const float b2 = u > 1.6f && u < 2.05f ? sinf((u - 1.6f) / 0.45f * PI_F) * 11 : 0;
        y -= b1 + b2;
        const float out = seg(7.6f, 8.4f);
        if (out >= 1) break;
        y += out * 200;
        const float sq = (fabsf(u - 1.05f) < 0.06f || fabsf(u - 1.6f) < 0.06f || fabsf(u - 2.05f) < 0.06f) ? 0.85f : 1.0f;
        const float w = 38 / sq, h = 38 * sq;
        const int pip = u < 2.3f ? 1 + ((int)(u * 14)) % 6 : 1 + (int)(sc.v[0] * 5.999f);  // tumbling, then settled
        const uint16_t ink = cc(40, 40, 50);
        cv.rrect(x - w / 2 - 2, y - h / 2 - 2, w + 4, h + 4, 9, ink);
        cv.rrect(x - w / 2, y - h / 2, w, h, 8, cc(250, 248, 240));
        static const int8_t P[7][6][2] = {{{0}}, {{0, 0}}, {{-1, -1}, {1, 1}}, {{-1, -1}, {0, 0}, {1, 1}},
                                          {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}}, {{-1, -1}, {1, -1}, {0, 0}, {-1, 1}, {1, 1}},
                                          {{-1, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {1, 1}}};
        const uint16_t pc = pip == 1 ? cc(220, 50, 60) : ink;
        for (int i = 0; i < pip; i++) cv.circleFast(x + P[pip][i][0] * w * 0.27f, y + P[pip][i][1] * h * 0.27f, pip == 1 ? 5.5f : 3.8f, pc);
        if (u > 2.5f && u < 3.6f && pip == 6)
          for (int i = 0; i < 5; i++) {
            const float a = i * 1.2566f + u * 2, r = 30 + 10 * seg(2.5f, 3.2f);
            star4(x + cosf(a) * r, y + sinf(a) * r, 6 * (1 - seg(3.0f, 3.6f)), 0.24f, cc(255, 220, 90));
          }
        break;
      }
      case SC_RPS: {
        if (back) break;
        const float in = seg(0.3f, 0.9f) * (1 - seg(8.2f, 8.9f));
        if (in < 0.01f) break;
        const float x = 272, y0 = 196 + (1 - in) * 80;
        const uint16_t skin = cc(255, 214, 170), line = cc(150, 100, 70);
        if (u < 3.5f) {  // the fist, shaking on each count
          const float sh = u > 1 ? fabsf(sinf((u - 1) / 0.8f * PI_F)) * 16 : 0;
          const float y = y0 - sh;
          cv.circleFast(x, y, 17, line);
          cv.circleFast(x, y, 15.5f, skin);
          for (int k = 0; k < 3; k++) cv.lineFast(x - 9 + k * 6, y - 6, x - 9 + k * 6, y + 2, 1.2f, line);
          // the count, as dots above
          const int count = u < 1.2f ? 0 : u < 2.0f ? 1 : u < 2.8f ? 2 : 3;
          for (int k = 0; k < count; k++) cv.circleFast(x - 12 + k * 12, y0 - 34, 3.5f, cc(255, 200, 80));
        } else {
          const int pick = (int)(sc.v[1] * 2.999f);  // 0 rock, 1 paper, 2 scissors
          const float pop = 1 + 0.25f * (1 - seg(3.5f, 3.9f));
          if (pick == 0) {
            cv.circleFast(x, y0, 18 * pop, cc(90, 90, 100));
            cv.circleFast(x, y0, 16 * pop, cc(150, 150, 160));
            cv.circleFast(x - 5, y0 - 6, 4 * pop, cc(190, 190, 200));
          } else if (pick == 1) {
            cv.rrect(x - 16 * pop, y0 - 21 * pop, 32 * pop, 42 * pop, 4, cc(120, 120, 130));
            cv.rrect(x - 15 * pop, y0 - 20 * pop, 30 * pop, 40 * pop, 3, cc(250, 250, 245));
            for (int k = 0; k < 4; k++) cv.lineFast(x - 10, y0 - 12 + k * 8, x + 10, y0 - 12 + k * 8, 1.0f, cc(160, 170, 200));
          } else {
            const uint16_t bl = cc(200, 205, 215), hd = cc(230, 70, 80);
            cv.lineFast(x - 4, y0 + 4, x + 14 * pop, y0 - 22 * pop, 3.0f, bl);
            cv.lineFast(x + 4, y0 + 4, x - 14 * pop, y0 - 22 * pop, 3.0f, bl);
            ring(x - 8, y0 + 12, 7, 2.2f, hd);
            ring(x + 8, y0 + 12, 7, 2.2f, hd);
          }
        }
        break;
      }
      case SC_GIFT: {
        if (back) break;
        const float in = seg(0.2f, 1.2f) * (1 - seg(9.2f, 9.9f));
        if (in < 0.01f) break;
        const float x = 270, y = 200 + (1 - in) * 80;
        const float wob = u > 1.6f && u < 3.6f ? sinf(u * 22) * 3 * (1 + fracf(u * 1.5f)) : 0;
        const uint16_t box = cc(220, 60, 80), ribbon = cc(255, 205, 70), dark = cc(150, 30, 50);
        cv.rrect(x - 20 + wob, y - 14, 40, 30, 4, dark);
        cv.rrect(x - 19 + wob, y - 13, 38, 28, 3, box);
        cv.rrect(x - 3 + wob, y - 13, 6, 28, 1, ribbon);
        // the lid: on, then it pops and flies off
        const float pop = seg(3.6f, 4.4f);
        const float lx = x + wob + pop * 40, ly = y - 18 - pop * 70;
        if (pop < 1) {
          cv.rrect(lx - 23, ly - 6, 46, 10, 3, dark);
          cv.rrect(lx - 22, ly - 5, 44, 8, 2, box);
          cv.rrect(lx - 3, ly - 5, 6, 8, 1, ribbon);
          ring(lx - 7, ly - 9, 5, 2, ribbon);
          ring(lx + 7, ly - 9, 5, 2, ribbon);
        }
        // the burst: sparkles and confetti out of the box
        if (u > 3.6f && u < 8.5f) {
          static const uint8_t col[6][3] = {{255, 120, 120}, {255, 200, 80}, {120, 220, 140}, {110, 180, 255}, {200, 140, 255}, {255, 150, 210}};
          const float t = u - 3.6f;
          for (int i = 0; i < 14; i++) {
            const float a = -PI_F / 2 + (fracf(i * 0.618f + sc.v[3]) - 0.5f) * 2.2f;
            const float sp = 70 + 50 * fracf(i * 0.37f);
            const float px = x + cosf(a) * sp * t, py = y - 16 + sinf(a) * sp * t + 45 * t * t;
            const float fade = 1 - clampf((t - 2.5f) / 2.0f, 0, 1);
            if (fade <= 0 || py > 240) continue;
            if (i % 3 == 0) star4(px, py, 6 * fade, 0.24f, cc(255, 240, 160));
            else cv.rrect(px - 2.5f, py - 2.5f, 5, 5, 1, fadeTo(C(col[i % 6][0], col[i % 6][1], col[i % 6][2]), fade));
          }
        }
        break;
      }
      case SC_WINK: {
        if (back) break;
        if (u > 0.4f && u < 1.6f) star4(sc.v[1] < 0.5f ? 110 : 210, sc.fy - 40, 9 * sinf((u - 0.4f) / 1.2f * PI_F), 0.24f, cc(255, 245, 190));
        break;
      }
      case SC_SELFIE: {
        if (back) break;
        const float in = seg(0.2f, 0.9f) * (1 - seg(9.4f, 10.4f));
        if (in > 0.01f) {
          const float dy = 260 * (1 - in);
          const uint16_t fr = to565(light ? C(60, 60, 75) : C(150, 150, 170));
          cv.rrect(34, 10 + dy, 252, 6, 3, fr);
          cv.rrect(34, 224 + dy, 252, 6, 3, fr);
          cv.rrect(34, 10 + dy, 6, 220, 3, fr);
          cv.rrect(280, 10 + dy, 6, 220, 3, fr);
          cv.rrect(140, 12 + dy, 40, 6, 3, to565(lidCol()));
          const uint16_t br = to565(inkish());
          const float bx[2] = {52, 268}, byy[2] = {28, 212};
          for (int i = 0; i < 2; i++)
            for (int j = 0; j < 2; j++) {
              const float sx = i ? -1 : 1, sy = j ? -1 : 1;
              cv.lineFast(bx[i], byy[j] + dy, bx[i] + sx * 12, byy[j] + dy, 1, br);
              cv.lineFast(bx[i], byy[j] + dy, bx[i], byy[j] + dy + sy * 12, 1, br);
            }
          for (int i = 0; i < 3; i++)
            if (u > 2.0f + i * 0.8f && u < 4.4f) cv.circle(144 + i * 16, 32 + dy, 4, to565(i == 2 ? C(255, 90, 90) : C(255, 255, 255)));
        }
        if (u > 4.4f && u < 5.0f) {  // the flash
          const float a = 1 - (u - 4.4f) / 0.6f;
          cv.rrect(0, 0, W, H, 0, fadeTo(C(255, 255, 255), a * a));
        }
        if (u > 5.4f && u < 9.6f) {  // the polaroid
          const float sl = seg(5.4f, 6.0f) * (1 - seg(9.0f, 9.6f));
          const float x = lerpf(330, 236, sl), y = 150;
          cv.rrect(x, y, 64, 74, 4, cc(250, 250, 248));
          cv.rrect(x + 6, y + 6, 52, 48, 2, to565(k.s.bg));
          cv.circleFast(x + 22, y + 28, 6, to565(k.eyeC));
          cv.circleFast(x + 42, y + 28, 6, to565(k.eyeC));
        }
        break;
      }
      default: drawScene2(back); break;  // face_scenes_more.h
    }
  }
#include "face_scenes_more.h"

