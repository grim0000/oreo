// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
#include <Arduino.h>
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "esp_core_dump.h"  // a crash saved in flash: logged at the next boot
#include "soc/usb_serial_jtag_reg.h"
#include <WiFi.h>
#include <esp_http_server.h>
#include "esp_camera.h"
#include "img_converters.h"
#include <lwip/sockets.h>
#include "human_face_detect_msr01.hpp"
#include "human_face_detect_mnp01.hpp"  // the second stage: eyes / nose / mouth (face_id.h)
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <LovyanGFX.hpp>
#include <Adafruit_SSD1306.h>
#include <Adafruit_VL53L0X.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <Update.h>
#include "xlog.h"
#include <esp_wifi.h>
#include <esp_netif_sta_list.h>
#include <ping/ping_sock.h>

// The back legs lying down (rest, sit, elegant sit): R4 R3 L3 L4, low enough that the body rests on its
// stomach. GET /cmd?backdown=80,38,84,48 (saved)
int REST_BACK[4] = {72, 44, 78, 56};
extern int tzOffsetSec;  // the clock's timezone (seconds from UTC, defined below; the face engine reads it)
#include "servo_map.h"  // which PCA9685 channel drives which servo (/servomap, saved)
#include "movement-sequences.h"
#include "emotes_more.h"  // more emotes, on the spot
#include "emotes_lib.h"   // the emote library: ~100 more, keyframes

// --- WIFI & AP CREDENTIALS ---
#include <DNSServer.h>
#include "controller_html_gz.h"
#include "audio.h"
#include "talk.h"  // phone mic -> robot speaker (WebSocket /talk, and a small HTTPS talk page)

// Your WiFi: set it on the robot's setup page (join the "Oreo" hotspot, open 192.168.4.1) - it's remembered.
// (Optional) a network to try when none was saved yet:
const char* ssid = "";
const char* password = "";

const char* ap_ssid = "Oreo";
const char* ap_password = ""; // Open network
const byte DNS_PORT = 53;
DNSServer dnsServer;

// I2C Pins
#define I2C_SDA 1
#define I2C_SCL 2

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40, Wire);
Adafruit_SSD1306 oled(128, 64, &Wire, -1);


// Battery Pin
#define BATT_PIN 3
#define TOUCH_PIN 19  // 38/39/40 belong to the board's microSD slot

// microSD slot, wired on the board in 1-bit SDMMC mode (card must be FAT32 - exFAT is off in this core)
#define SD_CLK 39
#define SD_CMD 38
#define SD_D0  40
int sdSizeMB = -1;  // -1 = no card / not readable, else the card's size

// --- Animation Globals & Mapping ---
int frameDelayFB = 200; // Slower walk for more stability (Front/Back)
int frameDelayLR = 65;  // Delay for Left/Right turning (the on-the-spot turn's frame; much shorter leaves ~2 servo writes a frame: jerky)
int walkCycles = 10;
String currentCommand = "";
unsigned long previousMillis = 0;
const long interval = 50;


// Globals for native smooth walk (CPG)
int sw_servos[8] = {L1, L2, L3, L4, R1, R2, R3, R4};
float sw_base[8] = {23.0, 24.0, 20.0, 117.0, 23.0, 130.0, 107.0, 7.0};  
float sw_amp_mapped[8] = {20.0, -15.0, 20.0, -15.0, -20.0, 15.0, -20.0, 15.0};  
float sw_phase[8] = {0.0, -1.5708, 3.14159, 1.5708, 3.14159, 1.5708, 0.0, -1.5708};
float sw_frequency = 0.012;
bool sw_rect[8] = {false};  // a knee that only lifts (half a cosine: up while its foot swings forward, flat in stance)
bool sw_snap[8] = {false};  // ...and lifts snappily: sqrt of that half cosine (up fast, held, down fast)
// The diagonal trot ("smooth_trot" / "_back", 2026-10-29): tuned in the MuJoCo sim on Oreo's model
// (1.5 Hz, 32 deg stride -> 8.6 cm/s, straight). A diagonal trot around the stand itself - hips swing the feet
// forward / back, knees kick the foot out and up only while it swings forward.
// trotSpeed (page slider, saved) = the rhythm: 0 -> 0.5 Hz ... 1 -> 2.0 Hz (sim best 1.5 Hz = 0.67).
float trotSpeed = 0.67f, trotStride = 32, trotLift = 25;  // (lift + hip swing: page sliders, saved - the sim's 45 was too much on the real legs)
float trotPhaseOff = 0;  // keeps the legs where they are when the speed changes mid-walk (no jump in the cycle)
inline float trotFreq() { return 6.2831853f * (0.5f + 1.5f * trotSpeed) / 1000.0f; }  // rad per ms
// The balanced walk ("smooth_balanced" / "_back"): knee lift and hip stride in degrees (/gait?ballift= &balstride=, saved)
float balLift = 24, balStride = 18;
double sw_innerTime = 0;       // CPG clock (ms), advanced at cpgSpeed x real time
// Elegant sit ("sit_elegant"): the front hips and front legs (R1 R2 L1 L2) at their calibrated MIN, the
// back four exactly as in the rest pose (R4 64, R3 50, L3 72, L4 64). R1 R2 L1 L2 R4 R3 L3 L4. Tune with
// GET /cmd?sitpose=... (saved). (The plain "sit" is the dead emote's pose: standing front, back legs down.)
int SIT_POSE[8] = {62, 80, -18, 76, 64, 50, 72, 64};
// ---- The steady gait + walking straight ----
// "smooth_steady" / "smooth_steady_back": the tuned forward / backward trot (walkFwd / walkBack), with
//  - a soft start: the strides grow from 0 to full over ~1.2 s (no jolt into the gait), and
//  - a soft stop: let go and the strides shrink to nothing over 0.5 s, then it stands.
// Walking straight (every straight CPG walk: smooth walk, Explore's straight walking, the steady gait):
// the heading when the walk starts is kept with the gyro (MPU-6050) - drifting right shortens the left
// side's strides a little (the way the turn presets steer), drifting left the right side's; a slow
// integral takes out a steady pull (one leg weaker). Needs the gyro's left / right sign (yawLeftSign),
// learned from a turn and saved.
bool gaitSteadyDpad = false;      // the steady gait everywhere: D-pad, RC, voice, Explore's straight walk (page switch, saved)
bool headHoldOn = true;           // walking straight with the gyro, on / off (page switch, saved)
volatile bool headHoldActive = false;  // it's steering a walk right now (the page shows it)
// the steady gait's soft start / stop for this walk? (its own buttons, or every straight walk when switched on)
bool isSteady(const String& c) {
  return c.startsWith("smooth_steady") || (gaitSteadyDpad && (c == "smooth_walk" || c == "smooth_walk_back")) ||
         c == "smooth_side_left" || c == "smooth_side_right" || c.startsWith("smooth_balanced") || c.startsWith("smooth_trot");
}
// The steady level-gait ("smooth_steady_level" / "_back"): the steady gait, plus the body kept parallel
// to the ground with the IMU (otherwise it walks nose-down). The tilt (accelerometer, smoothed) slowly bends
// the front knees up and the back knees down (which way each knee lowers its corner: from the stand -> lying-down
// poses). +-15 deg at most; what it learned stays for the next walk.
float levelTrim = 0;                    // the pitch that counts as level, deg (GET /gait?lvltrim=, saved)
volatile float levelPitch = 0, levelCorr = 0;  // (GET /gait: the smoothed pitch, + = nose down; the knee correction)
float levelOff[8] = {0};                // the knee offsets now (sw_ order), eased out by the soft stop
float headHoldKp = 0.035f;        // stride change per degree off course (0.035: 10 deg -> 35 %)
float headHoldKi = 0.010f;        // per degree-second
volatile float headErrNow = 0, headCorrNow = 0;  // (GET /gait: what the heading hold does now)
float cpgSpeed = 1.0f;
// The nav walk ("smooth_nav", /cmd?nav=vx,wz): the forward walk skid-steered by a speed (m/s) and a turn rate (rad/s,
// + = left) - each side's hips scaled -1..1, backward = played backwards. A caller sends (vx, wz), the walk does the rest.
float navVx = 0, navWz = 0;
const float NAV_VX_FULL = 0.075f, NAV_WZ_FULL = 0.6f, NAV_GATE_FULL = 0.05f;          // gait speed factor (Explore slows down near obstacles)
volatile float rcSpeed = 1.0f;  // walk mode: RC throttle (slide on the forward button), 0.5 .. 1.5; 1 outside RC
unsigned long sw_oldMillis = 0;
float cpgLastBase[8], cpgLastAmp[8], cpgLastPhase[8], cpgLastFreq = 0;  // the gait running last pass (live cross-fade)
bool cpgLastValid = false;
// Sideways walk balance (/gait?sidebal=, saved): + = the pair stepping like the forward walk pushes less and the
// reversed pair more, - = the other way. -0.6 .. 0.6
float sideBal = 0;
// Sideways walk foot lift (/gait?sidelift=, saved): the knees' swing x this (the forward walk's lift drags the feet
// sideways across the floor), the hips' stride x 0.75, the rhythm 15 % slower. 1.0 .. 2.2
float sideLift = 1.0f;

// Walk presets: Forward (user-tuned), Backward (user-tuned), Smooth Left/Right, Fwd-Left/Right
// S0=FLH, S1=BLH, S2=FLK, S3=BLK, S4=FRH, S5=BRH, S6=FRK, S7=BRK
const float WALK_FWD_BASE[8]  = {44.0, 24.0, 41.0, 117.0, 2.0, 130.0, 93.0, 7.0};
const float WALK_FWD_AMP[8]   = {-20.0, 20.0, -17.0, 17.0, 20.0, -20.0, 17.0, -17.0};
const float WALK_FWD_PHASE[8] = {0.0, -1.5708, 3.14159, 1.5708, 3.14159, 1.5708, 0.0, -1.5708};
const float WALK_FWD_FREQ = 0.012;

// Backward walk: user-tuned (freq=-0.012, bkBase=-20, S3 amp+1, S6 off+6, S7 off-9)
const float WALK_BACK_BASE[8] = {44.0, 24.0, 41.0, 97.0, 2.0, 130.0, 92.0, 18.0};
const float WALK_BACK_AMP[8]  = {-20.0, 20.0, -17.0, 18.0, 20.0, -20.0, 17.0, -17.0};
const float WALK_BACK_FREQ = -0.012;

// Smooth turns: reduce inner hip amps to 50%
const float WALK_LEFT_AMP[8]   = {-10.0, 10.0, -17.0, 17.0, 20.0, -20.0, 17.0, -17.0};
const float WALK_RIGHT_AMP[8]  = {-20.0, 20.0, -17.0, 17.0, 10.0, -10.0, 17.0, -17.0};

// Forward diagonals: reduce inner hip amps to 75%
const float WALK_FWD_LEFT_AMP[8]  = {-15.0, 15.0, -17.0, 17.0, 20.0, -20.0, 17.0, -17.0};
const float WALK_FWD_RIGHT_AMP[8] = {-20.0, 20.0, -17.0, 17.0, 15.0, -15.0, 17.0, -17.0};

// Sharp turns (CPG skid-steer): inner hips are REVERSED (negative stride) so the inner side
// walks backward while the outer side walks forward -> tight in-place turn.
// A 20% forward inner stride only produced a slightly tighter arc than the 50% smooth turn.
// Inner-side knees borrow the user-tuned backward-walk offsets (BLK 97/amp18, FRK 92, BRK 18)
// since those legs are now pushing backward.
const float WALK_SHARP_LEFT_BASE[8]  = {44.0, 24.0, 41.0, 97.0, 2.0, 130.0, 93.0, 7.0};
const float WALK_SHARP_LEFT_AMP[8]   = {12.0, -12.0, -17.0, 18.0, 20.0, -20.0, 17.0, -17.0};
const float WALK_SHARP_RIGHT_BASE[8] = {44.0, 24.0, 41.0, 117.0, 2.0, 130.0, 92.0, 18.0};
const float WALK_SHARP_RIGHT_AMP[8]  = {-20.0, 20.0, -17.0, 17.0, -12.0, 12.0, 17.0, -17.0};
// Slightly slower cycle gives the feet more traction while skidding around
const float WALK_SHARP_FREQ = 0.010;
// Level sharp turn (/gait?turnlevel=1, saved, default on): every knee at the forward walk's height. The inner
// legs' backward-walk knee offsets (up to 20 deg lower on one side) lean the body over for the whole turn.
// 0 = the original skid-steer, for an A/B.
bool turnLevel = true;

// Creator-style CPG turns: derived from Creator's frame sequences
// All hips swing in SAME rotational direction (unlike forward walk where they oppose)
// Phase: trot diagonal timing with knees leading hips by π/2
// Turn Left: S0(FLH)23→64, S1(BLH)39→78, S4(FRH)23→62, S5(BRH)115→156
const float CREATOR_LEFT_BASE[8]  = {43.5, 58.5, 33.0, 103.5, 42.5, 135.5, 92.5, 21.0};
const float CREATOR_LEFT_AMP[8]   = {20.0, 20.0, 25.0, 25.0, 20.0, 20.0, 25.0, 25.0};
// 4-beat crawl phases for sequential twisting: S0(3.14), S1(1.57), S2(-1.57), S3(3.14), S4(0), S5(-1.57), S6(0), S7(1.57)
const float CREATOR_LEFT_PHASE[8] = {3.14159, 1.5708, -1.5708, 3.14159, 0.0, -1.5708, 0.0, 1.5708};

const float CREATOR_RIGHT_BASE[8]  = {2.5, 19.5, 33.0, 103.5, 3.5, 94.5, 92.5, 21.0};
const float CREATOR_RIGHT_AMP[8]   = {-20.0, -20.0, 25.0, 25.0, -20.0, -20.0, 25.0, 25.0};
const float CREATOR_RIGHT_PHASE[8] = {3.14159, 1.5708, -1.5708, 3.14159, 0.0, -1.5708, 0.0, 1.5708};

// The walks in use: the built-in ones above, or ones saved from the page (Smooth walk settings >
// "Save as forward / backward walk"), kept in NVS ("walkF" / "walkB"). The forward walk's base
// offsets (a servo horn that slipped) carry over to the turns and diagonals.
struct WalkSet { float base[8], amp[8], phase[8], freq; };
WalkSet walkFwd, walkBack;
volatile uint8_t walkSaveReq = 0;  // flash work for applyPendingPrefs: 1 save fwd, 2 save back, 4 / 8 forget them
void walkDefaults(bool fwd, bool back) {
  for (int i = 0; i < 8; i++) {
    if (fwd) { walkFwd.base[i] = WALK_FWD_BASE[i]; walkFwd.amp[i] = WALK_FWD_AMP[i]; walkFwd.phase[i] = WALK_FWD_PHASE[i]; }
    if (back) { walkBack.base[i] = WALK_BACK_BASE[i]; walkBack.amp[i] = WALK_BACK_AMP[i]; walkBack.phase[i] = WALK_FWD_PHASE[i]; }
  }
  if (fwd) walkFwd.freq = WALK_FWD_FREQ;
  if (back) walkBack.freq = WALK_BACK_FREQ;
}

void sideGaitFill(bool left);  // (below the calibration: it reads LEG_MIN0 / MID0 / MAX0)

void balancedFill();  // (below the calibration)
void trotFill();
void loadWalkPreset(int preset) {
  for (int i = 0; i < 8; i++) sw_rect[i] = sw_snap[i] = false;
  if (preset == 14 || preset == 15) {
    trotFill();
    sw_frequency = (preset == 14 ? 1 : -1) * trotFreq();  // backward: the same gait, time reversed
    return;
  }
  if (preset == 12 || preset == 13) {
    balancedFill();
    sw_frequency = (preset == 12 ? 1 : -1) * fabsf(walkFwd.freq);  // backward: the same gait, time reversed
    return;
  }
  if (preset == 10 || preset == 11) {
    sideGaitFill(preset == 10);
    sw_frequency = walkFwd.freq;
    return;
  }
  for (int i = 0; i < 8; i++) {
    if (preset == 8) {
      sw_base[i] = CREATOR_LEFT_BASE[i];
      sw_amp_mapped[i] = CREATOR_LEFT_AMP[i];
      sw_phase[i] = CREATOR_LEFT_PHASE[i];
    } else if (preset == 9) {
      sw_base[i] = CREATOR_RIGHT_BASE[i];
      sw_amp_mapped[i] = CREATOR_RIGHT_AMP[i];
      sw_phase[i] = CREATOR_RIGHT_PHASE[i];
    } else if (preset == 4 || preset == 5) {
      // (Explore's turn - the original skid-steer; the page's Sharp L / R buttons use
      // sharpTurnUpdate(): the creator's turn, smooth)
      sw_phase[i] = walkFwd.phase[i];
      sw_base[i] = turnLevel ? walkFwd.base[i]
                             : ((preset == 4) ? WALK_SHARP_LEFT_BASE[i] : WALK_SHARP_RIGHT_BASE[i]) + (walkFwd.base[i] - WALK_FWD_BASE[i]);
      sw_amp_mapped[i] = (preset == 4) ? WALK_SHARP_LEFT_AMP[i] : WALK_SHARP_RIGHT_AMP[i];
    } else if (preset == 1) {
      sw_phase[i] = walkBack.phase[i];
      sw_base[i] = walkBack.base[i];
      sw_amp_mapped[i] = walkBack.amp[i];
    } else if (preset == 10 || preset == 11) {
      continue;  // sideways: sideGaitFill() below sets all eight
    } else {
      sw_phase[i] = walkFwd.phase[i];
      sw_base[i] = walkFwd.base[i];
      switch (preset) {
        case 2: sw_amp_mapped[i] = WALK_LEFT_AMP[i]; break;
        case 3: sw_amp_mapped[i] = WALK_RIGHT_AMP[i]; break;
        case 6: sw_amp_mapped[i] = WALK_FWD_LEFT_AMP[i]; break;
        case 7: sw_amp_mapped[i] = WALK_FWD_RIGHT_AMP[i]; break;
        default: sw_amp_mapped[i] = walkFwd.amp[i]; break;
      }
    }
  }
  if (preset == 1) sw_frequency = walkBack.freq;
  else if (preset == 4 || preset == 5) sw_frequency = WALK_SHARP_FREQ;
  else sw_frequency = walkFwd.freq;
}
float emoteDelayMultiplier = 3.5;
int walkHeadTilt = 80; // Angle to tilt head back during walks to shift CoG

// Map from original ServoName enum (R1=0, R2=1, L1=2, L2=3, R4=4, R3=5, L3=6, L4=7)
// to our physical pins:
// (mappedPins: servo_map.h - set on the servo mapping page)

// Our calibrated Min values
float ourMin[8] = {62.0, 80.0, -18.0, 76.0, 150.0, -18.0, 150.0, -18.0};
// Our calibrated Mid values
float ourMid[8] = {23.0, 115.0, 23.0, 39.0, -8.0, 107.0, 20.0, 132.0};
// Our safe Max values
float ourMax[8] = {-16.0, 150.0, 64.0, 0.0, -17.0, 130.0, -18.0, 140.0};
// Original robot's Standing angles
const int origStand[8] = {135, 45, 45, 135, 0, 180, 0, 180};

// Track current angles for smooth interpolation. At power-up the legs are limp (nothing is sent to
// them) and the robot is lying down, so start from the lying pose: the first smooth move then starts
float currentAngle[8] = {-16.0, 156.0, 64.0, 0.0, 64.0, 50.0, 72.0, 64.0};

// The built-in calibration: every pose, walk and emote was made on it. setServoAngle maps each leg
// angle piecewise (min..mid, mid..max) onto the calibration page's saved values, so a re-calibrated
// leg changes every move, not only the stand. currentAngle stays in built-in angles.
const float LEG_MIN0[8] = {62.0, 80.0, -18.0, 76.0, 150.0, -18.0, 150.0, -18.0};
const float LEG_MID0[8] = {23.0, 115.0, 23.0, 39.0, -8.0, 107.0, 20.0, 132.0};
const float LEG_MAX0[8] = {-16.0, 150.0, 64.0, 0.0, -17.0, 130.0, -18.0, 140.0};
static float calPiece(float a, float a0, float aMin, float aMax, float b0, float bMin, float bMax) {
  const bool minSide = (a - a0) * (aMin - a0) > 0;
  const float aE = minSide ? aMin : aMax, bE = minSide ? bMin : bMax;
  if (fabsf(aE - a0) < 0.5f) return b0 + (a - a0);
  return b0 + (a - a0) * (bE - b0) / (aE - a0);
}
float calMap(int ch, float a) { return calPiece(a, LEG_MID0[ch], LEG_MIN0[ch], LEG_MAX0[ch], ourMid[ch], ourMin[ch], ourMax[ch]); }
float calUnmap(int ch, float r) { return calPiece(r, ourMid[ch], ourMin[ch], ourMax[ch], LEG_MID0[ch], LEG_MIN0[ch], LEG_MAX0[ch]); }

// The sideways walk (body slides left / right, still facing ahead): the forward walk turned 90 degrees.
// The calibration: a hip's MID = the thigh diagonal, MAX = straight out to the side (lying down), MIN = along the
// body (the elegant sit's tucked legs); a knee's MIN = the foot up. The forward walk swings every hip between the
// diagonal and straight out (MID..MAX) with the knee lifting in step. Walking sideways, the body's left (or right)
// side is the "front": each leg does what the leg in that place does in the forward walk - its offset, stride,
// timing and knee lift - with its hip swinging toward MIN (along the body) instead of MAX. Built from the tuned
// forward walk, so a re-tuned forward walk carries over. sideLift scales the knee lift, sideBal the left / right
// stride balance (like the forward walk's trim: it turned while sliding).
// The balanced walk: a true diagonal trot (FL + BR together, then FR + BL), every leg with the SAME numbers from its
// own calibration - the same thigh angle, stride, knee bend and lift - so it's symmetric front / back and left / right.
// The forward walk was tuned leg by leg (front knees ~21 deg bent with a big lift, back ~15), so its back feet
// barely lift and the nose dips. Here the knee lifts only while its foot swings forward (sw_rect), the stance
// foot stays down. The IMU keeps it level (the level-gait's knee offsets) and straight (the heading hold).
void balancedFill() {
  static const int HIP[4] = {0, 1, 4, 5}, KNEE[4] = {2, 3, 6, 7};      // sw_ indices: FL, BL, FR, BR
  static const int EH[4] = {L1, L2, R1, R2}, EK[4] = {L3, L4, R3, R4};
  static const bool FRONT[4] = {true, false, true, false};
  static const float PH[4] = {0.0f, 3.14159265f, 3.14159265f, 0.0f};  // the two diagonal pairs half a cycle apart
  const float H0 = 18, K0 = 15;  // the thighs this far toward "out to the side" from the diagonal, the knees bent this much
  auto sgn = [](float a, float b) { return a > b ? 1.0f : -1.0f; };
  for (int k = 0; k < 4; k++) {
    const int eh = EH[k], ek = EK[k];
    const float sOut = sgn(LEG_MAX0[eh], LEG_MID0[eh]), sLift = sgn(LEG_MIN0[ek], LEG_MID0[ek]);
    // the foot's forward travel u = stride * sin(wt + ph): a front thigh moving out moves its foot back, a back one forward
    sw_base[HIP[k]] = LEG_MID0[eh] + sOut * H0;
    sw_amp_mapped[HIP[k]] = sOut * (FRONT[k] ? -1.0f : 1.0f) * balStride;
    sw_phase[HIP[k]] = PH[k] - 1.5707963f;
    // lifted while u grows (cos > 0): the swing forward
    sw_base[KNEE[k]] = LEG_MID0[ek] + sLift * K0;
    sw_amp_mapped[KNEE[k]] = sLift * balLift;
    sw_phase[KNEE[k]] = PH[k];
    sw_rect[KNEE[k]] = true;
  }
}

// The diagonal trot: balancedFill's trot centred on the stand (no thigh offset, knees straight), a bigger stride and
// a snappy lift that swings the foot out to the side (a knee's MIN = foot up).
void trotFill() {
  static const int HIP[4] = {0, 1, 4, 5}, KNEE[4] = {2, 3, 6, 7};      // sw_ indices: FL, BL, FR, BR
  static const int EH[4] = {L1, L2, R1, R2}, EK[4] = {L3, L4, R3, R4};
  static const bool FRONT[4] = {true, false, true, false};
  static const float PH[4] = {0.0f, 3.14159265f, 3.14159265f, 0.0f};  // FL + BR, then FR + BL
  auto sgn = [](float a, float b) { return a > b ? 1.0f : -1.0f; };
  for (int k = 0; k < 4; k++) {
    const int eh = EH[k], ek = EK[k];
    const float sOut = sgn(LEG_MAX0[eh], LEG_MID0[eh]), sLift = sgn(LEG_MIN0[ek], LEG_MID0[ek]);
    sw_base[HIP[k]] = LEG_MID0[eh];
    sw_amp_mapped[HIP[k]] = sOut * (FRONT[k] ? -1.0f : 1.0f) * trotStride;
    sw_phase[HIP[k]] = PH[k] - 1.5707963f + trotPhaseOff;
    sw_base[KNEE[k]] = LEG_MID0[ek];
    sw_amp_mapped[KNEE[k]] = sLift * trotLift;
    sw_phase[KNEE[k]] = PH[k] + trotPhaseOff;
    sw_rect[KNEE[k]] = sw_snap[KNEE[k]] = true;
  }
}

// The trot's speed changed: the new rhythm picks up exactly where the legs are (the phase is freq x clock + offset)
void trotSetSpeed(float s) {
  s = constrain(s, 0.0f, 1.0f);
  const float fOld = trotFreq();
  trotSpeed = s;
  if (currentCommand.startsWith("smooth_trot")) {
    const float way = currentCommand.endsWith("_back") ? -1.0f : 1.0f;
    trotPhaseOff = fmodf(trotPhaseOff + (float)fmod(way * (fOld - trotFreq()) * sw_innerTime, 6.283185307), 6.2831853f);
    loadWalkPreset(currentCommand.endsWith("_back") ? 15 : 14);
  }
}

void sideGaitFill(bool left) {
  static const int HIP[4] = {0, 1, 4, 5}, KNEE[4] = {2, 3, 6, 7};  // sw_ indices, legs FL, BL, FR, BR
  static const int EH[4] = {L1, L2, R1, R2}, EK[4] = {L3, L4, R3, R4};  // their servos (LEG_*0 index)
  // the forward-walk leg each one plays: going left FL->FR, BL->FL, FR->BR, BR->BL; going right FL->BL ...
  static const int ROLE_L[4] = {2, 0, 3, 1}, ROLE_R[4] = {1, 3, 0, 2};
  auto sgn = [](float a, float b) { return a > b ? 1.0f : -1.0f; };
  for (int k = 0; k < 4; k++) {
    const int r = left ? ROLE_L[k] : ROLE_R[k];
    const int ehr = EH[r], ekr = EK[r], eh = EH[k], ek = EK[k];
    // the forward walk's leg r: hip in "toward the side" units, knee in "toward lift" units
    const float sOutR = sgn(LEG_MAX0[ehr], LEG_MID0[ehr]), sLiftR = sgn(LEG_MIN0[ekr], LEG_MID0[ekr]);
    const float hipOff = (walkFwd.base[HIP[r]] - LEG_MID0[ehr]) * sOutR, hipAmp = walkFwd.amp[HIP[r]] * sOutR;
    const float kneeOff = (walkFwd.base[KNEE[r]] - LEG_MID0[ekr]) * sLiftR, kneeAmp = walkFwd.amp[KNEE[r]] * sLiftR;
    // on leg k: the hip toward MIN (along the body), the knee toward lift as before
    const float sAlong = sgn(LEG_MIN0[eh], LEG_MID0[eh]), sLift = sgn(LEG_MIN0[ek], LEG_MID0[ek]);
    float trim = 1;
    if (r < 2) trim = 1 - max(sideBal, 0.0f); else trim = 1 + min(sideBal, 0.0f);  // (the role's left / right side)
    sw_base[HIP[k]] = LEG_MID0[eh] + sAlong * hipOff;
    sw_amp_mapped[HIP[k]] = sAlong * hipAmp * trim;
    sw_phase[HIP[k]] = walkFwd.phase[HIP[r]];
    sw_base[KNEE[k]] = LEG_MID0[ek] + sLift * kneeOff;
    sw_amp_mapped[KNEE[k]] = sLift * kneeAmp * sideLift;
    sw_phase[KNEE[k]] = walkFwd.phase[KNEE[r]];
  }
}

// The calibration page moved this pin (millis): every other mover keeps off it for 5 s (otherwise they fight
// the page and the servos jitter while setting min / mid / max). servoRawNow: each pin's last angle.
unsigned long calPinMs[16] = {0};
float servoRawNow[16];
bool servoRawKnown[16] = {false};
inline bool calHolding(int pin) { return calPinMs[pin] != 0 && millis() - calPinMs[pin] < 5000; }


bool pressingCheck(String cmd, int ms) {
  unsigned long start = millis();
  while(millis() - start < ms) {
    if (currentCommand != cmd) return false;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return true;
}






bool legsPowered = false;  // false until the first leg command after power-up (the legs are limp)
// Whether we know where the legs are. At power-up we don't (limp: it may lie, or still stand on
// gear friction after a reboot), so the first move goes straight to its target - no guessed start
// pose (guessing "lying" would make a standing robot drop and pop up again).
// After "rest" the legs are relaxed but known: lying.
bool legsPoseKnown = false;
// Lying down needs no holding power: switch the 8 leg servos off (holding, they draw current all the time).
// The next move starts from the lying pose (smooth first stand).
void prefPut(const char* key, int32_t val, bool isBool);  // below
bool legsLyingSaved = false;  // NVS "legsLying": it last lay down (kept over a power-off)
void legsRelax() {
  if (!legsLyingSaved) { legsLyingSaved = true; prefPut("legsLying", 1, true); }  // (a camera-safe flash write, later)
  for (int i = 0; i < 8; i++) pwm.setPWM(mappedPins[i], 0, 4096);  // full off
  const float lying[8] = {-16, 156, 64, 0, 64, 50, 72, 64};
  for (int i = 0; i < 8; i++) currentAngle[i] = lying[i];
  legsPowered = false;
}
// Where the legs are when the ESP starts, from the servo board itself: after a restart (an update,
// a crash, the reset button) it still drives them - its registers hold each pulse, so the pose is
// known exactly and the first move glides from there. After a real power-up it starts asleep with
// the legs off: they've folded, so the first move starts from lying (not a jump at full speed).
// Call before pwm.begin().
void legsFromServoBoard() {
  static const float lying[8] = {-16, 156, 64, 0, 64, 50, 72, 64};
  static const float standing[8] = {23, 115, 23, 39, -8, 107, 20, 132};  // R1 R2 L1 L2 R4 R3 L3 L4
  auto rd = [](uint8_t reg, uint8_t* b, uint8_t n) {
    Wire.beginTransmission(0x40);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom((uint8_t)0x40, n) != n) return false;
    for (int i = 0; i < n; i++) b[i] = Wire.read();
    return true;
  };
  uint8_t mode1 = 0, pre = 0;
  if (!rd(0x00, &mode1, 1) || !rd(0xFE, &pre, 1)) return;  // no answer: unknown (moves go straight)
  bool driven = !(mode1 & 0x10);  // awake: it was running before this start
  float ang[8];
  for (int i = 0; i < 8 && driven; i++) {
    uint8_t r[4];
    if (!rd(0x06 + 4 * mappedPins[i], r, 4) || (r[3] & 0x10)) { driven = false; break; }  // full off: limp
    const int ticks = (((r[3] & 0x0F) << 8 | r[2]) - ((r[1] & 0x0F) << 8 | r[0])) & 0xFFF;
    const float us = ticks * (pre + 1) * 1e6f / 27e6f;
    ang[i] = (us - 732) * 180.0f / (2929 - 732);
    if (ang[i] < -60 || ang[i] > 240) driven = false;
  }
  // Powered up (legs off): standing or lying? It's what it last did: "legsLying" is saved when it lies down
  // (Rest, sleepy) and cleared at its next move - the usual case is standing.
  bool lyingNow = false;
  if (!driven) {
    Preferences lp;
    lp.begin("explore", true);
    lyingNow = lp.getBool("legsLying", false);
    lp.end();
    legsLyingSaved = lyingNow;
  }
  for (int i = 0; i < 8; i++) currentAngle[i] = driven ? (int)lroundf(ang[i]) : lyingNow ? lying[i] : standing[i];
  // (the read-back is a servo angle; currentAngle is a built-in angle - setup converts it with calUnmap once the
  // calibration is loaded)
  legsPowered = driven;
  // Limp and not lying: where the legs really are isn't known (sagged, half standing). Unknown: the
  // first move goes straight to its target, one joint after another.
  legsPoseKnown = driven || lyingNow;
  xlogSys("LEGS at start: %s", driven ? "still driven by the servo board (a restart): pose read back"
                                      : lyingNow ? "limp, it last lay down: taken as lying" : "limp, it last stood: taken as standing");
}

void setServoAngle(uint8_t channel, int angle) {
   if (channel > 7) return;
   if (legsLyingSaved) { legsLyingSaved = false; prefPut("legsLying", 0, true); }  // moving again: not lying any more
   legsPowered = true;
   legsPoseKnown = true;
   
   // the limits, in built-in angles (= the saved min / max after the mapping)
   const float safeMin = min(LEG_MIN0[channel], LEG_MAX0[channel]);
   const float safeMax = max(LEG_MIN0[channel], LEG_MAX0[channel]);
   if (angle < safeMin) angle = safeMin;
   if (angle > safeMax) angle = safeMax;

   currentAngle[channel] = angle;

   const int pin = mappedPins[channel];
   if (calHolding(pin)) return;  // the calibration page has it
   const float raw = calMap(channel, angle);
   servoRawNow[pin] = raw;
   servoRawKnown[pin] = true;
   pwm.writeMicroseconds(pin, (uint16_t)lroundf(732 + raw * (2929 - 732) / 180.0f));
}

// Pan jumps (more than 4 deg in one write) go in the log with who moved it (to trace random jerks).
// panWho is set by each mover just before its write.
const char* panWho = "?";
void panJumpCheck(uint8_t pin, float angle) {
  static float last = -999;
  if (pin != headPanCh) return;
  if (last > -900 && fabsf(angle - last) > 4) {
    static unsigned long lastLogMs = 0;
    if (millis() - lastLogMs > 300) { lastLogMs = millis(); xlog("PANJUMP %.1f -> %.1f (%s)", last, angle, panWho); }
  }
  last = angle;
}
void setHeadServoAngle(uint8_t pin, int angle) {
   if (calHolding(pin)) return;  // the calibration page has it
   servoRawNow[pin] = angle;
   servoRawKnown[pin] = true;
   panJumpCheck(pin, angle);
   int pulse = map(angle, 0, 180, 732, 2929);
   pwm.writeMicroseconds(pin, pulse);
}
// The same with a fractional angle: slow head moves glide instead of stepping a degree at a time
void setHeadServoF(uint8_t pin, float angle) {
   if (calHolding(pin)) return;  // the calibration page has it
   servoRawNow[pin] = angle;
   servoRawKnown[pin] = true;
   panJumpCheck(pin, angle);
   pwm.writeMicroseconds(pin, (uint16_t)lroundf(732 + angle * (2929 - 732) / 180.0f));
}

void moveServosSmooth(int tgts[8], int durationMs) {
  int steps = durationMs / 20; // 50Hz update rate
  if (steps < 1) steps = 1;
  
  if (!legsPoseKnown) {
    // Unknown pose (limp after power-up): straight to the target - no guessed start, that would make a
    // detour - but one joint after another, not all 8 at full speed at once (one violent jolt).
    bool all = true;
    for (int i = 0; i < 8; i++) {
      if (tgts[i] == -999) { all = false; continue; }
      setServoAngle(i, tgts[i]);
      vTaskDelay(pdMS_TO_TICKS(max(40, min(durationMs / 8, 110))));
    }
    legsPoseKnown = all;  // (a joint left out is still where nobody knows: the next move goes straight there too)
    return;
  }
  float startAngles[8];
  for (int i = 0; i < 8; i++) {
    startAngles[i] = currentAngle[i];
  }
  
  // Ease in / ease out (cosine): starts gently, lands softly - no jolt at either end
  for (int s = 1; s <= steps; s++) {
    const float e = 0.5f - 0.5f * cosf(PI * s / steps);
    for (int i = 0; i < 8; i++) {
      if (tgts[i] == -999) continue;
      float intermediate = startAngles[i] + (tgts[i] - startAngles[i]) * e;
      setServoAngle(i, (int)lroundf(intermediate));
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void moveSmoothHelper(int duration, int r1, int r2, int l1, int l2, int r4, int r3, int l3, int l4) {
  int tgts[8] = {r1, r2, l1, l2, r4, r3, l3, l4};
  moveServosSmooth(tgts, duration);
}


httpd_handle_t cmd_httpd = NULL;
extern httpd_handle_t stream_httpd;  // the video server (below)

// --- LOVYANGFX DISPLAY SETUP ---
// The ST7789 with LovyanGFX's own init commands minus the last one, "display on": that one would show the
// panel's memory - whatever power-up left in it - for a moment before the first frame (a half-lit, distorted
// screen at every start). setup() clears the memory to black first, then turns the display on (0x29).
class Panel_ST7789_Quiet : public lgfx::Panel_ST7789 {
 public:
  const uint8_t* getInitCommands(uint8_t listno) const override {
    if (listno != 0) return nullptr;
    static uint8_t list[160];
    static bool made = false;
    if (!made) {  // a copy of list 0 without CMD_DISPON (0x29, 0 args)
      const uint8_t* src = lgfx::Panel_ST7789::getInitCommands(0);
      int i = 0, o = 0;
      while (o < (int)sizeof(list) - 2) {
        const uint8_t cmd = src[i], n = src[i + 1];
        if (cmd == 0xFF && n == 0xFF) { list[o++] = 0xFF; list[o++] = 0xFF; break; }
        const int len = 2 + (n & 0x7F) + ((n & 0x80) ? 1 : 0);  // args, and a delay byte (CMD_INIT_DELAY)
        if (cmd != 0x29) for (int k = 0; k < len && o < (int)sizeof(list) - 2; k++) list[o++] = src[i + k];
        i += len;
      }
      made = true;
    }
    return list;
  }
};
class LGFX : public lgfx::LGFX_Device {
  Panel_ST7789_Quiet _panel_instance;
  lgfx::Bus_SPI      _bus_instance;
public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST; 
      cfg.spi_mode = 3;         
      cfg.freq_write = 40000000;  // 40 MHz: a full face frame in ~30 ms (smooth animation)
      cfg.freq_read  = 16000000;
      cfg.spi_3wire  = false;
      cfg.use_lock   = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 42;
      cfg.pin_mosi = 41;
      cfg.pin_miso = -1; 
      cfg.pin_dc   = 20;  // (39 is the SD card's clock)
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs           = 21; 
      cfg.pin_rst          = -1;  // (40 is the SD card's data line). The RST wire is joined to EN on GPIO 14: it rises with the display's power, and the panel gets a software reset. (Not GPIO 0/43/44: the unpowered display pulls its pins low at boot.)
      cfg.pin_busy         = -1;
      cfg.panel_width      = 240;
      cfg.panel_height     = 320;
      cfg.offset_x         = 0;
      cfg.offset_y         = 0;
      cfg.offset_rotation  = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = false;
      cfg.invert           = true;  
      cfg.rgb_order        = false; 
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = true;
      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }
};
LGFX tft;
LGFX_Sprite sprite(&tft);
LGFX_Sprite sprite2(&tft);  // the face draws into one while the other goes to the screen
#include "face_logic.h"
#define TFT_EN_PIN 14

// ================= ROBOT MODES: WALK ENGINE / FACE TRACKING / EXPLORE =================
// Walk engine: web D-pad + emotes (default).
// Face tracking: legs stand, head follows faces (ESP-DL MSR01, direct P-control with a
//   30px deadband, holds position when the face is lost).
// Explore: autonomous smooth walk that avoids obstacles with the head-mounted VL53L0X
//   (swept left/right with the pan servo) plus a cheap camera floor check.
//
// Threading: visionTask (core 0) owns the camera and, while a camera mode is active, the
// TFT. loop() (core 1) owns ALL I2C (PCA9685, OLED, MPU, ToF): Wire is not safe across
// tasks and concurrent use crashed with a Wire repeated-start / mutex assert. The task
// only publishes results (head targets, floor free-space) through volatiles.
#define CAM_PWDN   -1
#define CAM_RESET  -1
#define CAM_XCLK   15
#define CAM_SIOD   4
#define CAM_SIOC   5
#define CAM_Y9     16
#define CAM_Y8     17
#define CAM_Y7     18
#define CAM_Y6     12
#define CAM_Y5     10
#define CAM_Y4     8
#define CAM_Y3     9
#define CAM_Y2     11
#define CAM_VSYNC  6
#define CAM_HREF   7
#define CAM_PCLK   13

#define HEAD_PAN_PIN   headPanCh   // (servo_map.h)
#define HEAD_TILT_PIN  headTiltCh
// Head calibration (min / mid / max servo angles), set on the page ("Head calibration") and
// saved in NVS: mid = the head looks straight ahead, min / max = as far as it may go (the head
// touches the body beyond). Everything uses these: modes, emotes, RC, face tracking, Explore.
const float PAN_MIN0 = -18.0, PAN_MID0 = 60.0, PAN_MAX0 = 150.0;
const float TILT_MIN0 = 69.0, TILT_MID0 = 99.0, TILT_MAX0 = 150.0;  // (min: just before the head rests on the neck arms, ~36 deg up)
float PAN_MIN = PAN_MIN0, PAN_MID = PAN_MID0, PAN_MAX = PAN_MAX0;
float TILT_MIN = TILT_MIN0, TILT_MID = TILT_MID0, TILT_MAX = TILT_MAX0;
void exFloorGeometry();
void setHeadCal(float pmin, float pmid, float pmax, float tmin, float tmid, float tmax) {
  PAN_MIN = constrain(pmin, -40.0f, 180.0f);
  PAN_MAX = constrain(pmax, PAN_MIN + 10, 200.0f);
  PAN_MID = constrain(pmid, PAN_MIN, PAN_MAX);
  TILT_MIN = constrain(tmin, 0.0f, 140.0f);
  TILT_MAX = constrain(tmax, TILT_MIN + 10, 150.0f);  // (past 150 the head presses on the body)
  TILT_MID = constrain(tmid, TILT_MIN, TILT_MAX);
  exFloorGeometry();
}
const int   FACE_DEADBAND_PX = 25;
const float FACE_GAIN = 0.05;       // deg per px of error per frame
const float FACE_MAX_STEP = 9;      // deg per frame, so one bad detection can't jerk the head

enum RobotMode { MODE_WALK = 0, MODE_FACE = 1, MODE_EXPLORE = 2, MODE_DESK = 3, MODE_LIFE = 4 };
namespace deskm { void deskEnter(); void deskLeave(); void deskTick(unsigned long now); unsigned long bodyQuietMs(); }
namespace lifem { void lifeEnter(); void lifeLeave(); void lifeTick(unsigned long now); }  // life_mode.h (below)
namespace fid {  // face_id.h (below): who is this
  bool wanted(unsigned long now); void offer(const uint16_t* frame, const std::vector<int>& points); void begin();
  const char* recent(unsigned long ms = 5000); extern volatile unsigned long whoMs; extern char whoName[24];
}
volatile bool deskWalkActive = false;  // desk mode's edge-checked stroll is walking right now (its gaits pass)
volatile bool deskCamOff = false;  // desk mode's StandBy: the camera rests (desk_mode.h, below)
const char* const MODE_NAMES[] = {"walk", "face", "explore", "desk", "life"};
volatile int requestedMode = MODE_WALK;  // set by /setMode (web server task)
volatile int activeMode = MODE_WALK;     // owned by loop(): the mode the robot is actually in
volatile bool camReady = false;          // owned by visionTask: camera is running
volatile bool camFailed = false;         // owned by visionTask: camera unusable for this request
char modeError[128] = "";                 // last mode error, shown in the web UI
char camInitErr[72] = "";                 // the last camera start that failed, and the memory then

// Face tracking -> loop(): head targets
volatile int headPanTarget = (int)PAN_MID;
volatile unsigned long faceSeenGlobalMs = 0;  // face tracking: when a face was last seen (OLED)
volatile int headTiltTarget = (int)TILT_MID;
volatile bool headTargetDirty = false;
volatile bool deskFaceTrack = false;  // desk mode: the head follows a face it sees (the desk page's switch, saved)
volatile float deskUserPan = 0, deskUserTilt = 0;  // desk mode: where the user's face is (head angles), always
volatile unsigned long deskUserMs = 0;               // (when it was last seen there; 0 = never)
extern unsigned long exHeadMovedMs;                  // (below: the head's last move - a picture mid-move is off)
// Life mode (life_mode.h): the biggest face's width in the camera (px, 0 = none) and its OLED card
volatile int lifeFaceW = 0;
char lifeMsgTitle[14] = "", lifeMsgL1[22] = "";
unsigned long lifeMsgUntil = 0;

// Explore camera floor check -> loop(). How much of the lower image is still floor in
// each image band (0 = image left, 1 = center, 2 = image right): 0 = blocked right in
// front, 1 = floor continues to the horizon.
volatile float camFree[3] = {1, 1, 1};
volatile unsigned long camFreeMs = 0;
// Light levels of the latest Explore frame (luma 0..219): whole picture, the view ahead
// (around the horizon: under a bed shows up here) and the floor just in front of the feet
volatile float camLumaAll = -1, camLumaAhead = -1, camLumaNear = -1;
volatile unsigned long camLightMs = 0;

// ToF (VL53L0X on the head, beside the camera)
Adafruit_VL53L0X lox;
bool tofOk = false;
const int TOF_FAR_MM = 2000;             // "nothing in range" is treated as open space
volatile int tofMm = -1;                 // median of the last 3 readings, -1 = no sensor
unsigned long tofSeq = 0;                // increments on every new reading
bool tofRunning = false;                 // ranging (laser on): only while something uses it
volatile unsigned long tofNeedMs = 0;    // the head calibration panel used it (floor glances)

// Explore settings (persisted in NVS)
Preferences prefs;
// The clock's timezone: seconds from UTC (pref "tzOff"). Set on its own from the city on the desk page (the weather
// service tells its UTC offset), or by hand: GET /desk?tzoff=<seconds> (or tzoff=auto).
int tzOffsetSec = 0;
void applyUtcOffset(int sec, bool save) {
  sec = constrain(sec, -14 * 3600, 14 * 3600);
  tzOffsetSec = sec;
  configTime(sec, 0, "pool.ntp.org", "time.google.com");
  if (save) prefs.putInt("tzOff", sec);
}
bool exploreCamAssist = true;            // also stop / steer on the camera floor check
bool panPlusIsLeft = true;               // a bigger pan angle turns the head to the robot's left
const char* volatile exploreStateName = "idle";

bool startCamera(bool jpeg);
// the camera's kept DMA buffer (our esp32-camera copy, cam_hal.c; weak: the plain Arduino build hasn't them)
extern "C" size_t esp_camera_keep_dma(size_t size) __attribute__((weak));
extern "C" size_t esp_camera_kept_dma(void) __attribute__((weak));

// ---- Camera view (RC mode): the camera streamed to the page ----
// Walk mode: the camera runs in JPEG straight from the sensor (the TFT keeps the face) and
// the frames go to /stream on a second web server (port 81), so a video playing never
// blocks the controls. Face / Explore: a few frames a second are JPEG-encoded from the raw
// frames they use. The vision task publishes the newest frame here; the stream copies it.
const size_t JPG_MAX = 640 * 1024;  // a 2048 x 1536 JPEG fits (PSRAM)
uint8_t* jpgFrame = nullptr;       // PSRAM
size_t jpgFrameLen = 0;
volatile uint32_t jpgSeq = 0;
SemaphoreHandle_t jpgMutex = nullptr;
volatile int streamClients = 0;
volatile uint32_t streamGen = 0;   // /camView?new=1 bumps it: an older stream still running stops

// ---- Photos (SD card): what the camera saw at each Explore PROBLEM and USER MARK ----
// requestPhoto() asks the vision task for the next frame (JPEG as it is, or a raw frame
// encoded to JPEG); the photo task writes it to /photos/boot<N>/<uptime>-<tag>.jpg and logs a
// PHOTO line, so the log and the picture go together.
volatile bool photoReq = false;
volatile size_t photoLen = 0;
uint8_t* photoBuf = nullptr;       // PSRAM, JPG_MAX
char photoTag[24] = "";
TaskHandle_t photoTaskHandle = nullptr;
extern int sdSizeMB;

void requestPhoto(const char* tag) {
  if (sdSizeMB < 0 || !photoBuf || photoReq || photoLen) return;  // no card, or one still being saved
  strlcpy(photoTag, tag, sizeof(photoTag));
  photoReq = true;
}

// Vision task: fill the photo from this frame if one was asked for
void takePhotoFrom(camera_fb_t* fb) {
  if (!photoReq || photoLen) return;
  photoReq = false;
  if (fb->format == PIXFORMAT_JPEG) {
    if (fb->len > JPG_MAX) return;
    memcpy(photoBuf, fb->buf, fb->len);
    photoLen = fb->len;
  } else {
    uint8_t* out = nullptr;
    size_t len = 0;
    if (!frame2jpg(fb, 80, &out, &len) || !out) return;
    if (len <= JPG_MAX) {
      memcpy(photoBuf, out, len);
      photoLen = len;
    }
    free(out);
  }
  if (photoLen && photoTaskHandle) xTaskNotifyGive(photoTaskHandle);
}

void photoTask(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (!photoLen) continue;
    char dir[24], name[64];
    snprintf(dir, sizeof(dir), "/photos/boot%u", (unsigned)xl::bootNo);
    if (!SD_MMC.exists("/photos")) SD_MMC.mkdir("/photos");
    if (!SD_MMC.exists(dir)) SD_MMC.mkdir(dir);
    const unsigned long ms = millis();
    snprintf(name, sizeof(name), "%s/%06lu-%s.jpg", dir, ms / 100, photoTag);  // tenths of a second
    File f = SD_MMC.open(name, FILE_WRITE);
    const bool ok = f && f.write(photoBuf, photoLen) == photoLen;
    if (f) f.close();
    if (ok) xlog("PHOTO %s (%u KB) - /sdGet?name=%s", photoTag, (unsigned)(photoLen / 1024), name);
    photoLen = 0;
  }
}
// (a phone can keep the old connection of a closed RC screen open; the stream server serves one
// stream at a time, so the next RC screen would get no picture or a slow one)
// Face / Explore while someone watches the video: the camera sends its own JPEG (hardware
// compressed) straight to the page at full frame rate, and a task on core 1 (loop() sleeps
// 10 ms per pass, core 1 is mostly free) decodes each one to RGB565 for the camera task's
// checks and the TFT (~55-75 ms a frame, one frame behind) - far faster than encoding raw frames in software.
const size_t RAW_BYTES = 320 * 240 * 2;
const size_t DEC_IN_MAX = 48 * 1024;
uint8_t* decIn = nullptr;          // PSRAM: the JPEG being decoded
volatile size_t decInLen = 0;
uint8_t* decBuf[2] = {nullptr, nullptr};
uint8_t* volatile decFront = nullptr;  // newest decoded frame (the camera task reads it under decMutex)
volatile uint32_t decSeq = 0;
volatile bool decBusy = false;
SemaphoreHandle_t decMutex = nullptr;
TaskHandle_t decTaskHandle = nullptr;
volatile float decFps = 0, visFps = 0;
volatile int detMs = 0;            // last face detection time
volatile bool camViewWanted = false;
volatile bool chatCamBlock = false;  // a chat is on: the page's video stays off (camera + video cost ~35 KB of internal RAM the chat needs)
volatile bool camFrameWanted = false;  // gemini.h wants a photo: frames to the latest-JPEG buffer even with no viewer
// A conversation (gemini.h) wants to know where you are, so the head can turn to you: a face search
// in the camera's frames ~4 times a second (the screen keeps the face - unlike face tracking mode).
// chatFaceX / Y: where the biggest face is, -1..1 from the middle of the picture (x right, y down)
volatile bool chatFaceWanted = false;
volatile float chatFaceX = 0, chatFaceY = 0, chatFaceSize = 0;
volatile unsigned long chatFaceMs = 0;
volatile int chatFaceDetMs = 0;
volatile bool chatFaceOn = false;  // face tracking in a chat (the Talk panel's switch, saved: GET /gemini?fs=1|0)
volatile int camViewSize = FRAMESIZE_VGA;
volatile bool camViewSizeChanged = false;
volatile float camFps = 0, streamFps = 0;      // frames/s from the camera and sent to the page
volatile unsigned long camViewViewerMs = 0;    // last time someone watched (auto-off after 10 s)
const unsigned long STREAM_MIN_MS = 41;        // 24 fps cap (the camera itself makes ~14 at XCLK 10 MHz)

// RC mode open on the page: its status polls say so (?rc=1). Expires 5 s after the last
// one, so a phone that locks / closes the page doesn't leave the log paused.
volatile unsigned long rcSeenMs = 0;
volatile unsigned long webSeenMs = 0;  // the page asked something (it polls every 0.7 s while open)
bool rcActive() { return rcSeenMs && millis() - rcSeenMs < 5000; }
volatile bool rcDriveLease = false;    // an RC walk is running on the page's "hold" refreshes (loop() stops it when they stop)
volatile unsigned long rcDriveMs = 0;  // the last walk command / hold=1 from the RC screen

void publishJpeg(const uint8_t* data, size_t len) {
  if (!jpgFrame || len == 0 || len > JPG_MAX) return;
  xSemaphoreTake(jpgMutex, portMAX_DELAY);
  memcpy(jpgFrame, data, len);
  jpgFrameLen = len;
  jpgSeq++;
  xSemaphoreGive(jpgMutex);
}

// The face search for a conversation (gemini.h starts it): its own task, so it doesn't hold up the camera
// or the face's drawing (the camera task draws it).
// Twice a second: the newest camera JPEG, half size (160 x 120: a quarter of the work and memory), the
// camera's own byte order, the face detector. Only with internal RAM to spare (the detector takes some).
// A JPEG's width / height (its SOF header), false if not found in the first 2 KB
bool jpegSize(const uint8_t* d, size_t n, int* w, int* h) {
  for (size_t i = 2; i + 9 < n && i < 2048; i++)
    if (d[i] == 0xFF && (d[i + 1] == 0xC0 || d[i + 1] == 0xC2)) {
      *h = (d[i + 5] << 8) | d[i + 6];
      *w = (d[i + 7] << 8) | d[i + 8];
      return true;
    }
  return false;
}

void chatFaceTask(void*) {
  uint8_t* jpg = (uint8_t*)heap_caps_malloc(64 * 1024, MALLOC_CAP_SPIRAM);
  uint8_t* rgb = (uint8_t*)heap_caps_malloc(160 * 120 * 2, MALLOC_CAP_SPIRAM);
  static HumanFaceDetectMSR01* det = nullptr;  // (kept: made once)
  uint32_t lastSeq = 0;
  while (chatFaceWanted && jpg && rgb) {
    vTaskDelay(pdMS_TO_TICKS(500));
    if (!chatFaceOn || jpgSeq == lastSeq || heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 50000) continue;  // (it takes ~26 KB: 50 leaves the network ~24)
    size_t len = 0;
    xSemaphoreTake(jpgMutex, portMAX_DELAY);
    lastSeq = jpgSeq;
    if (jpgFrameLen <= 64 * 1024) { len = jpgFrameLen; memcpy(jpg, jpgFrame, len); }
    xSemaphoreGive(jpgMutex);
    const unsigned long t0 = millis();
    int jw = 0, jh = 0;  // (only 320 x 240 frames: a photo for Gemini is taken bigger, and wouldn't fit)
    if (!len || !jpegSize(jpg, len, &jw, &jh) || jw != 320 || jh != 240) continue;
    if (!jpg2rgb565(jpg, len, rgb, JPG_SCALE_2X)) continue;  // (a QVGA frame -> 160 x 120)
    uint32_t* w = (uint32_t*)rgb;
    for (size_t i = 0; i < 160 * 120 * 2 / 4; i++) w[i] = ((w[i] & 0x00FF00FFu) << 8) | ((w[i] >> 8) & 0x00FF00FFu);
    if (!det) det = new HumanFaceDetectMSR01(0.1F, 0.5F, 10, 0.4F);  // (scaled for the half size, like detectorHalf)
    std::list<dl::detect::result_t>& res = det->infer((uint16_t*)rgb, {120, 160, 3});
    int best = 0;
    for (auto& r : res) {
      const int area = (r.box[2] - r.box[0]) * (r.box[3] - r.box[1]);
      if (area <= best) continue;
      best = area;
      chatFaceX = ((r.box[0] + r.box[2]) / 2.0f - 80) / 80;
      chatFaceY = ((r.box[1] + r.box[3]) / 2.0f - 60) / 60;
      chatFaceSize = (float)(r.box[2] - r.box[0]) / 160;
    }
    if (best) chatFaceMs = millis();
    chatFaceDetMs = millis() - t0;
  }
  heap_caps_free(jpg);
  heap_caps_free(rgb);
  vTaskDelete(nullptr);
}

void decoderTask(void*) {
  uint32_t n = 0;
  unsigned long fpsMs = millis();
  for (;;) {
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) == 0) { decFps = 0; n = 0; fpsMs = millis(); continue; }
    uint8_t* back = decFront == decBuf[0] ? decBuf[1] : decBuf[0];
    if (jpg2rgb565(decIn, decInLen, back, JPG_SCALE_NONE)) {
      // jpg2rgb565 writes the other byte order than the camera's own RGB565
      uint32_t* w = (uint32_t*)back;
      for (size_t i = 0; i < RAW_BYTES / 4; i++) {
        const uint32_t x = w[i];
        w[i] = ((x & 0x00FF00FFu) << 8) | ((x >> 8) & 0x00FF00FFu);
      }
      xSemaphoreTake(decMutex, portMAX_DELAY);
      decFront = back;
      decSeq++;
      xSemaphoreGive(decMutex);
      n++;
    }
    decBusy = false;
    if (millis() - fpsMs >= 1000) {
      decFps = n * 1000.0f / (millis() - fpsMs);
      n = 0;
      fpsMs = millis();
    }
  }
}

// Size + JPEG quality for the camera view: sharper (lower number) where the WiFi can still
// carry ~24 / ~16 / ~10 fps (~215 KB/s)
// The camera's own sharpening, before JPEG compression (much better than sharpening a soft
// JPEG afterwards): -1 = as the driver sets it, 0..6 = softer .. sharper. The camera on this
// robot is an OV3660 (sensor id 0x3660, 3 MP) - its driver has set_sharpness (-3..3); an
// OV2640 would need its DSP registers 0x92 / 0x93. Saved; /camView?sharp=N.
extern "C" int SCCB_Write(uint8_t slv_addr, uint8_t reg, uint8_t data);
volatile int camSharp = -1;
volatile bool camSharpChanged = false;
volatile int camSharpRes = 0;  // result of the last setting (0 = ok) - web UI
void applyCamSharp(sensor_t* sn) {
  if (!sn || camSharp < 0) return;
  if (sn->id.PID == OV2640_PID) {
    static const uint8_t LV[7] = {0xc0, 0xc1, 0xc2, 0xc4, 0xc8, 0xd0, 0xdf};
    SCCB_Write(sn->slv_addr, 0xFF, 0x00);  // DSP register bank
    SCCB_Write(sn->slv_addr, 0x92, 0x01);
    camSharpRes = SCCB_Write(sn->slv_addr, 0x93, LV[min(6, (int)camSharp)]);
  } else if (sn->set_sharpness) {
    camSharpRes = sn->set_sharpness(sn, (int)camSharp - 3);
  }
}

// The RC view sizes, smallest to largest (the OV3660 goes up to 2048 x 1536)
struct CamSize { const char* name; framesize_t fs; int q; };
const CamSize CAM_SIZES[] = {
  {"qvga", FRAMESIZE_QVGA, 14}, {"hvga", FRAMESIZE_HVGA, 14}, {"vga", FRAMESIZE_VGA, 12}, {"svga", FRAMESIZE_SVGA, 12},  // (RC qvga/hvga 14: smaller frames, more of them through a weak link)
  {"xga", FRAMESIZE_XGA, 13}, {"hd", FRAMESIZE_HD, 13}, {"sxga", FRAMESIZE_SXGA, 14}, {"uxga", FRAMESIZE_UXGA, 14},
  {"qxga", FRAMESIZE_QXGA, 15},
};
const int CAM_SIZE_N = sizeof(CAM_SIZES) / sizeof(CAM_SIZES[0]);
const char* camSizeName(int fs) {
  for (int i = 0; i < CAM_SIZE_N; i++) if (CAM_SIZES[i].fs == fs) return CAM_SIZES[i].name;
  return "vga";
}

void govLog(const char* kind, const char* note);  // below (the governor's log)
volatile int camPhotoQ = 0;  // a photo for Gemini: this JPEG quality instead (lower = finer), 0 = the table's
void applyCamViewSize(sensor_t* sn) {
  sn->set_framesize(sn, (framesize_t)camViewSize);
  int q = 12;  // bigger pictures: a little more compression, or a frame gets too big for WiFi
  for (int i = 0; i < CAM_SIZE_N; i++) if (CAM_SIZES[i].fs == camViewSize) q = CAM_SIZES[i].q;
  if (camPhotoQ > 0) q = camPhotoQ;
  sn->set_quality(sn, q);
}

bool startCamera(bool jpeg) {
  // The camera needs one 32 KB block of internal RAM for its DMA. Without it, a start fails half-way and
  // leaves the driver's task (cam_task, top priority on core 0) spinning: the task watchdog restarts the
  // robot. Don't even try then - the caller retries in a moment.
  // (JPEG: its DMA buffer is kept from boot - esp_camera_keep_dma - then only small parts are needed:
  // the driver's task stack 2 KB, descriptors, its state)
  const size_t kept = esp_camera_kept_dma ? esp_camera_kept_dma() : 0;
  const size_t camNeed = kept >= 32768 || (jpeg && kept >= 16384) ? 4096 : 33000;
  if (heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL) < camNeed) {
    snprintf(camInitErr, sizeof(camInitErr), "skipped at %lu s: largest block %u (needs %u)", millis() / 1000,
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL), (unsigned)camNeed);
    return false;
  }
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = CAM_Y2;
  config.pin_d1 = CAM_Y3;
  config.pin_d2 = CAM_Y4;
  config.pin_d3 = CAM_Y5;
  config.pin_d4 = CAM_Y6;
  config.pin_d5 = CAM_Y7;
  config.pin_d6 = CAM_Y8;
  config.pin_d7 = CAM_Y9;
  config.pin_xclk = CAM_XCLK;
  config.pin_pclk = CAM_PCLK;
  config.pin_vsync = CAM_VSYNC;
  config.pin_href = CAM_HREF;
  config.pin_sccb_sda = CAM_SIOD;
  config.pin_sccb_scl = CAM_SIOC;
  config.pin_pwdn = CAM_PWDN;
  config.pin_reset = CAM_RESET;
  config.xclk_freq_hz = 10000000;  // (20 MHz: with the camera on, the WiFi lost 20 % of full-size packets)
  config.pixel_format = PIXFORMAT_RGB565; // ESP-DL needs raw RGB565
  config.frame_size = FRAMESIZE_QVGA;     // 320x240 = full TFT
  config.jpeg_quality = 12;
  config.fb_count = 2;                    // double-buffered for smooth video
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  if (jpeg) {                             // camera view: compressed by the sensor itself
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_QXGA;   // buffers sized for the largest view size (2048 x 1536)
    config.jpeg_quality = 10;
    config.grab_mode = CAMERA_GRAB_LATEST;  // always the newest frame: low lag
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    snprintf(modeError, sizeof(modeError), "error: camera init 0x%x", err);
    snprintf(camInitErr, sizeof(camInitErr), "0x%x at %lu s: internal free %u, largest block %u", err, millis() / 1000,
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    xlogSys("CAMERA start failed %s", camInitErr);
    Serial.println(modeError);
    return false;
  }
  modeError[0] = 0;
  sensor_t* sn = esp_camera_sensor_get();
  static bool pidLogged = false;
  if (sn && !pidLogged) {
    pidLogged = true;
    xlog("CAMERA sensor PID 0x%04X", (unsigned)sn->id.PID);
  }
  if (jpeg && sn) {
    if (requestedMode == MODE_WALK) applyCamViewSize(sn);   // RC: the size chosen on the page
    else {                                                  // face / Explore: 320 x 240 (decoded)
      sn->set_framesize(sn, FRAMESIZE_QVGA);
      sn->set_quality(sn, 10);
    }
    sn->set_bpc(sn, 1);      // black / white pixel correction, lens shading, gamma: a cleaner picture
    sn->set_wpc(sn, 1);
    sn->set_lenc(sn, 1);
    sn->set_raw_gma(sn, 1);
    camViewSizeChanged = false;
  }
  applyCamSharp(sn);
  return true;
}

// ---- Camera floor check (Explore) ----
// Learns the floor colour from a patch just in front of the feet, then walks up each
// image band until most samples stop looking like that floor. A close obstacle or wall
// cuts the floor off low in the image. Only ~3k pixel reads per frame.
const int FLOOR_TOP_ROW_LEVEL = 120;  // about the horizon with the head at mid tilt
const int FLOOR_TOP_ROW_DOWN = 8;     // head looking down: the whole image is floor ahead
volatile int floorTopRow = FLOOR_TOP_ROW_LEVEL;
const int FLOOR_BOTTOM_ROW = 236;

static inline void rgbAt(const uint8_t* buf, int w, int x, int y, int& r, int& g, int& b) {
  const uint8_t* p = buf + 2 * (y * w + x);  // camera RGB565 is big-endian
  r = (p[0] >> 3) << 1;                      // scale R and B to 6 bits like G
  g = ((p[0] & 7) << 3) | (p[1] >> 5);
  b = (p[1] & 0x1F) << 1;
}

void analyzeFloor(camera_fb_t* fb, int boundaryRow[3], int topRow) {
  const uint8_t* buf = fb->buf;
  const int w = fb->width;

  long sr = 0, sg = 0, sb = 0;
  int n = 0, r, g, b;
  for (int y = 222; y <= FLOOR_BOTTOM_ROW; y += 2)
    for (int x = 120; x <= 200; x += 4) { rgbAt(buf, w, x, y, r, g, b); sr += r; sg += g; sb += b; n++; }
  const int fr = sr / n, fg = sg / n, fbl = sb / n;
  long dev = 0;
  for (int y = 222; y <= FLOOR_BOTTOM_ROW; y += 2)
    for (int x = 120; x <= 200; x += 4) { rgbAt(buf, w, x, y, r, g, b); dev += abs(r - fr) + abs(g - fg) + abs(b - fbl); }
  const int thresh = max(18, (int)(3 * dev / n));  // textured floors get a wider tolerance

  const int bandW = w / 3;
  for (int band = 0; band < 3; band++) {
    int x0 = band * bandW + 4, x1 = (band + 1) * bandW - 4;
    int boundary = topRow, misses = 0;
    for (int y = FLOOR_BOTTOM_ROW; y >= topRow; y -= 4) {
      int total = 0, off = 0;
      for (int x = x0; x <= x1; x += 6) {
        rgbAt(buf, w, x, y, r, g, b);
        total++;
        if (abs(r - fr) + abs(g - fg) + abs(b - fbl) > thresh) off++;
      }
      if (off * 2 > total) {
        if (++misses >= 2) { boundary = y + 4; break; }  // two non-floor rows in a row
      } else {
        misses = 0;
      }
    }
    boundaryRow[band] = boundary;
  }
}

// ---- Camera motion check (Explore): "did I actually move?" ----
// The gait repeats every cycle, so a robot walking in place (legs sliding against an
// obstacle on a smooth floor) sees the same picture again at the same gait phase one
// cycle later, while a robot that really moves sees the scene shift further and further.
// Compare the newest small grey frame with the frames at the same gait phase one cycle
// (T) and two cycles (2T) ago:
//   moving -> diff(T) is large, or diff(2T) clearly bigger than diff(T) (scene drifts)
//   stuck  -> diff(T) and diff(2T) both small and about equal (only the periodic sway)
// The body's own gait sway cancels out because both comparisons are phase-matched.
const int MOT_W = 80, MOT_H = 60, MOT_FRAMES = 28;  // 4x downsampled, ~2.5 s history
struct MotFrame {
  uint8_t* px;
  unsigned long ms;
  float phase;
  uint8_t gait;
  bool valid;
};
MotFrame motRing[MOT_FRAMES];
int motHead = 0;
bool motAlloc = false;
volatile float cpgPhase = 0;         // loop(): current CPG phase (rad, 0..2pi)
volatile float cpgPeriodMs = 524;    // loop(): current gait cycle length
volatile uint8_t cpgGait = 0;        // loop(): Explore gait id, 0 = standing
volatile float motDiffT = 0, motDiff2T = 0, motNoise = 2;
volatile int motVerdict = 0;         // +1 moving, -1 not moving, 0 unknown
volatile unsigned long motMs = 0;    // when motVerdict was last updated
const float MOT_MOVING_MIN = 6;      // diff(T) above max(this, 3.5 x noise) = clearly moving

static void motDownsample(const uint8_t* buf, uint8_t* out) {
  for (int y = 0; y < MOT_H; y++) {
    const uint8_t* row = buf + 2 * ((y * 4 + 1) * 320);
    for (int x = 0; x < MOT_W; x++) {
      const uint8_t* p = row + 2 * (x * 4 + 1);
      int r = p[0] >> 3, g = ((p[0] & 7) << 3) | (p[1] >> 5), b = p[1] & 0x1F;
      out[y * MOT_W + x] = (uint8_t)(r * 2 + g * 2 + b);  // cheap luma, 0..219
    }
  }
}

static float motMad(const uint8_t* a, const uint8_t* b) {
  uint32_t sum = 0;
  for (int i = 0; i < MOT_W * MOT_H; i++) sum += abs((int)a[i] - (int)b[i]);
  return (float)sum / (MOT_W * MOT_H);
}

static float phaseDist(float a, float b) {
  float d = fabsf(a - b);
  return d > 3.14159f ? 6.28318f - d : d;
}

// Best phase-matched frame whose age is within [lo, hi] x the gait cycle
static int motFind(int cur, float loCycles, float hiCycles) {
  const MotFrame& c = motRing[cur];
  const float T = cpgPeriodMs;
  int best = -1;
  float bestD = 0.45f;  // max phase error (rad)
  for (int i = 0; i < MOT_FRAMES; i++) {
    const MotFrame& f = motRing[i];
    if (i == cur || !f.valid || f.gait != c.gait) continue;
    float age = (float)(c.ms - f.ms);
    if (age < loCycles * T || age > hiCycles * T) continue;
    float d = phaseDist(f.phase, c.phase);
    if (d < bestD) { bestD = d; best = i; }
  }
  return best;
}

// Light levels from the small grey frame (row 0 = far / up, row 59 = just ahead of the feet)
static float motRegionMean(const uint8_t* px, int y0, int y1, int x0, int x1) {
  uint32_t sum = 0;
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) sum += px[y * MOT_W + x];
  return (float)sum / ((y1 - y0) * (x1 - x0));
}

void updateLight() {
  if (!motAlloc) return;
  const uint8_t* px = motRing[(motHead + MOT_FRAMES - 1) % MOT_FRAMES].px;
  if (!px) return;
  camLumaAll = motRegionMean(px, 0, MOT_H, 0, MOT_W);
  camLumaAhead = motRegionMean(px, 0, 15, 16, 64);
  camLumaNear = motRegionMean(px, 44, MOT_H, 20, 60);
  camLightMs = millis();
}

void updateMotion(camera_fb_t* fb) {
  if (!motAlloc) {
    for (int i = 0; i < MOT_FRAMES; i++) {
      motRing[i].px = (uint8_t*)heap_caps_malloc(MOT_W * MOT_H, MALLOC_CAP_SPIRAM);
      motRing[i].valid = false;
    }
    motAlloc = true;
  }
  const int cur = motHead;
  const int prev = (cur + MOT_FRAMES - 1) % MOT_FRAMES;
  MotFrame& f = motRing[cur];
  if (!f.px) return;
  motDownsample(fb->buf, f.px);
  f.ms = millis();
  f.phase = cpgPhase;
  f.gait = cpgGait;
  f.valid = true;
  motHead = (cur + 1) % MOT_FRAMES;

  if (f.gait == 0) {
    // Standing still: learn the camera noise floor from consecutive frames
    const MotFrame& p = motRing[prev];
    if (p.valid && p.gait == 0 && f.ms - p.ms < 300) motNoise = 0.8f * motNoise + 0.2f * motMad(f.px, p.px);
    motVerdict = 0;
    return;
  }
  const int k1 = motFind(cur, 0.7f, 1.4f);
  const int k2 = motFind(cur, 1.7f, 2.4f);
  if (k1 < 0 || k2 < 0) { motVerdict = 0; return; }
  const float d1 = motMad(f.px, motRing[k1].px);
  const float d2 = motMad(f.px, motRing[k2].px);
  motDiffT = d1;
  motDiff2T = d2;
  const float movingAbs = max(MOT_MOVING_MIN, 3.5f * motNoise);
  if (d1 > movingAbs || d2 > d1 * 1.3f + 0.8f) motVerdict = 1;
  else if (d2 < d1 * 1.12f + 0.5f) motVerdict = -1;
  else motVerdict = 0;
  motMs = f.ms;
}

// Face tracking: the detector (ESP-DL MSR01) is trained on upright faces and misses a head
// tilted more than ~20 deg. When it finds nothing, a half-size copy of the frame turned by
// +-30 / +-55 deg is searched too (one angle per frame, the one that worked last first) - a
// tilted face looks upright in one of them. Detection takes ~10 ms, so this costs little.
const float FACE_ROLLS[] = {30, -30, 55, -55};
volatile int faceRoll = 0;                 // tilt it found the face at last (web UI)

// dst (160 x 120) = src (320 x 240) turned by deg around the centre, half size.
// dst (u, v) comes from src (160 + 2 (c (u-80) - s (v-60)), 120 + 2 (s (u-80) + c (v-60)))
void rotateHalf(const uint16_t* src, uint16_t* dst, float deg) {
  const float a = deg * DEG_TO_RAD;
  const int32_t C = (int32_t)(cosf(a) * 2 * 65536), S = (int32_t)(sinf(a) * 2 * 65536);
  for (int v = 0; v < 120; v++) {
    int32_t sx = (160 << 16) - C * 80 - S * (v - 60);
    int32_t sy = (120 << 16) - S * 80 + C * (v - 60);
    for (int u = 0; u < 160; u++) {
      const int x = sx >> 16, y = sy >> 16;
      *dst++ = (x >= 0 && x < 320 && y >= 0 && y < 240) ? src[y * 320 + x] : 0;
      sx += C;
      sy += S;
    }
  }
}

void unrotateHalf(float u, float v, float deg, float* x, float* y) {
  const float a = deg * DEG_TO_RAD, c = cosf(a), sn = sinf(a);
  *x = 160 + 2 * (c * (u - 80) - sn * (v - 60));
  *y = 120 + 2 * (sn * (u - 80) + c * (v - 60));
}

void visionTask(void* arg) {
  HumanFaceDetectMSR01* detector = nullptr;
  HumanFaceDetectMSR01* detectorHalf = nullptr;  // for the 160 x 120 turned copies
  HumanFaceDetectMNP01* detector2 = nullptr;     // desk / life: the face's points, for "who is this" (face_id.h)
  uint16_t* rotBuf = (uint16_t*)heap_caps_malloc(160 * 120 * 2, MALLOC_CAP_SPIRAM);
  unsigned long faceSeenMs = 0;
  int rollTry = 0;
  float panAngle = PAN_MID, tiltAngle = TILT_MID;
  float freeEma[3] = {1, 1, 1};
  int lastWant = MODE_WALK, lastDrawn = MODE_WALK, nullFrames = 0, initFails = 0;
  bool camIsJpeg = false, lastCamView = false;
  unsigned long lastStreamMs = 0;

  unsigned long camOffMs = 0;
  unsigned long deskDetMs = 0;  // desk mode: the last look for the user
  // The face (drawn into the sprite by loop / emotes) goes on the screen here - this task is the
  // TFT's only user. Shown in walk mode, and in Explore when the camera failed (ToF only).
  auto showFace = [&]() {
    const int m = activeMode;
    if (faceSelfPush || !faceDirty || !spriteMutex || !(m == MODE_WALK || m == MODE_DESK || m == MODE_LIFE || (m == MODE_EXPLORE && camFailed))) return;
    xSemaphoreTake(spriteMutex, portMAX_DELAY);
    faceDirty = false;
    const unsigned long t0 = micros();
    const int dw = faceDirtyW, dh = faceDirtyH;
    if (dw > 0 && dh > 0) {  // the battery saver face: only the part that changed
      tft.setClipRect(faceDirtyX, faceDirtyY, dw, dh);
      faceFront->pushSprite(0, 0);
      tft.clearClipRect();
    } else {
      faceFront->pushSprite(0, 0);
    }
    facePushMs = facePushMs * 0.9f + (micros() - t0) / 1000.0f * 0.1f;
    xSemaphoreGive(spriteMutex);
  };
  // The camera only runs while this task holds xl::camLock: flash access (log, settings)
  // must not happen while it streams pixels (see xlog.h)
  // the governor's log gets every camera stop / start and why (a "reconnecting" on the page: was it
  // the camera or the WiFi?)
  auto camLog = [](const char* what) { govLog("cam", what); };
  auto cameraOff = [&]() {
    esp_camera_deinit();
    camReady = false;
    xl::camOn = false;
    xSemaphoreGive(xl::camLock);
  };

  for (;;) {
    int want = requestedMode;
    if (want != lastWant) { camFailed = false; lastWant = want; }
    if (camViewWanted != lastCamView) { camFailed = false; lastCamView = camViewWanted; }
    // Walk mode needs the camera only for the camera view (JPEG). Face / Explore use raw frames
    // - or the camera's JPEG while someone watches the video (decoded for their checks)
    // (desk mode: JPEG too - a raw frame needs one 33 KB block of internal RAM, often not there by then;
    // the JPEG's buffer is kept from boot and the decoder makes the pixels)
    const bool wantJpeg = want == MODE_WALK || want == MODE_DESK || want == MODE_LIFE || camViewWanted;

    if (want == MODE_DESK && deskCamOff && !camViewWanted) {  // desk mode's StandBy: the camera rests
      if (camReady) { cameraOff(); camLog("camera off: StandBy"); }
      showFace();
      vTaskDelay(pdMS_TO_TICKS(4));
      continue;
    }
    if ((want == MODE_DESK || want == MODE_LIFE) && camFailed && !camViewWanted) {  // desk mode without a camera: the face only (ToF presence)
      showFace();
      vTaskDelay(pdMS_TO_TICKS(4));
      continue;
    }
    // desk mode looks only every 1.5 s: in between, just the face
    if (want == MODE_DESK && camReady && !camViewWanted && millis() - deskDetMs < 1500) {
      showFace();
      vTaskDelay(pdMS_TO_TICKS(4));
      continue;
    }
    // Life mode looks ~3 times a second (a person to walk to, the head following them)
    if (want == MODE_LIFE && camReady && !camViewWanted && millis() - deskDetMs < 350) {
      showFace();
      vTaskDelay(pdMS_TO_TICKS(4));
      continue;
    }
    if (want == MODE_WALK && !camViewWanted) {
      if (camReady) { cameraOff(); camLog("camera off: nobody needs it"); }
      lastDrawn = MODE_WALK;
      showFace();
      vTaskDelay(pdMS_TO_TICKS(4));  // a new face frame goes out within 4 ms
      continue;
    }
    if (camReady && camIsJpeg != wantJpeg) {  // camera view <-> face / Explore
      cameraOff();
      camLog("camera restart: video <-> face / Explore frames");
      continue;
    }
    // Someone needs the flash (log / settings): switch the camera off for a moment
    if (camReady && xl::camWaiters > 0) {
      cameraOff();
      camLog("camera paused for a flash write (settings)");
      camOffMs = millis();
      continue;
    }
    if (!camReady && !camFailed) {
      if (xl::camWaiters > 0) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
      xSemaphoreTake(xl::camLock, portMAX_DELAY);
      if (startCamera(wantJpeg)) {
        {
          char n[64];
          snprintf(n, sizeof(n), "camera on (%s)%s", wantJpeg ? "JPEG video" : "raw frames", initFails ? " after retries" : "");
          camLog(n);
        }
        initFails = 0;
        camIsJpeg = wantJpeg;
        camReady = true;
        xl::camOn = true;
        nullFrames = 0;
        if (camOffMs) {
          xl::lastCamGapMs = millis() - camOffMs;
          camOffMs = 0;
        }
      } else {
        xSemaphoreGive(xl::camLock);
        // Often only for a moment: the camera needs one 32 KB block of internal memory, and a
        // secure page opening its connections takes memory for a few seconds. Try again first.
        if (++initFails < 6) {
          vTaskDelay(pdMS_TO_TICKS(700));
          continue;
        }
        initFails = 0;
        camFailed = true;
        if (want == MODE_FACE) requestedMode = MODE_WALK;  // face mode needs the camera
        if (wantJpeg) camViewWanted = false;
      }
      continue;
    }
    // Frames per second from the camera (shown on the page)
    auto countCamFps = [&]() {
      static uint32_t camFrames = 0;
      static unsigned long camFpsMs = 0;
      camFrames++;
      if (millis() - camFpsMs >= 1000) {
        camFps = camFrames * 1000.0f / (millis() - camFpsMs);
        camFrames = 0;
        camFpsMs = millis();
      }
    };
    // Nobody watching for 10 s (page closed, phone locked): camera view off
    auto viewerCheck = [&]() {
      if (streamClients > 0) camViewViewerMs = millis();
      else if (camViewWanted && millis() - camViewViewerMs > 10000) {  // (only when it's on: otherwise desk mode logs "video off" every 1.5 s to SD + flash)
        camViewWanted = false;
        xlog("CAMERA view off: nobody watching");
        camLog("video off: no viewer for 10 s");
      }
    };
    if (camReady && camSharpChanged) {
      camSharpChanged = false;
      applyCamSharp(esp_camera_sensor_get());
    }
    if (camReady && camIsJpeg && want == MODE_WALK) {
      showFace();  // walk mode camera view: the screen keeps the face
      // A conversation's camera (photos, the face search) needs ~6 frames a second, not 25: between them
      // this task keeps drawing the face (it's the screen's only user) - at full camera speed the face would
      // wait for the camera's frames.
      if (chatFaceWanted && streamClients == 0) {
        static unsigned long lastGrabMs = 0;
        if (millis() - lastGrabMs < 150) {
          vTaskDelay(pdMS_TO_TICKS(4));
          continue;
        }
        lastGrabMs = millis();
      }
      // Walk-mode camera view: frames straight to the stream; the TFT keeps the face
      if (camViewSizeChanged) {
        camViewSizeChanged = false;
        sensor_t* sn = esp_camera_sensor_get();
        if (sn) applyCamViewSize(sn);
      }
      camera_fb_t* fb = esp_camera_fb_get();
      if (!fb) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
      takePhotoFrom(fb);  // RC snapshot (/camView?photo=1)
      if ((streamClients > 0 || camFrameWanted) && fb->format == PIXFORMAT_JPEG) publishJpeg(fb->buf, fb->len);
      esp_camera_fb_return(fb);
      countCamFps();
      viewerCheck();
      vTaskDelay(1);
      continue;
    }
    // Only draw camera frames once loop() has entered this mode
    if (!camReady || activeMode != want) { showFace(); vTaskDelay(pdMS_TO_TICKS(20)); continue; }

    if (want != lastDrawn) {  // fresh start of a mode
      panAngle = PAN_MID;
      tiltAngle = TILT_MID;
      for (int i = 0; i < 3; i++) freeEma[i] = 1;
      lastDrawn = want;
    }

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      if (++nullFrames > 20) {
        snprintf(modeError, sizeof(modeError), "error: camera gives no frames");
        xlog("CAMERA gives no frames - off");
        cameraOff();
        camFailed = true;  // Explore carries on with the ToF only
        if (want == MODE_FACE) requestedMode = MODE_WALK;
      }
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    nullFrames = 0;
    takePhotoFrom(fb);  // Explore PROBLEM / USER MARK asked for a picture
    // Video being watched: the camera's JPEG goes to the page as it is; the checks below get it
    // decoded (core 1, one frame behind)
    camera_fb_t vfb;
    bool decoded = false;
    if (fb->format == PIXFORMAT_JPEG) {
      camViewSizeChanged = false;  // the page's size choice is for RC (walk mode) only
      publishJpeg(fb->buf, fb->len);
      countCamFps();
      viewerCheck();
      if (!decBusy && decIn && fb->width == 320 && fb->height == 240 && fb->len <= DEC_IN_MAX) {
        memcpy(decIn, fb->buf, fb->len);
        decInLen = fb->len;
        decBusy = true;
        xTaskNotifyGive(decTaskHandle);
      }
      esp_camera_fb_return(fb);
      if (want == MODE_DESK || want == MODE_LIFE) deskDetMs = millis();  // desk mode: one frame per 1.5 s (the check at the top)
      static uint32_t lastDecSeq = 0;
      if (decSeq == lastDecSeq) {
        if (want == MODE_DESK || want == MODE_LIFE) showFace();  // (desk mode: the face keeps moving while a frame decodes)
        vTaskDelay(pdMS_TO_TICKS(2));
        continue;
      }
      lastDecSeq = decSeq;
      xSemaphoreTake(decMutex, portMAX_DELAY);
      vfb = camera_fb_t();
      vfb.buf = decFront;
      vfb.len = RAW_BYTES;
      vfb.width = 320;
      vfb.height = 240;
      vfb.format = PIXFORMAT_RGB565;
      fb = &vfb;
      decoded = true;
    }

    // Desk mode: the screen keeps the face; the camera only looks for the user (a face, every 1.5 s)
    // and how bright the room is
    if (want == MODE_DESK || want == MODE_LIFE) {
      {
        const uint16_t* px = (const uint16_t*)fb->buf;
        uint32_t sum = 0;
        int n = 0;
        for (int i = 0; i < 320 * 240; i += 97, n++) {
          const uint16_t v = (uint16_t)((px[i] >> 8) | (px[i] << 8));  // (big-endian RGB565)
          sum += ((v >> 11) & 31) * 8 + ((v >> 5) & 63) * 4 + (v & 31) * 8;
        }
        camLumaAll = sum / (3.0f * n);
        camLightMs = millis();
        if (!detector) detector = new HumanFaceDetectMSR01(0.1F, 0.5F, 10, 0.2F);
        std::list<dl::detect::result_t>& res = detector->infer((uint16_t*)fb->buf, {(int)fb->height, (int)fb->width, 3});
        if (!res.empty()) faceSeenGlobalMs = millis();
        // who is it? the face's points (second stage), then a copy to the recognizer (every 2 s at most)
        if (!res.empty() && fb->width == 320 && fb->height == 240 && fid::wanted(millis())) {
          if (!detector2) detector2 = new HumanFaceDetectMNP01(0.5F, 0.3F, 5);
          std::list<dl::detect::result_t>& r2 = detector2->infer((uint16_t*)fb->buf, {(int)fb->height, (int)fb->width, 3}, res);
          if (!r2.empty()) fid::offer((uint16_t*)fb->buf, r2.front().keypoint);
        }
        // Face tracking (desk switch): where the biggest face is -> a head target a bit toward it
        // (gentler than face mode; the desk loop decides when the head may follow)
        if (want == MODE_LIFE && res.empty()) lifeFaceW = 0;
        if (want == MODE_DESK && !res.empty() && millis() - exHeadMovedMs > 300) {  // where you are (desk_mode's "look at you")
          float bx = 0, by = 0;
          int ba = 0;
          for (auto& r : res) {
            const int area = (r.box[2] - r.box[0]) * (r.box[3] - r.box[1]);
            if (area > ba) { ba = area; bx = (r.box[0] + r.box[2]) / 2.0f; by = (r.box[1] + r.box[3]) / 2.0f; }
          }
          deskUserPan = constrain(emoPanNow + (bx - fb->width / 2.0f) * 0.18f, PAN_MIN, PAN_MAX);  // (~0.18 deg a pixel, the tracking's sign)
          deskUserTilt = constrain(emoTiltNow + (by - fb->height / 2.0f) * 0.18f, TILT_MIN, TILT_MAX);
          deskUserMs = millis();
        }
        if ((deskFaceTrack || want == MODE_LIFE) && !res.empty()) {
          int best = 0;
          float cx = 0, cy = 0;
          for (auto& r : res) {
            const int area = (r.box[2] - r.box[0]) * (r.box[3] - r.box[1]);
            if (area > best) { best = area; cx = (r.box[0] + r.box[2]) / 2.0f; cy = (r.box[1] + r.box[3]) / 2.0f; if (want == MODE_LIFE) lifeFaceW = r.box[2] - r.box[0]; }
          }
          const float ex = cx - fb->width / 2.0f, ey = cy - fb->height / 2.0f;
          float p = emoPanNow, t = emoTiltNow;
          if (fabsf(ex) > 18) p += constrain(ex * 0.08f, -12.0f, 12.0f);
          if (fabsf(ey) > 18) t += constrain(ey * 0.08f, -12.0f, 12.0f);
          headPanTarget = (int)constrain(p, PAN_MIN, PAN_MAX);
          headTiltTarget = (int)constrain(t, TILT_MIN + 2, TILT_MAX - 5);
          headTargetDirty = true;
        }
      }
      if (decoded) xSemaphoreGive(decMutex);
      else esp_camera_fb_return(fb);
      showFace();
      vTaskDelay(pdMS_TO_TICKS(40));
      continue;
    }

    tft.startWrite();
    tft.pushImage(0, 0, 320, 240, (uint16_t*)fb->buf);

    if (want == MODE_FACE) {
      tft.endWrite();  // face detection takes a while: not inside the SPI transaction
      if (!detector) detector = new HumanFaceDetectMSR01(0.1F, 0.5F, 10, 0.2F);
      const unsigned long d0 = millis();
      std::list<dl::detect::result_t>& results =
          detector->infer((uint16_t*)fb->buf, {(int)fb->height, (int)fb->width, 3});
      // The biggest (closest) face: centre and size in the frame
      bool found = false;
      float fcx = 0, fcy = 0, fw = 0, fh = 0;
      int bestArea = 0;
      for (auto& r : results) {
        const int area = (r.box[2] - r.box[0]) * (r.box[3] - r.box[1]);
        if (area > bestArea) {
          bestArea = area;
          found = true;
          fcx = (r.box[0] + r.box[2]) / 2.0f;
          fcy = (r.box[1] + r.box[3]) / 2.0f;
          fw = r.box[2] - r.box[0];
          fh = r.box[3] - r.box[1];
        }
      }
      if (found) faceRoll = 0;
      // Not upright: search a turned copy (a tilted head), for 4 s after the last sighting and
      // then every few frames
      static uint32_t lostFrames = 0;
      lostFrames = found ? 0 : lostFrames + 1;
      if (!found && rotBuf && (millis() - faceSeenMs < 4000 || lostFrames % 3 == 0)) {
        if (!detectorHalf) detectorHalf = new HumanFaceDetectMSR01(0.1F, 0.5F, 10, 0.4F);
        const float deg = faceRoll != 0 && lostFrames < 8 ? (float)faceRoll : FACE_ROLLS[rollTry++ % 4];
        rotateHalf((const uint16_t*)fb->buf, rotBuf, deg);
        std::list<dl::detect::result_t>& rr = detectorHalf->infer(rotBuf, {120, 160, 3});
        for (auto& r : rr) {
          const int area = (r.box[2] - r.box[0]) * (r.box[3] - r.box[1]);
          if (area > bestArea) {
            bestArea = area;
            found = true;
            unrotateHalf((r.box[0] + r.box[2]) / 2.0f, (r.box[1] + r.box[3]) / 2.0f, deg, &fcx, &fcy);
            fw = 2.0f * (r.box[2] - r.box[0]);
            fh = 2.0f * (r.box[3] - r.box[1]);
            faceRoll = (int)deg;
          }
        }
      }
      detMs = millis() - d0;
      tft.startWrite();

      if (found) {
        faceSeenMs = millis();
        faceSeenGlobalMs = faceSeenMs;
        const int bx1 = (int)(fcx - fw / 2), by1 = (int)(fcy - fh / 2), bx2 = (int)(fcx + fw / 2), by2 = (int)(fcy + fh / 2);
        tft.drawRect(bx1, by1, bx2 - bx1, by2 - by1, faceRoll ? TFT_CYAN : TFT_GREEN);  // cyan: found tilted

        // Error signs match the rotation(3) screen orientation
        int error_x = (bx1 + bx2) / 2 - (int)fb->width / 2;
        int error_y = (by1 + by2) / 2 - (int)fb->height / 2;
        if (abs(error_x) > FACE_DEADBAND_PX) panAngle += constrain(error_x * FACE_GAIN, -FACE_MAX_STEP, FACE_MAX_STEP);
        if (abs(error_y) > FACE_DEADBAND_PX) tiltAngle += constrain(error_y * FACE_GAIN, -FACE_MAX_STEP, FACE_MAX_STEP);
        panAngle = constrain(panAngle, PAN_MIN, PAN_MAX);
        tiltAngle = constrain(tiltAngle, TILT_MIN + 2, TILT_MAX - 5);  // off the hard stops

        headPanTarget = (int)panAngle;
        headTiltTarget = (int)tiltAngle;
        headTargetDirty = true;
      }
      // Face lost: hold the last position (no auto-centering)
    } else {
      updateMotion(fb);
      updateLight();
      int boundary[3];
      const int topRow = floorTopRow;
      analyzeFloor(fb, boundary, topRow);
      const int bandW = fb->width / 3;
      for (int band = 0; band < 3; band++) {
        float f = (float)(FLOOR_BOTTOM_ROW - boundary[band]) / (FLOOR_BOTTOM_ROW - topRow);
        freeEma[band] = 0.5f * freeEma[band] + 0.5f * f;
        camFree[band] = freeEma[band];
        // Yellow line = where the floor ends in this band
        tft.drawFastHLine(band * bandW + 2, boundary[band], bandW - 4, TFT_YELLOW);
        tft.drawFastHLine(band * bandW + 2, boundary[band] + 1, bandW - 4, TFT_YELLOW);
      }
      camFreeMs = millis();
      tft.drawFastVLine(bandW, topRow, 240 - topRow, TFT_DARKGREY);
      tft.drawFastVLine(2 * bandW, topRow, 240 - topRow, TFT_DARKGREY);

      int d = tofMm;
      tft.setTextSize(2);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setCursor(4, 4);
      if (d < 0) tft.printf("%s  ToF --", (const char*)exploreStateName);
      else if (d >= TOF_FAR_MM) tft.printf("%s  ToF far", (const char*)exploreStateName);
      else tft.printf("%s  %dcm", (const char*)exploreStateName, d / 10);
      // Motion check: green = moving, red = not moving, grey = no verdict yet
      int v = motVerdict;
      tft.fillCircle(306, 12, 7, v > 0 ? TFT_GREEN : (v < 0 ? TFT_RED : TFT_DARKGREY));
    }

    tft.endWrite();
    {
      static uint32_t vf = 0;
      static unsigned long vfMs = 0;
      vf++;
      if (millis() - vfMs >= 1000) {
        visFps = vf * 1000.0f / (millis() - vfMs);
        vf = 0;
        vfMs = millis();
      }
    }
    if (decoded) xSemaphoreGive(decMutex);
    else esp_camera_fb_return(fb);
    vTaskDelay(1);
  }
}

// ---- ToF (loop() only: I2C) ----
volatile int tofRawMm = -1, tofRawStatus = -1;  // last raw reading + VL53L0X range status (web diagnostics)

volatile float battVolts = 0;  // filtered battery voltage (loop(), 5x per second)
// ---- Battery (2S LiPo): warnings, back to the start, and sitting down when empty ----
// A ~30 s average decides (servo current dips the voltage for a moment). Below 5 V = no
// battery (USB power): no alerts. Levels are settings ("Battery").
float BAT_CAL = 1.166f;  // battery reading scale (setting "batcal"; the creator's value)
volatile float batPinMv = 0;  // the divider's output at the pin (calibrated mV)
float BAT_EMPTY_V = 6.6f, BAT_FULL_V = 8.05f;  // a fresh charge reads ~8.0-8.05 V (USB-C module stops there)
int BAT_WARN_PCT = 20, BAT_HOME_PCT = 12, BAT_STOP_PCT = 3;
// Off by default: no low-battery warnings, no sitting down, no going home -
// the robot keeps working until the battery dies. The reading and % still show (OLED, page).
bool BAT_ALERTS = false;
// Power-down at empty (setting "batsleep", 1 = on). Sitting down at empty still leaves the ESP32, screen, camera and
// WiFi on (~0.3-0.5 A): a pack runs on down to the BMS cut-off (~2.5 V/cell) and that damages a LiPo.
// Independent of BAT_ALERTS: it protects the pack, it doesn't lock the controls.
int BAT_SLEEP = 1;
#define PWRDN_MAGIC 0x0E0D1E5Au
RTC_DATA_ATTR uint32_t rtcSleepMagic = 0;  // asleep because the pack was empty
RTC_DATA_ATTR float rtcRestPinMv = 0;      // the battery pin at the last 10-minute check (resting, no load)
RTC_DATA_ATTR float rtcBatCal = 1.166f;    // BAT_CAL when it went to sleep (pin mV -> pack volts)
RTC_DATA_ATTR uint16_t rtcSleepChecks = 0; // 10-minute checks so far
// The page's choice for this session (not saved): look for WiFi networks, or stay on the robot's
// own hotspot and never search (the first window on the hotspot asks). tryNow: join right away.
volatile bool staSearch = true, staTryNow = false;
volatile bool apMoveAsked = false;  // "hotspot only" picked on the page: the hotspot may move to the quiet channel
// The network picked on the page is remembered (NVS "wifiS" / "wifiP") and joined at start;
// none picked: the built-in home network. wantSsid() / wantPass(): the one it looks for.
char savedSsid[33] = "", savedPass[65] = "";
volatile unsigned long wifiScanWantMs = 0;  // the page asked for a scan: the WiFi client stays on 30 s
// The radio's channel width on the home network: 20 or 40 MHz (/wifi?bw=, saved as "wifiBw")
int wifiBw = 20;  // 40 MHz on a busy 2.4 GHz band loses packets (downloads at about half the speed of 20 MHz)
volatile int wifiBwReq = 0;
volatile bool wifiSaveReq = false, wifiForgetReq = false;
extern const char* ssid;
extern const char* password;
const char* wantSsid() { return savedSsid[0] ? savedSsid : ssid; }
const char* wantPass() { return savedSsid[0] ? savedPass : password; }
// ---- The governor: one place that decides what gets the robot's time right now. Driving (RC),
// the video, your voice and updates come first; the play scenes, the face's frame rate and the
// OLED give way. perfNow() names the situation (shown on the page).
volatile bool otaActive = false;
const char* perfName = "idle";
volatile int otaPct = -1;                 // update progress (OLED)
volatile bool robotFallen = false;        // lying on its side / back (any mode): legs off, asks for help
unsigned long thanksUntilMs = 0;          // just stood back up: "Thanks" on the OLED
bool batCharging = false, batFull = false;  // from the voltage trend (no charge-detect wire)
long playInS = -1;                        // seconds to the next play scene by itself (-1: none coming)

// Governor health, per 10 s window: the control loop's worst and average period, "janks" (a loop
// over 60 ms = something held it up), for /gov and /logs/governor.csv
volatile float govLoopMaxMs = 0, govLoopAvgMs = 0;
volatile int govJanks = 0;
// Which part of the loop took the time: loop() marks the end of each part (govMark), a slow loop
// is named in the log ("worst loop 1057 ms: battery+oled 1040 ms")
enum { GS_MODES, GS_WIFI, GS_SENSORS, GS_HEAD, GS_PLAY, GS_TELEM, GS_OLED, GS_TOUCH, GS_CMDS, GS_WAIT, GS_N };
const char* const GS_NAMES[GS_N] = {"modes+face", "wifi", "sensors", "head+explore", "play+fall+charge",
                                    "telemetry", "battery+oled", "touch", "commands", "wait (other tasks)"};
float gsMs[GS_N];
unsigned long gsLastUs = 0;
inline void govMark(int part) {
  const unsigned long u = micros();
  gsMs[part] += (u - gsLastUs) / 1000.0f;
  gsLastUs = u;
}
char govWorst[72] = "";           // the last 10 s window's slowest loop and its slowest part
volatile int batPctShown = -1;    // the % shown (OLED, page): moves slowly, see batShownUpdate
volatile int oledFails = 0, oledRecoveries = 0;  // the OLED not answering (oledHealth)
volatile int pcaResets = 0;  // the servo board found reset (a power dip) and set up again
// Play scenes (see playUpdate): how often by itself, sounds on, how far the head follows
int PLAY_EVERY_S = 90;      // mean seconds between scenes while idle (0 = only when asked)
bool playSound = true;      // the scenes' little sounds
bool emoteSndOn = true;     // every emote's own sound (the page's "Emote sounds" switch, /audio?emosnd=1|0, saved)
unsigned long voiceLastMs = 0;  // the last voice sound (an emote's or a face's): never two on top of each other
bool playAuto = true;       // scenes by themselves at all (the page's "Play by itself" switch)
float PLAY_HEAD_DEG = 22;   // deg at full scale (always within the head calibration)
float batSlowV = 0;
volatile int batPctSlow = -1;
volatile int batState = 0;         // -1 USB / no battery, 0 ok, 1 low, 2 very low, 3 empty (sitting)
volatile bool batEmpty = false;    // sitting, motion refused until it's charged
int OLED_MARGIN = 3;               // (single margin, superseded by the four below)
// The OLED's visible area (the robot's frame covers its edges): px kept clear on each side. Set
// on the page ("OLED screen" panel, /oled), saved in NVS (oledL/T/R/B). The test frame shows the box.
int OLED_ML = 3, OLED_MT = 3, OLED_MR = 3, OLED_MB = 3;
volatile unsigned long oledTestUntil = 0;
volatile int battPercent = 0;

// Settings saves are flash writes too: queued here and done by the log task while the
// camera is off (see xlog.h)
struct PendingPref { char key[16]; int32_t val; bool isBool; };
const int PREF_Q = 12;  // queue and the copy in applyPendingPrefs: must be the same size (a smaller copy crashes)
PendingPref pendingPrefs[PREF_Q];
int pendingPrefN = 0;
portMUX_TYPE prefMux = portMUX_INITIALIZER_UNLOCKED;

void prefPut(const char* key, int32_t val, bool isBool) {
  portENTER_CRITICAL(&prefMux);
  int i = 0;
  while (i < pendingPrefN && strcmp(pendingPrefs[i].key, key) != 0) i++;
  if (i < PREF_Q) {
    strlcpy(pendingPrefs[i].key, key, sizeof(pendingPrefs[i].key));
    pendingPrefs[i].val = val;
    pendingPrefs[i].isBool = isBool;
    if (i == pendingPrefN) pendingPrefN++;
  }
  portEXIT_CRITICAL(&prefMux);
  xl::flushSoon = true;
}

void exParamsSaveIfDirty();
void batteryUpdate(float v);  // below (needs the Explore state)

// Battery % = the share of run time left, from the prototype's own discharge log (SD /logs/battery.csv:
// ~25 runs, minutes per 0.1 V with the legs powered - about 170 min from 8.25 V to the 6.6 V brown-out). The old
// textbook LiPo curve said 12 % at 7.05 V, but a third of the run time is still left there: it sat at "the last
// few %" for an hour. The table is stretched between BAT_EMPTY_V (0 %) and BAT_FULL_V (what a fresh charge reads here:
// the USB-C charger stops near 8.0-8.05 V), so the empty end stays at the brown-out whatever the charger reaches.
int batPercentOf(float v) {
  static const float packV[] = {6.60f, 6.75f, 6.85f, 6.95f, 7.05f, 7.15f, 7.25f, 7.35f, 7.45f,
                                7.55f, 7.65f, 7.75f, 7.85f, 7.95f, 8.05f, 8.15f, 8.25f};
  static const float pct[] = {0, 6, 22, 30, 34, 40, 47, 54, 58, 63, 67, 71, 75, 81, 86, 96, 100};
  const int n = sizeof(pct) / sizeof(pct[0]);
  const float c = 6.60f + (v - BAT_EMPTY_V) * (8.25f - 6.60f) / (BAT_FULL_V - BAT_EMPTY_V);
  if (c <= packV[0]) return 0;
  for (int i = 1; i < n; i++)
    if (c < packV[i]) return (int)lroundf(pct[i - 1] + (pct[i] - pct[i - 1]) * (c - packV[i - 1]) / (packV[i] - packV[i - 1]));
  return 100;
}

void applyPendingPrefs() {
  PendingPref local[PREF_Q];
  portENTER_CRITICAL(&prefMux);
  const int n = pendingPrefN;
  memcpy(local, pendingPrefs, sizeof(PendingPref) * n);
  pendingPrefN = 0;
  portEXIT_CRITICAL(&prefMux);
  for (int i = 0; i < n; i++) {
    if (local[i].isBool) prefs.putBool(local[i].key, local[i].val != 0);
    else prefs.putInt(local[i].key, local[i].val);
  }
  const uint8_t w = walkSaveReq;  // the walks saved / forgotten on the page
  walkSaveReq = 0;
  if (w & 1) prefs.putBytes("walkF", &walkFwd, sizeof(walkFwd));
  if (w & 2) prefs.putBytes("walkB", &walkBack, sizeof(walkBack));
  if (w & 4) prefs.remove("walkF");
  if (w & 8) prefs.remove("walkB");
  exParamsSaveIfDirty();
}

// The ToF only runs while it's used: Explore (from the moment it's asked for), or a minute after
// the head calibration panel (its floor glances). Otherwise stopped: laser off, no I2C reads.
void tofPower() {
  if (!tofOk) return;
  const bool want = activeMode == MODE_EXPLORE || requestedMode == MODE_EXPLORE || activeMode == MODE_LIFE ||
                    (tofNeedMs && millis() - tofNeedMs < 60000);
  if (want == tofRunning) return;
  tofRunning = want;
  if (want) lox.startRangeContinuous(33);
  else lox.stopRangeContinuous();
}

void pollTof() {
  static int hist[3] = {TOF_FAR_MM, TOF_FAR_MM, TOF_FAR_MM};
  static int idx = 0;
  tofPower();
  if (!tofOk || !tofRunning || !lox.isRangeComplete()) return;
  uint16_t r = lox.readRangeResult();
  tofRawMm = r;
  tofRawStatus = lox.readRangeStatus();  // 0 = valid, 2 = signal too weak, 4 = out of range
  int mm = (r == 0xFFFF || r >= 8000) ? TOF_FAR_MM : r;  // out of range / weak signal = open space
  hist[idx] = mm;
  idx = (idx + 1) % 3;
  // Median of the last 3 readings rejects single-sample spikes
  int a = hist[0], b = hist[1], c = hist[2];
  tofMm = max(min(a, b), min(max(a, b), c));
  tofSeq++;
}

// ---- MPU6050 (loop() only: I2C) ----
// The board's MPU6050 is a clone whose WHO_AM_I reads 0x00, so Adafruit_MPU6050::begin()
// rejects it. The register map is standard, so it is read directly here.
const uint8_t MPU_ADDR = 0x68;
// The I2C bus speed: 400 kHz (fast mode) when every device on it reads back cleanly at that speed,
// else the 100 kHz default. (The OLED library pushes its frames at 400 kHz anyway and then drops the bus to
// 100 kHz - i2cSpeedRestore() after each display() puts the chosen speed back.)
uint32_t i2cHz = 100000;
inline void i2cSpeedRestore() { Wire.setClock(i2cHz); }
// one register read from a device: true when it answers with a byte
static bool i2cProbeRead(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)1) != 1) return false;
  Wire.read();
  return true;
}
void i2cPickSpeed() {
  // which devices answer at the safe speed (servo board 0x40 MODE1, IMU 0x68 PWR_MGMT, ToF 0x29 model id, OLED 0x3C ack)
  struct D { uint8_t a, r; bool here; } d[] = {{0x40, 0x00, false}, {MPU_ADDR, 0x6B, false}, {0x29, 0xC0, false}, {0x3C, 0xFF, false}};
  Wire.setClock(100000);
  for (auto& x : d) {
    if (x.a == 0x3C) { Wire.beginTransmission(x.a); x.here = Wire.endTransmission() == 0; }
    else x.here = i2cProbeRead(x.a, x.r) && i2cProbeRead(x.a, x.r);
  }
  Wire.setClock(400000);
  int fails = 0, tries = 0;
  for (int k = 0; k < 20; k++)
    for (auto& x : d) {
      if (!x.here) continue;
      tries++;
      if (x.a == 0x3C) { Wire.beginTransmission(x.a); if (Wire.endTransmission() != 0) fails++; }
      else if (!i2cProbeRead(x.a, x.r)) fails++;
    }
  i2cHz = (tries > 0 && fails == 0) ? 400000 : 100000;
  Wire.setClock(i2cHz);
  Serial.printf("I2C %u kHz (%d/%d reads failed at 400 kHz)\n", (unsigned)(i2cHz / 1000), fails, tries);
  xlogSys("I2C %u kHz (%d of %d test reads failed at 400 kHz)", (unsigned)(i2cHz / 1000), fails, tries);
}
bool imuOk = false;
float imuAcc[3] = {0, 0, 1};    // g, raw chip axes (chip is mounted upside down: z ~ -1 g)
float imuGyro[3] = {0, 0, 0};   // deg/s, bias-corrected
float gyroBias[3] = {0, 0, 0};
float gravLp[3] = {0, 0, 1};    // ~0.3 s low-pass accel: gravity + slow sway (for jolts)
float gravSlow[3] = {0, 0, 1};  // ~0.8 s low-pass: posture averaged over a gait cycle (for tilt)
float gravBase[3] = {0, 0, 1};  // gravity direction standing on the floor when Explore started
float imuTiltDeg = 0;           // cycle-averaged body tilt vs gravBase (mounting-independent)
float imuTiltFastDeg = 0;       // ~0.3 s tilt vs gravBase: a leg dropping off an edge
float imuYawAngle = 0;          // integrated rotation around the vertical axis (deg)
float imuYawRate = 0;           // deg/s around the vertical axis, smoothed
float chkG[3] = {0, 0, 1};      // gyro + accel "up" (unit, chip axes): the chicken head's fast pitch
bool balanceOn = false;         // balance: standing still in walk mode, the knees keep the body level on a slope (/gait?balance=1, saved)
float balPitch0 = 0, balRoll0 = 0;  // what "level" reads on a flat floor (/gait?balzero=1, saved)
bool balZeroSet = false;            // level was set (by hand, or taken the first time balance runs)
int balPitchSign = 1, balRollSign = 1;  // which way each correction goes (checked as it works: flipped if it made it worse)
float balOffP = 0, balOffR = 0;     // the correction now (deg of tilt being undone)
bool chickenOn = false;         // chicken head: the head keeps its world direction while the body tilts / turns (/gait?chicken=1)
float imuShakeRms = 0;          // running RMS of the high-passed accel = normal gait shake (g)
float imuJolt = 0;              // latest high-passed accel magnitude (g)
bool imuCalibrating = false;
float imuCalGyro[3], imuCalAcc[3];
int imuCalN = 0;

void mpuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

// A slow I2C call (the loop can stall ~1 s now and then in this part): what, how long, the error.
// govTick puts it in the governor's log.
char i2cSlowNote[96] = "";
bool imuReadRaw(float acc[3], float gyro[3]) {
  const unsigned long t0 = millis();
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);  // ACCEL_XOUT_H .. GYRO_ZOUT_L
  const uint8_t e = Wire.endTransmission(false);
  const unsigned long t1 = millis();
  const uint8_t got = e == 0 ? Wire.requestFrom(MPU_ADDR, (uint8_t)14) : 0;
  const unsigned long t2 = millis();
  if (t2 - t0 > 100 && !i2cSlowNote[0])
    snprintf(i2cSlowNote, sizeof(i2cSlowNote), "IMU read %lu ms (write %lu ms err %u, read %lu ms got %u)", t2 - t0, t1 - t0,
             e, t2 - t1, got);
  if (e != 0 || got != 14) return false;
  int16_t v[7];
  for (int i = 0; i < 7; i++) v[i] = (int16_t)((Wire.read() << 8) | Wire.read());
  for (int i = 0; i < 3; i++) {
    acc[i] = v[i] / 4096.0f;      // +-8 g
    gyro[i] = v[4 + i] / 65.5f;   // +-500 dps
  }
  return true;
}

bool imuBegin() {
  mpuWrite(0x6B, 0x00);  // wake up, internal clock
  delay(50);
  mpuWrite(0x1A, 0x04);  // DLPF ~20 Hz: smooths servo / foot-strike vibration
  mpuWrite(0x1B, 0x08);  // gyro +-500 dps
  mpuWrite(0x1C, 0x10);  // accel +-8 g
  delay(20);
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1C);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MPU_ADDR, (uint8_t)1) != 1) return false;
  if (Wire.read() != 0x10) return false;  // config did not stick

  // Initial gyro bias + gravity reference (Explore re-does this standing on the floor)
  float a[3], g[3], sa[3] = {0, 0, 0}, sg[3] = {0, 0, 0};
  int n = 0;
  for (int k = 0; k < 60; k++) {
    if (imuReadRaw(a, g)) {
      for (int i = 0; i < 3; i++) { sa[i] += a[i]; sg[i] += g[i]; }
      n++;
    }
    delay(5);
  }
  if (n == 0) return false;
  for (int i = 0; i < 3; i++) {
    gyroBias[i] = sg[i] / n;
    gravBase[i] = gravLp[i] = gravSlow[i] = sa[i] / n;
  }
  return true;
}

// Start / finish re-calibrating while the robot stands still (Explore start)
float imuCalGyro2[3] = {0, 0, 0};

void imuCalStart() {
  for (int i = 0; i < 3; i++) imuCalGyro[i] = imuCalAcc[i] = imuCalGyro2[i] = 0;
  imuCalN = 0;
  imuCalibrating = true;
}

// A gyro zero point taken while the body still moved (settling after standing up, someone
// holding it) makes the heading run away (every walk then looks like "swivel", every turn like
// "can't turn"). So the zero point is only taken when the rate
// held steady; otherwise the old one stays (and the stillness check below keeps it right).
void imuCalFinish() {
  imuCalibrating = false;
  if (imuCalN < 10) return;
  float worst = 0;
  for (int i = 0; i < 3; i++) {
    const float m = imuCalGyro[i] / imuCalN;
    worst = max(worst, sqrtf(max(0.0f, imuCalGyro2[i] / imuCalN - m * m)));
  }
  for (int i = 0; i < 3; i++) {
    if (worst < 1.5f) gyroBias[i] = imuCalGyro[i] / imuCalN;
    gravBase[i] = gravLp[i] = gravSlow[i] = imuCalAcc[i] / imuCalN;
  }
  if (worst >= 1.5f) xlog("IMU moved while calibrating (gyro wobble %.1f deg/s) - kept the old zero point", worst);
  imuYawAngle = 0;
}

// Zero-point tracking: every second the gyro is checked; if the body was truly still (rate
// steady within 0.8 deg/s on every axis and no shaking), whatever it reads is drift, and the
// zero point moves most of the way there. Walking and turning always wobble, so they never count.
void imuTrackBias(const float g[3]) {
  static float sum[3] = {0, 0, 0}, sum2[3] = {0, 0, 0};
  static int n = 0;
  static unsigned long t0 = 0;
  const unsigned long now = millis();
  if (!t0) t0 = now;
  for (int i = 0; i < 3; i++) { sum[i] += g[i]; sum2[i] += g[i] * g[i]; }
  n++;
  if (now - t0 < 1000) return;
  float worst = 0, off = 0, mean[3];
  for (int i = 0; i < 3; i++) {
    mean[i] = sum[i] / n;
    worst = max(worst, sqrtf(max(0.0f, sum2[i] / n - mean[i] * mean[i])));
    off = max(off, fabsf(mean[i] - gyroBias[i]));
  }
  // A steady turn looks just like a zero-point error (a constant rate, no shake): carried round by hand, the
  // chicken head, an eased turn - it would leak into the bias and the heading would drift. A real drift
  // is a few deg/s: small offsets are taken at once, a big one only if it holds for 5 windows in a row.
  static float bigMean[3] = {0, 0, 0};
  static int bigN = 0;
  bool bigOk = false;
  if (off >= 8.0f && worst < 0.8f && imuShakeRms < 0.05f) {
    bool same = bigN > 0;
    for (int i = 0; i < 3 && same; i++) same = fabsf(mean[i] - bigMean[i]) < 1.0f;
    bigN = same ? bigN + 1 : 1;
    for (int i = 0; i < 3; i++) bigMean[i] = mean[i];
    bigOk = bigN >= 5;
  } else {
    bigN = 0;
  }
  if (n >= 20 && worst < 0.8f && imuShakeRms < 0.05f && (off < 8.0f || (bigOk && off < 60.0f)) && !imuCalibrating) {
    bigN = 0;
    for (int i = 0; i < 3; i++) gyroBias[i] += 0.7f * (mean[i] - gyroBias[i]);
    if (off > 2.0f) xlog("IMU zero point corrected by %.1f deg/s (standing still)", off);
  }
  for (int i = 0; i < 3; i++) sum[i] = sum2[i] = 0;
  n = 0;
  t0 = now;
}

void imuUpdate() {
  static unsigned long lastUs = 0;
  if (!imuOk) return;
  float a[3], g[3];
  if (!imuReadRaw(a, g)) return;
  unsigned long nowUs = micros();
  float dt = lastUs ? (nowUs - lastUs) / 1e6f : 0.01f;
  lastUs = nowUs;
  const float dtGap = constrain(dt, 0.001f, 1.5f);  // (yaw: a 1 s I2C stall - its turning counted too, see below)
  dt = constrain(dt, 0.001f, 0.2f);

  if (imuCalibrating) {
    for (int i = 0; i < 3; i++) { imuCalGyro[i] += g[i]; imuCalGyro2[i] += g[i] * g[i]; imuCalAcc[i] += a[i]; }
    imuCalN++;
  }
  imuTrackBias(g);
  for (int i = 0; i < 3; i++) {
    imuAcc[i] = a[i];
    imuGyro[i] = g[i] - gyroBias[i];
  }

  // Gravity estimates and the high-passed rest = shake / jolts
  const float kg = dt / (0.3f + dt);
  const float ks = dt / (0.8f + dt);  // longer than a gait cycle: the walking sway averages out
  float hp2 = 0;
  for (int i = 0; i < 3; i++) {
    gravLp[i] += kg * (a[i] - gravLp[i]);
    gravSlow[i] += ks * (a[i] - gravSlow[i]);
    float h = a[i] - gravLp[i];
    hp2 += h * h;
  }
  imuJolt = sqrtf(hp2);
  const float kr = dt / (2.0f + dt);  // ~2 s RMS window
  imuShakeRms = sqrtf(imuShakeRms * imuShakeRms + kr * (hp2 - imuShakeRms * imuShakeRms));

  // Tilt = angle between the cycle-averaged and the flat-floor gravity direction
  float dot = 0, n1 = 0, n2 = 0;
  for (int i = 0; i < 3; i++) {
    dot += gravSlow[i] * gravBase[i];
    n1 += gravSlow[i] * gravSlow[i];
    n2 += gravBase[i] * gravBase[i];
  }
  float c = (n1 > 0 && n2 > 0) ? dot / sqrtf(n1 * n2) : 1;
  imuTiltDeg = acosf(constrain(c, -1.0f, 1.0f)) * 57.2958f;
  float dotf = 0, nf = 0;
  for (int i = 0; i < 3; i++) {
    dotf += gravLp[i] * gravBase[i];
    nf += gravLp[i] * gravLp[i];
  }
  float cf = (nf > 0 && n2 > 0) ? dotf / sqrtf(nf * n2) : 1;
  imuTiltFastDeg = acosf(constrain(cf, -1.0f, 1.0f)) * 57.2958f;

  // Rotation around the vertical (gravity) axis
  float yaw = 0;
  if (n2 > 0) {
    for (int i = 0; i < 3; i++) yaw += imuGyro[i] * gravBase[i];
    yaw /= sqrtf(n2);
  }
  // the rate before and after the gap, averaged over all of it (capping it would lose the turning during the
  // IMU read's occasional 1 s stalls: the heading would drift, straight walks curve)
  static float yawPrev = 0;
  // a garbled read (the I2C bus: one bad sample, maybe right before a 1 s stall) would add tens of degrees at once
  // and walk-straight would steer toward a heading that never was: a rate that jumps > 150 deg/s from one sample
  // to the next is ignored (the body can't), unless it stays (then it's real: a fall, a hand spinning it)
  static int yawSpikes = 0;
  if (fabsf(yaw - yawPrev) > 150.0f && yawSpikes < 3) { yawSpikes++; yaw = yawPrev; }
  else yawSpikes = 0;
  imuYawAngle += 0.5f * (yaw + yawPrev) * dtGap;
  yawPrev = yaw;
  imuYawRate += (dt / (0.2f + dt)) * (yaw - imuYawRate);

  // A fast, steady "which way is up" for the chicken head: turned with the gyro every read (quick - no lag),
  // pulled gently toward the accelerometer (no drift); a jolt (not ~1 g) barely counts
  {
    static bool init = false;
    if (!init) { for (int i = 0; i < 3; i++) chkG[i] = a[i]; init = true; }
    const float r = 0.0174533f * dt;
    const float w0 = imuGyro[0] * r, w1 = imuGyro[1] * r, w2 = imuGyro[2] * r;
    const float c0 = w1 * chkG[2] - w2 * chkG[1], c1 = w2 * chkG[0] - w0 * chkG[2], c2 = w0 * chkG[1] - w1 * chkG[0];
    chkG[0] -= c0; chkG[1] -= c1; chkG[2] -= c2;  // a world-fixed vector seen from the body: v' = -w x v
    const float an = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    const float k = fabsf(an - 1.0f) < 0.15f ? dt / (0.6f + dt) : dt / (4.0f + dt);
    for (int i = 0; i < 3; i++) chkG[i] += k * (a[i] - chkG[i]);
    const float n = sqrtf(chkG[0] * chkG[0] + chkG[1] * chkG[1] + chkG[2] * chkG[2]);
    if (n > 0.1f) for (int i = 0; i < 3; i++) chkG[i] /= n;
  }
}

// ---- Explore state machine (loop() only) ----
// Walks around a room like a person would - head upright - and checks that every action
// actually worked:
//  - Forward / back: confirmed with the camera motion check (and the ToF when a wall is in
//    range). Sliding in place on a smooth floor feels like walking to the IMU, not to the
//    camera.
//  - Turns: turned by an angle measured with the gyro; no rotation = the turn failed.
//  - Something close ahead (ToF / camera floor check): slow down, stop, sweep the head slowly
//    across ~150 deg reading the ToF every ~2 deg, turn toward the middle of the widest gap
//    it fits through. It backs up only in a dead end.
//  - Table edges (web toggle): the head glances down so the ToF checks the floor ~36 cm
//    ahead only when there's a reason - after walking a stretch (~3 s at full speed), when
//    the camera sees the floor ending with no wall there, and after every turn before
//    walking off. An odd reading while walking is re-checked standing still first.
//    The robot's geometry is fixed, so the floor is always at a known distance for each
//    look-down angle; much longer / out of range = edge -> back up and turn away. It never
//    walks off without having seen floor ahead: at the start it turns on the spot until it
//    finds floor and gives up after a full circle. A fast body tilt is the backup.
//  - Any failed action or alarm: stop, back up (verified), look around and choose a new way -
//    never the turn that just failed, away from recently blocked headings. Repeated failures
//    -> turn further; 8 in a row -> stop and ask for help.
//  - Dark: the ToF and IMU work in the dark, the camera doesn't. Lights off -> camera checks
//    switch off (ToF + IMU only, slower). A dark place ahead in a lit room (under a bed) is
//    avoided like an obstacle (web toggle): the ToF may call it open, and it's where it'd
//    get wedged; the sweep also skips directions that look dark.
//  - Walking: curves gently around things it sees coming (Fwd-L / Fwd-R) and wanders a
//    little on open floor. Turns use the skid-steer sharp turn (smooth_sharp_*); with no floor that
//    way, in a tight space or when that turn is blocked, it turns on the spot with Sharp L / R
//    (sharp_*: the creator's turn played smoothly - the creator's step turn often fails on the spot),
//    because the skid-steer still moves forward a bit.
// Gaits reuse the walk engine CPG presets via currentCommand.

// ---- The creator's on-the-spot turn (D-pad left / right) ----
// Same keyframes as runTurnLeft() / runTurnRight() in movement-sequences.h, but played one
// frame per loop() pass instead of blocking, so the ToF, IMU and Explore keep running.
// Each frame moves two servos: {servo, angle, servo, angle}
const int16_t STEP_TURN_LEFT[8][4] = {
  {R3, 78, L4, 75}, {R1, 62, L2, 78}, {R3, 107, L4, 132}, {R1, 23, L2, 39},
  {R4, 50, L3, 46}, {R2, 156, L1, 64}, {R4, -8, L3, 20}, {R2, 115, L1, 23}};
const int16_t STEP_TURN_RIGHT[8][4] = {
  {R4, 50, L3, 46}, {R2, 74, L1, -18}, {R4, -8, L3, 20}, {R2, 115, L1, 23},
  {R3, 78, L4, 75}, {R1, -16, L2, 0}, {R3, 107, L4, 132}, {R1, 23, L2, 39}};
int stepTurnFrame = -1;  // -1 = not turning
bool stepTurnLeftNow = false;
unsigned long stepTurnMs = 0;

void stepTurnUpdate(bool left) {
  static float from0 = 0, from1 = 0;
  static unsigned long lastCallMs = 0;
  unsigned long now = millis();
  // A new turn (not called for a moment, or the other way): from the first frame, all eight legs at
  // the stand first (otherwise a hip swings back from the last turn's frame, and legs the first frames
  // don't move stay where the walk left them).
  if (left != stepTurnLeftNow || now - lastCallMs > 300) {
    stepTurnFrame = -1;
    bool there = legsPowered && legsPoseKnown;
    for (int i = 0; i < 8 && there; i++) there = fabsf(currentAngle[i] - POSE_STAND[i]) <= 2;
    if (!there) moveSmoothHelper(200, POSE_STAND[0], POSE_STAND[1], POSE_STAND[2], POSE_STAND[3], POSE_STAND[4], POSE_STAND[5], POSE_STAND[6], POSE_STAND[7]);
    now = millis();
  }
  lastCallMs = now;
  if (stepTurnFrame >= 0 && now - stepTurnMs < (unsigned long)frameDelayLR) {
    // the frame's two joints glide there over the whole frame (a shorter glide leaves only 1-3 writes a
    // frame at the loop's pace: steppy turns)
    const int16_t* f = stepTurnLeftNow ? STEP_TURN_LEFT[stepTurnFrame] : STEP_TURN_RIGHT[stepTurnFrame];
    const float u = min(1.0f, (now - stepTurnMs) / (float)frameDelayLR), e = u * u * (3 - 2 * u);
    setServoAngle(f[0], (int)lroundf(from0 + (f[1] - from0) * e));
    setServoAngle(f[2], (int)lroundf(from1 + (f[3] - from1) * e));
    return;
  }
  stepTurnLeftNow = left;
  stepTurnFrame = (stepTurnFrame + 1) % 8;
  const int16_t* f = left ? STEP_TURN_LEFT[stepTurnFrame] : STEP_TURN_RIGHT[stepTurnFrame];
  from0 = currentAngle[f[0]];
  from1 = currentAngle[f[2]];
  stepTurnMs = now;
  cpgPhase = stepTurnFrame * (6.283185307f / 8);  // for the camera motion check
  cpgPeriodMs = 8.0f * frameDelayLR;
}

// ---- Sharp L / R (the page's buttons, "sharp_left" / "sharp_right"): the creator's on-the-spot turn, smooth ----
// The creator's keyframes (above): each diagonal pair in turn - knees up, hips swing (in the air), knees down,
// hips swing back (on the floor: that's the push that turns it); the other pair stands meanwhile. Played as one
// continuous eased motion instead of 8 snapped frames, 1.5x slower, the feet always lifted before a hip swings
// (the CPG versions swung them on the floor: dragging). The gyro gently sizes the swings for a steady turn.
extern int yawLeftSign;  // (below: the gyro's left sign)
bool sharpActive = false;
float sharpScale = 1;
void sharpTurnUpdate(bool left) {
  static unsigned long t0 = 0, lastMs = 0;
  static bool lastLeft = false;
  unsigned long now = millis();
  if (!sharpActive || left != lastLeft || now - lastMs > 300) {  // starting: from the stand
    bool there = legsPowered && legsPoseKnown;
    for (int i = 0; i < 8 && there; i++) there = fabsf(currentAngle[i] - POSE_STAND[i]) <= 2;
    if (!there) moveSmoothHelper(250, POSE_STAND[0], POSE_STAND[1], POSE_STAND[2], POSE_STAND[3], POSE_STAND[4], POSE_STAND[5], POSE_STAND[6], POSE_STAND[7]);
    now = millis();
    t0 = now;
    sharpScale = 1;
  }
  const float dt = constrain((now - lastMs) / 1000.0f, 0.0f, 0.1f);
  lastMs = now;
  sharpActive = true;
  lastLeft = left;
  // the two pairs (servo, the creator's target) in the creator's order: left = R1/L2 first, right = R2/L1 first
  struct Pair { int h1, h2, k1, k2; float hT1, hT2, kT1, kT2; };
  static const Pair LA = {R1, L2, R3, L4, 62, 78, 78, 75}, LB = {R2, L1, R4, L3, 156, 64, 50, 46};
  static const Pair RA = {R2, L1, R4, L3, 74, -18, 50, 46}, RB = {R1, L2, R3, L4, -16, 0, 78, 75};
  const Pair* P[2] = {left ? &LA : &RA, left ? &LB : &RB};
  const float T = 8.0f * frameDelayLR * 1.5f;  // one whole cycle (both pairs), ms
  const float ph = fmodf((now - t0) / T, 1.0f);
  auto ss = [](float a, float b, float x) { const float u = constrain((x - a) / (b - a), 0.0f, 1.0f); return u * u * (3 - 2 * u); };
  // the gyro: aim for ~40 deg/s, gently (0.8 .. 1.15 of the creator's swing)
  if (yawLeftSign != 0 && now - t0 > 800) {
    const float rate = yawLeftSign * imuYawRate * (left ? 1 : -1);
    sharpScale = constrain(sharpScale + 0.002f * (40 - rate) * dt, 0.8f, 1.15f);
  }
  for (int p = 0; p < 2; p++) {
    const Pair& q = *P[p];
    float x = ph * 2 - p;  // this pair's own 0..1 in its half of the cycle
    const bool mine = x >= 0 && x < 1;
    float lift = 0, swing = 0;
    if (mine) {
      lift = ss(0.0f, 0.25f, x) * (1 - ss(0.55f, 0.78f, x));  // up, stay up, down
      swing = ss(0.18f, 0.50f, x) * (1 - ss(0.72f, 1.0f, x));  // swing while up, back on the floor
    }
    setServoAngle(q.k1, (int)lroundf(POSE_STAND[q.k1] + (q.kT1 - POSE_STAND[q.k1]) * lift));
    setServoAngle(q.k2, (int)lroundf(POSE_STAND[q.k2] + (q.kT2 - POSE_STAND[q.k2]) * lift));
    setServoAngle(q.h1, (int)lroundf(POSE_STAND[q.h1] + (q.hT1 - POSE_STAND[q.h1]) * swing * sharpScale));
    setServoAngle(q.h2, (int)lroundf(POSE_STAND[q.h2] + (q.hT2 - POSE_STAND[q.h2]) * swing * sharpScale));
  }
  cpgPhase = ph * 6.283185307f;  // for Explore's camera motion check (the gait's own shake)
  cpgPeriodMs = T;
}

int   EX_STOP_MM      = 120;   // obstacle closer than this ahead -> stop and look around
int   EX_CLEAR_MM     = 300;   // after a turn, the way ahead counts as open beyond this
int   EX_OPEN_MM      = 400;   // a direction seen while looking around counts as open beyond this
int   EX_TIGHT_MM     = 550;   // best open direction closer than this = tight space
int   EX_TOO_CLOSE_MM = 90;    // stopped this close: Sharp L / R's forward creep would touch it
float EX_CAM_BLOCK    = 0.15;  // camera: floor visible for less than 15% of the lower image
int   EX_CAM_BLOCK_FRAMES = 4;
// The camera can't see the floor closer than ~36 cm with the head upright, so a wall ahead
// "ends the floor" early. Camera stop / slow-down / curving only count when the ToF sees
// nothing this close; a wall the ToF sees is left to the ToF.
int   EX_CAM_TOF_MM   = 600;
// Looking around: the head sweeps slowly from one side to the other while the ToF keeps
// reading (every ~33 ms = every ~1.7 deg), like a small radar. +-75 deg reaches along a
// wall the robot faces head-on (+-55 sees the same wall on both sides).
const int   EX_SWEEP_MAX_DEG = 75;     // each side (pan mid 60 +- 75 = -15..135, inside the limits)
const int   EX_SWEEP_BIN_DEG = 5;
const int   EX_SWEEP_BINS    = 2 * EX_SWEEP_MAX_DEG / EX_SWEEP_BIN_DEG + 1;  // 31; bin 0 = right
float EX_SWEEP_DPS     = 50;     // head speed while sweeping (deg/s): ~3 s side to side
const unsigned long EX_SWEEP_START_MS = 350;  // head travel to the start side
unsigned long EX_SWEEP_LAG_MS   = 80;   // a reading shows where the head was this long ago
const unsigned long EX_SWEEP_END_MS   = 150;  // keep reading at the far end a moment
int   EX_GAP_HALF_DEG  = 10;     // the body needs room about this far either side of its path
int   EX_WALL_EDGE_DEG = 55;     // facing a wall: its far ends are seen beyond this angle...
float EX_WALL_RECEDE   = 1.8;    // ...at least this many times further than straight ahead
int   EX_WALL_MIN_MM   = 180;    // (and at least this far)
int   EX_SCAN_TILT_UP  = 8;      // look a bit higher while sweeping: over the floor, longer range
unsigned long EX_SETTLE_MS = 350;   // let the legs stand before looking around
unsigned long EX_BACKUP_TIGHT_MS = 1000;  // dead end: back up this long, then turn around
// Body alarms (MPU6050), tuned so normal walking sway / shake does not trigger them
float EX_TILT_STUCK_DEG = 15;   // cycle-averaged tilt vs flat floor -> hung up / climbing
unsigned long EX_TILT_MS = 600;
float EX_FALL_DEG       = 55;   // fell over -> stop exploring
float EX_FALL_PITCH_DEG = 45;   // ...tipped forward/back: the head or tail props it up at ~40-50 deg, it never gets to 55
float EX_JOLT_G         = 0.8;  // sudden jolt, and at least EX_JOLT_RATIO x normal gait shake
float EX_JOLT_RATIO     = 4.0;
unsigned long EX_SENSE_HOLDOFF_MS = 1500;  // ignore gait start-up shake
float EX_SWIVEL_DEG     = 40;   // rotated this much in EX_WINDOW_MS while walking straight
unsigned long EX_WINDOW_MS = 1500;
// Action checks
float EX_TURN_TARGET_DEG = 40;      // minimum turn away from something low (unseen when scanning)
float EX_TURN_STUCK_DEG  = 60;      // minimum turn after getting stuck
float EX_TURN_ESCAPE_DEG = 120;     // ...after repeated failures
float EX_TURN_AROUND_DEG = 160;     // dead end
float EX_TURN_EXTRA_DEG  = 45;      // not clear at the chosen angle: turn up to this much further
float EX_TURN_ROT_MIN_DEG = 8;      // must rotate at least this per EX_TURN_WIN_MS
unsigned long EX_TURN_WIN_MS = 2200;  // (shorter judges slow left turns "blocked", see EX_TURN)
unsigned long EX_TURN_MAX_MS = 9000;
int   EX_STILL_CHECKS   = 4;        // camera says "not moving" this many times in a row...
unsigned long EX_STILL_MS = 1200;   // ...for at least this long, and only when the ToF sees
float EX_MOT_NEAR_CAM   = 0.5;      // nothing but the camera sees this little free floor
unsigned long EX_WALK_OK_MS = 6000; // walked this long without trouble -> failures forgiven
const int   EX_MOVING_CHECKS  = 4;        // camera says "moving" this often -> walk verified
int   EX_MAX_FAILS      = 8;        // failures in a row before asking for help
// Frozen: walking (straight or curving) but neither the distance ahead nor the heading changes
// = pushing against something (curves flipping side to side keep restarting the other checks).
// Help from the page while exploring (the Explore D-pad): the user drives, the log records what
// the robot was doing and what the user did about it, then it carries on by itself. And a
// "something's wrong here" mark with a snapshot. For finding where Explore goes wrong.
volatile char exManualCmd[4] = "";         // "" = none; f b l r sl sr
volatile unsigned long exManualSeenMs = 0;  // the page repeats it while a button is held
volatile bool exMarkReq = false;
bool exManualOn = false;
char exManualLast[4] = "";
unsigned long exManualStartMs = 0;
float exManualYaw0 = 0, exManualOdoX0 = 0, exManualOdoY0 = 0;
// ---- Climbing low flat things (a book, a carpet edge) ----
// Stuck walking forward while the ToF at head height sees nothing close = something low is
// in the way of the legs. It stands and looks down at the floor-check angles: all three
// readings in the proportions of a flat surface, and the height they give (from how much
// closer than the floor they read) under the limit -> it tries: bigger steps (legs lift higher,
// swing further), slower, the IMU watching the body's tilt (too steep = back off). Otherwise
// the normal stuck handling. Limits: the ToF looks down at most ~35 deg (the head's down limit)
// and lands 30-40 cm ahead, so a small object right at the feet may not be under the beam (it
// then reads as floor: height ~0, and only the tilt check guards the try); 1-2 cm is within
// the ToF's noise.
int EX_CLIMB_ON = 0;  // off by default: it tends to climb into things at its feet
int EX_CLIMB_MAX_MM = 30;
float EX_CLIMB_AMP = 1.4f;         // gait amplitude while climbing (lift + swing)
unsigned long EX_CLIMB_MS = 4000;  // front and back legs both over
float EX_CLIMB_TILT_MAX = 22;      // body tilt that means "too steep": back off
float EX_CLIMB_FLAT = 0.08f;       // the three look-down readings agree within this (ratio to floor)
int exClimbMed[3] = {-1, -1, -1};
const char* exClimbWhy = "";
bool exClimbSkip = false;
float exClimbMaxTilt = 0;
unsigned long exClimbStepMs = 0;
unsigned long exUserHeadingUntilMs = 0;  // after the user drove: no keep-away / wander steering
int EX_TOF_H_MM = 0;               // the ToF's height above the floor (measure it; 0 = estimated)
// ---- Back to the start (dead reckoning: the live radar's position; drifts, so "about there") ----
volatile bool exHomeReq = false;
const char* volatile exHomeWhy = "";
bool exHoming = false;
unsigned long exHomeStartMs = 0, exHomeHoldUntilMs = 0;
int EX_HOME_ARRIVE_MM = 250;
unsigned long EX_HOME_MAX_MS = 180000;
// ---- The last decision's passable directions (web radar: the green band) ----
char exGapBins[32] = "";
unsigned long EX_FROZEN_MS = 3500;
int EX_FROZEN_MM = 40;
unsigned long exFrzMs = 0;
int exFrzTof = -1;
float exFrzYaw = 0;
unsigned long EX_PROGRESS_MS = 3000;  // ToF: wall ahead must get this much closer...
int   EX_PROGRESS_MM    = 30;         // ...within EX_PROGRESS_MS while walking forward
const int   EX_TOF_VALID_MM   = 1200;
// Table edges (ToF glances down at the floor)
// Look-down angles for the floor check (deg below straight ahead) and the floor distance there.
// With 45 deg of room: 25 / 35 / 45 -> 455 / 390 / 335 mm. With less room below
// straight ahead (tilt 119, down limit 150: 30 deg) they fit in (21 / 26 / 30), and the distances follow
// the geometry: d = 280 / sin(13 + angle) - the ToF looks ~13 deg below the head. Relearned.
int EX_FLOOR_ANGLES[3] = {25, 35, 45};
int EX_FLOOR_DEFAULT_MM[3] = {455, 390, 335};
int exFloorSig() { return EX_FLOOR_ANGLES[0] * 10000 + EX_FLOOR_ANGLES[1] * 100 + EX_FLOOR_ANGLES[2]; }
void exFloorGeometry() {
  const int room = (int)floorf(TILT_MAX - TILT_MID) - 1;
  if (room >= 45) {
    const int a[3] = {25, 35, 45}, d[3] = {455, 390, 335};
    for (int i = 0; i < 3; i++) { EX_FLOOR_ANGLES[i] = a[i]; EX_FLOOR_DEFAULT_MM[i] = d[i]; }
    return;
  }
  // As steep as the head can: on a shiny floor a shallow look gets no echo or odd readings
  // (the laser reflects away)
  const int top = max(12, room);
  EX_FLOOR_ANGLES[0] = top >= 20 ? top - 9 : (int)lroundf(top * 0.6f);
  EX_FLOOR_ANGLES[1] = top >= 20 ? top - 4 : (int)lroundf(top * 0.8f);
  EX_FLOOR_ANGLES[2] = top;
  for (int i = 0; i < 3; i++)
    EX_FLOOR_DEFAULT_MM[i] = (int)lroundf(280.0f / sinf((13 + EX_FLOOR_ANGLES[i]) * DEG_TO_RAD));
}
float EX_FLOOR_TOL      = 0.25;  // floor seen if within +-25% of the expected distance
float EX_CLIFF_RATIO    = 1.6;   // reading this much longer (or out of range) = edge
float EX_CAUTION_RATIO  = 1.3;   // a bit longer than usual: slow down, glance more often
float EX_OBST_RATIO     = 0.75;  // this much shorter = something low in the way
// The head looks down only when there's a reason: at the start, after turning into a new
// direction, when the camera sees the floor ending with no wall there, and after walking a
// stretch since the last check (walking time x gait speed, so slower = fewer glances)
float EX_GLANCE_WALK_S     = 3.0;  // full-speed walking seconds between glances
float EX_GLANCE_CAUTION_S  = 1.2;  // ...when careful
float EX_GLANCE_MIN_S      = 0.8;  // camera-triggered glances at most this often
float EX_CAM_EDGE          = 0.35; // camera: floor ends this close and the ToF sees no wall
unsigned long EX_GLANCE_SETTLE_MS  = 180;  // head travel before reading
unsigned long EX_GLANCE_MAX_MS     = 600;
unsigned long EX_CAUTION_HOLD_MS   = 8000;
int   EX_GLANCE_READINGS = 3;
unsigned long EX_CONFIRM_SETTLE_MS = 400;  // stand still before re-checking an odd reading
int   EX_CONFIRM_READINGS = 5;
float EX_EDGE_TILT_DEG   = 22;   // fast body tilt while walking = a leg went over an edge
float EX_TURN_CLIFF_DEG  = 120;  // turn away this far after an edge
float EX_SEARCH_TURN_DEG = 45;   // no floor ahead: turn this far on the spot and look again
float EX_SEARCH_MAX_DEG  = 400;  // ...for about a full circle
// Curving while walking (Fwd-L / Fwd-R)
int   EX_VEER_MM        = 320;   // something this close ahead: curve away from it
int   EX_VEER_CLEAR_MM  = 450;   // ...until the way ahead is at least this open again
unsigned long EX_VEER_HOLD_MS = 600;   // keep curving a moment after it looks clear (room to pass)
unsigned long EX_VEER_MAX_MS  = 6000;  // then walk straight; a stop takes over if still in the way
const unsigned long EX_VEER_LOCK_MS = 3000;
float EX_SWIVEL_VEER_DEG = 80;   // rotation allowed per window while curving on purpose
// Slowing down near things (gait speed factor)
int   EX_SLOW_MM      = 350;   // start slowing below this ToF distance
float EX_CAM_SLOW     = 0.35;  // camera: start slowing when less floor than this is visible
float EX_MIN_SPEED    = 0.6;
float EX_CAUTION_SPEED = 0.8;  // near a possible edge
float EX_BACKUP_SPEED = 0.8;   // back up carefully: nothing watches behind
// Light: the ToF (infrared laser) and the IMU work in the dark, the camera doesn't
float EX_DARK_ROOM_LUMA   = 18;   // whole picture darker than this (of 219): camera checks off
float EX_BRIGHT_ROOM_LUMA = 26;   // ...back on above this
unsigned long EX_DARK_HOLD_MS = 1500;  // (shorter: a glance / a shadow flips it)
float EX_DARK_AHEAD_LUMA  = 30;   // view ahead darker than this...
float EX_DARK_AHEAD_RATIO = 0.45; // ...and than this x the floor at its feet = dark place ahead
int   EX_DARK_FRAMES      = 4;    // frames in a row before it counts
float EX_DARK_SPEED       = 0.75; // walk more carefully in the dark
unsigned long EX_CAM_LAG_MS = 150;  // a camera frame shows where the head was this long ago
// Heading memory (gyro yaw)
const int   EX_BLOCK_SLOTS = 8;
unsigned long EX_BLOCK_MEMORY_MS = 90000;
float EX_BLOCK_NEAR_DEG = 30;
const float EX_TURN_HEADING_DEG = 70;     // typical heading change of an avoidance turn

// Tunable from the page
float EX_MAX_SPEED = 1.0;                 // gait speed cap
float EX_WANDER_EVERY_S = 0;              // wander a little now and then (0 = never: it heads straight for open space;
                                           // random curves make it zig-zag)
float EX_WANDER_FOR_S = 1.9;              // ...for about this long
unsigned long EX_STUCK_BACKUP_MS = 900;   // back up this long after getting stuck (x2 when repeated)

unsigned long exWanderDelayMs() {
  if (EX_WANDER_EVERY_S <= 0) return 0x0FFFFFFFUL;  // never
  return (unsigned long)(EX_WANDER_EVERY_S * random(60, 141) * 10);
}

String exParamsChangedString();  // Explore settings (below)

enum ExploreState { EX_START, EX_FLOORCAL, EX_FORWARD, EX_STOPPING, EX_SCAN,
                    EX_STUCK, EX_BACKUP, EX_TURN, EX_CHECKFLOOR, EX_CONFIRMFLOOR,
                    EX_CLIMBCHECK, EX_CLIMB };
enum ExploreGait { GAIT_NONE = 0, GAIT_FWD = 1, GAIT_BACK = 2, GAIT_LEFT = 3, GAIT_RIGHT = 4,
                   GAIT_STEP_LEFT = 5, GAIT_STEP_RIGHT = 6,
                   GAIT_GLANCE = 255 };  // camera frames taken while the head glances down
enum FloorVerdict { FLOOR_OK, FLOOR_CAUTION, FLOOR_EDGE, FLOOR_OBSTACLE };
ExploreState exState = EX_START;
unsigned long exStateMs = 0;
uint8_t exGaitId = GAIT_NONE;
unsigned long exLastTofSeq = 0, exLastCamMs = 0, exLastMotMs = 0;
int exCamBlockedFrames = 0, exClearCount = 0;
// Looking around
volatile int16_t exSweepMm[EX_SWEEP_BINS];  // last look-around per 5 deg, right to left (web UI)
volatile int exScanPick = 0;       // chosen direction (deg, + = left), 180 = turned around
int16_t exSweepVals[EX_SWEEP_BINS][6];
uint8_t exSweepN[EX_SWEEP_BINS];
float exSweepAngle = 0;            // commanded head direction (deg, + = left)
int exSweepDir = -1;
int exSweepPanCmd = -999;
bool exSweepMoving = false;
unsigned long exSweepLastMs = 0, exSweepEndMs = 0;
bool exScanFromLeft = false;       // sweep direction, alternates
int exStopFrontMm = -1;            // ToF distance when it stopped (-1 = stopped for another reason)
int exSameTurns = 0;               // turns in a row to the same side (spiral breaker)
bool exLastTurnWasLeft = false;
// Checks the head's left / right after turning (auto-fixes a mirrored pan servo)
bool exPanCheckArmed = false;
int exPanMismatch = 0;
unsigned long exPanMismatchMs = 0;
unsigned long exStumbleMs = 0;     // a single jolt that was let go (a stumble), 0 = none
const char* volatile exNote = "";
int exSamples[16], exSampleN = 0;  // ToF readings while looking to one side
bool exTurnLeft = false;
bool exCalStarted = false;
// Verification / stuck handling
unsigned long exTiltSinceMs = 0, exFallSinceMs = 0, exWinMs = 0, exProgMs = 0;
float exWinYaw = 0, exTurnStartYaw = 0, exWinRot = 0;
int exProgTof = -1;
int exStillCount = 0, exMovingCount = 0;
unsigned long exStillSinceMs = 0;
int exFails = 0;                   // failed actions in a row (reset by a verified walk)
int exStuckTotal = 0;
bool exBackupThenLook = false;
unsigned long exBackupMs = EX_BACKUP_TIGHT_MS;
float exTurnTargetDeg = EX_TURN_TARGET_DEG;
float exNextTurnDeg = 0;           // minimum for the next turn (0 = what the look-around says)
int exAvoidTurn = 0;               // -1 = don't turn left next time, +1 = don't turn right
unsigned long exLastTurnEndMs = 0;
float walkTrim = 0.18f;          // the forward walk's built-in left / right stride balance (saved, /gait?trim=)
bool yawSignByHand = false;     // set via /gait?yawsign= (saved): turns don't re-learn it
int yawLeftSign = 0;               // sign of the gyro yaw change when turning left (learned once, saved)
int yawSignDoubt = 0;
float exBlockedYaw[EX_BLOCK_SLOTS];
unsigned long exBlockedMs[EX_BLOCK_SLOTS];
int exBlockedNext = 0;
volatile float exTurnRot = 0;      // degrees rotated in the current turn (for the web UI)
const char* volatile exStuckWhy = "";
const char* volatile exWhyLog[4] = {"", "", "", ""};  // last few reasons, newest first (web UI)
// Table edges
bool exEdgeCheck = false;          // floor was seen at the start: glances can check for edges
bool exEdgeEnabled = true;         // web toggle (saved): table-edge detection on / off
volatile bool exEdgeToggled = false;
int exFloorMm = -1;                // floor distance at the glance angle
float exFloorTilt = TILT_MID;      // glance angle
int exFloorExpect[3] = {EX_FLOOR_DEFAULT_MM[0], EX_FLOOR_DEFAULT_MM[1], EX_FLOOR_DEFAULT_MM[2]};
int exFloorSaved[3] = {EX_FLOOR_DEFAULT_MM[0], EX_FLOOR_DEFAULT_MM[1], EX_FLOOR_DEFAULT_MM[2]};  // NVS copy (read at boot)
bool exFloorWatch = false;         // head is looking down right now
float exHeadTiltNow = TILT_MID;
unsigned long exHeadMovedMs = 0;
int exCalIdx = 0, exCalN = 0;
int exCalReadings[16];
int exCalMed[3] = {-1, -1, -1};
bool exGlancing = false;
unsigned long exGlanceStartMs = 0, exCautionUntilMs = 0, exLastTickMs = 0;
float exSinceGlance = 0;           // full-speed walking seconds since the floor ahead was checked
int exGlanceVals[8];
int exGlanceN = 0;
bool exSteepTry = false;  // floor re-check: already looked steeper after a no-echo reading
bool exSearching = false;          // no floor ahead yet: turning on the spot to find some
float exSearchRot = 0;
// Turn style
bool exTurnOnSpot = false;         // current turn uses the creator's on-the-spot turn
int exTurnTries = 0;               // gyro said "not rotating": recovery steps tried this turn
unsigned long exUnstickUntil = 0;  // backing off a little to free a caught leg (in a turn)
bool exWallTry = false;            // turning along a wall: if not open there, try the other side
bool exNextOnSpot = false;         // next look-around turn is a recovery (stuck / edge): on the spot
// Curving while walking forward
int exVeer = 0;                    // -1 curving left, +1 curving right, 0 straight
bool exVeerAvoid = false;          // curving around something ahead (else just wandering)
unsigned long exVeerSinceMs = 0, exVeerUntilMs = 0, exVeerClearMs = 0;
unsigned long exWanderNextMs = 0, exVeerLockUntilMs = 0;
// Light
bool exAvoidDark = true;           // web toggle (saved): don't walk into dark places (under beds)
volatile bool exCamDark = false;   // too dark for the camera: ToF + IMU only
unsigned long exDarkSwitchMs = 0, exLastLightMs = 0;
volatile int exDarkFrames = 0;     // frames in a row with a dark place ahead
uint8_t exSweepLightN[EX_SWEEP_BINS], exSweepDarkN[EX_SWEEP_BINS];
// Table-edge scan (edge check on): after the level sweep, two more with the head down, 120 deg wide -
// far (the shallow floor angle, ~36 cm ahead) and near (the steep one, ~17 cm): per direction, where
// the floor ends. An edge then counts like a wall that close, so the choice avoids it.
const int EX_EDGE_SWEEP_DEG = 60;
int exEdgePass = 0;                 // 0 the level sweep, 1 far floor, 2 near floor
unsigned long exEdgePassMs = 0;
uint8_t exEdgeN[2][EX_SWEEP_BINS], exEdgeHit[2][EX_SWEEP_BINS];
volatile bool exSweepDark[EX_SWEEP_BINS];

// ---- Live radar: what the ToF has seen while walking, kept around the robot ----
// The gyro heading plus walking time x a walking speed it learns (timing itself against walls)
// track roughly where the robot is (dead reckoning). Every ToF reading becomes a point in
// that room frame: obstacles, or "open up to here" when nothing came back. Looking at those
// points from where it is now gives the radar on the page and lets it pick its way at an
// obstacle without stopping to sweep - it only sweeps when it doesn't know enough yet.
struct MapPt { float x, y; uint32_t ms; uint8_t kind; };  // kind 0 = obstacle, 1 = open, 2 = dark place
const int MAP_N = 320;
MapPt (&mapPts)[MAP_N] = *(MapPt(*)[MAP_N])heap_caps_calloc(1, sizeof(MapPt) * (MAP_N), MALLOC_CAP_SPIRAM);  // (PSRAM: internal RAM is kept for DMA - WiFi, camera, audio)
int mapHead = 0, mapCount = 0;
float odoX = 0, odoY = 0;          // mm, room frame (x = heading 0 when exploring started)
float walkMmPerS = 60;             // learned: mm per second of walking at full gait speed
unsigned long mapLastAddMs = 0, exMapShowMs = 0;
float mapLastAddHdg = 0;
char exSweepAge[EX_SWEEP_BINS + 1] = "";   // per radar direction: age of what's known (web UI)
bool exTriedMemory = false;
float EX_MAP_AGE_S = 20;           // forget readings older than this
int EX_MAP_COVER_PCT = 55;         // decide from memory when this much of each side is known
// Glancing left / right while walking (like a person looking around), to fill in the radar
float EX_LOOK_EVERY_S = 5;         // 0 = never
int EX_LOOK_DEG = 40;
unsigned long EX_LOOK_HOLD_MS = 650;
int EX_LOOK_CLEAR_MM = 500;        // only with this much room straight ahead
const float EX_LOOK_DPS = 150;     // head speed: a calm, natural turn
bool exSideLooking = false;
int exLookPhase = 0, exLookAlt = 1;
float exLookCur = 0, exLookTarget = 0, exVeerYaw0 = 0;
unsigned long exLookPhaseMs = 0, exLookNextMs = 0;

// ---- Virtual box: the room the body needs (legs included) ----
// The ToF on the head sees a narrow line; up close the legs are much wider than that. Choosing
// a way only counts gaps this box fits through, and while walking, obstacles the live radar
// knows about beside its path (too close for the ToF to see now) make it curve away.
int EX_BODY_HALF_MM = 100;           // half the width the legs need, plus a margin
int EX_BOX_AHEAD_MM = 220;           // walking: known obstacles this close in the box -> curve away
const float EX_BIN_HALF_SIN = 0.0436f;  // sin 2.5 deg: a sweep reading stands for its 5 deg bin
const float EX_HEAD_AHEAD_MM = 80;   // the ToF (head) is about this far in front of the body's centre
// ---- Places it got stuck (or walked into the dark): kept away from for a while ----
struct NoGo { float x, y; uint32_t ms; uint8_t kind; };  // room frame (dead reckoning); kind 1 = dark
const int NOGO_N = 12;
NoGo noGo[NOGO_N];
int noGoNext = 0;
float EX_NOGO_MIN = 6;               // remember them for (minutes), 0 = off
int EX_NOGO_R_MM = 300;              // keep this far away from them
char exNoGoBins[EX_SWEEP_BINS + 1] = "";
unsigned long exNoGoCheckMs = 0, exLitSinceMs = 0;
bool exDarkEntered = false;          // the picture went dark while walking in a lit room
void noGoAdd(uint8_t kind, float aheadMm, const char* why);

// ---- The head, eased: mode changes, face tracking, Explore's looks and the calibration panel set a
// target and loop() glides there (a spring, at most 400 deg/s) instead of jumping straight
// to it. Emotes, scenes and RC move it themselves (eased too) and cancel a glide; the next glide
// starts from wherever the head is then (emoPanNow / emoTiltNow).
float headSlewP = NAN, headSlewT = NAN;  // the targets (NAN: nothing to do)
void headSlewTo(float pan, float tilt) {
  if (!isnan(pan)) headSlewP = pan;
  if (!isnan(tilt)) headSlewT = tilt;
}
void headSlewCancel() { headSlewP = headSlewT = NAN; }
void headSlewUpdate() {
  static float p = 0, t = 0, vp = 0, vt = 0, sentP = -999, sentT = -999;
  static bool on = false;
  static unsigned long ms = 0;
  const unsigned long now = millis();
  if (isnan(headSlewP) && isnan(headSlewT)) {
    on = false;
    return;
  }
  float dt = min(now - ms, 50UL) / 1000.0f;
  ms = now;
  if (!on || fabsf(p - emoPanNow) > 2.5f || fabsf(t - emoTiltNow) > 2.5f) {  // (re)start from where it is
    p = emoPanNow;
    t = emoTiltNow;
    vp = vt = 0;
    on = true;
    dt = 0;
  }
  const float tp = isnan(headSlewP) ? p : headSlewP, tt = isnan(headSlewT) ? t : headSlewT;
  auto step = [&](float& x, float& v, float target) {
    // (a stiffer spring jumps several deg in one 50 ms step at a start: visible jerks)
    const float w = 10;
    const float a = constrain(w * w * (target - x) - 2 * w * v, -900.0f, 900.0f);
    v = constrain(v + a * dt, -160.0f, 160.0f);  // (100 / 600 is smoother, but the head feels slow)
    x += v * dt;
  };
  step(p, vp, tp);
  step(t, vt, tt);
  const bool doneP = fabsf(tp - p) < 0.4f && fabsf(vp) < 3, doneT = fabsf(tt - t) < 0.4f && fabsf(vt) < 3;
  if (doneP) p = tp;
  if (doneT) t = tt;
  panWho = "slew";
  if (fabsf(p - sentP) >= 0.45f || (doneP && p != sentP)) { setHeadServoF(HEAD_PAN_PIN, p); sentP = p; }
  if (fabsf(t - sentT) >= 0.45f || (doneT && t != sentT)) { setHeadServoF(HEAD_TILT_PIN, t); sentT = t; }
  emoPanNow = (int)lroundf(p);
  emoTiltNow = (int)lroundf(t);
  if (!doneP || !doneT) exHeadMovedMs = now;  // the ToF / camera wait until it has stopped
  else headSlewCancel();
}

// ---- Head tilt for the ToF: upright (walk / look around) or down (floor check) ----
void headTiltTo(float angle) {
  angle = constrain(angle, TILT_MIN, TILT_MAX);
  headSlewTo(NAN, angle);  // eased (headSlewUpdate); the ToF waits until it has stopped
  exHeadTiltNow = angle;
  exHeadMovedMs = millis();
}

void exHeadLevel() {
  if (exHeadTiltNow != TILT_MID) headTiltTo(TILT_MID);
  exFloorWatch = false;
  floorTopRow = FLOOR_TOP_ROW_LEVEL;
}

void exHeadDown() {
  if (exHeadTiltNow != exFloorTilt) headTiltTo(exFloorTilt);
  exFloorWatch = true;
  floorTopRow = FLOOR_TOP_ROW_DOWN;
}

bool tofSettled() { return millis() - exHeadMovedMs > 300; }

// Head upright: distance to something ahead, or -1
// Head looking somewhere other than straight ahead (down at the floor, or to a side)
bool exHeadAway() { return exFloorWatch || exSideLooking; }

int exObstacleMm() {
  if (exHeadAway() || !tofSettled() || tofMm < 0 || tofMm >= TOF_FAR_MM) return -1;
  return tofMm;
}

bool exAheadClear() { return !exHeadAway() && tofSettled() && tofMm > EX_CLEAR_MM; }

// ToF distances are only meaningful for "is the wall getting closer" with the head upright
bool tofValidAhead() { return !exHeadAway() && tofSettled() && tofMm >= 0 && tofMm < EX_TOF_VALID_MM; }

// Floor check result for a look-down reading
// The camera sees plenty of floor ahead (lit, fresh): a ToF "no echo" is a shiny floor, not an edge
bool exCamSeesFloor() {
  return exploreCamAssist && camReady && !exCamDark && millis() - camFreeMs < 600 && camFree[1] > 0.5f;
}

FloorVerdict exFloorVerdict(int r) {
  if (r < 0 || r >= TOF_FAR_MM || r > EX_CLIFF_RATIO * exFloorMm) return FLOOR_EDGE;
  if (r < EX_OBST_RATIO * exFloorMm) return FLOOR_OBSTACLE;
  if (r > EX_CAUTION_RATIO * exFloorMm) return FLOOR_CAUTION;
  return FLOOR_OK;
}

static int medianOf(int* v, int n) {
  for (int i = 1; i < n; i++) {  // insertion sort (tiny arrays)
    int x = v[i], j = i - 1;
    while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; }
    v[j + 1] = x;
  }
  return v[n / 2];
}

bool exCaution() { return millis() < exCautionUntilMs; }

// Slow the gait down as something gets close; back up carefully
void exUpdateSpeed(bool camValid) {
  float target = 1.0f;
  if (exState == EX_FORWARD) {
    const int d = exObstacleMm();
    if (d >= 0 && d < EX_SLOW_MM)
      target = min(target, EX_MIN_SPEED + (1 - EX_MIN_SPEED) * (d - EX_STOP_MM) / (float)(EX_SLOW_MM - EX_STOP_MM));
    const bool tofNear = d >= 0 && d < EX_CAM_TOF_MM;
    if (camValid && !exHeadAway() && !tofNear && camFree[1] < EX_CAM_SLOW)
      target = min(target, EX_MIN_SPEED + (1 - EX_MIN_SPEED) * (camFree[1] - EX_CAM_BLOCK) / (EX_CAM_SLOW - EX_CAM_BLOCK));
    if (exCaution()) target = min(target, EX_CAUTION_SPEED);
    if (exCamDark || exDarkFrames > 0) target = min(target, EX_DARK_SPEED);
  } else if (exState == EX_CLIMB) {
    target = 0.7f;
  } else if (exState == EX_BACKUP) {
    target = EX_BACKUP_SPEED;
  }
  target = constrain(min(target, EX_MAX_SPEED), min(EX_MIN_SPEED, EX_MAX_SPEED), 1.0f);
  cpgSpeed += (target - cpgSpeed) * (target < cpgSpeed ? 0.25f : 0.05f);  // brake fast, speed up gently
}

void exSideLookStop();

void exSet(ExploreState s, const char* name) {
  exSideLookStop();
  exTriedMemory = false;
  exState = s;
  exStateMs = millis();
  exploreStateName = name;
  exTiltSinceMs = 0;
  exWinMs = exProgMs = exStateMs;
  exWinYaw = imuYawAngle;
  exProgTof = tofValidAhead() ? (int)tofMm : -1;
  exFrzMs = exStateMs;
  exFrzTof = -1;
  exStillCount = exMovingCount = 0;
  exLastMotMs = motMs;  // only count camera verdicts from this action onward
  exStillSinceMs = 0;
  exDarkFrames = 0;
  exVeer = 0;           // every walk starts straight
  exVeerAvoid = false;
  if (s == EX_FORWARD) {
    exWanderNextMs = exStateMs + exWanderDelayMs();
    exStopFrontMm = -1;
  }
  xlog("STATE %s tof=%d tilt=%.0f yaw=%.0f shake=%.2f mot=%d (%.1f/%.1f n%.1f) fails=%d",
                name, (int)tofMm, imuTiltDeg, imuYawAngle, imuShakeRms, (int)motVerdict,
                (float)motDiffT, (float)motDiff2T, (float)motNoise, exFails);
}

void exGait(const char* cmd, uint8_t gaitId) {
  if (currentCommand != cmd) currentCommand = cmd;
  exGaitId = gaitId;
}

void headPanTo(float angle) {
  headSlewTo(constrain(angle, PAN_MIN, PAN_MAX), NAN);  // eased (headSlewUpdate)
  exHeadMovedMs = millis();  // ToF + camera wait for the head to settle
}

// Camera band indices for the robot's left / right (image right = the pan+ direction)
int camLeftBand()  { return panPlusIsLeft ? 2 : 0; }
int camRightBand() { return panPlusIsLeft ? 0 : 2; }

static float angleDist(float a, float b) {
  float d = fmodf(fabsf(a - b), 360.0f);
  return d > 180 ? 360 - d : d;
}

void exRememberBlocked() {
  exBlockedYaw[exBlockedNext] = imuYawAngle;
  exBlockedMs[exBlockedNext] = millis();
  exBlockedNext = (exBlockedNext + 1) % EX_BLOCK_SLOTS;
}

bool exNearBlocked(float heading) {
  const unsigned long now = millis();
  for (int i = 0; i < EX_BLOCK_SLOTS; i++) {
    if (exBlockedMs[i] && now - exBlockedMs[i] < EX_BLOCK_MEMORY_MS &&
        angleDist(exBlockedYaw[i], heading) < EX_BLOCK_NEAR_DEG) return true;
  }
  return false;
}

void exploreStart() {
  exGait("", GAIT_NONE);
  exCamBlockedFrames = 0;
  exClearCount = 0;
  exCalStarted = false;
  exFails = 0;
  exStuckTotal = 0;
  exAvoidTurn = 0;
  exFallSinceMs = 0;
  exNextTurnDeg = 0;
  exStuckWhy = "";
  exSameTurns = 0;
  exPanCheckArmed = false;
  exPanMismatch = 0;
  exStumbleMs = 0;
  exNote = "";
  exScanPick = 0;
  for (int i = 0; i < EX_SWEEP_BINS; i++) {
    exSweepMm[i] = -1;
    exSweepDark[i] = false;
  }
  exCamDark = false;
  exDarkSwitchMs = 0;
  for (int i = 0; i < EX_BLOCK_SLOTS; i++) exBlockedMs[i] = 0;
  exEdgeCheck = false;
  exFloorMm = -1;
  exFloorWatch = false;
  exGlancing = false;
  exCautionUntilMs = 0;
  exSearching = false;
  exSearchRot = 0;
  exTurnOnSpot = exNextOnSpot = false;
  exVeerLockUntilMs = 0;
  exSinceGlance = 0;
  exLastTickMs = 0;
  exEdgeToggled = false;
  for (int i = 0; i < 4; i++) exWhyLog[i] = "";
  exHeadMovedMs = millis();
  exHeadTiltNow = TILT_MID;  // enterMode() just centered the head
  floorTopRow = FLOOR_TOP_ROW_LEVEL;
  cpgSpeed = 1.0f;
  for (int i = 0; i < 3; i++) {
    exFloorExpect[i] = exFloorSaved[i];
  }
  {
    const String ch = exParamsChangedString();
    xlog("PARAMS %s", ch.length() ? ch.c_str() : "all defaults");
  }
  exSet(EX_START, "start");
}

void exGiveUp(const char* msg) {
  exGait("", GAIT_NONE);
  exHeadLevel();
  snprintf(modeError, sizeof(modeError), "error: %s", msg);
  requestedMode = MODE_WALK;
  xlog("GAVE UP: %s", msg);
}

// Back to the floor distances the geometry gives (the head calibration changed: the old ones
// were learned at other angles)
void exFloorReset() {
  for (int i = 0; i < 3; i++) {
    exFloorSaved[i] = EX_FLOOR_DEFAULT_MM[i];
    exFloorExpect[i] = EX_FLOOR_DEFAULT_MM[i];
    char key[8];
    snprintf(key, sizeof(key), "floor%d", i);
    prefPut(key, EX_FLOOR_DEFAULT_MM[i], false);
  }
  prefPut("floorSig", exFloorSig(), false);
}

// How high the ToF is above the floor, from the floor distances at the three look-down angles
// (d = H / sin(offset + angle): find the offset that fits, then H). 150 mm if nothing fits.
float exTofHeightMm() {
  if (EX_TOF_H_MM > 0) return EX_TOF_H_MM;  // measured by hand
  float bestErr = 1e9f, bestH = 150;
  for (float ph = -20; ph <= 40; ph += 0.5f) {
    float h[3], m = 0;
    for (int i = 0; i < 3; i++) { h[i] = exFloorExpect[i] * sinf((ph + EX_FLOOR_ANGLES[i]) * DEG_TO_RAD); m += h[i] / 3; }
    float err = 0;
    for (int i = 0; i < 3; i++) err += (h[i] - m) * (h[i] - m);
    if (err < bestErr) { bestErr = err; bestH = m; }
  }
  return bestH > 60 && bestH < 350 ? bestH : 150;
}

// Battery levels with hysteresis: down after 10 s below a level, up again 3 % above it
void batteryUpdate(float v) {
  static unsigned long lowSinceMs = 0;
  const unsigned long now = millis();
  if (now < 5000) return;  // the first readings after boot spike: let the ADC and filter settle
  // ~30 s average, by time (5 readings a second, 1 in RC). The first minute only ~3 s: the first
  // readings come while the start-up load is on and are low - averaged slowly, the value then
  // climbs for minutes and looks like charging.
  static unsigned long lastMs = 0;
  const float dt = lastMs ? min((now - lastMs) / 1000.0f, 5.0f) : 0;
  lastMs = now;
  const float tau = now < 60000 ? 3.0f : 30.0f;
  batSlowV = batSlowV == 0 ? v : batSlowV + min(1.0f, dt / tau) * (v - batSlowV);
  // Only 5.9 .. 9.2 V is a believable 2S pack. Below = USB power / no battery; above = the
  // divider isn't reading the battery. Either
  // way no warnings and no sitting down - a false "empty" would lock the robot's movement.
  if (batSlowV < 5.9f || batSlowV > 9.2f) {
    const int st = batSlowV < 5.9f ? -1 : -2;
    if (batState != st) {
      if (st == -2) xlogSys("BATTERY reading %.2f V is not a 2S pack - battery checks off until it makes sense", batSlowV);
      batState = st;
      batEmpty = false;
    }
    batPctSlow = -1;
    return;
  }
  const int pct = batPercentOf(batSlowV);
  batPctSlow = pct;
  if (!BAT_ALERTS) {  // alerts off: always "ok", never locks the robot
    batState = 0;
    batEmpty = false;
    return;
  }
  const int want = pct <= BAT_STOP_PCT ? 3 : pct <= BAT_HOME_PCT ? 2 : pct <= BAT_WARN_PCT ? 1 : 0;
  const int cur = max(0, (int)batState);
  if (want > cur) {
    if (!lowSinceMs) lowSinceMs = now;
    if (now - lowSinceMs < 10000) return;
  } else {
    lowSinceMs = 0;
    const int upLimit = cur == 3 ? BAT_STOP_PCT : cur == 2 ? BAT_HOME_PCT : BAT_WARN_PCT;
    if (want < cur && pct < upLimit + 3) return;  // not clearly charged yet
    if (want == cur) { if (batState < 0) batState = cur; return; }
  }
  lowSinceMs = 0;
  batState = want;
  // charged back above empty (to low / very low): moving allowed again
  if (want == 1 || want == 2) batEmpty = false;
  if (want == 0) {
    xlogSys("BATTERY ok again: %d%% (%.2f V)", pct, batSlowV);
    if (batEmpty) { batEmpty = false; currentCommand = "stand"; }
  } else if (want == 1) {
    xlogSys("BATTERY low: %d%% (%.2f V) - charge soon", pct, batSlowV);
  } else if (want == 2) {
    xlogSys("BATTERY very low: %d%% (%.2f V)%s", pct, batSlowV, requestedMode == MODE_EXPLORE ? " - going back to the start" : "");
    if (requestedMode == MODE_EXPLORE) { exHomeWhy = "battery low"; exHomeReq = true; }
  } else {
    xlogSys("BATTERY empty: %d%% (%.2f V) - sitting down, please charge", pct, batSlowV);
    batEmpty = true;
    requestedMode = MODE_WALK;
    camViewWanted = false;
    currentCommand = "rest";
  }
}

// Stuck on something low while walking: look down first, maybe climb it (EX_CLIMBCHECK)
bool exTryClimbFirst(const char* why) {
  if (!EX_CLIMB_ON || exClimbSkip || exState != EX_FORWARD || !imuOk) return false;
  if (strncmp(why, "stuck: no progress", 18) && strncmp(why, "stuck: not moving", 17) && strncmp(why, "stuck: bump", 11)) return false;
  if (tofMm >= 0 && tofMm < 250) return false;  // the ToF sees it at head height: not low
  xlog("CLIMB? %s with nothing close at head height (tof=%d): looking down", why, (int)tofMm);
  exClimbWhy = why;
  exGait("", GAIT_NONE);
  exGlancing = false;
  exSet(EX_CLIMBCHECK, "look down (climb?)");
  exCalIdx = 0;
  exCalN = 0;
  exClimbStepMs = millis();
  exFloorTilt = TILT_MID + EX_FLOOR_ANGLES[0];
  exHeadDown();
  return true;
}

// ---- Floor check at the start: look down at a few angles ----
void exStartFloorCal() {
  exFloorTilt = TILT_MID + EX_FLOOR_ANGLES[exCalIdx];
  exHeadDown();
  exCalN = 0;
  exSet(EX_FLOORCAL, "check floor");
}

// Median of the collected readings if they look like a steady surface, else -1
int exCalResult() {
  int v[16], n = 0;
  for (int i = 0; i < exCalN; i++) if (exCalReadings[i] < TOF_FAR_MM) v[n++] = exCalReadings[i];
  if (exCalN < 5 || n < exCalN * 6 / 10) return -1;
  const int med = medianOf(v, n);
  if (med < 100 || med > 900) return -1;
  if (v[n - 1] - v[0] > med * 3 / 10) return -1;  // not steady
  return med;
}

void exStartTurn(bool left, float targetDeg, bool onSpot);

// Floor seen at one of the angles -> edge check on, walk. Otherwise search on the spot.
void exFinishFloorCal() {
  // All three angles consistent with a floor (closer when looking steeper): relearn the
  // robot's floor distances, so a single matching angle is enough next time (tables, edges)
  // ...and only in the proportions the look-down angles give (one stray reading would spoil a whole run)
  auto geoRatio = [](int a, int b) { return sinf((13 + b) * DEG_TO_RAD) / sinf((13 + a) * DEG_TO_RAD); };
  const float r01 = exCalMed[1] > 0 ? (float)exCalMed[0] / exCalMed[1] : 0;
  const float r12 = exCalMed[2] > 0 ? (float)exCalMed[1] / exCalMed[2] : 0;
  const bool fitsGeometry = r01 < geoRatio(EX_FLOOR_ANGLES[0], EX_FLOOR_ANGLES[1]) * 1.15f &&
                            r12 < geoRatio(EX_FLOOR_ANGLES[1], EX_FLOOR_ANGLES[2]) * 1.15f;
  if (exCalMed[0] > 0 && exCalMed[1] > 0 && exCalMed[2] > 0 &&
      exCalMed[0] > exCalMed[1] * 1.05f && exCalMed[1] > exCalMed[2] * 1.05f && fitsGeometry) {
    for (int i = 0; i < 3; i++) {
      char key[8];
      snprintf(key, sizeof(key), "floor%d", i);
      exFloorExpect[i] = exCalMed[i];
      exFloorSaved[i] = exCalMed[i];
      prefPut(key, exCalMed[i], false);
    }
    prefPut("floorSig", exFloorSig(), false);
  }
  // The steepest look that saw floor is used from now on (the most reliable reading)
  int best = -1, shallow = -1;
  for (int i = 0; i < 3; i++) {
    if (exCalMed[i] > 0 && fabsf((float)exCalMed[i] / exFloorExpect[i] - 1.0f) < EX_FLOOR_TOL) {
      best = i;
      if (shallow < 0) shallow = i;
    }
  }
  xlog("floor check %d/%d/%d mm (expect %d/%d/%d) -> %s\n",
                exCalMed[0], exCalMed[1], exCalMed[2], exFloorExpect[0], exFloorExpect[1],
                exFloorExpect[2], best >= 0 ? "floor ahead" : "no floor ahead");
  if (best >= 0) {
    exEdgeCheck = true;
    exSearching = false;
    exSearchRot = 0;
    exFloorMm = exCalMed[best];
    exFloorTilt = TILT_MID + EX_FLOOR_ANGLES[best];
    exHeadLevel();
    exSinceGlance = 0;
    // Only a steeper look found floor: an edge may be close ahead -> slower, glance more often
    if (shallow > 0) exCautionUntilMs = millis() + EX_CAUTION_HOLD_MS;
    exSet(EX_FORWARD, "forward");
  } else if (exSearchRot >= EX_SEARCH_MAX_DEG) {
    exGiveUp("can't see the floor around me (on the floor? turn off table-edge detection)");
  } else {
    // Maybe facing an edge: turn on the spot and look again
    exHeadLevel();
    exSearching = true;
    exStartTurn(true, EX_SEARCH_TURN_DEG, true);
  }
}

// An action failed, the body raised an alarm or the floor ended: stop, step back a little,
// then look around again. (A wall or obstacle seen ahead does NOT come here: it stops and
// turns without backing up.) pause = stand a moment first; turnDeg > 0 = minimum turn.
bool exTryClimbFirst(const char* why);
void exStuck(const char* why, bool pause = true, float turnDeg = 0) {
  if (exTryClimbFirst(why)) return;
  const unsigned long now = millis();
  requestPhoto("problem");
  xlog("PROBLEM %s (in %s; tilt=%.0f fast=%.0f jolt=%.2f shake=%.2f tof=%d mot=%d fails=%d)", why,
       (const char*)exploreStateName, imuTiltDeg, imuTiltFastDeg, imuJolt, imuShakeRms, (int)tofMm,
       (int)motVerdict, exFails + 1);
  const bool walking = exState == EX_FORWARD || exState == EX_CONFIRMFLOOR;
  // The last turn led straight into this -> don't pick that way next time
  if (exState == EX_TURN || (walking && now - exLastTurnEndMs < 4000))
    exAvoidTurn = exTurnLeft ? -1 : 1;
  if (walking) exRememberBlocked();
  // Remember the place (legs caught, wedged, an edge, a dark place) and keep away from it
  const bool darkPlace = strncmp(why, "dark", 4) == 0;
  noGoAdd(darkPlace ? 1 : 0, walking ? (darkPlace ? 200 : 120) : 0, why);
  exGlancing = false;
  exHeadLevel();
  exFails++;
  exStuckTotal++;
  exStuckWhy = why;
  for (int i = 3; i > 0; i--) exWhyLog[i] = exWhyLog[i - 1];
  exWhyLog[0] = why;
  // No floor that way: turn on the spot (Sharp L / R creeps forward). Tight spaces are
  // detected when it looks around.
  exNextOnSpot = strncmp(why, "edge", 4) == 0;
  if (exFails >= EX_MAX_FAILS) { exGiveUp("I'm stuck - please move me"); return; }
  const bool repeated = exFails >= 3;
  exNextTurnDeg = max(turnDeg, repeated ? EX_TURN_ESCAPE_DEG : EX_TURN_STUCK_DEG);
  exBackupMs = repeated ? 2 * EX_STUCK_BACKUP_MS : EX_STUCK_BACKUP_MS;
  exGait("", GAIT_NONE);
  if (exState == EX_BACKUP) {
    // Backing up is what failed (blocked behind): skip it, look around and turn
    exBackupThenLook = false;
    exSet(EX_STOPPING, why);
  } else {
    exBackupThenLook = true;
    exSet(pause ? EX_STUCK : EX_BACKUP, why);
  }
}

// Body alarms while moving; returns true if it reacted
bool exBodyCheck(unsigned long t, bool checkJolt) {
  if (!imuOk) return false;
  const unsigned long now = millis();
  if (imuTiltDeg > EX_TILT_STUCK_DEG) {
    if (exTiltSinceMs == 0) exTiltSinceMs = now;
    else if (now - exTiltSinceMs > EX_TILT_MS) { exStuck("stuck: tilted"); return true; }
  } else {
    exTiltSinceMs = 0;
  }
  // A jolt: one on its own in open space is usually a foot catching a rug edge or a cable,
  // not a crash, so it
  // walks on. A real bump: a second jolt within 3 s, a hard one (2x the limit), or something
  // close ahead. The head's own quick moves (floor glance) shake the body: not counted.
  const float joltLim = max(EX_JOLT_G, EX_JOLT_RATIO * imuShakeRms);
  if (checkJolt && t > EX_SENSE_HOLDOFF_MS && now - exHeadMovedMs > 400 && imuJolt > joltLim) {
    const int ahead = exObstacleMm();
    const bool hard = imuJolt > 2 * joltLim, near = ahead >= 0 && ahead < 250;
    const bool again = exStumbleMs && now - exStumbleMs > 250 && now - exStumbleMs < 3000;
    if (hard || near || again) {
      exStumbleMs = 0;
      exStuck("stuck: bump");
      return true;
    }
    if (!exStumbleMs || now - exStumbleMs >= 3000) {
      exStumbleMs = now;
      xlog("STUMBLE jolt %.2f g (limit %.2f, ahead %d mm) - walking on", imuJolt, joltLim, ahead);
    }
  }
  return false;
}

// Camera motion verdicts for the current walk; returns true if it reacted (not moving).
// When the ToF sees something ahead it decides instead (the wall must get closer: the
// no-progress check) - a plain wall close up looks the same frame to frame even while
// walking. In open space it can't be pushing against anything, and far scenery hardly
// changes with each small step. So "not moving" only counts when the ToF sees nothing but
// the camera sees something low close by, for at least EX_STILL_MS.
bool exMotionCheck(bool camValid) {
  if (!camReady || motMs == exLastMotMs) return false;
  exLastMotMs = motMs;
  if (motVerdict < 0) {
    exMovingCount = 0;
    const int d = exObstacleMm();
    const bool tofSees = d >= 0 && d < EX_TOF_VALID_MM;
    const bool lowThing = camValid && !exHeadAway() && camFree[1] < EX_MOT_NEAR_CAM;
    if (tofSees || !lowThing) return false;
    const unsigned long now = millis();
    if (exStillCount++ == 0) exStillSinceMs = now;
    if (exStillCount >= EX_STILL_CHECKS && now - exStillSinceMs > EX_STILL_MS) {
      exStuck("stuck: not moving");
      return true;
    }
  } else if (motVerdict > 0) {
    exStillCount = 0;
    exMovingCount++;
  }
  return false;
}

// Glance down at the floor while walking - only when there's a reason: after walking a
// stretch since the floor ahead was last checked (sooner when careful), or when the camera
// sees the floor ending ahead with no wall there. Returns true if it reacted.
bool exGlanceUpdate(unsigned long now, bool newTof, bool camValid) {
  if (!exEdgeCheck) return false;
  if (!exGlancing) {
    const float every = (exCaution() || exCamDark) ? EX_GLANCE_CAUTION_S : EX_GLANCE_WALK_S;
    const bool camEdge = camValid && !exHeadAway() && tofSettled() && camFree[1] < EX_CAM_EDGE &&
                         exObstacleMm() < 0 && exSinceGlance > EX_GLANCE_MIN_S;
    if ((exSinceGlance >= every || camEdge) && !exSideLooking) {
      exHeadDown();
      exGlancing = true;
      exGlanceStartMs = now;
      exGlanceN = 0;
    }
    return false;
  }
  if (newTof && now - exGlanceStartMs > EX_GLANCE_SETTLE_MS && exGlanceN < EX_GLANCE_READINGS)
    exGlanceVals[exGlanceN++] = tofMm;
  if (exGlanceN < EX_GLANCE_READINGS && now - exGlanceStartMs < EX_GLANCE_MAX_MS) return false;

  const int r = exGlanceN ? medianOf(exGlanceVals, exGlanceN) : -1;
  exGlancing = false;
  exSinceGlance = 0;
  const FloorVerdict v = exFloorVerdict(r);
  static const char* const VERDICT[] = {"ok", "careful", "EDGE", "low thing"};
  xlog("GLANCE %d mm (floor %d) -> %s", r, exFloorMm, VERDICT[v]);
  if (v == FLOOR_EDGE || v == FLOOR_OBSTACLE) {
    // The walking sway can spoil a reading: stop and look again standing still first
    xlog("glance: %d mm (floor %d) - stop and check again\n", r, exFloorMm);
    exGait("", GAIT_NONE);
    exGlanceN = 0;
    exSteepTry = false;
    exSet(EX_CONFIRMFLOOR, "check floor again");
    return true;
  }
  exHeadLevel();
  if (v == FLOOR_CAUTION) exCautionUntilMs = now + EX_CAUTION_HOLD_MS;
  return false;
}

// onSpot = the creator's turn: no floor that way, or a tight space. Otherwise Sharp L / R,
// which creeps forward a little while it turns.
void exStartTurn(bool left, float targetDeg, bool onSpot) {
  exTurnLeft = left;
  exTurnTargetDeg = targetDeg;
  exTurnOnSpot = onSpot;
  exTurnTries = 0;
  exUnstickUntil = 0;
  exWallTry = false;
  exTurnStartYaw = imuYawAngle;
  exWinRot = 0;
  exTurnRot = 0;
  exClearCount = 0;
  exGlancing = false;
  exHeadLevel();
  if (exTurnOnSpot) exSet(EX_TURN, left ? "turn L (spot)" : "turn R (spot)");
  else exSet(EX_TURN, left ? "turn L" : "turn R");
  xlog("TURN %s target=%.0f %s%s", left ? "left" : "right", targetDeg, onSpot ? "on the spot" : "sharp",
       exSearching ? " (looking for floor)" : "");
}

// A turn finished: find floor (searching), confirm floor ahead, or just walk on
void exTurnDone(float rot) {
  exLastTurnEndMs = millis();
  xlog("TURN done: rotated %.0f of %.0f in %lu ms, ahead %d mm", rot, exTurnTargetDeg,
       millis() - exStateMs, (int)tofMm);
  if (exSearching) {
    exSearchRot += rot;
    exGait("", GAIT_NONE);
    exCalIdx = 0;
    exStartFloorCal();
  } else if (exEdgeCheck) {
    exGait("", GAIT_NONE);
    exHeadDown();
    exGlanceN = 0;
    exSet(EX_CHECKFLOOR, "check floor");
  } else {
    exSet(EX_FORWARD, "forward");
  }
}

// ---- Looking around and deciding where to go ----
int exSweepAngleOf(int bin) { return -EX_SWEEP_MAX_DEG + bin * EX_SWEEP_BIN_DEG; }

int exSweepBinOf(float angle) {
  return constrain((int)lroundf((angle + EX_SWEEP_MAX_DEG) / EX_SWEEP_BIN_DEG), 0, EX_SWEEP_BINS - 1);
}

// Where the head was lagMs ago during the sweep
float exSweepSeenAt(unsigned long now, unsigned long lagMs) {
  const float since = exSweepEndMs ? (float)(now - exSweepEndMs) : 0.0f;
  return exSweepAngle - exSweepDir * EX_SWEEP_DPS * max(0.0f, lagMs - since) / 1000.0f;
}

// Head direction relative to the body (deg, + = robot's left)
void exHeadPanOff(float off) {
  const int pan = (int)lroundf(PAN_MID + (panPlusIsLeft ? off : -off));
  if (pan != exSweepPanCmd) {
    headPanTo(pan);
    exSweepPanCmd = pan;
  }
}

void exSweepBegin() {
  const bool fromLeft = exScanFromLeft;
  exScanFromLeft = !exScanFromLeft;  // sweep from alternating sides, like a person
  exSweepAngle = fromLeft ? EX_SWEEP_MAX_DEG : -EX_SWEEP_MAX_DEG;
  exSweepDir = fromLeft ? -1 : 1;
  for (int i = 0; i < EX_SWEEP_BINS; i++) exSweepN[i] = exSweepLightN[i] = exSweepDarkN[i] = exEdgeN[0][i] = exEdgeN[1][i] = exEdgeHit[0][i] = exEdgeHit[1][i] = 0;
  exEdgePass = 0;
  exSweepMoving = false;
  exSweepEndMs = 0;
  exSweepPanCmd = -999;
  exHeadPanOff(exSweepAngle);
  headTiltTo(TILT_MID - EX_SCAN_TILT_UP);
  exFloorWatch = false;
  exSet(EX_SCAN, "look around");
}

void exSweepAdd(float angle, int mm) {
  const int bin = exSweepBinOf(angle);
  if (exSweepN[bin] < 6) exSweepVals[bin][exSweepN[bin]++] = mm;
}

void mapAdd(float relDeg, int mm, uint8_t kind);

// The edge passes into the sweep: an edge counts like a wall that far away (the choice avoids it).
// Per direction: floor near and far = fine; floor near only = the edge 17-36 cm out (300 mm); no floor
// even near = right there (120 mm). Directions not seen (the outer 15 deg) keep the level reading.
void exEdgeApply() {
  char line[EX_SWEEP_BINS + 1];
  int edges = 0;
  for (int i = 0; i < EX_SWEEP_BINS; i++) {
    const bool seen = exEdgeN[0][i] || exEdgeN[1][i];
    const bool far = exEdgeN[0][i] && exEdgeHit[0][i] * 2 > exEdgeN[0][i];
    const bool near = exEdgeN[1][i] && exEdgeHit[1][i] * 2 > exEdgeN[1][i];
    line[i] = !seen ? ' ' : near ? 'X' : far ? 'e' : '.';
    if (near) exSweepMm[i] = min((int)exSweepMm[i], 120);
    else if (far) exSweepMm[i] = min((int)exSweepMm[i], 300);
    if (near || far) mapAdd(exSweepAngleOf(i), near ? 120 : 300, 0);  // (on the map too: a look from memory keeps it)
    if (near || far) edges++;
  }
  line[EX_SWEEP_BINS] = 0;
  xlog("EDGE scan right->left [%s] (. floor, e edge 17-36 cm, X edge <17 cm)%s", line, edges ? "" : " - floor all round");
}

// One distance per 5 deg: median of the readings there, gaps filled from the neighbours
void exSweepFinish() {
  int raw[EX_SWEEP_BINS];
  for (int i = 0; i < EX_SWEEP_BINS; i++) {
    int v[6];
    for (int k = 0; k < exSweepN[i]; k++) v[k] = exSweepVals[i][k];
    raw[i] = exSweepN[i] ? medianOf(v, exSweepN[i]) : -1;
  }
  for (int i = 0; i < EX_SWEEP_BINS; i++) {
    int v = raw[i];
    if (v < 0) {
      int a = -1, b = -1;
      for (int j = i - 1; j >= 0; j--) if (raw[j] >= 0) { a = raw[j]; break; }
      for (int j = i + 1; j < EX_SWEEP_BINS; j++) if (raw[j] >= 0) { b = raw[j]; break; }
      v = (a >= 0 && b >= 0) ? min(a, b) : max(a, b);
    }
    exSweepMm[i] = v;
    exSweepDark[i] = exSweepLightN[i] > 0 && exSweepDarkN[i] * 2 >= exSweepLightN[i];
  }
  // Dark directions go on the live map too
  for (int i = 0; i < EX_SWEEP_BINS; i++)
    if (exSweepDark[i]) mapAdd(exSweepAngleOf(i), exSweepMm[i], 2);
}

// ---- Live radar functions ----
// Which way the gyro's yaw goes for a left turn is fixed by how the IMU sits on the body:
// learned from the first real turn or curve and saved, so the map works from the first step
// of every later run. A saved sign only changes after 5 turns in a row disagree (a bump can
// twist the body the wrong way once).
void exLearnYawSign(int sgn);
// A left / right turn that lasted (any mode): which way the gyro's yaw goes, for walking straight
void yawSignFromTurns() {
  static String turnCmd = "";
  static float yaw0 = 0;
  static unsigned long t0 = 0;
  const String& c = currentCommand;
  const bool left = c == "left" || c == "smooth_walk_left" || c == "smooth_sharp_left" || c == "step_left" || c == "smooth_creator_left" || c == "sharp_left";
  const bool right = c == "right" || c == "smooth_walk_right" || c == "smooth_sharp_right" || c == "step_right" || c == "smooth_creator_right" || c == "sharp_right";
  if (!(left || right)) { turnCmd = ""; return; }
  if (c != turnCmd) { turnCmd = c; yaw0 = imuYawAngle; t0 = millis(); return; }
  const float d = imuYawAngle - yaw0;
  if (millis() - t0 > 800 && fabsf(d) > 15) {
    exLearnYawSign(((d > 0) == left) ? 1 : -1);
    yaw0 = imuYawAngle;  // (once per 15 degrees)
  }
}

void exLearnYawSign(int sgn) {
  if (sgn == yawLeftSign) { yawSignDoubt = 0; return; }
  // a saved sign changes only after 5 turns in a row disagree (a slipping turn or a hand turning it can
  // flip it wrongly, and walking straight would then steer the wrong way)
  if (yawSignByHand) return;  // set by hand (/gait?yawsign=): never overridden by turns
  if (yawLeftSign != 0 && ++yawSignDoubt < 5) return;
  yawSignDoubt = 0;
  yawLeftSign = sgn;
  prefPut("yawSign", sgn, false);
  mapHead = 0;  // points so far were placed with the wrong / unknown heading
  mapCount = 0;
  xlogSys("GYRO learned: yaw %s = turning left (saved)", sgn > 0 ? "up" : "down");
}

// Heading in the room frame (deg, + = left). The gyro's sign is learned from the first turn
// or curve; until then only readings from before any turning go on the map.
bool exMapUsable() { return yawLeftSign != 0 || fabsf(imuYawAngle) < 5; }
float exHdg() { return yawLeftSign != 0 ? yawLeftSign * imuYawAngle : 0.0f; }

void mapAdd(float relDeg, int mm, uint8_t kind) {
  if (!exMapUsable()) return;
  const float a = (exHdg() + relDeg) * DEG_TO_RAD;
  const bool open = kind == 1 || ((mm < 0 || mm >= EX_TOF_VALID_MM) && kind != 2);  // (a dark place with no echo stays dark, not "open")
  const float d = open || mm < 0 || mm >= EX_TOF_VALID_MM ? EX_TOF_VALID_MM : mm;
  MapPt& p = mapPts[mapHead];
  p.x = odoX + d * cosf(a);
  p.y = odoY + d * sinf(a);
  p.ms = millis();
  p.kind = open ? 1 : kind;
  mapHead = (mapHead + 1) % MAP_N;
  if (mapCount < MAP_N) mapCount++;
}

// A reading with the head level: straight ahead, or to the side during a glance
void exMapFeed(unsigned long now, float relDeg, int mm) {
  const float hdg = exHdg() + relDeg;
  if (now - mapLastAddMs < 120 && fabsf(hdg - mapLastAddHdg) < 3) return;
  mapLastAddMs = now;
  mapLastAddHdg = hdg;
  mapAdd(relDeg, mm, 0);
}

// Walking forward / backing up moves the robot along its heading
void exOdometry(float dtS) {
  if (!exMapUsable() || currentCommand.length() == 0) return;
  float v = 0;
  if (exGaitId == GAIT_FWD) v = walkMmPerS * cpgSpeed;
  else if (exGaitId == GAIT_BACK) v = -0.8f * walkMmPerS * cpgSpeed;
  if (v == 0) return;
  const float a = exHdg() * DEG_TO_RAD;
  odoX += v * dtS * cosf(a);
  odoY += v * dtS * sinf(a);
}

// ---- No-go places ----
bool noGoActive(const NoGo& g, uint32_t now) {
  return g.ms && EX_NOGO_MIN > 0 && now - g.ms < (uint32_t)(EX_NOGO_MIN * 60000.0f);
}

void noGoAdd(uint8_t kind, float aheadMm, const char* why) {
  if (!exMapUsable() || EX_NOGO_MIN <= 0) return;
  const float a = exHdg() * DEG_TO_RAD;
  NoGo& g = noGo[noGoNext];
  g.x = odoX + aheadMm * cosf(a);
  g.y = odoY + aheadMm * sinf(a);
  g.ms = max(1UL, millis());
  g.kind = kind;
  noGoNext = (noGoNext + 1) % NOGO_N;
  xlog("NOGO remembered (%s) at %.0f,%.0f mm - kept away from for %.0f min", why, g.x, g.y, EX_NOGO_MIN);
}

// How close the path from here in direction relDeg (+ = left, up to lenMm) passes a no-go
// place. Places it's standing at now don't count (it can't avoid being where it is).
float noGoPathDist(float relDeg, float lenMm) {
  if (!exMapUsable()) return 1e9f;
  const uint32_t now = millis();
  const float a = (exHdg() + relDeg) * DEG_TO_RAD, ux = cosf(a), uy = sinf(a);
  float best = 1e9f;
  for (int i = 0; i < NOGO_N; i++) {
    const NoGo& g = noGo[i];
    if (!noGoActive(g, now)) continue;
    const float dx = g.x - odoX, dy = g.y - odoY;
    if (dx * dx + dy * dy < (float)EX_NOGO_R_MM * EX_NOGO_R_MM) continue;
    const float t = constrain(dx * ux + dy * uy, 0.0f, lenMm);
    const float ex = dx - t * ux, ey = dy - t * uy;
    best = min(best, sqrtf(ex * ex + ey * ey));
  }
  return best;
}

// Virtual box while walking: an obstacle the live radar knows about beside its path, inside
// the body's width and closer than EX_BOX_AHEAD_MM, that the ToF beam no longer covers (it's
// narrower than the legs up close). -1 = on the left, +1 = on the right, 0 = none,
// 2 = on both sides (a tight passage: the ToF / stop logic decides, no steering).
int exBoxSide() {
  if (!exMapUsable() || EX_BOX_AHEAD_MM <= 0) return 0;
  const uint32_t now = millis();
  const float a = exHdg() * DEG_TO_RAD, c = cosf(a), sn = sinf(a);
  const float beamTan = tanf(10 * DEG_TO_RAD);
  bool left = false, right = false;
  int nl = 0, nr = 0;
  for (int k = 0; k < mapCount; k++) {
    const MapPt& p = mapPts[k];
    if (p.kind == 1 || now - p.ms > 4000) continue;  // (the odometry drifts: old points slide beside the path)
    const float dx = p.x - odoX, dy = p.y - odoY;
    const float fwd = dx * c + dy * sn, lat = -dx * sn + dy * c;  // lat + = left
    if (fwd < 30 || fwd > EX_BOX_AHEAD_MM || fabsf(lat) >= EX_BODY_HALF_MM) continue;
    if (fabsf(lat) < fwd * beamTan) continue;  // the ToF sees it: the normal stop handles it
    if (lat > 0) nl++; else nr++;
  }
  // two points at least: one stray reading would flip it left / right on open floor
  left = nl >= 2;
  right = nr >= 2;
  return left && right ? 2 : (left ? -1 : (right ? 1 : 0));
}

// The map seen from here, per radar direction (-75..+75): the nearest obstacle, else how far
// it's known to be open, else -1. Also marks dark places and the age of what's known.
void mapProject() {
  const uint32_t now = millis();
  const float hdg = exHdg();
  int obst[EX_SWEEP_BINS], openMm[EX_SWEEP_BINS];
  uint32_t age[EX_SWEEP_BINS];
  bool dark[EX_SWEEP_BINS];
  for (int i = 0; i < EX_SWEEP_BINS; i++) { obst[i] = INT32_MAX; openMm[i] = -1; age[i] = UINT32_MAX; dark[i] = false; }
  const uint32_t maxAge = (uint32_t)(EX_MAP_AGE_S * 1000);
  for (int k = 0; k < mapCount; k++) {
    const MapPt& p = mapPts[k];
    const uint32_t a = now - p.ms;
    if (a > maxAge) continue;
    const float dx = p.x - odoX, dy = p.y - odoY;
    const float dist = sqrtf(dx * dx + dy * dy);
    if (dist < 30) continue;
    float rel = atan2f(dy, dx) * RAD_TO_DEG - hdg;
    while (rel > 180) rel -= 360;
    while (rel < -180) rel += 360;
    if (fabsf(rel) > EX_SWEEP_MAX_DEG + EX_SWEEP_BIN_DEG / 2.0f) continue;
    const int b = exSweepBinOf(rel);
    if (p.kind == 1) openMm[b] = max(openMm[b], (int)dist);
    else {
      obst[b] = min(obst[b], (int)dist);
      if (p.kind == 2) dark[b] = true;
    }
    age[b] = min(age[b], a);
  }
  for (int i = 0; i < EX_SWEEP_BINS; i++) {
    exSweepMm[i] = obst[i] != INT32_MAX ? min(obst[i], TOF_FAR_MM) : (openMm[i] >= 0 ? min(openMm[i], TOF_FAR_MM) : -1);
    exSweepDark[i] = dark[i];
    exSweepAge[i] = age[i] == UINT32_MAX ? '-' : (char)('0' + min(9UL, (unsigned long)(age[i] / 2000)));
  }
  exSweepAge[EX_SWEEP_BINS] = 0;
  // No-go places within 1.5 m, for the radar picture
  for (int i = 0; i < EX_SWEEP_BINS; i++) exNoGoBins[i] = '0';
  exNoGoBins[EX_SWEEP_BINS] = 0;
  for (int i = 0; i < NOGO_N; i++) {
    const NoGo& g = noGo[i];
    if (!noGoActive(g, now)) continue;
    const float dx = g.x - odoX, dy = g.y - odoY, dist = sqrtf(dx * dx + dy * dy);
    if (dist < 50 || dist > 1500) continue;
    float rel = atan2f(dy, dx) * RAD_TO_DEG - hdg;
    while (rel > 180) rel -= 360;
    while (rel < -180) rel += 360;
    if (fabsf(rel) > EX_SWEEP_MAX_DEG + EX_SWEEP_BIN_DEG / 2.0f) continue;
    exNoGoBins[exSweepBinOf(rel)] = g.kind == 1 ? '2' : '1';
  }
}

// Enough known on both sides (from the latest projection) to decide without a sweep?
bool exMapCovered() {
  int l = 0, r = 0, nl = 0, nr = 0;
  for (int i = 0; i < EX_SWEEP_BINS; i++) {
    const int a = exSweepAngleOf(i);
    if (a >= 20) { nl++; if (exSweepMm[i] >= 0) l++; }
    if (a <= -20) { nr++; if (exSweepMm[i] >= 0) r++; }
  }
  return EX_MAP_COVER_PCT < 100 && l * 100 >= EX_MAP_COVER_PCT * nl && r * 100 >= EX_MAP_COVER_PCT * nr;
}

// -1 = the left knows more (look right), +1 = look left
int exSideLess() {
  int l = 0, r = 0;
  for (int i = 0; i < EX_SWEEP_BINS; i++) {
    const int a = exSweepAngleOf(i);
    if (a >= 20 && exSweepMm[i] >= 0 && exSweepAge[i] < '4') l++;
    if (a <= -20 && exSweepMm[i] >= 0 && exSweepAge[i] < '4') r++;
  }
  if (l == r) return exLookAlt = -exLookAlt;
  return l < r ? 1 : -1;
}

// Glance left / right while walking: a calm head turn, a short look, back to the front
void noGoClear() {
  for (int i = 0; i < NOGO_N; i++) noGo[i].ms = 0;
  exDarkEntered = false;
  exLitSinceMs = 0;
}

void exSideLookStop() {
  if (!exSideLooking) return;
  exSideLooking = false;
  exLookCur = 0;
  exSweepPanCmd = -999;
  headPanTo(PAN_MID);
}

void exSideLookUpdate(unsigned long now, float dtS, bool camValid) {
  if (!exSideLooking) {
    if (EX_LOOK_EVERY_S <= 0 || now < exLookNextMs || exGlancing || exFloorWatch || !tofSettled()) return;
    const bool clearAhead = tofMm >= 0 && (tofMm >= TOF_FAR_MM || tofMm > EX_LOOK_CLEAR_MM);
    if (!clearAhead || (camValid && camFree[1] < 0.35f) || exDarkFrames > 0) return;
    exLookTarget = exSideLess() * (float)EX_LOOK_DEG;
    exSideLooking = true;
    exLookPhase = 0;
    exLookPhaseMs = now;
    exLookCur = 0;
    exSweepPanCmd = -999;
    return;
  }
  const float step = EX_LOOK_DPS * dtS;
  if (exLookPhase == 0) {
    exLookCur += constrain(exLookTarget - exLookCur, -step, step);
    exHeadPanOff(exLookCur);
    if (fabsf(exLookTarget - exLookCur) < 0.5f) { exLookPhase = 1; exLookPhaseMs = now; }
  } else if (exLookPhase == 1) {
    if (now - exLookPhaseMs > EX_LOOK_HOLD_MS) exLookPhase = 2;
  } else {
    exLookCur += constrain(-exLookCur, -step, step);
    exHeadPanOff(exLookCur);
    if (fabsf(exLookCur) < 0.5f) {
      exSideLookStop();
      exLookNextMs = now + (unsigned long)(EX_LOOK_EVERY_S * random(70, 131) * 10);
      exProgMs = now;  // the "wall getting closer" check starts over
      exProgTof = -1;
    }
  }
}

void exNoteTurn(bool left) {
  exSameTurns = (left == exLastTurnWasLeft) ? exSameTurns + 1 : 1;
  exLastTurnWasLeft = left;
}

// The way ahead is blocked and it has swept its head around. Like a person:
//  - a direction is only open if there's room either side of the path too (the closest
//    reading within +-10 deg counts), so it heads through gaps it actually fits
//  - prefer the widest open gap, and its middle rather than its edge
//  - about equally good -> the smaller turn
//  - not toward headings that were blocked recently, not the turn that just failed
//  - after 2+ turns the same way, favour the other side (no circling the same lap)
//  - turn by the angle it saw (measured with the gyro): Sharp L / R normally, the creator's
//    on-the-spot turn only in a tight space, right against the wall, or with no floor that way
//  - nothing open within the sweep but a wall that gets further away toward one side: it's
//    facing a wall head-on (the sweep can't see along it) -> turn ~90 deg to walk along it
//  - nothing open at all (dead end, boxed in): the only time a wall makes it back up - a
//    little - and then turn around on the spot
// Where the start is, from here: angle (+ = left of where it faces) and distance
float exHomeBearing() {
  float e = atan2f(-odoY, -odoX) * RAD_TO_DEG - exHdg();
  while (e > 180) e -= 360;
  while (e < -180) e += 360;
  return e;
}
float exHomeDist() { return sqrtf(odoX * odoX + odoY * odoY); }

bool exDecide(bool fromMemory = false) {
  const int B = EX_SWEEP_BINS;
  const int half = EX_GAP_HALF_DEG / EX_SWEEP_BIN_DEG;
  int clr[EX_SWEEP_BINS], run[EX_SWEEP_BINS], mid[EX_SWEEP_BINS];
  const bool skipDark = exAvoidDark && !exCamDark;
  for (int i = 0; i < B; i++) {
    int c = TOF_FAR_MM;
    for (int j = max(0, i - half); j <= min(B - 1, i + half); j++) c = min(c, max(0, (int)exSweepMm[j]));
    // Virtual box: anything within the body's width along this way counts, however far off
    // to the side it reads (close things cover a wide angle - the legs are wider than the ToF
    // line). The path starts at the body's centre, which it turns around (the head is in front).
    const float th = exSweepAngleOf(i) * DEG_TO_RAD, ct = cosf(th), st = sinf(th);
    for (int j = 0; j < B; j++) {
      const int d = exSweepMm[j];
      if (d <= 0 || d >= TOF_FAR_MM) continue;
      const float a = exSweepAngleOf(j) * DEG_TO_RAD;
      const float x = EX_HEAD_AHEAD_MM + d * cosf(a), y = d * sinf(a);
      const float fwd = x * ct + y * st;
      const float lat = fabsf(-x * st + y * ct) - d * EX_BIN_HALF_SIN;
      if (fwd <= 0 || lat >= EX_BODY_HALF_MM) continue;
      c = min(c, max(0, (int)(fwd - EX_HEAD_AHEAD_MM)));
    }
    if (skipDark && exSweepDark[i]) c = min(c, EX_OPEN_MM - 1);  // open for the ToF, but dark (under a bed)
    clr[i] = c;
  }
  // Open gaps: width, and how far each direction is from the gap's edge
  for (int i = 0; i < B;) {
    if (clr[i] < EX_OPEN_MM) { run[i] = mid[i] = 0; i++; continue; }
    int j = i;
    while (j < B && clr[j] >= EX_OPEN_MM) j++;
    for (int k = i; k < j; k++) {
      run[k] = j - i;
      mid[k] = min(k - i, j - 1 - k);
    }
    i = j;
  }
  for (int i = 0; i < B; i++) exGapBins[i] = clr[i] >= EX_OPEN_MM ? '1' : '0';
  exGapBins[B] = 0;
  const float minTurn = exNextTurnDeg;
  const int avoid = exAvoidTurn;
  const float minAngle = max(15.0f, min(minTurn, 60.0f));  // straight ahead is what stopped it
  int best = -1;
  float bestScore = -9;
  for (int i = 0; i < B; i++) {
    const int a = exSweepAngleOf(i);
    if (clr[i] < EX_OPEN_MM || abs(a) < minAngle) continue;
    float score = min(clr[i], 1500) / 1500.0f;
    score += 0.25f * run[i] * EX_SWEEP_BIN_DEG / 150.0f;  // wide open space
    score += 0.03f * min(mid[i], 4);                      // middle of the gap
    score -= 0.15f * abs(a) / (float)EX_SWEEP_MAX_DEG;    // smaller turn
    if ((a > 0 && avoid < 0) || (a < 0 && avoid > 0)) score -= 0.5f;
    if (exSameTurns >= 2 && (a > 0) != exLastTurnWasLeft) score += 0.25f;
    if (yawLeftSign != 0 && exNearBlocked(imuYawAngle + yawLeftSign * a)) score -= 0.8f;
    if (noGoPathDist(a, 900) < EX_NOGO_R_MM) score -= 0.7f;  // toward a place it got stuck
    if (exHoming) score += 0.6f * cosf((a - exHomeBearing()) * DEG_TO_RAD);  // on the way back
    score += random(0, 20) / 1000.0f;  // break ties
    if (score > bestScore) { bestScore = score; best = i; }
  }
  // From memory, only a clear choice: "nothing open" is checked with a real sweep first
  if (fromMemory && best < 0) return false;
  exHomeHoldUntilMs = millis() + 4000;  // going back: first get clear of what stopped it
  exNextTurnDeg = 0;
  exAvoidTurn = 0;
  {
    char sw[200], dk[EX_SWEEP_BINS + 1];
    int n = 0;
    for (int i = 0; i < B && n < (int)sizeof(sw) - 6; i++) {
      const int v = exSweepMm[i];
      n += snprintf(sw + n, sizeof(sw) - n, "%s%d", i ? "," : "", v < 0 ? -1 : v / 10);
    }
    for (int i = 0; i < B; i++) dk[i] = exSweepDark[i] ? '1' : '0';
    dk[B] = 0;
    xlog("LOOK%s cm right->left [%s] dark %s front=%d minTurn=%.0f avoid=%d same=%d -> %d%s",
         fromMemory ? " (from memory, no sweep)" : "", sw, dk,
         exStopFrontMm, minTurn, avoid, exSameTurns, best < 0 ? 180 : exSweepAngleOf(best),
         best < 0 ? " (nothing open)" : "");
  }
  const int front = exStopFrontMm >= 0 ? exStopFrontMm : (int)exSweepMm[B / 2];
  if (best < 0) {
    // Facing a wall head-on: nothing reads open (the ToF's 25 deg beam also pulls the far,
    // slanted readings closer), but the wall gets further away toward the sides
    int edgeL = 0, edgeR = 0, sumL = 0, sumR = 0;
    for (int i = 0; i < B; i++) {
      const int a = exSweepAngleOf(i), v = max(0, (int)exSweepMm[i]);
      if (a >= EX_WALL_EDGE_DEG) edgeL = max(edgeL, v);
      if (a <= -EX_WALL_EDGE_DEG) edgeR = max(edgeR, v);
      if (a >= 40) sumL += v;
      if (a <= -40) sumR += v;
    }
    const int need = max(EX_WALL_MIN_MM, (int)(EX_WALL_RECEDE * max(front, 0)));
    const bool okL = edgeL >= need, okR = edgeR >= need;
    if (okL || okR) {
      bool left = okL && (!okR || edgeL >= edgeR);  // the side where it recedes more
      if (okL && okR && abs(edgeL - edgeR) < 0.15f * max(edgeL, edgeR)) {
        // About the same at the edges: the side that's roomier overall (a corner shows
        // there first), else not the turn that failed, else no circling the same lap
        if (abs(sumL - sumR) > 0.08f * max(sumL, sumR)) left = sumL > sumR;
        else if (avoid != 0) left = avoid > 0;
        else if (exSameTurns >= 2) left = !exLastTurnWasLeft;
        else left = random(2);
      }
      xlog("wall ahead: edges L%d R%d -> along it to the %s\n", edgeL, edgeR, left ? "left" : "right");
      exScanPick = left ? 90 : -90;
      exPanCheckArmed = false;
      exNoteTurn(left);
      exNextOnSpot = false;
      exStartTurn(left, max(90.0f, minTurn), true);  // close to the wall: on the spot
      exWallTry = true;
      return true;
    }
    exTurnLeft = sumL >= sumR;
    exTurnTargetDeg = max(minTurn, EX_TURN_AROUND_DEG);
    exScanPick = 180;
    exPanCheckArmed = false;
    exNoteTurn(exTurnLeft);
    exBackupThenLook = false;
    exBackupMs = EX_BACKUP_TIGHT_MS;
    exNextOnSpot = false;
    exSet(EX_BACKUP, "backup (dead end)");
    return true;
  }
  const int off = exSweepAngleOf(best);
  const bool left = off > 0;
  exScanPick = off;
  const bool tight = clr[best] < EX_TIGHT_MM || (front >= 0 && front < EX_TOO_CLOSE_MM);
  // Facing the chosen way, the ToF should see about what it saw there. Checked only with
  // a clear contrast between the two sides (see exCheckPanMapping), and only on solid
  // evidence: a real echo >= 1 m (not "no echo" - dark fabric under a bed returns nothing
  // at a slant and looks open), in the light, while nothing is going wrong.
  exPanCheckArmed = minTurn <= abs(off) && clr[best] >= 1000 && clr[best] < TOF_FAR_MM &&
                    clr[B - 1 - best] < 500 && !exCamDark && !exSweepDark[best] && exFails <= 1;
  exNoteTurn(left);
  const bool onSpot = exNextOnSpot || tight;
  exNextOnSpot = false;
  // the edge scan saw floor that way, near and far: turn just that far (the 120 deg after an edge
  // would turn past the floor it had found)
  const bool floorSeen = exEdgePass == 2 && abs(off) <= EX_EDGE_SWEEP_DEG && exEdgeN[0][best] && exEdgeN[1][best] &&
                         exEdgeHit[0][best] * 2 <= exEdgeN[0][best] && exEdgeHit[1][best] * 2 <= exEdgeN[1][best];
  exStartTurn(left, floorSeen ? max((float)abs(off), 15.0f) : max((float)abs(off), minTurn), onSpot);
  return true;
}

// Right after turning to the chosen way: a short reading there although it looked open,
// 3 times in a row (within 3 min), means the head's left / right is mirrored -> swap it (saved)
void exCheckPanMapping() {
  if (!tofSettled() || tofMm < 0) return;
  if (tofMm < 500) {
    if (exPanMismatch > 0 && millis() - exPanMismatchMs > 180000) exPanMismatch = 0;
    exPanMismatchMs = millis();
    xlog("pan check: expected open, see %d mm (%d of 3)\n", (int)tofMm, exPanMismatch + 1);
    if (++exPanMismatch >= 3) {
      panPlusIsLeft = !panPlusIsLeft;
      prefPut("panPlusLeft", panPlusIsLeft, true);
      exPanMismatch = 0;
      exNote = "head left/right was mirrored - swapped it";
      xlog("learned: head pan left/right was mirrored, swapped");
    }
  } else if (tofMm >= 800) {
    exPanMismatch = 0;
  }
}

// ---- Curving while walking (Fwd-L / Fwd-R) ----
// Which way to curve (-1 left, +1 right): toward more open floor (camera), away from
// headings that were blocked recently, else either way
int exVeerSide(bool camValid) {
  float l = 0, r = 0;
  if (camValid) {
    l += camFree[camLeftBand()];
    r += camFree[camRightBand()];
  }
  // Live radar: the side that's known to be more open (decided before reaching it)
  float ml = 0, mr = 0;
  for (int i = 0; i < EX_SWEEP_BINS; i++) {
    const int a = exSweepAngleOf(i), v = exSweepMm[i];
    if (v < 0 || abs(a) < 20) continue;
    if (a > 0) ml += min(v, 1500); else mr += min(v, 1500);
  }
  if (ml + mr > 0 && fabsf(ml - mr) > 0.15f * max(ml, mr)) {
    if (ml > mr) l += 0.6f; else r += 0.6f;
  }
  if (yawLeftSign != 0) {
    if (exNearBlocked(imuYawAngle + yawLeftSign * EX_TURN_HEADING_DEG)) l -= 0.5f;
    if (exNearBlocked(imuYawAngle - yawLeftSign * EX_TURN_HEADING_DEG)) r -= 0.5f;
  }
  if (noGoPathDist(35, 600) < EX_NOGO_R_MM) l -= 0.8f;
  if (noGoPathDist(-35, 600) < EX_NOGO_R_MM) r -= 0.8f;
  if (fabsf(l - r) < 0.1f) return random(2) ? -1 : 1;
  return l > r ? -1 : 1;
}

void exSetVeer(int dir, bool avoid, const char* name = nullptr) {
  avoid = avoid && dir != 0;
  if (dir == exVeer && avoid == exVeerAvoid) return;
  const unsigned long now = millis();
  exVeer = dir;
  exVeerAvoid = avoid;
  exVeerSinceMs = now;
  exVeerYaw0 = imuYawAngle;
  exVeerClearMs = 0;
  // The swivel / no-progress checks start over on the new path
  exWinMs = exProgMs = now;
  exWinYaw = imuYawAngle;
  exProgTof = tofValidAhead() ? (int)tofMm : -1;
  if (dir == 0) exploreStateName = "forward";
  else if (avoid) exploreStateName = dir < 0 ? "curve L (avoid)" : "curve R (avoid)";
  else exploreStateName = dir < 0 ? "wander L" : "wander R";
  if (name) exploreStateName = name;
  exGait(dir < 0 ? "smooth_fwd_left" : (dir > 0 ? "smooth_fwd_right" : "smooth_walk"), GAIT_FWD);
  xlog("STEER %s tof=%d", (const char*)exploreStateName, (int)tofMm);
}

// Walk like a person: curve gently around something ahead instead of marching straight at
// it (the stop at EX_STOP_MM still takes over if it gets close), and wander a little now
// and then on open floor. Holds its course while the head glances down.
void exSteer(unsigned long now, unsigned long t, bool camValid, int obstacle) {
  const bool headUp = !exHeadAway() && tofSettled();
  const bool tofNear = obstacle >= 0 && obstacle < EX_VEER_MM;
  // Camera: less floor straight ahead and clearly more to one side = a way around it
  const bool camNear = EX_VEER_MM > 0 && camValid && headUp && camFree[1] < EX_CAM_SLOW &&
                       !(obstacle >= 0 && obstacle < EX_CAM_TOF_MM) &&
                       max(camFree[0], camFree[2]) > camFree[1] + 0.15f;
  if ((tofNear || camNear) && now >= exVeerLockUntilMs) {
    exVeerClearMs = 0;
    if (exVeerAvoid) {
      if (now - exVeerSinceMs > EX_VEER_MAX_MS) {
        // Still in the way after curving a while: go straight, the normal stop handles it
        exSetVeer(0, false);
        exVeerLockUntilMs = now + EX_VEER_LOCK_MS;
      }
    } else if (exVeer != 0) {
      // Wandering swept the ToF onto something: curve back the other way
      exSetVeer(-exVeer, true);
    } else {
      exSetVeer(exVeerSide(camValid), true);
    }
  } else if (exVeerAvoid) {
    if (!headUp) return;
    if (obstacle >= 0 && obstacle < EX_VEER_CLEAR_MM) { exVeerClearMs = 0; return; }
    // Looks clear: keep curving a moment to pass it with some room, then straighten out
    if (exVeerClearMs == 0) exVeerClearMs = now;
    else if (now - exVeerClearMs > EX_VEER_HOLD_MS) {
      exSetVeer(0, false);
      exWanderNextMs = now + exWanderDelayMs();
    }
  } else if (exVeer != 0) {
    if (now > exVeerUntilMs || exCaution()) {
      exSetVeer(0, false);
      exWanderNextMs = now + exWanderDelayMs();
    }
  } else if (EX_WANDER_EVERY_S > 0 && !exCaution() && t > 3000 && now >= exWanderNextMs && headUp && !camNear &&
             now > exUserHeadingUntilMs && !exHoming &&
             (obstacle < 0 || obstacle > EX_SLOW_MM)) {
    exSetVeer(exVeerSide(camValid), false);
    exVeerUntilMs = now + (unsigned long)(EX_WANDER_FOR_S * random(65, 136) * 10);
  }
}

void exploreUpdate() {
  const unsigned long now = millis();
  const unsigned long t = now - exStateMs;
  // Too dark for the camera (lights off): its checks switch off, ToF + IMU only
  if (camReady && now - camLightMs < 500) {
    const bool flip = exCamDark ? camLumaAll > EX_BRIGHT_ROOM_LUMA : camLumaAll < EX_DARK_ROOM_LUMA;
    // only with the head upright and settled: looking down / to a side, or the head moving, darkens the
    // picture for a moment (it would "walk into a dark place", back out and block the spot)
    if (!flip || exHeadAway() || !tofSettled()) exDarkSwitchMs = 0;
    else if (exDarkSwitchMs == 0) exDarkSwitchMs = now;
    else if (now - exDarkSwitchMs > EX_DARK_HOLD_MS) {
      exCamDark = !exCamDark;
      exDarkSwitchMs = 0;
      xlog("%s (light %.0f)\n", exCamDark ? "too dark for the camera: ToF + IMU only" : "light again: camera back on", (float)camLumaAll);
      // The whole picture went dark while walking in a lit room: it walked in under something
      if (exCamDark && exState == EX_FORWARD && exLitSinceMs && now - exLitSinceMs > 3000) exDarkEntered = true;
    }
  }
  if (exState == EX_FORWARD && !exCamDark) { if (!exLitSinceMs) exLitSinceMs = now; }
  else if (exState != EX_FORWARD) exLitSinceMs = 0;
  // A dark place ahead in a lit room (under a bed, a sofa): the view ahead is much darker
  // than the floor at its feet
  bool lightNew = false, darkFrame = false;
  if (camReady && camLightMs != exLastLightMs) {
    exLastLightMs = camLightMs;
    lightNew = true;
    darkFrame = !exCamDark && camLumaAhead < EX_DARK_AHEAD_LUMA &&
                camLumaAhead < EX_DARK_AHEAD_RATIO * camLumaNear;
    exDarkFrames = (darkFrame && !exHeadAway() && tofSettled()) ? exDarkFrames + 1 : 0;
  }
  const bool camValid = exploreCamAssist && camReady && !exCamDark && (now - camFreeMs < 500);
  const bool newTof = tofSeq != exLastTofSeq;
  exLastTofSeq = tofSeq;
  const float dtS = exLastTickMs ? min((now - exLastTickMs) / 1000.0f, 0.1f) : 0.0f;
  exLastTickMs = now;

  // Table-edge detection switched on / off on the web page
  if (exEdgeToggled) {
    exEdgeToggled = false;
    if (!exEdgeEnabled && (exEdgeCheck || exSearching || exState == EX_FLOORCAL)) {
      exEdgeCheck = false;
      exSearching = false;
      exGlancing = false;
      exHeadLevel();
      if (exState == EX_FLOORCAL || exState == EX_CHECKFLOOR || exState == EX_CONFIRMFLOOR)
        exSet(EX_FORWARD, "forward");
    } else if (exEdgeEnabled && !exEdgeCheck && exState != EX_START) {
      exGait("", GAIT_NONE);  // stop and check the floor before walking on
      exCalIdx = 0;
      exStartFloorCal();
    }
  }

  // Fell over: stop everything and ask for help on the web page
  if (imuOk && exState != EX_START && imuTiltDeg > EX_FALL_DEG) {
    if (exFallSinceMs == 0) exFallSinceMs = now;
    else if (now - exFallSinceMs > 300) { exGiveUp("fell over - put me back on my feet"); return; }
  } else {
    exFallSinceMs = 0;
  }

  exUpdateSpeed(camValid);

  // Live radar: where the robot is, what the ToF sees (head level, not during a floor glance)
  exOdometry(dtS);
  if (newTof && exState != EX_SCAN && !exFloorWatch && tofSettled())
    exMapFeed(now, exSideLooking ? exLookCur : 0.0f, (int)tofMm);
  if (now - exMapShowMs > 400) {
    exMapShowMs = now;
    if (exState != EX_SCAN) mapProject();  // the sweep shows its own readings meanwhile
  }

  // Back to the start (button, or the battery): walk back by dead reckoning
  if (exHomeReq) {
    exHomeReq = false;
    if (!exMapUsable()) {
      xlog("HOME: can't yet - the gyro's direction isn't known (it learns it at the first turn)");
      exNote = "going back needs one turn first";
    } else if (!exHoming) {
      exHoming = true;
      exHomeStartMs = now;
      exHomeHoldUntilMs = 0;
      xlog("HOME: going back to the start (%s): %.0f mm away, %+.0f deg", (const char*)exHomeWhy, exHomeDist(), exHomeBearing());
      exNote = "going back to the start";
    }
  }
  if (exHoming) {
    if (exHomeDist() < EX_HOME_ARRIVE_MM) {
      xlog("HOME: back at the start (about %.0f mm away by dead reckoning, after %.0f s)", exHomeDist(), (now - exHomeStartMs) / 1000.0f);
      exHoming = false;
      exGait("", GAIT_NONE);
      exNote = "back at the start";
      requestedMode = MODE_WALK;
      return;
    }
    if (now - exHomeStartMs > EX_HOME_MAX_MS) {
      exHoming = false;
      exGiveUp("couldn't find the way back to the start");
      return;
    }
  }
  // "Something's wrong here" from the page: a snapshot for later
  if (exMarkReq) {
    exMarkReq = false;
    requestPhoto("mark");
    char sw[200];
    int n = 0;
    for (int i = 0; i < EX_SWEEP_BINS && n < (int)sizeof(sw) - 6; i++)
      n += snprintf(sw + n, sizeof(sw) - n, "%s%d", i ? "," : "", exSweepMm[i] < 0 ? -1 : exSweepMm[i] / 10);
    xlog("USER MARK: something looks wrong here - %s, tof=%d, veer=%d, fails=%d, speed=%.2f, mot=%d, free=%.2f/%.2f/%.2f, "
         "tilt=%.0f, yaw=%.0f, last choice %d, why: %s | %s | radar cm right->left [%s]",
         (const char*)exploreStateName, (int)tofMm, exVeer, exFails, cpgSpeed, (int)motVerdict,
         (float)camFree[camLeftBand()], (float)camFree[1], (float)camFree[camRightBand()], imuTiltDeg,
         imuYawAngle, (int)exScanPick, (const char*)exWhyLog[0], (const char*)exWhyLog[1], sw);
  }
  // The user drives (Explore D-pad held): the state machine waits
  const bool manualWanted = exManualCmd[0] && now - exManualSeenMs < 1200 && exState != EX_START;
  if (manualWanted) {
    char cmd[4];
    strlcpy(cmd, (const char*)exManualCmd, sizeof(cmd));
    if (!exManualOn) {
      exManualOn = true;
      exManualStartMs = now;
      exManualYaw0 = imuYawAngle;
      exManualOdoX0 = odoX;
      exManualOdoY0 = odoY;
      exManualLast[0] = 0;
      exSideLookStop();
      exGlancing = false;
      exHeadLevel();
      xlog("USER took over while \"%s\" (tof=%d, veer=%d, fails=%d, mot=%d, last choice %d, why: %s)",
           (const char*)exploreStateName, (int)tofMm, exVeer, exFails, (int)motVerdict, (int)exScanPick,
           (const char*)exWhyLog[0]);
    }
    if (strcmp(cmd, exManualLast) != 0) {
      strlcpy(exManualLast, cmd, sizeof(exManualLast));
      const char* what = !strcmp(cmd, "f") ? "forward" : !strcmp(cmd, "b") ? "back" : !strcmp(cmd, "l") ? "turn left"
                       : !strcmp(cmd, "r") ? "turn right" : !strcmp(cmd, "sl") ? "spin left" : "spin right";
      xlog("USER %s (tof=%d)", what, (int)tofMm);
    }
    if (!strcmp(cmd, "f")) exGait("smooth_walk", GAIT_FWD);
    else if (!strcmp(cmd, "b")) exGait("smooth_walk_back", GAIT_BACK);
    else if (!strcmp(cmd, "l")) exGait("smooth_sharp_left", GAIT_LEFT);
    else if (!strcmp(cmd, "r")) exGait("smooth_sharp_right", GAIT_RIGHT);
    else if (!strcmp(cmd, "sl")) exGait("sharp_left", GAIT_LEFT);
    else if (!strcmp(cmd, "sr")) exGait("sharp_right", GAIT_RIGHT);
    exploreStateName = "you drive";
    return;
  }
  if (exManualOn) {
    exManualOn = false;
    const float dx = odoX - exManualOdoX0, dy = odoY - exManualOdoY0;
    xlog("USER gave control back after %.1f s: turned %+.0f deg, moved ~%.0f mm (tof=%d now)",
         (now - exManualStartMs) / 1000.0f, imuYawAngle - exManualYaw0, sqrtf(dx * dx + dy * dy), (int)tofMm);
    exGait("", GAIT_NONE);
    exFails = 0;
    // The user pointed it somewhere: walk on that way (rather than looking around and turning away
    // again right away). Only if they
    // backed it off something, or something is close ahead: look around.
    const bool clearAhead = tofMm < 0 || tofMm >= TOF_FAR_MM || tofMm > EX_SLOW_MM;
    exUserHeadingUntilMs = now + 5000;
    if (clearAhead && strcmp(exManualLast, "b") != 0) exSet(EX_FORWARD, "forward (your way)");
    else exSet(EX_STOPPING, "stop (after your help)");
    return;
  }

  switch (exState) {
    case EX_START:
      // Stand still: re-learn the gyro bias and what "flat floor" feels like
      if (t > 400 && !exCalStarted) { imuCalStart(); exCalStarted = true; }
      if (t > 1200) {
        imuCalFinish();  // heading 0 = the way it faces now (live radar frame)
        odoX = odoY = 0;
        mapCount = mapHead = 0;
        noGoClear();  // places are kept in this run's frame only
        exHoming = false;
        if (exEdgeEnabled) {
          exCalIdx = 0;
          exStartFloorCal();
        } else {
          exEdgeCheck = false;  // table-edge detection off: head stays upright, just walk
          exHeadLevel();
          exSet(EX_FORWARD, "forward");
        }
      }
      break;

    case EX_FLOORCAL:
      // Look down at the floor and collect ToF readings once the head has settled
      if (t > 400 && newTof && exCalN < 16) exCalReadings[exCalN++] = tofMm;
      if (t > 1000) {
        exCalMed[exCalIdx] = exCalResult();
        if (++exCalIdx < 3) {
          exStartFloorCal();
        } else {
          exFinishFloorCal();
        }
      }
      break;

    case EX_FORWARD: {
      exGait(exVeer < 0 ? "smooth_fwd_left" : (exVeer > 0 ? "smooth_fwd_right" : "smooth_walk"), GAIT_FWD);
      // A leg went over an edge: the body tips fast
      if (imuOk && t > 600 && imuTiltFastDeg > EX_EDGE_TILT_DEG) {
        exStuck("edge: tipping", false, EX_TURN_CLIFF_DEG);
        break;
      }
      // Table edge / low things: glance down at the floor when there's a reason
      if (!exGlancing) exSinceGlance += dtS * cpgSpeed;
      if (exGlanceUpdate(now, newTof, camValid)) break;
      if (camValid && camFreeMs != exLastCamMs) {
        exLastCamMs = camFreeMs;
        exCamBlockedFrames = (camFree[1] < EX_CAM_BLOCK) ? exCamBlockedFrames + 1 : 0;
      }
      const int obstacle = exObstacleMm();
      const bool tofBlocked = obstacle >= 0 && obstacle < EX_STOP_MM;
      const bool camBlocked = camValid && !exHeadAway() && exCamBlockedFrames >= EX_CAM_BLOCK_FRAMES &&
                              !(obstacle >= 0 && obstacle < EX_CAM_TOF_MM);
      if (tofBlocked || camBlocked) {
        exRememberBlocked();
        exGait("", GAIT_NONE);  // CPG stops -> stand pose
        exGlancing = false;
        exHeadLevel();
        exCamBlockedFrames = 0;
        // Wall: the look-around decides. Something low (camera): turn at least a bit.
        exNextTurnDeg = tofBlocked ? 0 : EX_TURN_TARGET_DEG;
        exNextOnSpot = false;
        exSet(EX_STOPPING, tofBlocked ? "stop (tof)" : "stop (cam)");
        exStopFrontMm = tofBlocked ? obstacle : -1;
        break;
      }
      if (exAvoidDark && exDarkFrames >= EX_DARK_FRAMES) {
        // Dark place ahead (under a bed): the ToF may see "open", the camera can't help in
        // there and that's where it gets wedged -> treat it like something in the way
        exRememberBlocked();
        exGait("", GAIT_NONE);
        exGlancing = false;
        exHeadLevel();
        exNextTurnDeg = EX_TURN_TARGET_DEG;
        exNextOnSpot = false;
        exSet(EX_STOPPING, "stop (dark ahead)");
        break;
      }
      if (exDarkEntered) {
        // Walked in under a bed / sofa (the whole picture went dark): back out, turn away,
        // and remember the place
        exDarkEntered = false;
        if (exAvoidDark) {
          exStuck("dark place: walked into it, backing out", false, 120);
          break;
        }
      }
      if (exBodyCheck(t, true)) break;
      if (exMotionCheck(camValid)) break;
      if (exMovingCount >= EX_MOVING_CHECKS || t > EX_WALK_OK_MS) exFails = 0;  // this walk works
      exSteer(now, t, camValid, obstacle);
      // Keep away from places it got stuck / things beside its path - only while the ToF sees
      // nothing close ahead (then its own curving decides, or the two fight and flip the curve
      // side to side), and once curving one way, that way for at least 1.5 s
      static unsigned long keepAwayUntil = 0;
      static int keepAwayDir = 0;
      // Going back to the start: turn / curve toward it whenever the way ahead is clear
      if (exHoming && t > 600 && now > exHomeHoldUntilMs && now > exUserHeadingUntilMs &&
          (obstacle < 0 || obstacle > EX_VEER_MM)) {
        const float e = exHomeBearing();
        if (fabsf(e) > 60) {
          xlog("HOME: turning toward the start (%+.0f deg, %.0f mm away)", e, exHomeDist());
          exGait("", GAIT_NONE);
          exStartTurn(e > 0, fabsf(e), false);
          exHomeHoldUntilMs = now + 2500;
          break;
        }
        const int hdir = e > 12 ? -1 : (e < -12 ? 1 : 0);
        if (hdir != exVeer || exVeerAvoid)
          exSetVeer(hdir, false, hdir ? (hdir < 0 ? "home: curve L" : "home: curve R") : "home: straight");
        exVeerUntilMs = now + 800;
      }
      if (t > 800 && now - exNoGoCheckMs > 250 && (obstacle < 0 || obstacle > EX_VEER_MM) && now > exUserHeadingUntilMs && !exHoming) {
        exNoGoCheckMs = now;
        int dir = 0;
        const char* why = "";
        // Heading for a place it got stuck: curve away before getting there
        if (noGoPathDist(0, 550) < EX_NOGO_R_MM) {
          dir = noGoPathDist(35, 550) >= noGoPathDist(-35, 550) ? -1 : 1;
          why = "a place it got stuck";
        }
        // Virtual box: something known beside its path, where the legs would catch it
        const int side = exBoxSide();
        if (side == -1 || side == 1) { dir = -side; why = "something beside its path (body box)"; }
        if (dir != 0 && now < keepAwayUntil + 1500 && dir == -keepAwayDir) dir = keepAwayDir;  // no flip-flop (3 s)
        if (dir != 0) {
          if (exVeer != dir || exVeerAvoid) {
            xlog("STEER curve %s: %s", dir < 0 ? "L" : "R", why);
            exSetVeer(dir, false, dir < 0 ? "curve L (keep away)" : "curve R (keep away)");
            keepAwayUntil = now + 1500;
            keepAwayDir = dir;
          }
          exVeerUntilMs = max(exVeerUntilMs, now + 1500);
        }
      }
      if (t > 2000) exSideLookUpdate(now, dtS, camValid);
      // The gyro's sign from a curve (Fwd-L / Fwd-R), if no turn has shown it yet
      if (yawLeftSign == 0 && exVeer != 0 && now - exVeerSinceMs > 800 && fabsf(imuYawAngle - exVeerYaw0) > 12)
        exLearnYawSign(((imuYawAngle - exVeerYaw0 > 0) == (exVeer < 0)) ? 1 : -1);
      // Walking speed: timed against a wall straight ahead (for the live radar)
      {
        static unsigned long spdMs = 0;
        static int spdTof = -1;
        if (exVeer != 0 || exHeadAway() || !tofSettled()) spdTof = -1;
        else if (now - spdMs >= 1000) {
          const int d = tofValidAhead() && tofMm < 1000 ? (int)tofMm : -1;
          if (spdTof > 0 && d > 0) {
            const float obs = (spdTof - d) / ((now - spdMs) / 1000.0f) / max(0.3f, cpgSpeed);
            if (obs > 15 && obs < 250) walkMmPerS += 0.15f * (obs - walkMmPerS);
          }
          spdTof = d;
          spdMs = now;
        }
      }
      if (t > EX_SENSE_HOLDOFF_MS) {
        // Walking but the body keeps rotating (more than a curve would): a leg is caught
        if (imuOk && now - exWinMs > EX_WINDOW_MS) {
          const float maxSwivel = exVeer ? EX_SWIVEL_VEER_DEG : EX_SWIVEL_DEG;
          if (fabsf(imuYawAngle - exWinYaw) > maxSwivel) { exStuck("stuck: swivel"); break; }
          exWinMs = now;
          exWinYaw = imuYawAngle;
        }
        // Frozen (see EX_FROZEN_MS): distance ahead and heading unchanged while walking
        if (tofSettled() && !exHeadAway()) {
          if (tofValidAhead() && tofMm < TOF_FAR_MM) {
            if (exFrzTof < 0 || abs((int)tofMm - exFrzTof) > EX_FROZEN_MM || fabsf(imuYawAngle - exFrzYaw) > 12) {
              exFrzTof = tofMm;
              exFrzYaw = imuYawAngle;
              exFrzMs = now;
            } else if (now - exFrzMs > EX_FROZEN_MS) {
              exStuck("stuck: not moving (distance and heading unchanged)");
              break;
            }
          } else {
            exFrzTof = -1;  // nothing in range: the ToF can't tell (the camera check does)
          }
        }
        // Walking but the wall ahead is not getting closer: pushing against something low.
        // Only while walking straight: curving changes what the ToF is looking at.
        if (exVeer) {
          exProgMs = now;
          exProgTof = -1;
        } else if (now - exProgMs > EX_PROGRESS_MS) {
          if (exProgTof >= 0 && tofValidAhead() && exProgTof - tofMm < EX_PROGRESS_MM) {
            exStuck("stuck: no progress");
            break;
          }
          exProgMs = now;
          exProgTof = tofValidAhead() ? (int)tofMm : -1;
        }
      }
      break;
    }

    case EX_STOPPING:
      exHeadLevel();
      // Knows both sides well enough from walking (live radar)? Choose right away - no
      // stop-and-sweep. Otherwise, or if memory says nothing is open, sweep to be sure.
      if (!exTriedMemory && t > 150) {
        exTriedMemory = true;
        mapProject();
        if (exMapCovered() && exDecide(true)) break;
      }
      if (t > EX_SETTLE_MS) exSweepBegin();
      break;

    case EX_SCAN: {
      // Sweep the head slowly across, reading the ToF all the way (a small radar)
      if (!exSweepMoving) {
        if (t < EX_SWEEP_START_MS) break;  // head still travelling to the start side
        if (exEdgePass && now - exEdgePassMs < 450) break;  // (an edge pass: the head tilting down, settling)
        exSweepMoving = true;
        exSweepLastMs = now;
        break;
      }
      if (exSweepEndMs == 0) {
        exSweepAngle += exSweepDir * EX_SWEEP_DPS * (now - exSweepLastMs) / 1000.0f;
        const int lim = exEdgePass ? EX_EDGE_SWEEP_DEG : EX_SWEEP_MAX_DEG;
        if (fabsf(exSweepAngle) >= lim) {
          exSweepAngle = constrain(exSweepAngle, (float)-lim, (float)lim);
          exSweepEndMs = now;
        }
        exHeadPanOff(exSweepAngle);
      }
      exSweepLastMs = now;
      if (newTof) {
        // Raw reading (the 3-sample median would smear neighbouring angles), placed where the
        // head was a moment ago: ranging time + servo lag
        const int raw = tofRawMm;
        const int rmm = (raw < 0 || raw >= 8000) ? TOF_FAR_MM : min(raw, TOF_FAR_MM);
        if (exEdgePass) {
          // looking down: floor there, or not (out of range / much longer than the floor = an edge)
          const int k = exEdgePass - 1, bin = exSweepBinOf(exSweepSeenAt(now, EX_SWEEP_LAG_MS));
          const int expect = exFloorExpect[k ? 2 : 0];
          if (exEdgeN[k][bin] < 255) exEdgeN[k][bin]++;
          if ((rmm >= TOF_FAR_MM || rmm > EX_CLIFF_RATIO * expect) && exEdgeHit[k][bin] < 255) exEdgeHit[k][bin]++;
        } else {
          exSweepAdd(exSweepSeenAt(now, EX_SWEEP_LAG_MS), rmm);
          exMapFeed(now, exSweepSeenAt(now, EX_SWEEP_LAG_MS), rmm);
        }
      }
      if (lightNew && !exEdgePass) {
        // The camera turns with the head: which directions look like a dark place
        const int bin = exSweepBinOf(exSweepSeenAt(now, EX_CAM_LAG_MS));
        if (exSweepLightN[bin] < 255) exSweepLightN[bin]++;
        if (darkFrame && exSweepDarkN[bin] < 255) exSweepDarkN[bin]++;
      }
      if (exSweepEndMs && now - exSweepEndMs > EX_SWEEP_END_MS && exEdgeCheck && exFloorMm > 0 && exEdgePass < 2) {
        // next: an edge pass, back the other way (the head is at this end already)
        exEdgePass++;
        exEdgePassMs = now;
        exSweepDir = -exSweepDir;
        exSweepAngle = constrain(exSweepAngle, (float)-EX_EDGE_SWEEP_DEG, (float)EX_EDGE_SWEEP_DEG);
        exSweepMoving = false;
        exSweepEndMs = 0;
        exSweepPanCmd = -999;
        exHeadPanOff(exSweepAngle);
        headTiltTo(TILT_MID + EX_FLOOR_ANGLES[exEdgePass == 1 ? 0 : 2]);
        break;
      }
      if (exSweepEndMs && now - exSweepEndMs > EX_SWEEP_END_MS) {
        exSweepFinish();
        if (exEdgePass == 2) exEdgeApply();
        exSweepPanCmd = -999;
        headPanTo(PAN_MID);
        exHeadLevel();
        exDecide();
      }
      break;
    }

    case EX_STUCK:
      // Pause a moment, like a person noticing they're stuck
      if (t > 350) exSet(EX_BACKUP, "backup");
      break;

    case EX_BACKUP: {
      exGait("smooth_walk_back", GAIT_BACK);
      // Nothing watches behind: stop backing up on a body alarm or if not moving
      if (imuOk && t > 600 && imuTiltFastDeg > EX_EDGE_TILT_DEG) { exStuck("edge: tipping", false, EX_TURN_CLIFF_DEG); break; }
      if (exBodyCheck(t, true)) break;
      if (exMotionCheck(camValid)) break;
      if (t > exBackupMs) {
        if (exBackupThenLook) {
          exBackupThenLook = false;
          exGait("", GAIT_NONE);
          exSet(EX_STOPPING, "stop");
        } else {
          exStartTurn(exTurnLeft, exTurnTargetDeg, true);
        }
      }
      break;
    }

    case EX_TURN: {
      if (now < exUnstickUntil) {  // freeing a caught leg: a short step back, then turn again
        exGait("smooth_walk_back", GAIT_BACK);
        exWinMs = now;
        exWinRot = fabsf(imuYawAngle - exTurnStartYaw);
        break;
      }
      if (exTurnOnSpot)
        exGait(exTurnLeft ? "sharp_left" : "sharp_right", exTurnLeft ? GAIT_STEP_LEFT : GAIT_STEP_RIGHT);  // on the spot: Sharp L / R (the creator's turn, smooth)
      else
        exGait(exTurnLeft ? "smooth_sharp_left" : "smooth_sharp_right", exTurnLeft ? GAIT_LEFT : GAIT_RIGHT);
      if (exBodyCheck(t, false)) break;
      // Turn to the chosen angle; the ToF must see the way open there (the camera doesn't
      // get a say: next to a wall it never sees much floor and would turn whole laps)
      if (newTof) exClearCount = exAheadClear() ? exClearCount + 1 : 0;
      const bool clear = exSearching || exClearCount >= 2;

      if (!imuOk) {
        // No gyro: fall back to a timed turn
        if (t > 700 + (unsigned long)(exTurnTargetDeg * 10) && clear) exTurnDone(exTurnTargetDeg);
        else if (t > 4000) { exGait("", GAIT_NONE); exSet(EX_STOPPING, "stop"); }
        break;
      }
      const float d = imuYawAngle - exTurnStartYaw;
      const float rot = fabsf(d);
      if (rot > 20 && exTurnRot <= 20) exLearnYawSign(((d > 0) == exTurnLeft) ? 1 : -1);  // once per turn
      exTurnRot = rot;
      // Verify the turn: the body must actually rotate
      // (2.2 s: each gait switch blends in over 0.4 s and the left turn rotates only ~15 deg/s - a shorter window
      // judges it "blocked" and sends the robot through every recovery step over and over)
      if (now - exWinMs > EX_TURN_WIN_MS) {
        if (rot - exWinRot < EX_TURN_ROT_MIN_DEG) {
          // The gyro says it isn't turning: a leg caught, or no room. Recover step by step, each
          // checked by the gyro again: sharp -> on the spot -> back off a little (frees a caught
          // leg) and on the spot again -> the other way -> give up.
          const float got = rot - exWinRot;
          exTurnTries++;
          if (exTurnTries == 1 && !exTurnOnSpot) {
            exTurnOnSpot = true;
            exSet(EX_TURN, exTurnLeft ? "turn L (spot)" : "turn R (spot)");
            xlog("TURN gyro: only %.0f deg in %lu ms (rotated %.0f of %.0f) - the sharp turn is blocked, turning on the spot",
                 got, EX_TURN_WIN_MS, rot, exTurnTargetDeg);
          } else if (exTurnTries <= 2) {
            exTurnOnSpot = true;
            exUnstickUntil = now + 700;
            exSet(EX_TURN, exTurnLeft ? "turn L (free leg)" : "turn R (free leg)");
            xlog("TURN gyro: only %.0f deg in %lu ms on the spot - a leg caught? backing off a little, then again", got, EX_TURN_WIN_MS);
          } else if (exTurnTries == 3) {
            exTurnLeft = !exTurnLeft;
            exTurnStartYaw = imuYawAngle;
            exTurnTargetDeg = min(exTurnTargetDeg, 90.0f);
            exTurnRot = 0;
            exSet(EX_TURN, exTurnLeft ? "turn L (other way)" : "turn R (other way)");
            xlog("TURN gyro: still only %.0f deg - trying the other way (%s %.0f)", got, exTurnLeft ? "left" : "right", exTurnTargetDeg);
            exWinMs = now;
            exWinRot = 0;
            break;
          } else {
            xlog("TURN gyro: no rotation either way (rotated %.0f of %.0f) - stuck", rot, exTurnTargetDeg);
            exStuck("stuck: can't turn");
            break;
          }
          exWinMs = now;
          exWinRot = rot;
          break;
        }
        exWinMs = now;
        exWinRot = rot;
      }
      if (rot >= exTurnTargetDeg && exPanCheckArmed) {
        exPanCheckArmed = false;
        exCheckPanMapping();
      }
      if (rot >= exTurnTargetDeg && clear) {
        exTurnDone(rot);
      } else if (exWallTry && rot >= exTurnTargetDeg + 20) {
        // Along the wall this way is blocked (a corner): turn to the other side instead
        xlog("blocked along the wall this way, trying the other side");
        exScanPick = -exScanPick;
        exNoteTurn(!exTurnLeft);
        exStartTurn(!exTurnLeft, rot + 90, true);
      } else if (rot >= exTurnTargetDeg + EX_TURN_EXTRA_DEG || t > EX_TURN_MAX_MS) {
        // Still not open a bit past the chosen angle: stop and look around again
        exFails++;
        if (exFails >= EX_MAX_FAILS) { exGiveUp("I'm stuck - please move me"); break; }
        exGait("", GAIT_NONE);
        exNextOnSpot = true;
        exSet(EX_STOPPING, "stop");
      }
      break;
    }

    case EX_CHECKFLOOR: {
      // Standing, head down: is there floor in the new direction before walking off?
      exGait("", GAIT_NONE);
      if (newTof && t > EX_GLANCE_SETTLE_MS + 100 && exGlanceN < EX_GLANCE_READINGS)
        exGlanceVals[exGlanceN++] = tofMm;
      if (exGlanceN < EX_GLANCE_READINGS && t < EX_GLANCE_MAX_MS + 200) break;
      const int r = exGlanceN ? medianOf(exGlanceVals, exGlanceN) : -1;
      FloorVerdict v = exFloorVerdict(r);
      if (v == FLOOR_EDGE && (r < 0 || r >= TOF_FAR_MM) && exCamSeesFloor()) {
        xlog("no echo from the floor, but the camera sees floor ahead (%.0f%%): shiny floor, carefully", camFree[1] * 100);
        v = FLOOR_CAUTION;
      }
      exHeadLevel();
      if (v == FLOOR_OK || v == FLOOR_CAUTION) {
        xlog("CHECK floor after the turn: %d mm (floor %d) -> %s", r, exFloorMm, v == FLOOR_OK ? "ok" : "careful");
        if (v == FLOOR_CAUTION) exCautionUntilMs = now + EX_CAUTION_HOLD_MS;
        exSearchRot = 0;
        exSinceGlance = 0;
        exSet(EX_FORWARD, "forward");
      } else {
        // Edge or something low this way too: keep turning the same way
        exSearchRot += exTurnRot;
        xlog("no floor this way (%d mm), turned %.0f so far\n", r, exSearchRot);
        if (exSearchRot >= EX_SEARCH_MAX_DEG) {
          exGiveUp("no safe floor around me - please move me");
          break;
        }
        exStartTurn(exTurnLeft, EX_SEARCH_TURN_DEG, true);
      }
      break;
    }

    case EX_CLIMBCHECK: {
      // Standing, looking down at the three angles in turn (as the floor check)
      exGait("", GAIT_NONE);
      if (exCalIdx < 3) {
        if (newTof && now - exClimbStepMs > 350 && exCalN < 16) exCalReadings[exCalN++] = tofMm;
        if (exCalN >= 6 || now - exClimbStepMs > 1600) {
          exClimbMed[exCalIdx] = exCalResult();
          exCalIdx++;
          exCalN = 0;
          exClimbStepMs = now;
          if (exCalIdx < 3) {
            exFloorTilt = TILT_MID + EX_FLOOR_ANGLES[exCalIdx];
            exHeadDown();
          }
        }
        break;
      }
      exHeadLevel();
      float r[3], rmin = 9, rmax = -9, rsum = 0;
      bool all = true;
      for (int i = 0; i < 3; i++) {
        r[i] = exClimbMed[i] > 0 && exFloorExpect[i] > 0 ? (float)exClimbMed[i] / exFloorExpect[i] : -1;
        if (r[i] < 0) { all = false; continue; }
        rmin = min(rmin, r[i]);
        rmax = max(rmax, r[i]);
        rsum += r[i];
      }
      const bool flat = all && rmax - rmin < EX_CLIMB_FLAT && rmin > 0.5f && rmax < 1.08f;
      const float H = exTofHeightMm();
      const int hMm = all ? (int)lroundf(max(0.0f, H * (1 - rsum / 3))) : -1;
      // A real step shows up as nearer readings (a raised surface): 12 mm or more. The plain floor
      // at its usual distance means the thing stopping the legs is right at the feet, under the
      // head's view (the beam passes 10-15 cm up there) - climbing would just walk into it.
      const bool go = flat && hMm >= 12 && hMm <= EX_CLIMB_MAX_MM;
      if (flat && hMm < 12) xlog("CLIMB floor looks plain (%d mm): blocked at the feet by something too low for the head sensor", hMm);
      xlog("CLIMB check: %d/%d/%d mm vs floor %d/%d/%d (ratios %.2f/%.2f/%.2f) -> %s, height ~%d mm (ToF %.0f mm up) -> %s",
           exClimbMed[0], exClimbMed[1], exClimbMed[2], exFloorExpect[0], exFloorExpect[1], exFloorExpect[2],
           r[0], r[1], r[2], flat ? "flat" : "not flat", hMm, H, go ? "climbing" : "not climbing");
      if (go) {
        exClimbMaxTilt = 0;
        exSet(EX_CLIMB, "climb");
      } else {
        exClimbSkip = true;
        exStuck(exClimbWhy);
        exClimbSkip = false;
      }
      break;
    }

    case EX_CLIMB: {
      // Bigger, slower steps; the IMU watches the tilt
      exGait("smooth_climb", GAIT_FWD);
      exClimbMaxTilt = max(exClimbMaxTilt, imuTiltDeg);
      if (imuOk && t > 300 && imuTiltDeg > EX_CLIMB_TILT_MAX) {
        xlog("CLIMB too steep (tilt %.0f deg): backing off", imuTiltDeg);
        exClimbSkip = true;
        exStuck("climb: too steep", false, EX_TURN_STUCK_DEG);
        exClimbSkip = false;
        break;
      }
      if (t > EX_CLIMB_MS) {
        xlog("CLIMB done after %.1f s (body tilted up to %.0f deg)", t / 1000.0f, exClimbMaxTilt);
        exSet(EX_FORWARD, "forward");
      }
      break;
    }

    case EX_CONFIRMFLOOR: {
      // A walking glance looked odd: stand still, head still down, and look properly
      exGait("", GAIT_NONE);
      if (newTof && t > EX_CONFIRM_SETTLE_MS && exGlanceN < EX_CONFIRM_READINGS)
        exGlanceVals[exGlanceN++] = tofMm;
      if (exGlanceN < EX_CONFIRM_READINGS && t < EX_CONFIRM_SETTLE_MS + 800) break;
      const int r = exGlanceN ? medianOf(exGlanceVals, exGlanceN) : -1;
      FloorVerdict v = exFloorVerdict(r);
      if (v == FLOOR_EDGE && (r < 0 || r >= TOF_FAR_MM) && exCamSeesFloor()) v = FLOOR_CAUTION;  // shiny floor
      // No echo at all: on a shiny floor the laser glances off at a shallow angle (a raw 8191 = too
      // weak, not a drop). Look steeper - at the
      // floor right in front, which does reflect - before believing there's a drop.
      if (exSteepTry) {
        v = (r > 60 && r < 700) ? FLOOR_CAUTION : FLOOR_EDGE;
        xlog("floor re-check, steeper: %d mm -> %s", r, v == FLOOR_EDGE ? "edge" : "floor is there (shiny)");
      } else if (v == FLOOR_EDGE && (r < 0 || r >= TOF_FAR_MM)) {
        exSteepTry = true;
        headTiltTo(min(TILT_MAX - 2, exHeadTiltNow + 12));
        exGlanceN = 0;
        exStateMs = now;  // settle again at the new angle
        xlog("floor re-check: no echo (shiny floor?) - looking steeper");
        break;
      }
      xlog("floor re-check: %d mm (floor %d) -> %s\n", r, exFloorMm,
                    v == FLOOR_EDGE ? "edge" : (v == FLOOR_OBSTACLE ? "low thing" : "fine"));
      if (v == FLOOR_EDGE) {
        exStuck("edge: floor ends", false, EX_TURN_CLIFF_DEG);
      } else if (v == FLOOR_OBSTACLE) {
        exRememberBlocked();
        exHeadLevel();
        exNextTurnDeg = EX_TURN_TARGET_DEG;
        exNextOnSpot = false;
        exSet(EX_STOPPING, "stop (low thing)");
      } else {
        exHeadLevel();  // false alarm (walking sway): carry on
        exSinceGlance = 0;
        exSet(EX_FORWARD, "forward");
      }
      break;
    }
  }
}

// ---- Explore settings: tunable from the web page (/exParams), saved in NVS ----
// Defaults are the values the variables start with. Only changed ones are saved, as
// "key=value;" in NVS key "exparams" (by the log task, with the camera off).
enum ParamType : uint8_t { PT_INT, PT_FLOAT, PT_ULONG };
struct ExParam {
  const char* key;
  const char* group;
  const char* label;
  const char* unit;
  const char* help;
  void* ptr;
  ParamType type;
  float mn, mx, st;
  float def;
};
#define PRM(key, group, label, unit, help, var, type, mn, mx, st) {key, group, label, unit, help, (void*)&var, type, mn, mx, st, 0}
const char* const G_SPEED = "Speed and stopping";
const char* const G_CURVE = "Curving and wandering";
const char* const G_LOOK = "Looking around and choosing a way";
const char* const G_TURN = "Turning and backing up";
const char* const G_STUCK = "Stuck detection";
const char* const G_CAM = "Camera";
const char* const G_EDGE = "Table edges";
const char* const G_DARK = "Darkness";
const char* const G_MEM = "Memory";
const char* const G_RADAR = "Live radar";
const char* const G_BOX = "Body box and places to avoid";
const char* const G_EMO = "Emotes (walk mode)";
const char* const G_CLIMB = "Climbing low flat things";
const char* const G_BAT = "Battery";
const char* const G_DISP = "Display";
ExParam exParams[] = {
  PRM("maxspd", G_SPEED, "Top speed", "x", "Gait speed cap (1 = full speed)", EX_MAX_SPEED, PT_FLOAT, 0.3, 1, 0.05),
  PRM("minspd", G_SPEED, "Slowest speed near things", "x", "Speed when about to stop", EX_MIN_SPEED, PT_FLOAT, 0.2, 1, 0.05),
  PRM("stop", G_SPEED, "Stop distance", "mm", "Something closer than this ahead: stop and look around", EX_STOP_MM, PT_INT, 50, 500, 10),
  PRM("slow", G_SPEED, "Start slowing at", "mm", "Slows down from here to the stop distance", EX_SLOW_MM, PT_INT, 100, 1200, 10),
  PRM("cautspd", G_SPEED, "Speed near a possible edge", "x", "While careful after an odd floor reading", EX_CAUTION_SPEED, PT_FLOAT, 0.2, 1, 0.05),
  PRM("darkspd", G_SPEED, "Speed in the dark", "x", "Camera off or a dark place ahead", EX_DARK_SPEED, PT_FLOAT, 0.2, 1, 0.05),
  PRM("backspd", G_SPEED, "Backing-up speed", "x", "Nothing watches behind", EX_BACKUP_SPEED, PT_FLOAT, 0.2, 1, 0.05),

  PRM("veer", G_CURVE, "Curve away from things at", "mm", "Gently curves (Fwd-L / Fwd-R) around something this close. 0 = never", EX_VEER_MM, PT_INT, 0, 1000, 10),
  PRM("veerclr", G_CURVE, "Straighten out beyond", "mm", "Way ahead this open again: walk straight", EX_VEER_CLEAR_MM, PT_INT, 100, 1500, 10),
  PRM("veerhold", G_CURVE, "Keep curving after clear", "ms", "Extra room when passing something", EX_VEER_HOLD_MS, PT_ULONG, 0, 3000, 50),
  PRM("veermax", G_CURVE, "Give up curving after", "ms", "Then walks straight; the stop takes over", EX_VEER_MAX_MS, PT_ULONG, 1000, 20000, 500),
  PRM("wander", G_CURVE, "Wander every", "s", "Drifts left or right now and then on open floor (+-40%). 0 = never", EX_WANDER_EVERY_S, PT_FLOAT, 0, 30, 0.5),
  PRM("wanderlen", G_CURVE, "Wander for", "s", "How long each drift lasts (+-35%)", EX_WANDER_FOR_S, PT_FLOAT, 0.3, 6, 0.1),

  PRM("sweepdps", G_LOOK, "Head sweep speed", "deg/s", "Slower = more readings per direction", EX_SWEEP_DPS, PT_FLOAT, 15, 150, 5),
  PRM("scantilt", G_LOOK, "Look up while sweeping", "deg", "Head tilted up this much: over the floor, longer range", EX_SCAN_TILT_UP, PT_INT, 0, 30, 1),
  PRM("settle", G_LOOK, "Stand still before looking", "ms", "Legs settle after stopping", EX_SETTLE_MS, PT_ULONG, 0, 2000, 50),
  PRM("open", G_LOOK, "A direction is open beyond", "mm", "When choosing a way", EX_OPEN_MM, PT_INT, 150, 1500, 10),
  PRM("gap", G_LOOK, "Room needed each side", "deg", "Closest reading within +- this counts: only gaps it fits", EX_GAP_HALF_DEG, PT_INT, 0, 25, 5),
  PRM("tight", G_LOOK, "Tight space below", "mm", "Best open way closer than this: turn on the spot", EX_TIGHT_MM, PT_INT, 200, 1500, 10),
  PRM("tooclose", G_LOOK, "Too close for a Sharp turn", "mm", "Stopped this close: turn on the spot", EX_TOO_CLOSE_MM, PT_INT, 0, 300, 10),
  PRM("walledge", G_LOOK, "Wall ends seen beyond", "deg", "Facing a wall: its far ends at this sweep angle or more", EX_WALL_EDGE_DEG, PT_INT, 30, 75, 5),
  PRM("wallrec", G_LOOK, "Wall recedes if", "x", "...that many times further than straight ahead: turn along it", EX_WALL_RECEDE, PT_FLOAT, 1.1, 4, 0.1),
  PRM("wallmin", G_LOOK, "...and at least", "mm", "", EX_WALL_MIN_MM, PT_INT, 50, 800, 10),
  PRM("sweeplag", G_LOOK, "ToF reading lag", "ms", "Where the head was when a sweep reading was taken", EX_SWEEP_LAG_MS, PT_ULONG, 0, 300, 10),
  PRM("camlag", G_LOOK, "Camera frame lag", "ms", "Same for the camera (dark directions)", EX_CAM_LAG_MS, PT_ULONG, 0, 500, 10),

  PRM("clear", G_TURN, "After a turn, open beyond", "mm", "The ToF must read this much ahead to walk off", EX_CLEAR_MM, PT_INT, 100, 1000, 10),
  PRM("turnlow", G_TURN, "Min turn from something low", "deg", "Camera / floor glance saw it; the sweep can't", EX_TURN_TARGET_DEG, PT_FLOAT, 0, 120, 5),
  PRM("turnstuck", G_TURN, "Min turn after getting stuck", "deg", "", EX_TURN_STUCK_DEG, PT_FLOAT, 0, 180, 5),
  PRM("turnesc", G_TURN, "Min turn after repeated trouble", "deg", "3+ failures in a row, or an edge", EX_TURN_ESCAPE_DEG, PT_FLOAT, 0, 180, 5),
  PRM("turnaround", G_TURN, "Dead end: turn around", "deg", "", EX_TURN_AROUND_DEG, PT_FLOAT, 90, 200, 5),
  PRM("turnextra", G_TURN, "Turn further if not open", "deg", "Past the chosen angle before looking again", EX_TURN_EXTRA_DEG, PT_FLOAT, 0, 120, 5),
  PRM("turnmin", G_TURN, "Turn check: at least", "deg", "...rotation per check window, else the turn failed", EX_TURN_ROT_MIN_DEG, PT_FLOAT, 0, 40, 1),
  PRM("turnwin", G_TURN, "Turn check window", "ms", "", EX_TURN_WIN_MS, PT_ULONG, 300, 5000, 100),
  PRM("turnmax", G_TURN, "Longest turn", "ms", "", EX_TURN_MAX_MS, PT_ULONG, 2000, 30000, 500),
  PRM("backstuck", G_TURN, "Back up after stuck", "ms", "Twice as long after repeated trouble", EX_STUCK_BACKUP_MS, PT_ULONG, 0, 5000, 100),
  PRM("backtight", G_TURN, "Back up in a dead end", "ms", "", EX_BACKUP_TIGHT_MS, PT_ULONG, 0, 5000, 100),

  PRM("tiltstuck", G_STUCK, "Tilted: stuck above", "deg", "Body tilt (averaged over the gait) held for...", EX_TILT_STUCK_DEG, PT_FLOAT, 5, 45, 1),
  PRM("tiltms", G_STUCK, "...for", "ms", "", EX_TILT_MS, PT_ULONG, 100, 3000, 50),
  PRM("fall", G_STUCK, "Fell over above", "deg", "Stops exploring", EX_FALL_DEG, PT_FLOAT, 20, 90, 1),
  PRM("fallpitch", G_STUCK, "...on its nose/back above", "deg", "Propped on the head or tail it rests lower", EX_FALL_PITCH_DEG, PT_FLOAT, 20, 90, 1),
  PRM("jolt", G_STUCK, "Bump: jolt above", "g", "...and above this many times the normal gait shake:", EX_JOLT_G, PT_FLOAT, 0.2, 3, 0.05),
  PRM("joltx", G_STUCK, "Bump: x gait shake", "x", "", EX_JOLT_RATIO, PT_FLOAT, 1, 10, 0.5),
  PRM("holdoff", G_STUCK, "Ignore start-up shake for", "ms", "", EX_SENSE_HOLDOFF_MS, PT_ULONG, 0, 5000, 100),
  PRM("swivel", G_STUCK, "Swivel: rotating more than", "deg", "...per window while walking straight = a leg caught", EX_SWIVEL_DEG, PT_FLOAT, 10, 180, 5),
  PRM("swivelv", G_STUCK, "Swivel while curving", "deg", "", EX_SWIVEL_VEER_DEG, PT_FLOAT, 10, 180, 5),
  PRM("swivelwin", G_STUCK, "Swivel window", "ms", "", EX_WINDOW_MS, PT_ULONG, 300, 5000, 100),
  PRM("frozenms", G_STUCK, "Not moving: distance and heading unchanged for", "ms", "Walking or curving, something in ToF range", EX_FROZEN_MS, PT_ULONG, 1500, 10000, 250),
  PRM("frozenmm", G_STUCK, "...within", "mm", "", EX_FROZEN_MM, PT_INT, 10, 150, 5),
  PRM("progms", G_STUCK, "No progress: wall must get closer within", "ms", "Walking toward a wall the ToF sees", EX_PROGRESS_MS, PT_ULONG, 1000, 10000, 250),
  PRM("progmm", G_STUCK, "...by at least", "mm", "", EX_PROGRESS_MM, PT_INT, 5, 200, 5),
  PRM("stillchk", G_STUCK, "Not moving: camera verdicts", "", "In a row, only with something low close by", EX_STILL_CHECKS, PT_INT, 1, 20, 1),
  PRM("stillms", G_STUCK, "...over at least", "ms", "", EX_STILL_MS, PT_ULONG, 0, 5000, 100),
  PRM("motcam", G_STUCK, "...camera free floor below", "x", "", EX_MOT_NEAR_CAM, PT_FLOAT, 0, 1, 0.05),
  PRM("walkok", G_STUCK, "Forgive failures after walking", "ms", "", EX_WALK_OK_MS, PT_ULONG, 1000, 30000, 500),
  PRM("maxfails", G_STUCK, "Give up after failures", "", "In a row, then asks for help", EX_MAX_FAILS, PT_INT, 2, 50, 1),

  PRM("camblock", G_CAM, "Camera stop: free floor below", "x", "Only when the ToF sees nothing close (low things)", EX_CAM_BLOCK, PT_FLOAT, 0, 0.8, 0.01),
  PRM("camframes", G_CAM, "...for frames", "", "", EX_CAM_BLOCK_FRAMES, PT_INT, 1, 20, 1),
  PRM("camslow", G_CAM, "Camera slow-down below", "x", "", EX_CAM_SLOW, PT_FLOAT, 0, 1, 0.01),
  PRM("camtof", G_CAM, "Ignore the camera if the ToF sees within", "mm", "Walls are left to the ToF", EX_CAM_TOF_MM, PT_INT, 0, 2000, 50),
  PRM("camedge", G_CAM, "Floor ends ahead below", "x", "With no wall there: glance down for an edge", EX_CAM_EDGE, PT_FLOAT, 0, 1, 0.01),

  PRM("glwalk", G_EDGE, "Glance down every", "s", "Walking time x speed since the last floor check", EX_GLANCE_WALK_S, PT_FLOAT, 0.5, 15, 0.1),
  PRM("glcaut", G_EDGE, "...when careful", "s", "", EX_GLANCE_CAUTION_S, PT_FLOAT, 0.3, 10, 0.1),
  PRM("glmin", G_EDGE, "Camera-triggered glances at most every", "s", "", EX_GLANCE_MIN_S, PT_FLOAT, 0.2, 10, 0.1),
  PRM("glsettle", G_EDGE, "Glance: head settle", "ms", "", EX_GLANCE_SETTLE_MS, PT_ULONG, 50, 1000, 10),
  PRM("glmax", G_EDGE, "Glance: longest", "ms", "", EX_GLANCE_MAX_MS, PT_ULONG, 200, 3000, 50),
  PRM("glread", G_EDGE, "Glance: readings", "", "", EX_GLANCE_READINGS, PT_INT, 1, 8, 1),
  PRM("cfsettle", G_EDGE, "Re-check: stand still for", "ms", "Before trusting an odd walking glance", EX_CONFIRM_SETTLE_MS, PT_ULONG, 0, 2000, 50),
  PRM("cfread", G_EDGE, "Re-check: readings", "", "", EX_CONFIRM_READINGS, PT_INT, 1, 8, 1),
  PRM("ftol", G_EDGE, "Floor match tolerance", "x", "At the start: floor within +- this of the expected distance", EX_FLOOR_TOL, PT_FLOAT, 0.05, 0.6, 0.01),
  PRM("cliff", G_EDGE, "Edge if the floor reads over", "x", "...this many times its usual distance (or nothing)", EX_CLIFF_RATIO, PT_FLOAT, 1.1, 4, 0.05),
  PRM("caut", G_EDGE, "Careful if over", "x", "", EX_CAUTION_RATIO, PT_FLOAT, 1.05, 3, 0.05),
  PRM("lowobs", G_EDGE, "Something low if under", "x", "", EX_OBST_RATIO, PT_FLOAT, 0.3, 0.95, 0.05),
  PRM("cauthold", G_EDGE, "Stay careful for", "ms", "", EX_CAUTION_HOLD_MS, PT_ULONG, 0, 60000, 1000),
  PRM("edgetilt", G_EDGE, "Tipping over an edge above", "deg", "Fast body tilt while walking", EX_EDGE_TILT_DEG, PT_FLOAT, 5, 60, 1),
  PRM("turncliff", G_EDGE, "Turn away from an edge", "deg", "", EX_TURN_CLIFF_DEG, PT_FLOAT, 45, 200, 5),
  PRM("search", G_EDGE, "No floor: turn steps of", "deg", "Turns on the spot looking for floor", EX_SEARCH_TURN_DEG, PT_FLOAT, 10, 120, 5),
  PRM("searchmax", G_EDGE, "...give up after", "deg", "", EX_SEARCH_MAX_DEG, PT_FLOAT, 90, 1080, 10),

  PRM("darkroom", G_DARK, "Too dark for the camera below", "", "Whole picture brightness (0-219)", EX_DARK_ROOM_LUMA, PT_FLOAT, 0, 120, 1),
  PRM("brightroom", G_DARK, "Camera back on above", "", "", EX_BRIGHT_ROOM_LUMA, PT_FLOAT, 0, 150, 1),
  PRM("darkhold", G_DARK, "...for", "ms", "", EX_DARK_HOLD_MS, PT_ULONG, 0, 10000, 100),
  PRM("darkahead", G_DARK, "Dark place ahead below", "", "Brightness of the view ahead...", EX_DARK_AHEAD_LUMA, PT_FLOAT, 0, 150, 1),
  PRM("darkratio", G_DARK, "...and below x the floor at its feet", "x", "", EX_DARK_AHEAD_RATIO, PT_FLOAT, 0.1, 1, 0.05),
  PRM("darkframes", G_DARK, "...for frames", "", "", EX_DARK_FRAMES, PT_INT, 1, 30, 1),

  PRM("lookevery", G_RADAR, "Glance left / right every", "s", "While walking, to fill in the live radar (+-30%). 0 = never", EX_LOOK_EVERY_S, PT_FLOAT, 0, 30, 0.5),
  PRM("lookdeg", G_RADAR, "Glance angle", "deg", "How far the head turns", EX_LOOK_DEG, PT_INT, 15, 75, 5),
  PRM("lookhold", G_RADAR, "Glance hold", "ms", "Looking that way (the first ~300 ms the ToF settles)", EX_LOOK_HOLD_MS, PT_ULONG, 300, 2000, 50),
  PRM("lookclear", G_RADAR, "Only glance with room ahead", "mm", "It walks on while looking aside", EX_LOOK_CLEAR_MM, PT_INT, 200, 1500, 50),
  PRM("mapage", G_RADAR, "Remember readings for", "s", "The live radar forgets older ones", EX_MAP_AGE_S, PT_FLOAT, 3, 60, 1),
  PRM("mapcover", G_RADAR, "Decide from memory when each side is known", "%", "Else it stops and sweeps. 100 = always sweep", EX_MAP_COVER_PCT, PT_INT, 0, 100, 5),

  PRM("tofh", G_CLIMB, "ToF height above the floor", "mm", "Measure it (standing, head straight); 0 = estimated from the floor readings", EX_TOF_H_MM, PT_INT, 0, 300, 5),
  PRM("batcal", G_BAT, "Battery reading x", "", "Scale for the battery reading: measure the pack with a multimeter, then set this to (multimeter volts / shown volts) x the current value", BAT_CAL, PT_FLOAT, 0.7, 1.4, 0.005),
  PRM("batfull", G_BAT, "Battery full at", "V", "100 %: the pack's voltage just after charging", BAT_FULL_V, PT_FLOAT, 7.8, 8.5, 0.05),
  PRM("batempty", G_BAT, "Battery empty at", "V", "0 %: below this the robot browns out (2S LiPo)", BAT_EMPTY_V, PT_FLOAT, 6.0, 7.4, 0.05),
  PRM("batwarn", G_BAT, "Warn at", "%", "The page shows a battery-low banner", BAT_WARN_PCT, PT_INT, 5, 50, 1),
  PRM("bathome", G_BAT, "Go back to the start at", "%", "While exploring", BAT_HOME_PCT, PT_INT, 3, 40, 1),
  PRM("batstop", G_BAT, "Sit down and stop at", "%", "Refuses to move until charged", BAT_STOP_PCT, PT_INT, 0, 20, 1),
  PRM("batsleep", G_BAT, "Power down when empty", "on", "1 = on: at the stop % for 90 s (no charger), screen, WiFi and servos off and the chip sleeps, so the LiPo can't run down to damage. Wakes when charging (checks every 10 min) or when its head is touched", BAT_SLEEP, PT_INT, 0, 1, 1),
  PRM("homemm", G_BAT, "Back at the start within", "mm", "Dead reckoning drifts: about there", EX_HOME_ARRIVE_MM, PT_INT, 100, 800, 25),
  PRM("climb", G_CLIMB, "Try climbing low flat things", "on", "1 = on: stuck on something low (book, carpet edge) -> look down, climb if flat and low", EX_CLIMB_ON, PT_INT, 0, 1, 1),
  PRM("climbmax", G_CLIMB, "Highest step to try", "mm", "From the look-down readings", EX_CLIMB_MAX_MM, PT_INT, 5, 60, 5),
  PRM("climbamp", G_CLIMB, "Step size while climbing", "x", "Legs lift higher and swing further (servo limits still apply)", EX_CLIMB_AMP, PT_FLOAT, 1.0, 1.8, 0.05),
  PRM("climbms", G_CLIMB, "Climb for", "ms", "Front and back legs over", EX_CLIMB_MS, PT_ULONG, 1500, 8000, 250),
  PRM("climbtilt", G_CLIMB, "Back off above a body tilt of", "deg", "Too steep to climb", EX_CLIMB_TILT_MAX, PT_FLOAT, 10, 35, 1),
  PRM("patbrain", G_EMO, "Dynamic head pats", "on", "1 = taps, double taps, petting and how often you pat get different, never-repeating reactions; 0 = the old one: hold its head a second and it plays the patted pose", PAT_BRAIN, PT_INT, 0, 1, 1),
  PRM("wavegyro", G_EMO, "Wave: gyro log", "on", "1 = logs the body's pitch / roll while waving (a WAVE line: how steady it stands); 0 = off", WAVE_GYRO, PT_INT, 0, 1, 1),
  PRM("waver4", G_EMO, "Wave: lean (R4 down)", "deg", "How far the back-right knee goes down before waving: more = leans back more; too far lifts its foot and it rocks on two diagonal legs", WAVE_R4, PT_INT, 30, 72, 1),
  PRM("waver2", G_EMO, "Wave: back-right hip in", "%", "While leaning, R2 (back-right hip) this far toward its tucked min: the back-right leg comes under the body and takes the weight", WAVE_R2_PCT, PT_INT, 0, 80, 5),
  PRM("wavel4", G_EMO, "Wave: back-left knee down", "%", "While leaning, L4 (back-left knee) this far toward lying: the whole back sinks so it tilts back", WAVE_L4_PCT, PT_INT, 0, 80, 5),
  PRM("emoamp", G_EMO, "Emote movement size", "x", "1 = as designed; more = bigger head and leg moves in the custom emotes (servo limits still apply)", EMO_AMP, PT_FLOAT, 0.8, 1.6, 0.05),
  PRM("bodyhalf", G_BOX, "Body half-width (legs included)", "mm", "Choosing a way: only gaps this wide either side of its path", EX_BODY_HALF_MM, PT_INT, 50, 200, 5),
  PRM("boxahead", G_BOX, "Box ahead while walking", "mm", "Something known beside its path this close: curve away. 0 = off", EX_BOX_AHEAD_MM, PT_INT, 0, 500, 10),
  PRM("nogomin", G_BOX, "Avoid places it got stuck for", "min", "Also dark places it walked into (dead reckoning, this Explore run). 0 = off", EX_NOGO_MIN, PT_FLOAT, 0, 30, 0.5),
  PRM("nogor", G_BOX, "...keeping this far away", "mm", "", EX_NOGO_R_MM, PT_INT, 100, 800, 25),

  PRM("blockmem", G_MEM, "Remember blocked directions for", "ms", "Avoids turning back toward them", EX_BLOCK_MEMORY_MS, PT_ULONG, 0, 600000, 5000),
  PRM("blocknear", G_MEM, "...within", "deg", "", EX_BLOCK_NEAR_DEG, PT_FLOAT, 5, 90, 5),
};
#undef PRM
const int EX_PARAM_N = sizeof(exParams) / sizeof(exParams[0]);
volatile bool exParamsDirty = false;
// Explore profiles: "My room" (your own tuned settings) and "New place" (careful, generic:
// table edges on, more room kept, slower, no climbing, problem spots remembered longer).
// Switching keeps your room's settings (NVS "exroom") and brings them back exactly.
volatile int exProfile = 0;        // 0 = my room, 1 = new place
String exRoomSaved;                // the room's changed settings while "new place" is on
bool exRoomEdge = true;
volatile bool exRoomDirty = false;
const char* const EX_NEW_PLACE = "stop=180;slow=450;maxspd=0.8;bodyhalf=110;climb=0;nogomin=10;";

float exParamGet(const ExParam& p) {
  switch (p.type) {
    case PT_INT: return (float)*(int*)p.ptr;
    case PT_FLOAT: return *(float*)p.ptr;
    default: return (float)*(unsigned long*)p.ptr;
  }
}

void exParamSet(ExParam& p, float v) {
  v = constrain(v, p.mn, p.mx);
  switch (p.type) {
    case PT_INT: *(int*)p.ptr = (int)lroundf(v); break;
    case PT_FLOAT: *(float*)p.ptr = v; break;
    default: *(unsigned long*)p.ptr = (unsigned long)lroundf(v); break;
  }
}

ExParam* exParamFind(const char* key) {
  for (int i = 0; i < EX_PARAM_N; i++)
    if (strcmp(exParams[i].key, key) == 0) return &exParams[i];
  return nullptr;
}

bool exParamChanged(const ExParam& p) { return fabsf(exParamGet(p) - p.def) > 1e-4f; }

// "key=value;..." of the changed settings
String exParamsChangedString() {
  String out;
  for (int i = 0; i < EX_PARAM_N; i++) {
    if (!exParamChanged(exParams[i])) continue;
    char b[40];
    snprintf(b, sizeof(b), "%s=%g;", exParams[i].key, exParamGet(exParams[i]));
    out += b;
  }
  return out;
}

// Apply "key=value;key=value" (also accepts ':' and ',')
int exParamsApply(const char* text, bool log) {
  int n = 0;
  char buf[1200];
  strlcpy(buf, text, sizeof(buf));
  for (char* tok = strtok(buf, ";,"); tok; tok = strtok(nullptr, ";,")) {
    char* eq = strpbrk(tok, "=:");
    if (!eq) continue;
    *eq = 0;
    ExParam* p = exParamFind(tok);
    if (!p) continue;
    exParamSet(*p, atof(eq + 1));
    if (log) xlog("PARAM %s = %g (default %g)", p->key, exParamGet(*p), p->def);
    n++;
  }
  return n;
}

// setup(): defaults = the values the variables start with; then the saved changes
void exParamsInit() {
  for (int i = 0; i < EX_PARAM_N; i++) exParams[i].def = exParamGet(exParams[i]);
  const String saved = prefs.getString("exparams", "");
  if (saved.length()) exParamsApply(saved.c_str(), false);
  exProfile = prefs.getInt("exprof", 0) == 1 ? 1 : 0;
  exRoomSaved = prefs.getString("exroom", "");
  exRoomEdge = prefs.getBool("exroomEdge", true);
}

// Log task, camera off (flash-safe)
void exParamsSaveIfDirty() {
  if (exRoomDirty) {
    exRoomDirty = false;
    prefs.putString("exroom", exRoomSaved);
    prefs.putBool("exroomEdge", exRoomEdge);
    prefs.putInt("exprof", exProfile);
  }
  if (!exParamsDirty) return;
  exParamsDirty = false;
  prefs.putString("exparams", exParamsChangedString());
}

extern bool exEdgeEnabled;
extern volatile bool exEdgeToggled;
void exSetProfile(int prof) {
  if (prof == exProfile) return;
  if (prof == 1) {  // room -> new place: keep the room's settings
    exRoomSaved = exParamsChangedString();
    exRoomEdge = exEdgeEnabled;
  }
  for (int i = 0; i < EX_PARAM_N; i++) exParamSet(exParams[i], exParams[i].def);
  exParamsApply(prof == 1 ? EX_NEW_PLACE : exRoomSaved.c_str(), false);
  const bool edge = prof == 1 ? true : exRoomEdge;
  if (edge != exEdgeEnabled) {
    exEdgeEnabled = edge;
    exEdgeToggled = true;
    prefPut("edge", edge, true);
  }
  exProfile = prof;
  exRoomDirty = true;
  exParamsDirty = true;
  xl::flushSoon = true;
  xlog("PROFILE %s: %s", prof ? "new place" : "my room", exParamsChangedString().c_str());
}

// Query values arrive URL-encoded (%3A for ':')
void urlDecodeInPlace(char* s) {
  char* o = s;
  for (; *s; s++) {
    if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
      const char h[3] = {s[1], s[2], 0};
      *o++ = (char)strtol(h, nullptr, 16);
      s += 2;
    } else {
      *o++ = (*s == '+') ? ' ' : *s;
    }
  }
  *o = 0;
}

// GET /exParams                       list (JSON)
// GET /exParams?set=stop:100,slow:300  change some
// GET /exParams?reset=all | reset=stop defaults
esp_err_t ex_params_handler(httpd_req_t* req) {
  char q[512] = "", val[480];
  const size_t qlen = httpd_req_get_url_query_len(req);
  if (qlen > 0 && qlen < sizeof(q)) httpd_req_get_url_query_str(req, q, sizeof(q));
  bool changed = false;
  if (httpd_query_key_value(q, "reset", val, sizeof(val)) == ESP_OK) {
    urlDecodeInPlace(val);
    for (int i = 0; i < EX_PARAM_N; i++) {
      if (strcmp(val, "all") != 0 && strcmp(val, exParams[i].key) != 0) continue;
      exParamSet(exParams[i], exParams[i].def);
    }
    xlog("PARAM reset %s", val);
    changed = true;
  }
  if (httpd_query_key_value(q, "set", val, sizeof(val)) == ESP_OK) {
    urlDecodeInPlace(val);
    changed |= exParamsApply(val, true) > 0;
  }
  if (httpd_query_key_value(q, "profile", val, sizeof(val)) == ESP_OK) exSetProfile(!strcmp(val, "new") ? 1 : 0);
  // "the battery is at N volts right now" (e.g. 8.4 just after charging): sets batcal from it
  if (httpd_query_key_value(q, "batnow", val, sizeof(val)) == ESP_OK && batPinMv > 100) {
    const float target = atof(val);
    if (target > 5 && target < 9.5f) {
      const float cal = target / (batPinMv / 1000.0f * 11.0f);
      if (cal > 0.7f && cal < 1.4f) {
        char sv[40];
        snprintf(sv, sizeof(sv), "batcal=%.4f", cal);
        changed |= exParamsApply(sv, true) > 0;
        batSlowV = 0;  // the slow average starts again from the new scale
        xlogSys("BATTERY calibrated: %.2f V = %.0f mV at the pin -> batcal %.4f", target, (float)batPinMv, cal);
      }
    }
  }
  if (changed) {
    exParamsDirty = true;
    xl::flushSoon = true;  // saved in the next camera-safe moment
  }
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, "application/json");
  static char (&buf)[1600] = *(char(*)[1600])heap_caps_calloc(1, sizeof(char) * (1600), MALLOC_CAP_SPIRAM);
  int n = snprintf(buf, sizeof(buf), "{\"profile\":\"%s\",\"params\":[", exProfile ? "new" : "room");
  for (int i = 0; i < EX_PARAM_N; i++) {
    const ExParam& p = exParams[i];
    if (n > (int)sizeof(buf) - 400) {
      if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) return ESP_FAIL;
      n = 0;
    }
    n += snprintf(buf + n, sizeof(buf) - n,
                  "%s{\"k\":\"%s\",\"g\":\"%s\",\"l\":\"%s\",\"u\":\"%s\",\"h\":\"%s\",\"v\":%g,\"d\":%g,"
                  "\"mn\":%g,\"mx\":%g,\"st\":%g}",
                  i ? "," : "", p.key, p.group, p.label, p.unit, p.help, exParamGet(p), p.def, p.mn, p.mx, p.st);
  }
  n += snprintf(buf + n, sizeof(buf) - n, "]}");
  httpd_resp_send_chunk(req, buf, n);
  return httpd_resp_send_chunk(req, nullptr, 0);
}

// Enter a mode (loop() only). Head goes to the calibrated mid, legs stand.
void enterMode(int mode) {
  if (activeMode == MODE_DESK && mode != MODE_DESK) deskm::deskLeave();
  if (activeMode == MODE_LIFE && mode != MODE_LIFE) lifem::lifeLeave();
  if (mode == MODE_EXPLORE || mode == MODE_DESK || mode == MODE_LIFE) xl::recording = true;  // the log is for Explore (and desk mode) only
  currentCommand = "";
  sw_oldMillis = 0;
  // Stand only if the legs are already driven (then it's standing: no motion) or Explore needs
  // them. Face tracking / walk mode leave limp legs alone.
  if ((legsPowered || mode == MODE_EXPLORE) && mode != MODE_DESK) runStandPose(0);
  headTargetDirty = false;
  headSlewTo(PAN_MID, TILT_MID);  // glides to the middle
  cpgSpeed = 1.0f;
  floorTopRow = FLOOR_TOP_ROW_LEVEL;
  if (mode == MODE_EXPLORE) exploreStart();
  else exploreStateName = "idle";
  activeMode = mode;  // the camera task draws frames from now on (it's the TFT's only user)
  xlog("MODE %s", MODE_NAMES[mode]);  // leaving Explore: the last line until the next Explore
  xl::recording = mode == MODE_EXPLORE || mode == MODE_DESK || mode == MODE_LIFE;
  if (mode == MODE_DESK) deskm::deskEnter();
  if (mode == MODE_LIFE) lifem::lifeEnter();
  if (!otaActive) prefPut("deskMode", mode == MODE_DESK, false);  // (desk mode comes back after a restart, an update too; Explore never does)
}

// Open web connections (diagnostics: browsers keep some open between requests)
int httpConnCount() {
  size_t n = 16;
  int fds[16];
  return (cmd_httpd && httpd_get_client_list(cmd_httpd, &n, fds) == ESP_OK) ? (int)n : -1;
}

// JSON for /setMode and /modeStatus
int buildStatusJson(char* out, size_t len) {
  int req = requestedMode, act = activeMode;
  const char* st;
  char whyLog[112] = "";
  char sweepDark[EX_SWEEP_BINS + 1];  // '1' = that direction looked dark
  for (int i = 0; i < EX_SWEEP_BINS; i++) sweepDark[i] = exSweepDark[i] ? '1' : '0';
  sweepDark[EX_SWEEP_BINS] = 0;
  char sweep[200];  // last look-around in cm, right to left, -1 = none
  int sn = 0;
  for (int i = 0; i < EX_SWEEP_BINS && sn < (int)sizeof(sweep) - 8; i++) {
    const int v = exSweepMm[i];
    sn += snprintf(sweep + sn, sizeof(sweep) - sn, "%s%d", i ? "," : "", v < 0 ? -1 : (v + 5) / 10);
  }
  for (int i = 0; i < 4; i++) {
    const char* w = exWhyLog[i];
    if (!w || !w[0]) break;
    if (i) strlcat(whyLog, ", ", sizeof(whyLog));
    strlcat(whyLog, w, sizeof(whyLog));
  }
  if (req != act) st = (req == MODE_WALK) ? "stopping" : "starting";
  else if (act == MODE_WALK && modeError[0]) st = modeError;
  else st = MODE_NAMES[act];
  return snprintf(out, len,
      "{\"mode\":\"%s\",\"status\":\"%s\",\"tof\":%d,\"explore\":\"%s\",\"cam\":%d,"
      "\"free\":[%.2f,%.2f,%.2f],\"assist\":%d,\"flip\":%d,"
      "\"imu\":%d,\"tilt\":%.0f,\"yaw\":%.0f,\"shake\":%.2f,\"jolt\":%.2f,"
      "\"stuck\":%d,\"why\":\"%s\",\"fails\":%d,\"rot\":%.0f,"
      "\"mot\":%d,\"dT\":%.1f,\"d2T\":%.1f,\"noise\":%.1f,"
      "\"floor\":%d,\"fwatch\":%d,\"speed\":%d,\"tofRaw\":%d,\"tofStatus\":%d,"
      "\"edge\":%d,\"caution\":%d,\"search\":%d,\"whylog\":\"%s\","
      "\"sweep\":[%s],\"pick\":%d,\"note\":\"%s\","
      "\"avoidDark\":%d,\"dark\":%d,\"darkAhead\":%d,\"light\":[%d,%d,%d],\"sweepDark\":\"%s\","
      "\"logKB\":%d,\"logDrop\":%u,\"boot\":%u,\"conns\":%d,\"apClients\":%d,\"camView\":%d,\"camOn\":%d,\"camFps\":%.1f,\"streamFps\":%.1f,\"rc\":%d,\"logPaused\":%d,"
      "\"sweepAge\":\"%s\",\"walkSpd\":%d,\"mapN\":%d,\"looking\":%d,"
      "\"nogo\":\"%s\",\"logOn\":%d,\"visFps\":%.1f,\"decFps\":%.1f,\"detMs\":%d,\"faceRoll\":%d,"
      "\"batV\":%.2f,\"batPct\":%d,\"batState\":%d,\"home\":%d,\"homeDist\":%d,\"profile\":\"%s\",\"gaps\":\"%s\",\"sd\":%d}",
      MODE_NAMES[req], st, tofOk && !tofRunning ? -2 : (int)tofMm, (const char*)exploreStateName, camReady ? 1 : 0,
      (float)camFree[camLeftBand()], (float)camFree[1], (float)camFree[camRightBand()],
      exploreCamAssist ? 1 : 0, panPlusIsLeft ? 0 : 1,
      imuOk ? 1 : 0, imuTiltDeg, imuYawRate, imuShakeRms, imuJolt,
      exStuckTotal, (const char*)exStuckWhy, exFails, (float)exTurnRot,
      (int)motVerdict, (float)motDiffT, (float)motDiff2T, (float)motNoise,
      exEdgeCheck ? exFloorMm : -1, exFloorWatch ? 1 : 0, (int)(cpgSpeed * 100 + 0.5f),
      (int)tofRawMm, (int)tofRawStatus, exEdgeEnabled ? 1 : 0, exCaution() ? 1 : 0, exSearching ? 1 : 0,
      whyLog, sweep, (int)exScanPick, (const char*)exNote,
      exAvoidDark ? 1 : 0, exCamDark ? 1 : 0, exDarkFrames > 0 ? 1 : 0,
      (int)camLumaAll, (int)camLumaAhead, (int)camLumaNear, sweepDark,
      xl::part ? (int)(xl::storedBytes / 1024) : -1, (unsigned)xl::dropped, (unsigned)xl::bootNo,
      httpConnCount(), (int)WiFi.softAPgetStationNum(), camViewWanted ? 1 : 0, camReady ? 1 : 0,
      (float)camFps, (float)streamFps, rcActive() ? 1 : 0, xl::paused ? 1 : 0,
      exSweepAge, (int)walkMmPerS, mapCount, exSideLooking ? 1 : 0,
      exNoGoBins, xl::recording ? 1 : 0, (float)visFps, (float)decFps, (int)detMs, (int)faceRoll,
      batSlowV, batPctShown >= 0 ? (int)batPctShown : (int)batPctSlow, (int)batState, exHoming ? 1 : 0, exHoming ? (int)exHomeDist() : -1,
      exProfile ? "new" : "room", exGapBins, sdSizeMB);
}

// /servo writes are queued here and applied by loop() (keeps I2C on one core)
volatile float pendingServoDeg[16];  // NAN = none
volatile int pendingServoMs = 0;      // glide time for them (0 = at once)
volatile bool pendingServoAny = false;

// Handler for /servo?p0=90&p1=45...
esp_err_t servo_handler(httpd_req_t *req) {
  char buf[256];
  size_t buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1 && buf_len <= sizeof(buf)) {
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      
      bool updated = false;
      for (int i = 0; i < 16; i++) {
        char key[8];
        snprintf(key, sizeof(key), "p%d", i);
        char val[16];
        if (httpd_query_key_value(buf, key, val, sizeof(val)) == ESP_OK) {
           float angle = atof(val);
           if (angle >= -90 && angle <= 180) {
              pendingServoDeg[i] = angle;
              updated = true;
           }
        }
      }

      char ms[8];
      pendingServoMs = httpd_query_key_value(buf, "ms", ms, sizeof(ms)) == ESP_OK ? constrain(atoi(ms), 0, 3000) : 0;
      if (updated) pendingServoAny = true;  // loop() writes them (I2C stays on one core)
    }
  }
  
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, "OK", 2);
}

// Handler for the creator's /cmd API
esp_err_t cmd_handler(httpd_req_t *req) {
  char buf[256];
  size_t buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1 && buf_len <= sizeof(buf)) {
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      char val[32];
      if (httpd_query_key_value(buf, "stop", val, sizeof(val)) == ESP_OK) {
        if (requestedMode == MODE_EXPLORE) xlog("WEB stop (ends Explore)");
        currentCommand = "";
        rcDriveLease = false;
        if (requestedMode == MODE_EXPLORE) requestedMode = MODE_WALK;  // STOP always ends exploring
      }
      else if (httpd_query_key_value(buf, "hold", val, sizeof(val)) == ESP_OK) {
        rcDriveMs = millis();  // RC: the finger is still on the pad (the walk carries on)
        rcSeenMs = millis();
      }
      else if (httpd_query_key_value(buf, "exm", val, sizeof(val)) == ESP_OK) {
        // Explore D-pad: held = the page repeats it; 0 = let go
        if (requestedMode == MODE_EXPLORE) {
          strlcpy((char*)exManualCmd, val[0] == '0' ? "" : val, sizeof(exManualCmd));
          exManualSeenMs = millis();
        }
      }
      else if (httpd_query_key_value(buf, "face", val, sizeof(val)) == ESP_OK) {
        setFace(String(val));  // just the face, no movement (e.g. happy, sad, love, talk_happy)
      }
      else if (httpd_query_key_value(buf, "speed", val, sizeof(val)) == ESP_OK) {
        if (rcActive()) rcSpeed = constrain((float)atof(val), 0.5f, 1.5f);  // RC throttle only
      }
      else if (httpd_query_key_value(buf, "mark", val, sizeof(val)) == ESP_OK) {
        if (requestedMode == MODE_EXPLORE) exMarkReq = true;
      }
      else if (batEmpty) {
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        return httpd_resp_send(req, "BATTERY_EMPTY", HTTPD_RESP_USE_STRLEN);
      }
      else if (requestedMode != MODE_WALK || activeMode != MODE_WALK) {
        // Walk engine / emotes are locked out while face tracking or explore owns the robot
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        return httpd_resp_send(req, "MODE_LOCKED", HTTPD_RESP_USE_STRLEN);
      }
      else if (httpd_query_key_value(buf, "nav", val, sizeof(val)) == ESP_OK) {  // nav=vx,wz (m/s, rad/s + = left)
        float vx = 0, wz = 0;
        sscanf(val, "%f,%f", &vx, &wz);
        navVx = constrain(vx, -0.08f, 0.1f);
        navWz = constrain(wz, -0.8f, 0.8f);
        currentCommand = "smooth_nav";
        if (httpd_query_key_value(buf, "rc", val, sizeof(val)) == ESP_OK) { rcDriveLease = true; rcDriveMs = millis(); }
      }
      else if (httpd_query_key_value(buf, "go", val, sizeof(val)) == ESP_OK) {
        xlog("WEB go=%s", val);
        String dir = String(val);
        currentCommand = dir;
        if (httpd_query_key_value(buf, "rc", val, sizeof(val)) == ESP_OK) { rcDriveLease = true; rcDriveMs = millis(); }
      }
      else if (httpd_query_key_value(buf, "limp", val, sizeof(val)) == ESP_OK && val[0] == '1') {
        // test: the legs off and their pose unknown - exactly like a fresh power-up (nothing saved)
        currentCommand = "";
        for (int i = 0; i < 8; i++) pwm.setPWM(mappedPins[i], 0, 4096);
        legsPowered = false;
        legsPoseKnown = false;
        xlogSys("LEGS limp (test: like a power-up)");
      }
      else if (httpd_query_key_value(buf, "backdown", val, sizeof(val)) == ESP_OK) {  // 4 angles, R4 R3 L3 L4
        int a[4], n = 0;
        for (char* t = strtok(val, ","); t && n < 4; t = strtok(nullptr, ",")) a[n++] = constrain(atoi(t), -30, 180);
        if (n == 4) {
          memcpy(REST_BACK, a, sizeof(a));
          char sv[40];
          snprintf(sv, sizeof(sv), "%d,%d,%d,%d", a[0], a[1], a[2], a[3]);
          prefs.putString("restBack", sv);
          xlog("WEB back legs down %s", sv);
        }
      }
      else if (httpd_query_key_value(buf, "sitpose", val, sizeof(val)) == ESP_OK) {  // 8 angles, R1 R2 L1 L2 R4 R3 L3 L4
        int a[8], n = 0;
        for (char* t = strtok(val, ","); t && n < 8; t = strtok(nullptr, ",")) a[n++] = constrain(atoi(t), -30, 180);
        if (n == 8) {
          memcpy(SIT_POSE, a, sizeof(a));
          char sv[64];
          snprintf(sv, sizeof(sv), "%d,%d,%d,%d,%d,%d,%d,%d", a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
          prefs.putString("sitPose", sv);
          xlog("WEB sit pose %s", sv);
        }
      }
      else if (httpd_query_key_value(buf, "pose", val, sizeof(val)) == ESP_OK) {
        xlog("WEB pose=%s", val);
        currentCommand = val;
        // (a walk from the RC pad - "rc=1": held, so it runs on the page's hold=1; tricks / emotes are one-shot)
        char rv[4];
        rcDriveLease = httpd_query_key_value(buf, "rc", rv, sizeof(rv)) == ESP_OK;
        if (rcDriveLease) rcDriveMs = millis();
      }
    }
  }
  
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, "OK", 2);
}

// Captive Portal Handler (Serves the controller HTML)
esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  return httpd_resp_send(req, (const char*)controller_html_gz, controller_html_gz_len);
}

// Android's connectivity check: "204 No Content" = no login page needed
esp_err_t no_content_handler(httpd_req_t *req) {
  httpd_resp_set_status(req, "204 No Content");
  return httpd_resp_send(req, NULL, 0);
}

// Handler for setting delays: /setDelay?fb=200&lr=50
esp_err_t delay_handler(httpd_req_t *req) {
  char buf[128];
  size_t buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1 && buf_len <= sizeof(buf)) {
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      char val[16];
      if (httpd_query_key_value(buf, "fb", val, sizeof(val)) == ESP_OK) {
        frameDelayFB = constrain(atoi(val), 30, 1000);
      }
      if (httpd_query_key_value(buf, "lr", val, sizeof(val)) == ESP_OK) {
        frameDelayLR = constrain(atoi(val), 20, 500);  // (0 would make the sharp turn's period 0)
      }
      if (httpd_query_key_value(buf, "emoteDelay", val, sizeof(val)) == ESP_OK) {
        emoteDelayMultiplier = constrain((float)atof(val), 0.3f, 10.0f);  // (negative would be an endless delay)
      }
    }
  }
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, "OK", 2);
}

// /setMode?mode=walk|face|explore (faceTracking = toggle face), &assist=0|1, &flip=0|1
esp_err_t mode_handler(httpd_req_t *req) {
  char buf[128];
  size_t buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1 && buf_len <= sizeof(buf)) {
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      char val[32];
      if (httpd_query_key_value(buf, "mode", val, sizeof(val)) == ESP_OK) {
        int m = -1;
        if (strcmp(val, "walk") == 0) m = MODE_WALK;
        else if (strcmp(val, "face") == 0) m = MODE_FACE;
        else if (strcmp(val, "explore") == 0) m = MODE_EXPLORE;
        else if (strcmp(val, "desk") == 0) m = MODE_DESK;
        else if (strcmp(val, "life") == 0) m = MODE_LIFE;
        else if (strcmp(val, "faceTracking") == 0) m = (requestedMode == MODE_FACE) ? MODE_WALK : MODE_FACE;
        if (m >= 0 && m != requestedMode) {
          xlog("WEB setMode %s", val);
          modeError[0] = 0;
          if (m == MODE_EXPLORE && !tofOk) {
            snprintf(modeError, sizeof(modeError), "error: ToF sensor not found");
          } else if (m == MODE_EXPLORE && robotFallen) {  // (entering stands it up: legs pushing while on its side)
            snprintf(modeError, sizeof(modeError), "error: I'm lying on my side - stand me up first");
          } else if (batEmpty && m != MODE_WALK) {
            snprintf(modeError, sizeof(modeError), "error: battery empty - please charge");
          } else {
            currentCommand = "";  // stop any walk/emote in progress
            requestedMode = m;
          }
        }
      }
      if (httpd_query_key_value(buf, "assist", val, sizeof(val)) == ESP_OK) {
        exploreCamAssist = (val[0] == '1');
        xlog("WEB option assist=%d", exploreCamAssist ? 1 : 0);
        prefPut("camAssist", exploreCamAssist, true);
      }
      if (httpd_query_key_value(buf, "edge", val, sizeof(val)) == ESP_OK) {
        exEdgeEnabled = (val[0] == '1');
        xlog("WEB option edge=%d", exEdgeEnabled ? 1 : 0);
        prefPut("edge", exEdgeEnabled, true);
        exEdgeToggled = true;  // loop() applies it
      }
      if (httpd_query_key_value(buf, "home", val, sizeof(val)) == ESP_OK && val[0] == '1' && requestedMode == MODE_EXPLORE) {
        xlog("WEB home: go back to the start");
        exHomeWhy = "asked on the page";
        exHomeReq = true;
      }
      if (httpd_query_key_value(buf, "avoiddark", val, sizeof(val)) == ESP_OK) {
        exAvoidDark = (val[0] == '1');
        xlog("WEB option avoiddark=%d", exAvoidDark ? 1 : 0);
        prefPut("avoidDark", exAvoidDark, true);
      }
      if (httpd_query_key_value(buf, "flip", val, sizeof(val)) == ESP_OK) {
        panPlusIsLeft = (val[0] != '1');
        xlog("WEB option flip=%d", panPlusIsLeft ? 0 : 1);
        prefPut("panPlusLeft", panPlusIsLeft, true);
      }
    }
  }
  static char (&json)[2400] = *(char(*)[2400])heap_caps_calloc(1, sizeof(char) * (2400), MALLOC_CAP_SPIRAM);
  buildStatusJson(json, sizeof(json));
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

// Plain http only: "Connection: close" keeps the http server's sockets free (the page polls every
// 0.7 s). Never on the secure server: there every new connection costs the robot a ~1 s TLS
// handshake (secure RC would lag, the video stall and the camera run out of memory).
static inline void connCloseHttp(httpd_req_t* req) {
  if (!tls_httpd || req->handle != tls_httpd) httpd_resp_set_hdr(req, "Connection", "close");
}

// /modeStatus -> {"mode","status","tof","explore","cam","free":[L,C,R],"assist","flip"}
esp_err_t mode_status_handler(httpd_req_t *req) {
  char q[32] = "", val[4];
  const size_t qlen = httpd_req_get_url_query_len(req);
  if (qlen > 0 && qlen < sizeof(q) && httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
      httpd_query_key_value(q, "rc", val, sizeof(val)) == ESP_OK && val[0] == '1') rcSeenMs = millis();  // RC mode open
  webSeenMs = millis();
  static char (&json)[2400] = *(char(*)[2400])heap_caps_calloc(1, sizeof(char) * (2400), MALLOC_CAP_SPIRAM);
  buildStatusJson(json, sizeof(json));
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  connCloseHttp(req);  // no idle connections piling up (polled every 0.7 s)
  return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

// GET /tune_walk?freq=&b0..b7=&a0..a7=&p0..p7=   the walk's numbers (smooth_tuned walks with them)
//   &save=fwd|back   ...and keep them as THE forward / backward walk (checked: forward walks with
//                    a positive speed, backward with a negative one)
//   ?reset=fwd|back|all   back to the built-in walks
esp_err_t tune_walk_handler(httpd_req_t *req) {
  char buf[512];  // 25 numbers with decimals: a shorter buffer drops the query
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
    char rs[8];
    if (httpd_query_key_value(buf, "reset", rs, sizeof(rs)) == ESP_OK) {
      const bool f = strcmp(rs, "back") != 0, b = strcmp(rs, "fwd") != 0;
      walkDefaults(f, b);
      walkSaveReq |= (f ? 4 : 0) | (b ? 8 : 0);
      xl::flushSoon = true;
      xlogSys("WALK back to the built-in %s", f && b ? "walks" : f ? "forward walk" : "backward walk");
      return httpd_resp_sendstr(req, "RESET");
    }
    char param[32];
    if (httpd_query_key_value(buf, "freq", param, sizeof(param)) == ESP_OK) {
      sw_frequency = constrain((float)atof(param), -0.1f, 0.1f);
      if (fabsf(sw_frequency) < 0.0005f) sw_frequency = sw_frequency < 0 ? -0.0005f : 0.0005f;  // (0: an endless gait period)
    }
    for (int i = 0; i < 8; i++) {
      char key[4];
      sprintf(key, "b%d", i);
      if (httpd_query_key_value(buf, key, param, sizeof(param)) == ESP_OK) sw_base[i] = atof(param);
      sprintf(key, "a%d", i);
      if (httpd_query_key_value(buf, key, param, sizeof(param)) == ESP_OK) sw_amp_mapped[i] = atof(param);
      sprintf(key, "p%d", i);
      if (httpd_query_key_value(buf, key, param, sizeof(param)) == ESP_OK) sw_phase[i] = atof(param);
    }
    char sv[8];
    if (httpd_query_key_value(buf, "save", sv, sizeof(sv)) == ESP_OK) {
      const bool fwd = !strcmp(sv, "fwd");
      if (!fwd && strcmp(sv, "back")) return httpd_resp_sendstr(req, "ERROR: save=fwd or save=back");
      if (fwd && !(sw_frequency > 0))
        return httpd_resp_sendstr(req, "ERROR: this walk's speed is negative (it walks backward) - it can't be the forward walk");
      if (!fwd && !(sw_frequency < 0))
        return httpd_resp_sendstr(req, "ERROR: this walk's speed is positive (it walks forward) - it can't be the backward walk");
      WalkSet& w = fwd ? walkFwd : walkBack;
      memcpy(w.base, sw_base, sizeof(w.base));
      memcpy(w.amp, sw_amp_mapped, sizeof(w.amp));
      memcpy(w.phase, sw_phase, sizeof(w.phase));
      w.freq = sw_frequency;
      walkSaveReq |= fwd ? 1 : 2;
      xl::flushSoon = true;
      xlogSys("WALK saved as the %s walk (speed %.4f)", fwd ? "forward" : "backward", w.freq);
      return httpd_resp_sendstr(req, fwd ? "SAVED forward" : "SAVED backward");
    }
  }
  return httpd_resp_send(req, "OK", 2);
}

// Captive Portal 404 Redirect Handler (For iOS/Android Captive Portals)
// ---- Settings panel on the page: /getSettings, /setSettings, WiFi (/api/wifi/*) ----
// Same API as the creator's firmware (without them the page's requests would hit the captive-portal
// redirect to 192.168.4.1, unreachable from the home WiFi).
// WiFi.persistent(false) keeps the WiFi driver from writing flash (not while the camera runs).
int motorCurrentDelay = 20;  // Settings page field (the creator's firmware uses it; kept for the page)
enum WifiSetupState { WIFI_SETUP_IDLE, WIFI_SETUP_QUEUED, WIFI_SETUP_CONNECTING };
volatile WifiSetupState wifiSetupState = WIFI_SETUP_IDLE;
char wifiSetupSsid[33] = "", wifiSetupPass[65] = "", wifiSetupError[48] = "";
volatile unsigned long wifiSetupQueuedMs = 0;
unsigned long wifiSetupStartMs = 0;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
const unsigned long WIFI_SETUP_START_DELAY_MS = 300;  // let the reply reach the page first

static void jsonEscapeTo(char* out, size_t len, const char* in) {
  size_t o = 0;
  for (; *in && o + 7 < len; in++) {
    const unsigned char ch = (unsigned char)*in;
    if (ch == '"' || ch == '\\') { out[o++] = '\\'; out[o++] = ch; }
    else if (ch < 0x20) o += snprintf(out + o, len - o, "\\u%04x", ch);
    else out[o++] = ch;
  }
  out[o] = 0;
}

static esp_err_t sendJson(httpd_req_t* req, const char* json) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, json);
}

esp_err_t get_settings_handler(httpd_req_t* req) {
  char json[200];
  snprintf(json, sizeof(json),
           "{\"frameDelay\":%d,\"frameDelayLR\":%d,\"walkCycles\":%d,\"motorCurrentDelay\":%d,"
           "\"faceFps\":%d,\"emoteDelay\":%.1f}",
           frameDelayFB, frameDelayLR, walkCycles, motorCurrentDelay, faceFps, emoteDelayMultiplier);
  return sendJson(req, json);
}

esp_err_t set_settings_handler(httpd_req_t* req) {
  char q[200] = "", val[16];
  const size_t qlen = httpd_req_get_url_query_len(req);
  if (qlen > 0 && qlen < sizeof(q)) httpd_req_get_url_query_str(req, q, sizeof(q));
  if (httpd_query_key_value(q, "frameDelay", val, sizeof(val)) == ESP_OK) frameDelayFB = constrain(atoi(val), 30, 1000);  // (as /setDelay)
  if (httpd_query_key_value(q, "walkCycles", val, sizeof(val)) == ESP_OK) walkCycles = constrain(atoi(val), 1, 50);
  if (httpd_query_key_value(q, "motorCurrentDelay", val, sizeof(val)) == ESP_OK) motorCurrentDelay = constrain(atoi(val), 0, 500);
  if (httpd_query_key_value(q, "faceFps", val, sizeof(val)) == ESP_OK) faceFps = constrain(atoi(val), 1, 30);
  xlog("WEB settings frameDelay=%d walkCycles=%d motorCurrentDelay=%d", frameDelayFB, walkCycles, motorCurrentDelay);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_sendstr(req, "OK");
}

// GET /api/wifi/status -> connection state for the Settings panel (polled while connecting)
esp_err_t wifi_status_handler(httpd_req_t* req) {
  const bool connected = WiFi.status() == WL_CONNECTED;
  char json[320], ssid[80] = "", err[100] = "";
  int n = snprintf(json, sizeof(json), "{\"connected\":%s,\"connecting\":%s",
                   connected ? "true" : "false", wifiSetupState != WIFI_SETUP_IDLE ? "true" : "false");
  if (wifiSetupError[0]) {
    jsonEscapeTo(err, sizeof(err), wifiSetupError);
    n += snprintf(json + n, sizeof(json) - n, ",\"lastError\":\"%s\"", err);
  }
  if (connected) {
    jsonEscapeTo(ssid, sizeof(ssid), WiFi.SSID().c_str());
    const String ip = WiFi.localIP().toString();
    n += snprintf(json + n, sizeof(json) - n, ",\"ssid\":\"%s\",\"ip\":\"%s\",\"host\":\"%s\",\"mdns\":false,\"rssi\":%d",
                  ssid, ip.c_str(), ip.c_str(), (int)WiFi.RSSI());
  }
  char home[80];
  jsonEscapeTo(home, sizeof(home), wantSsid());
  n += snprintf(json + n, sizeof(json) - n, ",\"search\":%s,\"home\":\"%s\",\"apUsers\":%d", staSearch ? "true" : "false", home,
                WiFi.softAPgetStationNum());
  // boot: the page's first window asks once per start of the robot
  snprintf(json + n, sizeof(json) - n, ",\"apIp\":\"%s\",\"boot\":%u}", WiFi.softAPIP().toString().c_str(), (unsigned)xl::bootNo);
  return sendJson(req, json);
}

// GET /wifi?search=0|1 (look for WiFi networks this session, or the hotspot only) &try=1 (join
// the home network now). Answers like /api/wifi/status.
esp_err_t wifi_choice_handler(httpd_req_t* req) {
  char q[40] = "", v[4];
  httpd_req_get_url_query_str(req, q, sizeof(q));
  if (httpd_query_key_value(q, "search", v, sizeof(v)) == ESP_OK) {
    staSearch = v[0] == '1';
    apMoveAsked = !staSearch;
    prefPut("hsOnly", staSearch ? 0 : 1, true);  // remembered: a restart away from home stays hotspot-only
    xlogSys("WIFI %s", staSearch ? "looking for WiFi networks" : "hotspot only (remembered, no searching)");
  }
  if (httpd_query_key_value(q, "try", v, sizeof(v)) == ESP_OK && v[0] == '1') { staSearch = true; staTryNow = true; prefPut("hsOnly", 0, true); }
  if (httpd_query_key_value(q, "forget", v, sizeof(v)) == ESP_OK && v[0] == '1') wifiForgetReq = true;  // back to the built-in one
  if (httpd_query_key_value(q, "bw", v, sizeof(v)) == ESP_OK && (atoi(v) == 20 || atoi(v) == 40)) wifiBwReq = atoi(v);  // loop() reconnects
  if (httpd_query_key_value(q, "tx", v, sizeof(v)) == ESP_OK && atoi(v) >= 2 && atoi(v) <= 20) {  // TX power, dBm (test)
    esp_wifi_set_max_tx_power((int8_t)(atoi(v) * 4));
    xlogSys("WIFI TX power %d dBm", atoi(v));
  }
  return wifi_status_handler(req);
}

// GET /api/wifi/scan -> {"scanning":true} while the background scan runs, then the list
// The networks from the last scan, kept 20 s (opening the list again is instant). A scan: the WiFi
// client switched on first (off in pure-hotspot mode, where a scan finds nothing),
// ~120 ms a channel (~1.6 s).
struct ScanNet { char ssid[33]; int8_t rssi; bool secure; };
ScanNet (&scanNets)[24] = *(ScanNet(*)[24])heap_caps_calloc(1, sizeof(ScanNet) * (24), MALLOC_CAP_SPIRAM);
int scanNetN = -1;
unsigned long scanNetMs = 0;
esp_err_t wifi_scan_handler(httpd_req_t* req) {
  wifiScanWantMs = millis();
  const int n = WiFi.scanComplete();
  if (n >= 0) {  // a scan just finished: keep its results
    scanNetN = 0;
    for (int i = 0; i < n && scanNetN < 24; i++) {
      const String s = WiFi.SSID(i);
      if (!s.length()) continue;
      ScanNet& e = scanNets[scanNetN++];
      strlcpy(e.ssid, s.c_str(), sizeof(e.ssid));
      e.rssi = WiFi.RSSI(i);
      e.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();
    scanNetMs = millis();
  } else if (n == WIFI_SCAN_RUNNING || wifiSetupState != WIFI_SETUP_IDLE) {
    return sendJson(req, "{\"scanning\":true}");
  } else if (scanNetN < 0 || millis() - scanNetMs > 20000) {
    if (!(WiFi.getMode() & WIFI_MODE_STA)) WiFi.enableSTA(true);
    WiFi.scanNetworks(true, false, false, 120);  // async: the robot keeps running
    return sendJson(req, "{\"scanning\":true}");
  }
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, "application/json");
  char buf[160], ssid[80];
  httpd_resp_send_chunk(req, "[", 1);
  for (int i = 0; i < scanNetN; i++) {
    jsonEscapeTo(ssid, sizeof(ssid), scanNets[i].ssid);
    const int len = snprintf(buf, sizeof(buf), "%s{\"ssid\":\"%s\",\"rssi\":%d,\"secure\":%s}", i ? "," : "", ssid,
                             (int)scanNets[i].rssi, scanNets[i].secure ? "true" : "false");
    httpd_resp_send_chunk(req, buf, len);
  }
  httpd_resp_send_chunk(req, "]", 1);
  return httpd_resp_send_chunk(req, nullptr, 0);
}

// POST /api/wifi/connect (form: ssid, password) -> the attempt runs from loop()
esp_err_t wifi_connect_handler(httpd_req_t* req) {
  char body[256] = "";
  const int len = min((int)req->content_len, (int)sizeof(body) - 1);
  int got = 0;
  while (got < len) {
    const int r = httpd_req_recv(req, body + got, len - got);
    if (r <= 0) break;
    got += r;
  }
  body[got] = 0;
  char ssid[100] = "", pass[200] = "";
  httpd_query_key_value(body, "ssid", ssid, sizeof(ssid));
  httpd_query_key_value(body, "password", pass, sizeof(pass));
  urlDecodeInPlace(ssid);
  urlDecodeInPlace(pass);
  if (!ssid[0]) return sendJson(req, "{\"success\":false,\"error\":\"SSID required\"}");
  if (wifiSetupState != WIFI_SETUP_IDLE) return sendJson(req, "{\"success\":false,\"error\":\"Connection attempt already in progress\"}");
  strlcpy(wifiSetupSsid, ssid, sizeof(wifiSetupSsid));
  strlcpy(wifiSetupPass, pass, sizeof(wifiSetupPass));
  wifiSetupError[0] = 0;
  wifiSetupQueuedMs = millis();
  wifiSetupState = WIFI_SETUP_QUEUED;
  xlog("WEB wifi connect to \"%s\"", wifiSetupSsid);
  return sendJson(req, "{\"success\":true,\"pending\":true}");
}

// The home network, away from home: while it's searching for its router the radio hops across
// every channel, leaving the robot's own access point for a moment each time - away from home the
// hotspot lags badly (slow to join, RC video and controls stuttering). So: never search while a
// phone is on the access point; otherwise try for 10 s, after 30 s, then less and less often (up
// to every 5 min). Lost it at home (router restart): the same, starting at 30 s.
// Saving / forgetting the picked network: a flash write, done with the camera paused (as the log)
void wifiSaveIfAsked() {
  if (!wifiSaveReq && !wifiForgetReq) return;
  if (!xl::lockFlash(2000)) return;  // try again next pass
  if (wifiForgetReq) {
    prefs.remove("wifiS");
    prefs.remove("wifiP");
    savedSsid[0] = savedPass[0] = 0;
    xlogSys("WIFI forgot the picked network: the built-in one from now on");
  } else {
    prefs.putString("wifiS", savedSsid);
    prefs.putString("wifiP", savedPass);
    xlogSys("WIFI remembered \"%s\" for the next start", savedSsid);
  }
  xl::unlockFlash();
  wifiSaveReq = wifiForgetReq = false;
}

// Pure hotspot (no WiFi client) whenever the robot isn't on, or joining, a router. In hotspot +
// client mode with the client idle (hotspot only chosen, or the home network not found) the hotspot
// is 10-40x slower: frames take 0.4-2 s instead of ~30 ms, even with the phone right next to it.
// It stays on the channel the phone is on.
// The hotspot's channel on its own. On a router it has to use the router's channel (a router picks
// a quiet one). On its own it would fall back to the default,
// channel 1, often the busiest there is (a phone next to it can lose 25-90 % of packets: RC freezes).
// So: the router's channel if it was on one this
// session, else the least busy of 1 / 6 / 11 from a quick scan (~1.5 s; overlapping networks count
// by their strength).
uint8_t wifiRouterChan = 0;  // the router's channel, while / after it was connected
uint8_t wifiScanChan = 0;    // the quietest of 1 / 6 / 11 from the last scan (0: none yet)
bool wifiNetSeen = false;    // the last scan actually saw the network it would join (not just "maybe a hidden iPhone hotspot")
uint8_t wifiNetChan = 0;     // that network's channel (the hotspot starts there: joining it later would move the hotspot and
                             // drop the phone)
// One scan (~1.5 s; the WiFi client must be on): which remembered network is in range - the one
// picked on the page (e.g. the iPhone's Personal Hotspot) first, then the built-in home one - and
// how busy the channels are. Returns the SSID to join (and its password), or nullptr.
volatile bool bootWifiPending = false;  // setup()'s scan still running: the hotspot / join wait for it (loop)
const char* wifiKnownHere(const char** pass, int scanned = -100) {
  const int n = scanned != -100 ? scanned : WiFi.scanNetworks(false, false, false, 120);
  bool saved = false, builtin = false;
  uint8_t savedCh = 0, builtinCh = 0;
  float load[14] = {0};
  for (int i = 0; i < n; i++) {
    const String name = WiFi.SSID(i);
    const int c = WiFi.channel(i);
    if (savedSsid[0] && name == savedSsid) { saved = true; savedCh = c; }
    if (name == ssid) { builtin = true; builtinCh = c; }
    const float w = max(0, WiFi.RSSI(i) + 100);  // -40 dBm = 60, -90 dBm = 10
    for (int k = 1; k <= 13; k++) {
      const int d = abs(k - c);
      load[k] += w * (d == 0 ? 1.0f : d == 1 ? 0.7f : d == 2 ? 0.4f : d == 3 ? 0.15f : 0.0f);
    }
  }
  WiFi.scanDelete();
  const uint8_t cand[3] = {1, 6, 11};
  uint8_t best = 6;
  for (uint8_t c : cand) if (load[c] < load[best]) best = c;
  wifiScanChan = best;
  wifiNetSeen = saved || builtin;
  wifiNetChan = saved ? savedCh : builtin ? builtinCh : 0;
  char note[128];
  snprintf(note, sizeof(note), "scan: %d networks%s%s; channel loads 1: %.0f, 6: %.0f, 11: %.0f -> quietest %u", n,
           saved ? ", the picked one here" : "", builtin ? ", home here" : "", load[1], load[6], load[11], best);
  govLog("wifi", note);
  if (saved) { *pass = savedPass; return savedSsid; }
  if (builtin) { *pass = password; return ssid; }
  // the network picked on the page, even unseen: an iPhone's Personal Hotspot hides unless its
  // settings screen is open, but it answers a device asking for it by name
  if (savedSsid[0]) { *pass = savedPass; return savedSsid; }
  return nullptr;
}
uint8_t wifiQuietChannel() {
  if (wifiRouterChan) return wifiRouterChan;
  if (bootWifiPending) return wifiScanChan ? wifiScanChan : 6;  // (the boot scan is running: no second one)
  if (!wifiScanChan) {
    const char* pw;
    wifiKnownHere(&pw);  // (sets wifiScanChan)
  }
  return wifiScanChan ? wifiScanChan : 6;
}

void wifiApOnly(bool on) {
  const bool apOnlyNow = !(WiFi.getMode() & WIFI_MODE_STA);
  if (on == apOnlyNow) return;
  // a scan from the page (running, or its results not picked up yet): after it
  if (on && (WiFi.scanComplete() != WIFI_SCAN_FAILED || millis() - wifiScanWantMs < 30000)) return;
  if (on) {
    const uint8_t ch = wifiQuietChannel();
    wifi_config_t c;
    // (never with a phone on it: moving the channel kicks the phone off -
    // unless "hotspot only" was just picked on the page: then the phone blinks off and rejoins, as it says)
    if (esp_wifi_get_config(WIFI_IF_AP, &c) == ESP_OK && c.ap.channel != ch && (WiFi.softAPgetStationNum() == 0 || apMoveAsked)) {
      c.ap.channel = ch;  // (a phone on the hotspot follows in a few seconds)
      esp_wifi_set_config(WIFI_IF_AP, &c);
    }
    apMoveAsked = false;
    WiFi.enableSTA(false);
    esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);  // on its own: a plain 20 MHz channel (see setup)
    char note[48];
    snprintf(note, sizeof(note), "pure hotspot on channel %u", ch);
    govLog("wifi", note);
    xlogSys("WIFI %s", note);
    return;
  } else {
    WiFi.enableSTA(true);
    WiFi.setSleep(false);
    esp_wifi_set_bandwidth(WIFI_IF_STA, wifiBw == 20 ? WIFI_BW_HT20 : WIFI_BW_HT40);
  }
  xlogSys("WIFI %s", on ? "pure hotspot (no WiFi client: all the radio for the phone)" : "hotspot + WiFi client (joining a network)");
  govLog("wifi", on ? "pure hotspot: no WiFi client" : "hotspot + WiFi client");
}

// The WiFi link itself can read "connected" (good signal) while nothing reaches the robot - not even a ping.
// Every 30 s: a TCP connect to the router (an answer -
// accepted or refused - means the link works; silence means it doesn't). 3 silent tries in a row: reconnect.
#include "lwip/sockets.h"
bool routerTry();
// up to 3 tries: on a lossy link one lost connect isn't a dead link (reconnecting would kill a chat that is
// still working)
bool routerAnswers() {
  for (int i = 0; i < 3; i++) {
    if (routerTry()) return true;
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  return false;
}
bool routerTry() {
  const IPAddress gw = WiFi.gatewayIP();
  if (gw == IPAddress(0, 0, 0, 0)) return true;  // (no router address: nothing to test)
  const int s = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s < 0) return true;  // (out of sockets: not the link's fault)
  lwip_fcntl(s, F_SETFL, lwip_fcntl(s, F_GETFL, 0) | O_NONBLOCK);
  struct sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(80);
  a.sin_addr.s_addr = (uint32_t)gw;
  bool alive = false;
  if (lwip_connect(s, (struct sockaddr*)&a, sizeof(a)) == 0) alive = true;
  else if (errno == EINPROGRESS) {
    fd_set w;
    FD_ZERO(&w);
    FD_SET(s, &w);
    struct timeval tv = {5, 0};  // (longer than lwIP's own 3 s first retry of the connect)
    if (lwip_select(s + 1, nullptr, &w, nullptr, &tv) > 0) {
      int e = 0;
      socklen_t l = sizeof(e);
      lwip_getsockopt(s, SOL_SOCKET, SO_ERROR, &e, &l);
      alive = e == 0 || e == ECONNREFUSED;  // (refused: the router answered)
    }
  } else alive = errno == ECONNREFUSED;
  lwip_close(s);
  return alive;
}
volatile bool linkCheckNow = false;  // gemini.h: the chat's connection broke - is the WiFi link dead? check now
// setup()'s WiFi scan is done: the hotspot on the quietest channel, DNS, and the remembered network (if here)
void bootWifiFinish() {
  // the scan: the blocking one (the background scan finds 0 networks at boot).
  // Run from loop() once, right after the boot: the face is already up,
  // only loop() waits for it
  bootWifiPending = false;
  const char* bootPw = nullptr;
  const char* bootNet = wifiKnownHere(&bootPw);
  // joining a network seen in the scan: the hotspot starts on ITS channel (one radio: the hotspot follows the
  // router anyway - starting elsewhere means a channel jump once joined, dropping every phone on the hotspot)
  if (staSearch && bootNet && wifiNetChan >= 1 && wifiNetChan <= 13) wifiRouterChan = wifiNetChan;
  esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
  WiFi.softAP(ap_ssid, ap_password, wifiQuietChannel());
  esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
  // DNS on the robot's own network: only its name ("oreo") - NOT every name (phones would take it for a hotel login)
  dnsServer.start(DNS_PORT, "oreo", WiFi.softAPIP());
  if (!staSearch) bootNet = nullptr;
  if (bootNet) WiFi.begin(bootNet, bootPw);  // (wifiKeepUp gives up after 12 s: hotspot only)
  else {
    WiFi.disconnect(false, false);  // the access point alone, lag-free
    xlogSys("WIFI %s: access point only on channel %u%s", !staSearch ? "hotspot-only mode" : "no known network here",
            (unsigned)wifiQuietChannel(), staSearch ? " (looks again after 90 s with nobody on it)" : "");
  }
  xlogSys("WIFI ready at %.1f s: hotspot on channel %u%s%s", millis() / 1000.0f, (unsigned)wifiQuietChannel(),
          bootNet ? ", joining " : "", bootNet ? bootNet : "");
}

void linkCheckTask(void*) {
  int silent = 0;
  for (;;) {
    // every 30 s - or at once when the chat lost its connection (a dead link would take ~65 s to notice:
    // the chat gives up before that)
    for (int i = 0; i < 30 && !linkCheckNow; i++) vTaskDelay(pdMS_TO_TICKS(1000));
    const bool urgent = linkCheckNow;
    linkCheckNow = false;
    if (WiFi.status() != WL_CONNECTED || otaActive) { silent = 0; continue; }
    if (routerAnswers()) { silent = 0; continue; }
    if (urgent) {  // a second check (3 more tries) before reconnecting: two failed checks, not one lost packet
      vTaskDelay(pdMS_TO_TICKS(3000));
      if (routerAnswers()) { silent = 0; continue; }
      silent = 3;
    }
    if (++silent < 3) continue;
    silent = 0;
    xlogSys("WIFI link dead (\"connected\", the router doesn't answer) - reconnecting");
    govLog("wifi", "link dead: reconnecting");
    WiFi.disconnect(false, false);
    vTaskDelay(pdMS_TO_TICKS(500));
    WiFi.reconnect();
  }
}

void wifiKeepUp() {
  if (wifiBwReq) {  // a new channel width: reconnect with it (a few seconds; the hotspot stays up)
    wifiBw = wifiBwReq;
    wifiBwReq = 0;
    prefPut("wifiBw", wifiBw, false);
    esp_wifi_set_bandwidth(WIFI_IF_STA, wifiBw == 20 ? WIFI_BW_HT20 : WIFI_BW_HT40);
    xlogSys("WIFI channel width %d MHz: reconnecting", wifiBw);
    esp_wifi_disconnect();
    esp_wifi_connect();
  }
  wifiSaveIfAsked();
  static unsigned long nextTry = 120000, tryingSince = 0, backoff = 120000, apEmptySince = 0;
  if (wifiSetupState != WIFI_SETUP_IDLE) return;  // the page is connecting it
  const unsigned long now = millis();
  static bool first = true;
  if (first) {  // setup() started joining the home network: that's a try too (12 s)
    first = false;
    tryingSince = now;
  }
  if (!staSearch) {
    // Hotspot only: not searching AND off the router - one radio serves both, and a router's crowded
    // channel starves the phone's RC on the hotspot.
    if (WiFi.status() == WL_CONNECTED) {
      xlogSys("WIFI hotspot only: leaving the router");
      WiFi.disconnect(false, false);
    }
    if (tryingSince) {
      WiFi.disconnect(false, false);
      tryingSince = 0;
    }
    wifiApOnly(true);
    return;
  }
  if (staTryNow) {  // asked on the page: join now, even with a phone on the hotspot
    staTryNow = false;
    if (WiFi.status() != WL_CONNECTED) {
      wifiApOnly(false);
      const char* pw;
      const char* net = wifiKnownHere(&pw);
      WiFi.begin(net ? net : wantSsid(), net ? pw : wantPass());  // (asked for: tried even if the scan missed it)
      tryingSince = now;
      backoff = 120000;
      return;
    }
  }
  static bool wasOn = false;
  if (WiFi.status() != WL_CONNECTED && wasOn) {
    // just dropped off the router (a blip): straight back on once (otherwise it stays hotspot-only and off the
    // home network for minutes). Not with a phone on the hotspot (its RC comes first).
    wasOn = false;
    if (WiFi.softAPgetStationNum() == 0 && !tryingSince) {
      xlogSys("WIFI dropped off the router: joining again");
      esp_wifi_connect();  // (the network it was just on, as configured)
      tryingSince = now;
      return;
    }
  }
  if (WiFi.status() == WL_CONNECTED) {
    wasOn = true;
    backoff = 120000;
    tryingSince = 0;
    nextTry = now + backoff;
    uint8_t ch = 0;
    wifi_second_chan_t c2;
    if (esp_wifi_get_channel(&ch, &c2) == ESP_OK && ch) wifiRouterChan = ch;  // the hotspot's channel later, on its own
    return;
  }
  const int apUsers = WiFi.softAPgetStationNum();
  if (tryingSince) {
    if (now - tryingSince > 12000 || (apUsers > 0 && now - tryingSince > 12000)) {  // not found: stop
      WiFi.disconnect(false, false);
      wifiApOnly(true);  // until the next try: all the radio for the hotspot
      tryingSince = 0;
      backoff = min(backoff * 2, 600000UL);
      nextTry = now + backoff;
    }
    return;
  }
  // Looking again flips the radio into "hotspot + client" for a while (the hotspot hiccups, its channel can
  // move): only when nobody has been on the hotspot for 90 s - an iPhone drops a "no internet" network for a
  // moment all the time - and only for a network the scan actually saw (retrying an unseen remembered
  // network every few minutes would knock the phone off each time)
  if (apUsers > 0) apEmptySince = 0;
  else if (!apEmptySince) apEmptySince = now ? now : 1;
  if (apUsers == 0 && apEmptySince && now - apEmptySince > 90000 && (long)(now - nextTry) >= 0) {
    wifiApOnly(false);
    const char* pw;
    const char* net = wifiKnownHere(&pw);
    if (net && wifiNetSeen) {
      WiFi.begin(net, pw);
      tryingSince = now;
      return;
    }
    backoff = min(backoff * 2, 600000UL);  // nothing known in range: look again later
    nextTry = now + backoff;
  }
  wifiApOnly(true);  // waiting for the next try (or after a scan from the page): pure hotspot
}

// loop(): run a queued connect without blocking
void updateWifiSetup() {
  if (wifiSetupState == WIFI_SETUP_IDLE) return;
  if (wifiSetupState == WIFI_SETUP_QUEUED) {
    // Joining a router on another channel moves the robot's own access point too:
    // give the reply time to reach the page first
    if (millis() - wifiSetupQueuedMs < WIFI_SETUP_START_DELAY_MS) return;
    wifiApOnly(false);
    WiFi.begin(wifiSetupSsid, wifiSetupPass);
    wifiSetupStartMs = millis();
    wifiSetupState = WIFI_SETUP_CONNECTING;
    return;
  }
  const wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    xlog("WIFI connected to \"%s\" as %s", wifiSetupSsid, WiFi.localIP().toString().c_str());
    strlcpy(savedSsid, wifiSetupSsid, sizeof(savedSsid));  // remembered: joined at the next start too
    strlcpy(savedPass, wifiSetupPass, sizeof(savedPass));
    wifiSaveReq = true;
    wifiSetupPass[0] = 0;
    wifiSetupState = WIFI_SETUP_IDLE;
    return;
  }
  // Right after WiFi.begin() the status can still show the previous attempt
  const bool terminal = millis() - wifiSetupStartMs > 1000 && (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL);
  if (terminal || millis() - wifiSetupStartMs >= WIFI_CONNECT_TIMEOUT_MS) {
    strlcpy(wifiSetupError, st == WL_NO_SSID_AVAIL ? "Network not found"
                            : st == WL_CONNECT_FAILED ? "Wrong password or connection rejected"
                                                      : "Connection timed out", sizeof(wifiSetupError));
    xlog("WIFI connect to \"%s\" failed: %s", wifiSetupSsid, wifiSetupError);
    WiFi.disconnect();  // stop background retries; the robot's own access point stays up
    wifiSetupPass[0] = 0;
    wifiSetupState = WIFI_SETUP_IDLE;
  }
}

// GET :81/stream -> MJPEG (multipart), newest frame each time, until the page closes it or the
// camera view stays off for 3 s
// Video pacing (the governor for the WiFi): a frame takes sendMs to go out; the next waits at least
// as long again, so the video uses at most half the airtime. On a good link that's nothing (a few
// ms: full rate); on a weak one (driving away, the body in the way) the video slows down instead of
// filling the air (the drive commands and the RC heartbeat would queue behind it and RC would freeze).
volatile float videoSendMs = 0;  // average time a frame takes to go out
volatile bool videoPace = false;  // /camView?pace=1: on (off by default: it makes video 3-5x slower and the controls no faster)
unsigned long videoGapMs(unsigned long minMs) {
  if (!videoPace || videoSendMs < 80) return minMs;  // a healthy link (a VGA frame goes out in 40-70 ms): full rate
  return min((unsigned long)(2 * videoSendMs), 1000UL);
}
volatile int camAskedSize = FRAMESIZE_VGA;  // the size the page picked: the auto size never goes above it
void videoSendTook(float ms) {
  videoSendMs = videoSendMs * 0.7f + ms * 0.3f;
  // Auto size: a VGA frame on a weak link can take 1.8-2.6 s to go out, and you only see a
  // frame once it's all there. Slow frames for 2 s: one size down (vga -> hvga
  // -> qvga); quick again for 8 s: one size back up, never above what the page asked for.
  {
    static unsigned long slowSince = 0, fastSince = 0;
    const unsigned long now = millis();
    int cur = -1, asked = -1;
    for (int i = 0; i < CAM_SIZE_N; i++) {
      if (CAM_SIZES[i].fs == camViewSize) cur = i;
      if (CAM_SIZES[i].fs == camAskedSize) asked = i;
    }
    if (cur >= 0 && !camPhotoQ) {  // (not while a photo for the chat is being taken)
      if (videoSendMs > 200) {
        fastSince = 0;
        if (!slowSince) slowSince = now;
        if (now - slowSince > 2000 && cur > 0) {
          slowSince = now;
          camViewSize = CAM_SIZES[cur - 1].fs;
          camViewSizeChanged = true;
          char n[72];
          snprintf(n, sizeof(n), "video auto size down to %s (frames took %.0f ms)", CAM_SIZES[cur - 1].name, (float)videoSendMs);
          govLog("cam", n);
        }
      } else if (videoSendMs < 60) {
        slowSince = 0;
        if (!fastSince) fastSince = now;
        if (now - fastSince > 8000 && asked >= 0 && cur < asked) {
          fastSince = now;
          camViewSize = CAM_SIZES[cur + 1].fs;
          camViewSizeChanged = true;
          char n[64];
          snprintf(n, sizeof(n), "video auto size up to %s", CAM_SIZES[cur + 1].name);
          govLog("cam", n);
        }
      } else {
        slowSince = fastSince = 0;
      }
    }
  }
  static unsigned long logMs = 0;
  if (videoSendMs > 150 && millis() - logMs > 10000) {
    logMs = millis();
    char n[80];
    snprintf(n, sizeof(n), "video paced: a frame takes %.0f ms to send (weak WiFi) - %.1f fps max", (float)videoSendMs,
             1000.0f / videoGapMs(STREAM_MIN_MS));
    govLog("cam", n);
  }
}

esp_err_t stream_handler(httpd_req_t* req) {
  const size_t HDR_ROOM = 96;
  uint8_t* buf = (uint8_t*)heap_caps_malloc(HDR_ROOM + JPG_MAX + 4, MALLOC_CAP_SPIRAM);
  if (!buf) return httpd_resp_send_500(req);
  // Raw writes, not httpd's chunked encoding: that splits every write into three and runs at
  // ~89 KB/s; raw with no Nagle delay ~215 KB/s = 24 fps with room to spare
  const int one = 1;
  setsockopt(httpd_req_to_sockfd(req), IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  // ?raw=1: the page reads the stream itself (fetch) - labelled plain bytes, which every browser
  // (Safari too) hands over as it arrives; the same frames. Without it: MJPEG for an <img>.
  char q[24] = "", rv[4] = "";
  httpd_req_get_url_query_str(req, q, sizeof(q));
  const bool raw = httpd_query_key_value(q, "raw", rv, sizeof(rv)) == ESP_OK && rv[0] == '1';
  const char* head = raw
    ? "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
      "Access-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n"
    : "HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace;boundary=frame\r\n"
      "Access-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
  // Never blocks for long: a phone that keeps a closed screen's stream open but stops reading
  // fills the send buffer, and a blocking send would sit there for 5 s while the new stream waits
  // behind it. Waits in 100 ms steps, gives up when a newer stream is asked for or after 4 s.
  const int fd = httpd_req_to_sockfd(req);
  const uint32_t myGen = streamGen;
  auto sendAll = [&](const char* d, size_t n) {
    unsigned long stuckMs = millis();
    while (n > 0) {
      if (streamGen != myGen) return false;
      fd_set w;
      FD_ZERO(&w);
      FD_SET(fd, &w);
      timeval tv = {0, 100000};
      const int sel = select(fd + 1, nullptr, &w, nullptr, &tv);
      if (sel < 0) return false;
      if (sel == 0) {
        if (millis() - stuckMs > 4000) return false;
        continue;
      }
      const int r = send(fd, d, n, MSG_DONTWAIT);
      if (r < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
        return false;
      }
      d += r;
      n -= r;
      stuckMs = millis();
    }
    return true;
  };
  bool ok = sendAll(head, strlen(head));
  __atomic_add_fetch(&streamClients, 1, __ATOMIC_SEQ_CST);  // (two tasks count viewers: a lost ++/-- would leave the camera on for good)
  camViewViewerMs = millis();
  uint32_t lastSeq = jpgSeq, sentFrames = 0;
  unsigned long lastFrameMs = millis(), lastSentMs = 0, fpsMs = millis();
  while (ok) {
    if (streamGen != myGen) break;  // a newer stream was asked for: this one is stale
    if (jpgSeq == lastSeq || millis() - lastSentMs < videoGapMs(STREAM_MIN_MS)) {
      if (!camViewWanted && millis() - lastFrameMs > 3000) break;
      if (millis() - lastFrameMs > 8000) break;  // no frame for 8 s: close (otherwise it holds the server task and hangs its restart)
      vTaskDelay(pdMS_TO_TICKS(2));
      continue;
    }
    // One write per frame: part header + JPEG + CRLF
    xSemaphoreTake(jpgMutex, portMAX_DELAY);
    const size_t len = jpgFrameLen;
    const int hl = snprintf((char*)buf, HDR_ROOM, "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", (unsigned)len);
    memcpy(buf + hl, jpgFrame, len);
    lastSeq = jpgSeq;
    xSemaphoreGive(jpgMutex);
    buf[hl + len] = '\r';
    buf[hl + len + 1] = '\n';
    lastFrameMs = lastSentMs = millis();
    ok = sendAll((const char*)buf, hl + len + 2);
    videoSendTook(millis() - lastSentMs);
    sentFrames++;
    if (millis() - fpsMs >= 1000) {
      streamFps = sentFrames * 1000.0f / (millis() - fpsMs);
      sentFrames = 0;
      fpsMs = millis();
    }
  }
  __atomic_sub_fetch(&streamClients, 1, __ATOMIC_SEQ_CST);
  if (streamClients == 0) streamFps = 0;
  free(buf);
  return ESP_FAIL;  // raw response: let the server close the connection
}

// ---- The video for the secure (https) page: a WebSocket, /vws on the HTTPS server ----
// A secure page may not read the plain http :81 stream. Each new camera frame goes to the newest
// viewer as one binary message, sent from the HTTPS server's own task (httpd_queue_work) - one at
// a time, the newest frame only, so a slow phone never builds a backlog.
volatile int vwsFd = -1;
volatile bool vwsBusy = false;
uint8_t* vwsBuf = nullptr;
volatile size_t vwsLen = 0;

esp_err_t vws_handler(httpd_req_t* req) {
  vwsFd = httpd_req_to_sockfd(req);  // the newest viewer (a new one replaces an old one)
  if (req->method == HTTP_GET) return ESP_OK;  // the handshake
  httpd_ws_frame_t f;  // the page's keep-alive ("k" every second): read it off
  memset(&f, 0, sizeof(f));
  if (httpd_ws_recv_frame(req, &f, 0) != ESP_OK) return ESP_FAIL;
  uint8_t b[32];
  if (f.len > 0 && f.len <= sizeof(b)) {
    f.payload = b;
    httpd_ws_recv_frame(req, &f, f.len);
  }
  return ESP_OK;
}

volatile float vwsSendMsLast = -1;  // the last secure frame's own send time (not its wait in the queue)
static void vwsSendWork(void*) {
  const unsigned long t0 = millis();
  const int fd = vwsFd;
  if (fd >= 0 && httpd_ws_get_fd_info(tls_httpd, fd) == HTTPD_WS_CLIENT_WEBSOCKET) {
    httpd_ws_frame_t f;
    memset(&f, 0, sizeof(f));
    f.type = HTTPD_WS_TYPE_BINARY;
    f.final = true;
    f.payload = vwsBuf;
    f.len = vwsLen;
    if (httpd_ws_send_frame_async(tls_httpd, fd, &f) != ESP_OK && vwsFd == fd) vwsFd = -1;
  } else if (vwsFd == fd) {
    vwsFd = -1;
  }
  vwsSendMsLast = millis() - t0;
  vwsBusy = false;
}

void vwsTask(void*) {
  uint32_t lastSeq = 0;
  unsigned long lastMs = 0, fpsMs = millis(), busySince = 0;
  int sent = 0;
  bool counted = false;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(4));
    const bool on = vwsFd >= 0 && tls_httpd;
    if (on != counted) {  // a viewer: the camera publishes frames while someone watches
      counted = on;
      __atomic_add_fetch(&streamClients, on ? 1 : -1, __ATOMIC_SEQ_CST);
      if (!on && streamClients == 0) streamFps = 0;
    }
    if (!on) continue;
    camViewViewerMs = millis();
    static bool measured = true;
    if (vwsBusy) {
      if (millis() - busySince > 6000) vwsBusy = false;  // a send that never came back
      continue;
    }
    if (!measured) {
      measured = true;
      if (vwsSendMsLast >= 0) videoSendTook(vwsSendMsLast);
    }
    if (jpgSeq == lastSeq || millis() - lastMs < videoGapMs(STREAM_MIN_MS)) continue;
    if (!vwsBuf) vwsBuf = (uint8_t*)heap_caps_malloc(JPG_MAX, MALLOC_CAP_SPIRAM);
    if (!vwsBuf) continue;
    xSemaphoreTake(jpgMutex, portMAX_DELAY);
    const size_t len = jpgFrameLen;
    if (len <= JPG_MAX) memcpy(vwsBuf, jpgFrame, len);
    lastSeq = jpgSeq;
    xSemaphoreGive(jpgMutex);
    if (len > JPG_MAX) continue;
    vwsLen = len;
    vwsBusy = true;
    measured = false;
    busySince = lastMs = millis();
    if (httpd_queue_work(tls_httpd, vwsSendWork, nullptr) != ESP_OK) vwsBusy = false;
    if (++sent && millis() - fpsMs >= 1000) {
      streamFps = sent * 1000.0f / (millis() - fpsMs);
      sent = 0;
      fpsMs = millis();
    }
  }
}

const char* oledKindName();  // oled_ui.h (included further down)
// The hotspot's first phone as the robot hears it: signal (dBm) and the radio mode it's on
// (b/g/n); 0 = nobody on the hotspot
static int apStaRssi(char* phy = nullptr) {
  wifi_sta_list_t l;
  if (esp_wifi_ap_get_sta_list(&l) != ESP_OK || l.num < 1) return 0;
  if (phy) strcpy(phy, l.sta[0].phy_11n ? "n" : l.sta[0].phy_11g ? "g" : l.sta[0].phy_11b ? "b" : "?");
  return l.sta[0].rssi;
}

// ---- The robot pings the hotspot's phone twice a second (while one is on it): the link's own round
// trip and lost packets, for the governor's log (a lost packet makes TCP wait ~1 s before it sends
// again: a 1 s RC freeze).
volatile uint32_t phN = 0, phLost = 0, phSum = 0, phMax = 0;
esp_ping_handle_t phPing = nullptr;
uint32_t phIp = 0;
static void phOk(esp_ping_handle_t h, void*) {
  uint32_t t = 0;
  esp_ping_get_profile(h, ESP_PING_PROF_TIMEGAP, &t, sizeof(t));
  phN++;
  phSum += t;
  if (t > phMax) phMax = t;
}
static void phTimeout(esp_ping_handle_t, void*) { phLost++; }
void phonePingUpdate() {  // loop(), every 2 s: start / stop it as a phone comes and goes
  static unsigned long ms = 0;
  if (millis() - ms < 2000) return;
  ms = millis();
  uint32_t ip = 0;
  wifi_sta_list_t wl;
  esp_netif_sta_list_t nl;
  if (esp_wifi_ap_get_sta_list(&wl) == ESP_OK && wl.num > 0 && esp_netif_get_sta_list(&wl, &nl) == ESP_OK && nl.num > 0)
    ip = nl.sta[0].ip.addr;
  if (ip == phIp) return;
  if (phPing) {
    esp_ping_stop(phPing);
    esp_ping_delete_session(phPing);
    phPing = nullptr;
  }
  phIp = ip;
  if (!ip) return;
  esp_ping_config_t c = ESP_PING_DEFAULT_CONFIG();
  c.target_addr.type = IPADDR_TYPE_V4;
  c.target_addr.u_addr.ip4.addr = ip;
  c.count = ESP_PING_COUNT_INFINITE;
  c.interval_ms = 500;
  c.timeout_ms = 1000;
  c.data_size = 32;
  c.task_stack_size = 3072;
  esp_ping_callbacks_t cb = {};
  cb.on_ping_success = phOk;
  cb.on_ping_timeout = phTimeout;
  if (esp_ping_new_session(&c, &cb, &phPing) == ESP_OK) esp_ping_start(phPing);
}

// open connections of a server (-1: not running)
static int clientCount(httpd_handle_t h) {
  if (!h) return -1;
  size_t n = 16;
  int fds[16];
  return httpd_get_client_list(h, &n, fds) == ESP_OK ? (int)n : -1;
}

// GET /gov?stacks=1: each task's stack never used so far (bytes) - to size them (internal RAM)
static int govStacks(char* o, size_t len) {
  static const char* const names[] = {"loopTask", "Vision", "face", "xlog", "jpgdec", "photo", "serialCmd", "webdog",
                                      "vws", "httpd", "talk", "sfx", "player", "IDLE", "tiT", "wifi", "esp_timer", "cam_task"};
  int n = snprintf(o, len, "{");
  for (const char* nm : names) {
    TaskHandle_t h = xTaskGetHandle(nm);
    if (!h) continue;
    n += snprintf(o + n, len - n, "%s\"%s\":%u", n > 1 ? "," : "", nm, (unsigned)uxTaskGetStackHighWaterMark(h));
    if (n >= (int)len - 40) break;
  }
  n += snprintf(o + n, len - n, "}");
  return n;
}

// GET /tasks: every task's share of a core over 2 s (FreeRTOS run-time stats), busiest first - to see what
// starves the network. 100 = one core all the time.
esp_err_t tasks_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  const UBaseType_t cap = uxTaskGetNumberOfTasks() + 8;
  TaskStatus_t* a = (TaskStatus_t*)heap_caps_malloc(cap * sizeof(TaskStatus_t), MALLOC_CAP_SPIRAM);
  TaskStatus_t* b = (TaskStatus_t*)heap_caps_malloc(cap * sizeof(TaskStatus_t), MALLOC_CAP_SPIRAM);
  char* out = (char*)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
  if (!a || !b || !out) { free(a); free(b); free(out); return httpd_resp_send_500(req); }
  uint32_t ta = 0, tb = 0;
  const UBaseType_t na = uxTaskGetSystemState(a, cap, &ta);
  vTaskDelay(pdMS_TO_TICKS(2000));
  const UBaseType_t nb = uxTaskGetSystemState(b, cap, &tb);
  const float total = (float)(tb - ta);  // (time passed, in the run-time counter's units: one core's 100 %)
  float pct[64];
  int idx[64], n = 0;
  for (UBaseType_t i = 0; i < nb && n < 64; i++) {
    uint32_t prev = b[i].ulRunTimeCounter;  // (a task that started in between: counted from 0)
    for (UBaseType_t j = 0; j < na; j++) if (a[j].xHandle == b[i].xHandle) { prev = a[j].ulRunTimeCounter; break; }
    pct[n] = total > 0 ? 100.0f * (b[i].ulRunTimeCounter - prev) / total : 0;
    idx[n++] = i;
  }
  for (int i = 1; i < n; i++)  // busiest first
    for (int j = i; j > 0 && pct[j] > pct[j - 1]; j--) { std::swap(pct[j], pct[j - 1]); std::swap(idx[j], idx[j - 1]); }
  int o = snprintf(out, 4096, "%-16s core prio  cpu%%  stackLeft\n", "task");
  for (int k = 0; k < n && o < 3900; k++) {
    const TaskStatus_t& t = b[idx[k]];
    const int core = t.xCoreID > 1 ? -1 : (int)t.xCoreID;  // (-1: either core)
    o += snprintf(out + o, 4096 - o, "%-16s %4d %4u %5.1f %6u\n", t.pcTaskName, core, (unsigned)t.uxCurrentPriority, pct[k],
                  (unsigned)t.usStackHighWaterMark);
  }
  const esp_err_t r = httpd_resp_send(req, out, o);
  free(a);
  free(b);
  free(out);
  return r;
}

// GET /gov: the governor right now (what it's giving priority to, how the robot is coping)
esp_err_t gov_handler(httpd_req_t* req) {
  {
    char q[16] = "";
    httpd_req_get_url_query_str(req, q, sizeof(q));
    if (strstr(q, "stacks")) {
      char o[700];
      const int n = govStacks(o, sizeof(o));
      httpd_resp_set_type(req, "application/json");
      return httpd_resp_send(req, o, n);
    }
  }
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  // the radio: power save (0 none, 1 min modem, 2 max), TX power (0.25 dBm), 802.11 b/g/n bits,
  // bandwidth (1 HT20, 2 HT40), channel, mode (1 STA, 2 AP, 3 both)
  wifi_ps_type_t wps = WIFI_PS_NONE;
  esp_wifi_get_ps(&wps);
  int8_t txp = 0;
  esp_wifi_get_max_tx_power(&txp);
  uint8_t proto = 0, chan = 0;
  esp_wifi_get_protocol(WIFI_IF_STA, &proto);
  wifi_bandwidth_t bw = WIFI_BW_HT20, apBw = WIFI_BW_HT20;
  esp_wifi_get_bandwidth(WIFI_IF_STA, &bw);
  esp_wifi_get_bandwidth(WIFI_IF_AP, &apBw);
  wifi_second_chan_t ch2;
  esp_wifi_get_channel(&chan, &ch2);
  wifi_mode_t wm = WIFI_MODE_NULL;
  esp_wifi_get_mode(&wm);
  char apPhy[4] = "-";
  const int apRssi = apStaRssi(apPhy);
  char o[1000];
  const int n = snprintf(o, sizeof(o),
      "{\"perf\":\"%s\",\"loopMaxMs\":%.1f,\"loopAvgMs\":%.1f,\"janks\":%d,\"faceFps\":%.1f,\"drawMs\":%.1f,\"pushMs\":%.1f,"
      "\"faceFpsCap\":%d,\"streamFps\":%.1f,\"heap\":%u,\"minHeap\":%u,\"psram\":%u,\"rssi\":%d,\"apUsers\":%d,"
      "\"scene\":%d,\"overlay\":%d,\"talk\":%d,\"rc\":%d,\"mode\":\"%s\",\"cmd\":\"%s\",\"fallen\":%d,\"charging\":%d,"
      "\"oled\":\"%s\",\"uptime\":%lu,\"worst\":\"%s\",\"intFree\":%u,\"dmaBlk\":%u,\"camInitErr\":\"%s\","
      "\"sdQueueDrop\":%u,\"sdFails\":%u,\"sdWriteMaxMs\":%.0f,\"batV\":%.2f,\"batPct\":%d,\"batShown\":%d,\"batPinMv\":%.0f,"
      "\"camFps\":%.1f,\"camOn\":%d,\"camView\":%d,\"tls\":%d,\"wifiPs\":%d,\"txPow\":%d,\"proto\":%d,\"bw\":%d,"
      "\"chan\":%d,\"wifiMode\":%d,\"oledFails\":%d,\"oledRecoveries\":%d,\"pcaResets\":%d,\"cpuMhz\":%u,\"headPan\":%d,\"headTilt\":%d,\"videoSendMs\":%.0f,\"httpConns\":%d,\"tlsConns\":%d,\"streamConns\":%d,\"apRssi\":%d,\"apPhy\":\"%s\",\"apBw\":%d}",
      perfName, (float)govLoopMaxMs, (float)govLoopAvgMs, (int)govJanks, (float)faceFpsNow, (float)faceDrawMs, (float)facePushMs,
      (int)faceFpsWanted, (float)streamFps, ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getFreePsram(),
      WiFi.status() == WL_CONNECTED ? (int)WiFi.RSSI() : 0, WiFi.softAPgetStationNum(), faceScenePlaying ? 1 : 0,
      faceOverlayOn ? 1 : 0, aud::talkOn ? 1 : 0, rcActive() ? 1 : 0, MODE_NAMES[activeMode], currentCommand.c_str(),
      robotFallen ? 1 : 0, batCharging ? 1 : 0, oledKindName(), millis() / 1000, govWorst,
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
      camInitErr, (unsigned)xl::sideDropped, (unsigned)xl::sideFails, (float)xl::sideMaxMs, batSlowV, (int)batPctSlow, (int)batPctShown,
      (float)batPinMv, (float)camFps, camReady ? 1 : 0, camViewWanted ? 1 : 0, tls_httpd ? 1 : 0, (int)wps, (int)txp,
      (int)proto, (int)bw, (int)chan, (int)wm, (int)oledFails, (int)oledRecoveries, (int)pcaResets, (unsigned)getCpuFrequencyMhz(), emoPanNow, emoTiltNow, (float)videoSendMs, clientCount(cmd_httpd),
      clientCount(tls_httpd), clientCount(stream_httpd), apRssi, apPhy, (int)apBw);
  return httpd_resp_send(req, o, min(n, (int)sizeof(o) - 1));
}

// GET /oled?l=&t=&r=&b= (px kept clear at each edge, saved) &test=1 (show the test frame 30 s)
esp_err_t oled_handler(httpd_req_t* req) {
  char q[80] = "", v[8];
  httpd_req_get_url_query_str(req, q, sizeof(q));
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  struct { const char* k; int* var; const char* key; int mx; } F[4] = {
    {"l", &OLED_ML, "oledL", 40}, {"t", &OLED_MT, "oledT", 30}, {"r", &OLED_MR, "oledR", 40}, {"b", &OLED_MB, "oledB", 30}};
  for (auto& f : F)
    if (httpd_query_key_value(q, f.k, v, sizeof(v)) == ESP_OK) {
      *f.var = constrain(atoi(v), 0, f.mx);
      prefPut(f.key, *f.var, false);
    }
  if (httpd_query_key_value(q, "test", v, sizeof(v)) == ESP_OK) oledTestUntil = v[0] == '1' ? millis() + 30000 : 0;
  if (httpd_query_key_value(q, "shot", v, sizeof(v)) == ESP_OK) {  // the screen as a 1-bit PBM picture
    static uint8_t (&pbm)[16 + 128 * 64 / 8] = *(uint8_t(*)[16 + 128 * 64 / 8])heap_caps_calloc(1, sizeof(uint8_t) * (16 + 128 * 64 / 8), MALLOC_CAP_SPIRAM);
    const int hl = snprintf((char*)pbm, 16, "P4\n128 64\n");
    const uint8_t* fb = oled.getBuffer();
    for (int y = 0; y < 64; y++)
      for (int xb = 0; xb < 16; xb++) {
        uint8_t v8 = 0;
        for (int k = 0; k < 8; k++) if (fb[(xb * 8 + k) + (y / 8) * 128] >> (y & 7) & 1) v8 |= 0x80 >> k;
        pbm[hl + y * 16 + xb] = v8;
      }
    httpd_resp_set_type(req, "image/x-portable-bitmap");
    return httpd_resp_send(req, (const char*)pbm, hl + 128 * 64 / 8);
  }
  char o[120];
  const int n = snprintf(o, sizeof(o), "{\"l\":%d,\"t\":%d,\"r\":%d,\"b\":%d,\"w\":128,\"h\":64,\"test\":%d}",
                         OLED_ML, OLED_MT, OLED_MR, OLED_MB, millis() < oledTestUntil ? 1 : 0);
  return httpd_resp_send(req, o, n);
}

// GET /camView?on=1&size=qvga|hvga|vga
esp_err_t cam_view_handler(httpd_req_t* req) {
  char q[64] = "", val[16];
  const size_t qlen = httpd_req_get_url_query_len(req);
  if (qlen > 0 && qlen < sizeof(q)) httpd_req_get_url_query_str(req, q, sizeof(q));
  if (httpd_query_key_value(q, "size", val, sizeof(val)) == ESP_OK) {
    int fs = FRAMESIZE_VGA;
    for (int i = 0; i < CAM_SIZE_N; i++) if (!strcmp(val, CAM_SIZES[i].name)) fs = CAM_SIZES[i].fs;
    camAskedSize = fs;  // (the auto size starts from the page's choice again)
    if (fs != camViewSize) {
      camViewSize = fs;
      camViewSizeChanged = true;
    }
  }
  if (httpd_query_key_value(q, "rc", val, sizeof(val)) == ESP_OK && val[0] == '1') rcSeenMs = millis();
  if (httpd_query_key_value(q, "pace", val, sizeof(val)) == ESP_OK) videoPace = val[0] == '1';
  if (httpd_query_key_value(q, "new", val, sizeof(val)) == ESP_OK && val[0] == '1') streamGen++;
  if (httpd_query_key_value(q, "photo", val, sizeof(val)) == ESP_OK && val[0] == '1') requestPhoto("snap");  // saved to the SD card
  if (httpd_query_key_value(q, "sharp", val, sizeof(val)) == ESP_OK) {
    const int v = constrain(atoi(val), -1, 6);
    if (v != camSharp) {
      camSharp = v;
      camSharpChanged = true;
      prefPut("camSharp", v, false);
    }
  }
  if (httpd_query_key_value(q, "on", val, sizeof(val)) == ESP_OK) {
    const bool on = val[0] == '1' && !chatCamBlock;  // (no video while chatting)
    if (on != camViewWanted) xlog("WEB camera view %s", on ? "on" : "off");
    if (on) camViewViewerMs = millis();
    camViewWanted = on;
  }
  char json[160];
  snprintf(json, sizeof(json), "{\"on\":%d,\"size\":\"%s\",\"port\":81,\"sharp\":%d,\"sharpRes\":%d}", camViewWanted ? 1 : 0,
           camSizeName(camViewSize), (int)camSharp, (int)camSharpRes);
  return sendJson(req, json);
}

// GET /head?x=-1..1&y=-1..1 (joystick: x right, y up; 0,0 = the calibrated middle).
// Walk mode only - face tracking and Explore steer the head themselves.
volatile float rcPanTarget = PAN_MID, rcTiltTarget = TILT_MID;
volatile bool rcHeadActive = false;
volatile unsigned long headManualMs = 0;  // the page's head pad (m=1): idle moves keep off the head 20 s
esp_err_t head_handler(httpd_req_t* req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  if (requestedMode != MODE_WALK || activeMode != MODE_WALK) return httpd_resp_sendstr(req, "MODE_LOCKED");
  char q[64] = "", val[16];
  const size_t qlen = httpd_req_get_url_query_len(req);
  if (qlen > 0 && qlen < sizeof(q)) httpd_req_get_url_query_str(req, q, sizeof(q));
  float x = 0, y = 0;
  if (httpd_query_key_value(q, "x", val, sizeof(val)) == ESP_OK) x = constrain(atof(val), -1.0f, 1.0f);
  if (httpd_query_key_value(q, "y", val, sizeof(val)) == ESP_OK) y = constrain(atof(val), -1.0f, 1.0f);
  const float leftPan = panPlusIsLeft ? PAN_MAX : PAN_MIN, rightPan = panPlusIsLeft ? PAN_MIN : PAN_MAX;
  rcPanTarget = x < 0 ? PAN_MID + (-x) * (leftPan - PAN_MID) : PAN_MID + x * (rightPan - PAN_MID);
  rcTiltTarget = y > 0 ? TILT_MID - y * (TILT_MID - TILT_MIN - 2) : TILT_MID + (-y) * (TILT_MAX - 5 - TILT_MID);  // tilt+ = down; off the stops
  rcHeadActive = true;
  if (httpd_query_key_value(q, "m", val, sizeof(val)) == ESP_OK && val[0] == '1') headManualMs = millis();  // (the head pad: not RC)
  else rcSeenMs = millis();  // only RC mode moves the head from the page
  return httpd_resp_sendstr(req, "OK");
}

// ---- All ten servos' calibration (the calibration page, /calibrate): min / mid / max ----
// The legs (r1 r2 l1 l2 r4 r3 l3 l4 = pins 14 4 12 13 8 9 11 10): their limits (min / max) and their middle
// (= the stand pose); the head (pan = pin 7, tilt = pin 5): the head calibration. Saved on the robot.
// GET /servoCal                       all of them (JSON)
// GET /servoCal?p14=min,mid,max&...   new values for those pins (applied and saved)
// GET /servoCal?reset=1               the built-in values
const char* const LEG_NAMES[8] = {"R1", "R2", "L1", "L2", "R4", "R3", "L3", "L4"};
void legCalApply() {
  // (POSE_STAND stays the built-in middles: setServoAngle maps them onto the saved ones)
  for (int i = 0; i < 8; i++) POSE_STAND[i] = (int)lroundf(LEG_MID0[i]);
}
void legCalLoad() {
  char buf[200] = "";
  prefs.getString("legCal", buf, sizeof(buf));
  float v[24];
  int n = 0;
  for (char* t = strtok(buf, ","); t && n < 24; t = strtok(nullptr, ",")) v[n++] = atof(t);
  if (n != 24) return;  // none saved: the built-in values
  for (int i = 0; i < 8; i++) {
    ourMin[i] = v[3 * i];
    ourMid[i] = v[3 * i + 1];
    ourMax[i] = v[3 * i + 2];
  }
  legCalApply();
}
bool legCalSave() {
  char buf[200];
  int k = 0;
  for (int i = 0; i < 8; i++) k += snprintf(buf + k, sizeof(buf) - k, "%s%.0f,%.0f,%.0f", i ? "," : "", ourMin[i], ourMid[i], ourMax[i]);
  if (!xl::lockFlash(2000)) return false;  // (the camera paused for the flash write)
  prefs.putString("legCal", buf);
  xl::unlockFlash();
  return true;
}
esp_err_t servo_cal_handler(httpd_req_t* req) {
  char q[400] = "", val[40];
  httpd_req_get_url_query_str(req, q, sizeof(q));
  bool legChange = false, headChange = false, saved = true;
  float h[6] = {PAN_MIN, PAN_MID, PAN_MAX, TILT_MIN, TILT_MID, TILT_MAX};
  if (httpd_query_key_value(q, "reset", val, sizeof(val)) == ESP_OK && val[0] == '1') {
    for (int i = 0; i < 8; i++) { ourMin[i] = LEG_MIN0[i]; ourMid[i] = LEG_MID0[i]; ourMax[i] = LEG_MAX0[i]; }
    h[0] = PAN_MIN0; h[1] = PAN_MID0; h[2] = PAN_MAX0; h[3] = TILT_MIN0; h[4] = TILT_MID0; h[5] = TILT_MAX0;
    legChange = headChange = true;
  }
  auto three = [&](const char* key, float& a, float& b, float& c) {
    if (httpd_query_key_value(q, key, val, sizeof(val)) != ESP_OK) return false;
    float x, y, z;
    if (sscanf(val, "%f,%f,%f", &x, &y, &z) != 3) return false;
    a = constrain(x, -90.0f, 200.0f); b = constrain(y, -90.0f, 200.0f); c = constrain(z, -90.0f, 200.0f);
    return true;
  };
  for (int i = 0; i < 8; i++) {
    char key[8];
    snprintf(key, sizeof(key), "p%d", mappedPins[i]);
    if (three(key, ourMin[i], ourMid[i], ourMax[i])) legChange = true;
  }
  char hk[8];
  snprintf(hk, sizeof(hk), "p%d", headPanCh);  // (the head's channels: servo_map.h)
  if (three(hk, h[0], h[1], h[2])) headChange = true;
  snprintf(hk, sizeof(hk), "p%d", headTiltCh);
  if (three(hk, h[3], h[4], h[5])) headChange = true;
  if (legChange) {
    legCalApply();
    saved = legCalSave();
  }
  if (headChange) {
    setHeadCal(h[0], h[1], h[2], h[3], h[4], h[5]);
    prefPut("panMin", (int32_t)lroundf(PAN_MIN), false);
    prefPut("panMid", (int32_t)lroundf(PAN_MID), false);
    prefPut("panMax", (int32_t)lroundf(PAN_MAX), false);
    prefPut("tiltMin", (int32_t)lroundf(TILT_MIN), false);
    prefPut("tiltMid", (int32_t)lroundf(TILT_MID), false);
    prefPut("tiltMax", (int32_t)lroundf(TILT_MAX), false);
  }
  if (legChange || headChange) xlogSys("SERVO calibration saved from the page (%s%s)", legChange ? "legs" : "", headChange ? (legChange ? " + head" : "head") : "");
  char out[1400];
  int k = snprintf(out, sizeof(out), "{\"saved\":%d,\"servos\":[", saved ? 1 : 0);
  for (int i = 0; i < 8; i++)
    k += snprintf(out + k, sizeof(out) - k, "{\"pin\":%d,\"name\":\"%s\",\"min\":%.0f,\"mid\":%.0f,\"max\":%.0f,\"now\":%.0f},",
                  mappedPins[i], LEG_NAMES[i], ourMin[i], ourMid[i], ourMax[i],
                  servoRawKnown[mappedPins[i]] ? servoRawNow[mappedPins[i]] : -999.0f);  // (now: its real angle)
  k += snprintf(out + k, sizeof(out) - k, "{\"pin\":%d,\"name\":\"Pan\",\"min\":%.0f,\"mid\":%.0f,\"max\":%.0f},"
                "{\"pin\":%d,\"name\":\"Tilt\",\"min\":%.0f,\"mid\":%.0f,\"max\":%.0f}]}",
                headPanCh, PAN_MIN, PAN_MID, PAN_MAX, headTiltCh, TILT_MIN, TILT_MID, TILT_MAX);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, out, k);
}
// GET /servomap: the servo mapping page (servomap.html). API: ?json=1  ?set=<role 0-9>,<channel 0-15>  ?test=<channel>
// (wiggles that channel: find which servo is which)  ?reset=1 (the reference wiring). Saved in flash.
#include "servomap_html_gz.h"
esp_err_t servomap_handler(httpd_req_t* req) {
  char q[64] = "", v[16];
  bool api = false;
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
    if (httpd_query_key_value(q, "json", v, sizeof(v)) == ESP_OK) api = true;
    if (httpd_query_key_value(q, "set", v, sizeof(v)) == ESP_OK) {
      int role = -1, ch = -1;
      if (sscanf(v, "%d,%d", &role, &ch) == 2) { servoMapSet(role, ch); servoMapSave(); xlog("SERVOMAP %s -> channel %d", role >= 0 && role < 10 ? SERVO_ROLE_ID[role] : "?", ch); }
      api = true;
    }
    if (httpd_query_key_value(q, "reset", v, sizeof(v)) == ESP_OK) { servoMapReset(); api = true; }
    if (httpd_query_key_value(q, "test", v, sizeof(v)) == ESP_OK) {  // a small wiggle around the middle
      const int ch = atoi(v);
      if (ch >= 0 && ch < 16) {
        calPinMs[ch] = millis();  // (every other mover keeps off this pin for 5 s)
        auto us = [](float a) { return (uint16_t)lroundf(732 + a * (2929 - 732) / 180.0f); };
        const float mid = servoRawKnown[ch] ? servoRawNow[ch] : 90.0f;
        pwm.writeMicroseconds(ch, us(constrain(mid - 15, 0.0f, 180.0f))); delay(300);
        pwm.writeMicroseconds(ch, us(constrain(mid + 15, 0.0f, 180.0f))); delay(300);
        pwm.writeMicroseconds(ch, us(mid));
      }
      api = true;
    }
  }
  if (!api) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char*)servomap_html_gz, servomap_html_gz_len);
  }
  char o[700];
  int n = snprintf(o, sizeof(o), "{\"roles\":[");
  for (int i = 0; i < 10; i++)
    n += snprintf(o + n, sizeof(o) - n, "%s{\"id\":\"%s\",\"name\":\"%s\",\"ch\":%d,\"def\":%d}", i ? "," : "", SERVO_ROLE_ID[i],
                  SERVO_ROLE_NAME[i], servoChOf(i), SERVO_DEFAULT_CH[i]);
  snprintf(o + n, sizeof(o) - n, "]}");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_sendstr(req, o);
}

// GET /calibrate: the calibration page (calibrate.html, packed into calibrate_html_gz.h)
#include "calibrate_html_gz.h"
esp_err_t calibrate_page_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  return httpd_resp_send(req, (const char*)calibrate_html_gz, calibrate_html_gz_len);
}
// GET /sprites.webp: the page's face style / play scene thumbnails (cached a week by the phone)
#include "sprites_webp.h"
esp_err_t sprites_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "image/webp");
  httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=604800");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, (const char*)sprites_webp, sprites_webp_len);
}

// GET /headCal                     the calibration (JSON)
// GET /headCal?pan=P&tilt=T         move the head there (walk mode) to find a position
// GET /headCal?save=mid|pmid|tmid|pmin|pmax|tmin|tmax   where the head is now becomes that (saved)
// GET /headCal?set=1&pmin=&pmid=&pmax=&tmin=&tmid=&tmax=   values directly (any of them)
// GET /headCal?reset=1              back to the defaults (-18 / 60 / 150, 59 / 99 / 150)
volatile float calPan = -999, calTilt = -999;
volatile bool calDirty = false;
esp_err_t head_cal_handler(httpd_req_t* req) {
  tofNeedMs = millis();  // the panel shows the floor distances: the ToF runs a minute
  char q[192] = "", val[16];
  const size_t qlen = httpd_req_get_url_query_len(req);
  if (qlen > 0 && qlen < sizeof(q)) httpd_req_get_url_query_str(req, q, sizeof(q));
  const bool walk = requestedMode == MODE_WALK && activeMode == MODE_WALK;
  float p = calPan > -900 ? (float)calPan : PAN_MID, t = calTilt > -900 ? (float)calTilt : TILT_MID;
  bool move = false, change = false;
  if (httpd_query_key_value(q, "pan", val, sizeof(val)) == ESP_OK) { p = constrain((float)atof(val), -30.0f, 180.0f); move = true; }
  if (httpd_query_key_value(q, "tilt", val, sizeof(val)) == ESP_OK) { t = constrain((float)atof(val), 20.0f, 150.0f); move = true; }  // (150: the head touches the body - a stalled servo)
  float c[6] = {PAN_MIN, PAN_MID, PAN_MAX, TILT_MIN, TILT_MID, TILT_MAX};
  if (httpd_query_key_value(q, "reset", val, sizeof(val)) == ESP_OK) {
    const float d[6] = {PAN_MIN0, PAN_MID0, PAN_MAX0, TILT_MIN0, TILT_MID0, TILT_MAX0};
    for (int i = 0; i < 6; i++) c[i] = d[i];
    p = PAN_MID0;
    t = TILT_MID0;
    move = change = true;
  }
  if (httpd_query_key_value(q, "save", val, sizeof(val)) == ESP_OK) {
    if (!strcmp(val, "pmin")) c[0] = p;
    else if (!strcmp(val, "pmax")) c[2] = p;
    else if (!strcmp(val, "tmin")) c[3] = t;
    else if (!strcmp(val, "tmax")) c[5] = t;
    else if (!strcmp(val, "pmid")) c[1] = p;   // pan middle only
    else if (!strcmp(val, "tmid")) c[4] = t;   // tilt middle only
    else { c[1] = p; c[4] = t; }  // "mid" (or 1): straight ahead, both
    change = true;
  }
  if (httpd_query_key_value(q, "set", val, sizeof(val)) == ESP_OK) {
    const char* keys[6] = {"pmin", "pmid", "pmax", "tmin", "tmid", "tmax"};
    for (int i = 0; i < 6; i++)
      if (httpd_query_key_value(q, keys[i], val, sizeof(val)) == ESP_OK) { c[i] = atof(val); change = true; }
  }
  if (move && walk) {
    calPan = p;
    calTilt = t;
    calDirty = true;
  }
  if (change) {
    const float oldMid = TILT_MID, oldRoom = TILT_MAX - TILT_MID;
    setHeadCal(c[0], c[1], c[2], c[3], c[4], c[5]);
    // Floor distances were learned at the old look-down angles: back to the geometry's
    if (fabsf(TILT_MID - oldMid) >= 1 || fabsf((TILT_MAX - TILT_MID) - oldRoom) >= 1) exFloorReset();
    prefPut("panMin", (int32_t)lroundf(PAN_MIN), false);
    prefPut("panMid", (int32_t)lroundf(PAN_MID), false);
    prefPut("panMax", (int32_t)lroundf(PAN_MAX), false);
    prefPut("tiltMin", (int32_t)lroundf(TILT_MIN), false);
    prefPut("tiltMid", (int32_t)lroundf(TILT_MID), false);
    prefPut("tiltMax", (int32_t)lroundf(TILT_MAX), false);
    xlogSys("HEAD calibration: pan %.0f / %.0f / %.0f, tilt %.0f / %.0f / %.0f (min / straight / max); floor glances %d / %d / %d deg",
            PAN_MIN, PAN_MID, PAN_MAX, TILT_MIN, TILT_MID, TILT_MAX, EX_FLOOR_ANGLES[0], EX_FLOOR_ANGLES[1], EX_FLOOR_ANGLES[2]);
  }
  char json[320];
  snprintf(json, sizeof(json),
           "{\"pan\":%.0f,\"tilt\":%.0f,\"panMin\":%.0f,\"panMax\":%.0f,\"tiltMin\":%.0f,\"tiltMax\":%.0f,"
           "\"panDef\":%.0f,\"tiltDef\":%.0f,\"atPan\":%.0f,\"atTilt\":%.0f,\"walk\":%d,\"floorAng\":[%d,%d,%d]}",
           PAN_MID, TILT_MID, PAN_MIN, PAN_MAX, TILT_MIN, TILT_MAX, PAN_MID0, TILT_MID0, p, t, walk ? 1 : 0,
           EX_FLOOR_ANGLES[0], EX_FLOOR_ANGLES[1], EX_FLOOR_ANGLES[2]);
  return sendJson(req, json);
}

esp_err_t my_404_handler(httpd_req_t *req, httpd_err_code_t err) {
  // API paths get a quick 404: a redirect to the access-point address would leave a page
  // opened over the home WiFi waiting for a connection that can't happen
  if (strncmp(req->uri, "/api/", 5) == 0) {
    httpd_resp_set_status(req, "404 Not Found");
    return sendJson(req, "{\"error\":\"Not found\"}");
  }
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
  return httpd_resp_send(req, NULL, 0);
}

httpd_handle_t stream_httpd = NULL;

// ---- Web server self-check ----
// Every 15 s a tiny request to its own web server over the internal loopback. Two misses in a
// row (server stuck, or its connections used up by a phone that went away) -> both web
// servers are restarted, which drops every connection; if even that hangs, the robot restarts.
// ---- Wireless firmware update: POST /ota with the firmware .bin as the body ----
// It's written to the program slot that isn't running, checked, and the robot restarts into
// it. Flash writes stop the CPU cache, so the camera is switched off for the whole update
// (the same lock the log uses). Optional ?md5=<hex> is checked too.
esp_err_t ota_handler(httpd_req_t* req) {
  char q[80] = {0}, md5[40] = {0};
  httpd_req_get_url_query_str(req, q, sizeof(q));
  httpd_query_key_value(q, "md5", md5, sizeof(md5));
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  const size_t total = req->content_len;
  if (total < 100000) return httpd_resp_sendstr(req, "OTA: send the firmware .bin as the request body\n");
  if (!xl::lockFlash(8000)) return httpd_resp_sendstr(req, "OTA: busy (camera wouldn't stop) - try again\n");
  xl::httpBusy = true;
  otaActive = true;
  requestedMode = MODE_WALK;
  currentCommand = "";  // stop what it's doing - no pose: the legs may be limp on a table, don't move them
  xlogSys("OTA update starting: %u bytes", (unsigned)total);
  bool ok = Update.begin(total, U_FLASH);
  if (ok && md5[0]) ok = Update.setMD5(md5);
  const size_t B = 4096;
  uint8_t* buf = (uint8_t*)heap_caps_malloc(B, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  size_t left = total;
  int stalls = 0;
  while (ok && buf && left > 0) {
    const int r = httpd_req_recv(req, (char*)buf, min(left, B));
    if (r == HTTPD_SOCK_ERR_TIMEOUT && ++stalls < 4) continue;
    stalls = 0;
    if (r <= 0 || Update.write(buf, r) != (size_t)r) { ok = false; break; }
    left -= r;
    otaPct = (int)((uint64_t)(total - left) * 100 / total);
  }
  free(buf);
  ok = ok && left == 0 && Update.end(true);
  char msg[120];
  if (ok) snprintf(msg, sizeof(msg), "OTA OK: %u bytes written - restarting into the new firmware\n", (unsigned)total);
  else {
    snprintf(msg, sizeof(msg), "OTA FAILED: %s (%u of %u bytes) - still running the old firmware\n", Update.errorString(),
             (unsigned)(total - left), (unsigned)total);
    Update.abort();
  }
  xlogSys("%s", msg);
  xl::unlockFlash();
  xl::httpBusy = false;
  otaActive = false;
  otaPct = -1;
  httpd_resp_sendstr(req, msg);
  if (ok) {
    delay(800);  // the reply and the log line get out first
    xl::shutdownFlush();  // the SD card closed cleanly first: a restart mid-write can break governor.csv
    ESP.restart();
  }
  return ESP_OK;
}

// GET /face                       style, expressions, speed
// GET /face?style=cat             switch the face style (saved)
// GET /face?set=happy_max         show an expression now (a fresh take each time)
// GET /face?mood=0.6,0.4          a procedural mood (valence, arousal)
// GET /face?list=1                all expression names
esp_err_t face_handler(httpd_req_t* req) {
  char q[96] = {0}, v[48];
  httpd_req_get_url_query_str(req, q, sizeof(q));
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  if (httpd_query_key_value(q, "scol", v, sizeof(v)) == ESP_OK && strlen(v) == 6) {  // the saver's eye colour RRGGBB, saved
    saverColor = (uint32_t)strtoul(v, nullptr, 16) & 0xFFFFFF;
    prefPut("saverCol", (int32_t)saverColor, false);
  }
  if (httpd_query_key_value(q, "p3d", v, sizeof(v)) == ESP_OK) {  // 3D props in the play scenes, saved
    props3d = v[0] == '1';
    prefPut("props3d", props3d ? 1 : 0, true);
  }
  if (httpd_query_key_value(q, "bright", v, sizeof(v)) == ESP_OK) {  // 10..100 %
    const int b = constrain(atoi(v), 10, 100);
    faceBright = b / 100.0f;
    prefPut("faceBright", b, false);
  }
  if (httpd_query_key_value(q, "style", v, sizeof(v)) == ESP_OK) {
    const int st = faceStyleFromName(v);
    if (st >= 0) {
      faceStyleReq = st;
      prefPut("faceStyle", st, false);  // saved in the next camera-safe moment
      xlogSys("FACE style %s", fe::STYLE_NAMES[st]);
    }
  }
  if (httpd_query_key_value(q, "set", v, sizeof(v)) == ESP_OK) setFaceNamed(v, true);
  if (httpd_query_key_value(q, "play", v, sizeof(v)) == ESP_OK) {  // a play scene now (name, random or stop)
    if (activeMode == MODE_WALK && strcmp(currentFaceName, "idle")) setFaceNamed("idle", false);
    faceScene(v);
  }
  if (httpd_query_key_value(q, "playevery", v, sizeof(v)) == ESP_OK) {
    PLAY_EVERY_S = constrain(atoi(v), 0, 3600);
    prefPut("playEvery", PLAY_EVERY_S, false);
  }
  if (httpd_query_key_value(q, "playauto", v, sizeof(v)) == ESP_OK) {
    playAuto = v[0] == '1';
    prefPut("playAuto", playAuto ? 1 : 0, true);
  }
  if (httpd_query_key_value(q, "playsound", v, sizeof(v)) == ESP_OK) {
    playSound = v[0] == '1';
    prefPut("playSound", playSound ? 1 : 0, true);
  }
  if (httpd_query_key_value(q, "mood", v, sizeof(v)) == ESP_OK) {
    char* e;
    const float va = strtof(v, &e);
    const float ar = *e == ',' ? strtof(e + 1, nullptr) : 0;
    char name[40];
    snprintf(name, sizeof(name), "mood:%.2f,%.2f", va, ar);
    setFaceNamed(name, true);
  }
  if (httpd_query_key_value(q, "list", v, sizeof(v)) == ESP_OK) {
    String o = "{\"names\":[";
    const int n = fe::libCount();
    for (int i = 0; i < n; i++) {
      const fe::LibEntry& e = fe::libEntries()[i];
      bool dup = false;  // a card pack can redefine a built-in one: list it once
      for (int j = i + 1; j < n && !dup; j++) dup = fe::libEntries()[j].nlen == e.nlen && !strncasecmp(fe::libEntries()[j].name, e.name, e.nlen);
      if (dup) continue;
      if (o.length() > 10) o += ",";
      o += "\"";
      o.concat(e.name, e.nlen);
      o += "\"";
    }
    o += "]}";
    return httpd_resp_send(req, o.c_str(), o.length());
  }
  const int st = faceStyleReq >= 0 ? faceStyleReq : faceEng.style;
  char scol[8];
  snprintf(scol, sizeof(scol), "%06lx", (unsigned long)saverColor);
  String o = "{\"bright\":" + String((int)lroundf(faceBright * 100)) + ",\"p3d\":" + String(props3d ? 1 : 0) + ",\"scol\":\"" + scol + "\",\"style\":\"";
  o += fe::STYLE_NAMES[st];
  o += "\",\"styles\":[";
  for (int i = 0; i < fe::ST_NUM; i++) { if (i) o += ","; o += "\""; o += fe::STYLE_NAMES[i]; o += "\""; }
  o += "],\"face\":\"" + String(currentFaceName) + "\",\"count\":" + String(fe::libCount()) + ",\"fromCard\":" + String(faceSdEntries);
  o += ",\"prof\":\"";
  const char* pk[6] = {"clear", "rrect", "circle", "ellipse", "tri", "line"};
  for (int i = 0; i < 6; i++) { o += pk[i]; o += " " + String((float)faceProf[i], 1) + "ms/" + String((int)faceProfN[i]) + " "; }
  o += "\"";
  char scn[16];
  portENTER_CRITICAL(&faceMux);
  strlcpy(scn, faceSceneName, sizeof(scn));
  portEXIT_CRITICAL(&faceMux);
  o += ",\"scene\":\"";
  o += faceScenePlaying ? scn : "";
  o += "\",\"scenes\":[";
  for (int i = 1; i < fe::Face::SC_O_FIRST; i++) { if (i > 1) o += ","; o += "\""; o += fe::Face::sceneNameAt(i); o += "\""; }
  o += "],\"playEvery\":" + String(PLAY_EVERY_S) + ",\"playSound\":" + String(playSound ? 1 : 0) + ",\"playAuto\":" + String(playAuto ? 1 : 0);
  o += ",\"perf\":\"";
  o += perfName;
  o += "\"";
  o += ",\"fps\":" + String((float)faceFpsNow, 1) + ",\"drawMs\":" + String((float)faceDrawMs, 1) + ",\"pushMs\":" + String((float)facePushMs, 1) + ",\"showing\":" + String(faceShowing ? 1 : 0) + "}";
  return httpd_resp_send(req, o.c_str(), o.length());
}

esp_err_t ping_handler(httpd_req_t* req) {
  connCloseHttp(req);
  return httpd_resp_sendstr(req, "ok");
}

// 1 = the server answered, 0 = it didn't, -1 = no socket free to ask with (lwIP's 16 shared by the three servers
// and the UDP listeners - the server itself may be fine, so no reboot over that)
int webSelfCheck() {
  const int sk = socket(AF_INET, SOCK_STREAM, 0);
  if (sk < 0) return -1;
  timeval tv = {8, 0};  // (above the server's own 5 s send timeout: a slow phone isn't a dead server)
  setsockopt(sk, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(sk, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(80);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bool ok = false;
  if (connect(sk, (sockaddr*)&a, sizeof(a)) == 0) {
    const char* rq = "GET /ping HTTP/1.1\r\nHost: robot\r\nConnection: close\r\n\r\n";
    char b[16];
    ok = send(sk, rq, strlen(rq), 0) > 0 && recv(sk, b, sizeof(b), 0) > 0;
  }
  close(sk);
  return ok ? 1 : 0;
}

// GET /gait                the steady gait switch, the heading hold and the gyro's sign
// GET /gait?steady=1|0      the D-pad / RC forward + backward use the steady gait (saved)
// GET /gait?kp=0.035&ki=0.01  how hard the heading hold steers (saved)
esp_err_t gait_handler(httpd_req_t* req) {
  char q[96] = {0}, v[16];
  httpd_req_get_url_query_str(req, q, sizeof(q));
  if (httpd_query_key_value(q, "steady", v, sizeof(v)) == ESP_OK) {
    gaitSteadyDpad = v[0] == '1';
    prefPut("gaitSteady", gaitSteadyDpad ? 1 : 0, true);
  }
  if (httpd_query_key_value(q, "hold", v, sizeof(v)) == ESP_OK) {
    headHoldOn = v[0] == '1';
    prefPut("hhOn", headHoldOn ? 1 : 0, true);
    if (!headHoldOn) { headHoldActive = false; headErrNow = headCorrNow = 0; }
  }
  if (httpd_query_key_value(q, "trim", v, sizeof(v)) == ESP_OK) {
    walkTrim = constrain(atof(v), -0.4, 0.4);
    prefPut("wTrim", (int32_t)lroundf(walkTrim * 1000), false);
  }
  if (httpd_query_key_value(q, "kp", v, sizeof(v)) == ESP_OK) {
    headHoldKp = constrain(atof(v), 0.0, 0.2);
    prefPut("hhKp", (int32_t)lroundf(headHoldKp * 1000), false);
  }
  if (httpd_query_key_value(q, "ki", v, sizeof(v)) == ESP_OK) {
    headHoldKi = constrain(atof(v), 0.0, 0.1);
    prefPut("hhKi", (int32_t)lroundf(headHoldKi * 1000), false);
  }
  httpd_resp_set_type(req, "application/json");
  if (httpd_query_key_value(q, "yawsign", v, sizeof(v)) == ESP_OK) {  // the gyro's left sign by hand: 1, -1 (0 = learn again)
    const int s = atoi(v);
    yawLeftSign = s > 0 ? 1 : s < 0 ? -1 : 0;
    prefPut("yawSign", yawLeftSign, false);
    yawSignByHand = yawLeftSign != 0;
    prefPut("yawHand", yawSignByHand ? 1 : 0, false);  // (an int: saved as a bool it doesn't stick)
    xlogSys("GYRO sign set by hand: %d", yawLeftSign);
  }
  if (httpd_query_key_value(q, "sidebal", v, sizeof(v)) == ESP_OK) {  // the sideways walk's pair balance
    sideBal = constrain(atof(v), -0.6, 0.6);
    prefPut("sideBal", (int32_t)lroundf(sideBal * 1000), false);
  }
  if (httpd_query_key_value(q, "balance", v, sizeof(v)) == ESP_OK) {  // the balance toggle (saved)
    balanceOn = v[0] == '1';
    prefPut("balOn", balanceOn ? 1 : 0, false);
    xlogSys("BALANCE %s", balanceOn ? "on" : "off");
  }
  if (httpd_query_key_value(q, "balzero", v, sizeof(v)) == ESP_OK && imuOk) {  // "level" = how it stands right now (on a flat floor)
    const float n = sqrtf(gravLp[0] * gravLp[0] + gravLp[1] * gravLp[1] + gravLp[2] * gravLp[2]);
    if (n > 0.5f) {
      (void)n;
      balPitch0 = asinf(constrain(chkG[1], -1.0f, 1.0f)) * 57.2958f;  // (the same fused tilt balance reads)
      balRoll0 = asinf(constrain(chkG[0], -1.0f, 1.0f)) * 57.2958f;
      balOffP = balOffR = 0;
      balZeroSet = true;
      prefPut("balP0", (int32_t)lroundf(balPitch0 * 10), false);
      prefPut("balR0", (int32_t)lroundf(balRoll0 * 10), false);
      xlogSys("BALANCE level set here: pitch %.1f roll %.1f", balPitch0, balRoll0);
    }
  }
  if (httpd_query_key_value(q, "chicken", v, sizeof(v)) == ESP_OK) chickenOn = v[0] == '1';  // (not saved: off at every start)
  if (httpd_query_key_value(q, "ballift", v, sizeof(v)) == ESP_OK) {  // the balanced walk's knee lift, deg
    balLift = constrain(atof(v), 8.0, 40.0);
    prefPut("balLift", (int32_t)lroundf(balLift * 10), false);
    if (currentCommand.startsWith("smooth_balanced")) loadWalkPreset(currentCommand.endsWith("_back") ? 13 : 12);
  }
  if (httpd_query_key_value(q, "balstride", v, sizeof(v)) == ESP_OK) {  // ...its hip stride, deg
    balStride = constrain(atof(v), 6.0, 30.0);
    prefPut("balStride", (int32_t)lroundf(balStride * 10), false);
    if (currentCommand.startsWith("smooth_balanced")) loadWalkPreset(currentCommand.endsWith("_back") ? 13 : 12);
  }
  if (httpd_query_key_value(q, "trotspeed", v, sizeof(v)) == ESP_OK) {  // the trot's speed 0..1 (0.5 .. 2 Hz), live
    trotSetSpeed(atof(v));
    prefPut("trotSpd", (int32_t)lroundf(trotSpeed * 100), false);
  }
  if (httpd_query_key_value(q, "trotstride", v, sizeof(v)) == ESP_OK) {  // the trot's hip swing, deg each way, live
    trotStride = constrain(atof(v), 8.0, 40.0);
    prefPut("trotStride", (int32_t)lroundf(trotStride * 10), false);
    if (currentCommand.startsWith("smooth_trot")) loadWalkPreset(currentCommand.endsWith("_back") ? 15 : 14);
  }
  if (httpd_query_key_value(q, "trotlift", v, sizeof(v)) == ESP_OK) {  // the trot's knee lift, deg, live
    trotLift = constrain(atof(v), 5.0, 45.0);
    prefPut("trotLift", (int32_t)lroundf(trotLift * 10), false);
    if (currentCommand.startsWith("smooth_trot")) loadWalkPreset(currentCommand.endsWith("_back") ? 15 : 14);
  }
  if (httpd_query_key_value(q, "sidelift", v, sizeof(v)) == ESP_OK) {  // the sideways walk's foot lift
    sideLift = constrain(atof(v), 1.0, 2.2);
    prefPut("sideLift", (int32_t)lroundf(sideLift * 1000), false);
  }
  if (httpd_query_key_value(q, "turnlevel", v, sizeof(v)) == ESP_OK) {  // the sharp turn: level knees (1) or the original skid-steer (0)
    turnLevel = v[0] == '1';
    prefPut("turnLevel", turnLevel ? 1 : 0, true);
    if (currentCommand == "smooth_sharp_left") loadWalkPreset(4);  // turning now: at once
    else if (currentCommand == "smooth_sharp_right") loadWalkPreset(5);
  }
  if (httpd_query_key_value(q, "sidebal", v, sizeof(v)) == ESP_OK || httpd_query_key_value(q, "sidelift", v, sizeof(v)) == ESP_OK) {
    if (currentCommand == "smooth_side_left") loadWalkPreset(10);       // walking sideways now: at once
    else if (currentCommand == "smooth_side_right") loadWalkPreset(11);
  }
  if (httpd_query_key_value(q, "lvltrim", v, sizeof(v)) == ESP_OK) {  // the level-gait's "level", deg
    levelTrim = constrain(atof(v), -15.0, 15.0);
    prefPut("lvlTrim", (int)lroundf(levelTrim * 10), false);
  }
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  char o[900];
  // (pitchNow: the tilt standing still too - + = nose down)
  const float pitchNow = imuOk ? asinf(constrain(imuAcc[1], -1.0f, 1.0f)) * 57.2958f : 0;
  // (fall check: the gyro-fed tilt next to the accelerometer's)
  const float accN = sqrtf(imuAcc[0] * imuAcc[0] + imuAcc[1] * imuAcc[1] + imuAcc[2] * imuAcc[2]) + 1e-6f;
  const float fastP = asinf(constrain(chkG[1], -1.0f, 1.0f)) * 57.2958f, fastR = asinf(constrain(chkG[0], -1.0f, 1.0f)) * 57.2958f;
  const float accP = asinf(constrain(imuAcc[1] / accN, -1.0f, 1.0f)) * 57.2958f, accR = asinf(constrain(imuAcc[0] / accN, -1.0f, 1.0f)) * 57.2958f;
  const int pre = snprintf(o, sizeof(o), "{\"fastP\":%.1f,\"fastR\":%.1f,\"accP\":%.1f,\"accR\":%.1f,\"gyro\":[%.1f,%.1f,%.1f],\"turnLevel\":%d,", fastP, fastR,
                           accP, accR, imuGyro[0], imuGyro[1], imuGyro[2], turnLevel ? 1 : 0);
  snprintf(o + pre, sizeof(o) - pre, "\"balance\":%d,\"balP\":%.1f,\"balR\":%.1f,\"balLift\":%.1f,\"balStride\":%.1f,\"trotSpeed\":%.2f,\"trotLift\":%.1f,\"trotStride\":%.1f,\"chicken\":%d,\"sideBal\":%.2f,\"sideLift\":%.2f,\"pitchNow\":%.1f,\"lvlPitch\":%.1f,\"lvlCorr\":%.1f,\"lvlTrim\":%.1f,\"steady\":%d,\"hold\":%d,\"active\":%d,\"kp\":%.3f,\"ki\":%.3f,\"trim\":%.2f,\"yawSign\":%d,\"yaw\":%.1f,\"headErr\":%.1f,\"corr\":%.2f,\"cmd\":\"%s\"}",
           balanceOn ? 1 : 0, (float)balOffP, (float)balOffR, balLift, balStride, trotSpeed, trotLift, trotStride, chickenOn ? 1 : 0, sideBal, sideLift, pitchNow, (float)levelPitch, (float)levelCorr, levelTrim, gaitSteadyDpad ? 1 : 0, headHoldOn ? 1 : 0, headHoldActive ? 1 : 0, headHoldKp, headHoldKi, walkTrim, yawLeftSign, imuYawAngle, (float)headErrNow, (float)headCorrNow,
           currentCommand.c_str());
  return httpd_resp_sendstr(req, o);
}

// The secure server's handlers: startServer() registers them on it once per start of it (a
// conversation pauses the secure server for its memory - gemini.h - and starts it again after)
bool tlsHandlersDone = false;
void webRestartWithTls();
#include "gemini.h"  // conversations: Gemini Live, straight from the robot
#include "beat.h"      // the beat in the room (dance to any song) - fed by the wake word's listening
#include "wakeword.h"  // the wake word: a conversation, hands free
float musicBeatPeriodMs() { return beat::periodMs; }
unsigned long musicNextBeat(unsigned long t) { return beat::nextBeat(t); }
float musicBeatConf() { return beat::conf; }
float musicLevelDb() { return beat::level; }
bool musicEarsOn() { return ww::holdingNow; }
void musicBeatReset() { beat::reset(); }
void musicMuteFor(int ms) { beat::muteUntilMs = millis() + ms; }  // its own servos: not the music
void musicFreeze(bool on) { beat::frozen = on; }                     // dancing: the tempo estimate holds still
void musicHint(float periodMs) { beat::hintFrames = periodMs > 0 ? periodMs / beat::FRAME_MS : 0; }
void musicCharacter(float* bass, float* mid, float* hi, float* busy, float* rise) {
  *bass = beat::bassDb; *mid = beat::midDb; *hi = beat::hiDb; *busy = beat::busy; *rise = beat::rise;
}
#include "desk_mode.h"  // desk mode: Oreo's inner life on the desk
#include "life_mode.h"  // life mode: Oreo on its own - wanders, finds you, greets you, plays (separate mode)
#include "face_id.h"    // who is this: learns faces, greets people by name (desk / life)
#include "desk_clock.h" // timers, alarms, the sleep timer (every mode)
#include "music.h"      // songs from the card, ambient sounds, bobbing along (every mode)
#include "voice_tools.h" // "set a tea timer", "wake me at 7", "play some rain": Gemini's desk tools
void startServer();
void startStreamServer();
volatile bool webRestartDone = false;
// The plain servers stopped: the handles cleared FIRST, a moment for whoever just read them (loop()'s
// saverPower asks the server for its clients every 250 ms), then stopped (stopped first, the handle
// would point at a freed server for a moment: a crash in httpd_get_client_list).
void webStopPlain() {
  httpd_handle_t c = cmd_httpd, st = stream_httpd;
  cmd_httpd = NULL;
  stream_httpd = NULL;
  vTaskDelay(pdMS_TO_TICKS(100));
  if (c) httpd_stop(c);
  if (st) httpd_stop(st);
}

// The watchdog's restart: only the main (port 80) server. The video server is left alone - stopping it waits
// for a running /stream to end, which can hang the restart (and the watchdog then reboots the robot).
void webRestartTask(void*) {
  httpd_handle_t c = cmd_httpd;
  cmd_httpd = NULL;
  vTaskDelay(pdMS_TO_TICKS(100));
  if (c) httpd_stop(c);
  startServer();
  if (!stream_httpd) startStreamServer();
  webRestartDone = true;
  vTaskDelete(NULL);
}

// Rebooting is the last resort, and never while the robot is doing something (legs moving, RC, a chat, an update).
// A mode is NOT a reason: desk mode is restored at boot, so a dead page there would wait for a power cycle.
// legsMoving is set by loop() (reading the String currentCommand from this task would be a data race).
volatile bool legsMoving = false;
bool robotBusyForReboot() {
  return legsMoving || rcActive() || aud::talkOn || gem::state != gem::S_OFF || otaActive;
}

// After a conversation: the secure server back, with its handlers (the plain servers restart too -
// startServer() is what registers them; the page reconnects within a second)
void webRestartTlsTask(void*) {
  webStopPlain();
  startTlsServer();
  tlsHandlersDone = false;
  startServer();
  startStreamServer();
  xlogSys("WEB secure server back (after a conversation)");
  vTaskDelete(NULL);
}
void webRestartWithTls() { xTaskCreate(webRestartTlsTask, "webrestart", 6144, nullptr, 5, nullptr); }

void webWatchdogTask(void*) {
  int misses = 0;
  unsigned long lastNoSock = 0;
  bool hung = false;
  unsigned long hungSince = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(15000));
    // The log task stuck in a card call (the SD driver never came back: no log, /log hangs
    // port 80): only a restart undoes that. The waiting log lines are in RTC memory and survive it.
    static unsigned long sdStuckSince = 0;
    if (xl::sdOn && (xl::sdDead || (xl::taskBeatMs && millis() - xl::taskBeatMs > 60000))) {
      if (!sdStuckSince) {
        sdStuckSince = millis();
        xlogSys("LOG the SD card %s - restarting the robot once it's idle", xl::sdDead ? "stopped answering" : "task is stuck");
      }
      if ((!robotBusyForReboot() || millis() - sdStuckSince > 600000) && !otaActive && !legsMoving) esp_restart();
      continue;
    }
    sdStuckSince = 0;
    // a long log download holds the server - but not for ever
    static unsigned long busySince = 0;
    if (xl::httpBusy) {
      if (!busySince) busySince = millis();
      if (millis() - busySince < 300000) { misses = 0; continue; }
    } else {
      busySince = 0;
    }
    const int r = webSelfCheck();
    if (r == 1) { misses = 0; hung = false; hungSince = 0; continue; }
    if (r < 0) {  // out of sockets: not the server's fault - noted, not acted on
      if (millis() - lastNoSock > 60000) {
        lastNoSock = millis();
        xlogSys("WEB out of sockets (%d open connections) - not restarting anything", httpConnCount());
      }
      continue;
    }
    if (hung) {  // a restart already hung: reboot once the robot is idle (or after 10 min whatever it does, not mid-update)
      const bool overdue = millis() - hungSince > 600000;
      if ((!robotBusyForReboot() || overdue) && !otaActive && !legsMoving) {
        xlogSys("WEB still not answering%s - restarting the robot", overdue ? " for 10 min" : " and the robot is idle");
        xl::shutdownFlush();
        esp_restart();
      }
      continue;
    }
    if (++misses < 3) continue;  // (3 misses = 45 s: one slow moment on a lossy link isn't a dead server)
    misses = 0;
    xlogSys("WEB server not answering (%d open connections) - restarting it", httpConnCount());
    webRestartDone = false;
    xTaskCreate(webRestartTask, "webrestart", 6144, nullptr, 5, nullptr);
    for (int i = 0; i < 80 && !webRestartDone; i++) vTaskDelay(pdMS_TO_TICKS(100));
    if (!webRestartDone) {
      hung = true;
      hungSince = millis();
      xlogSys("WEB server restart hung - the robot keeps going; it restarts once idle if the page stays dead");
    }
  }
}

// Second, small web server only for the video (port 81): a stream runs for minutes and
// would otherwise hold up every control request
void startStreamServer() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = 81;
  cfg.ctrl_port = 32769;  // the main server uses 32768
  cfg.max_open_sockets = 2;
  cfg.max_uri_handlers = 2;
  cfg.lru_purge_enable = true;  // a new viewer replaces a stale one
  cfg.stack_size = 5120;  // (4 KB leaves only ~1 KB at the deepest)
  cfg.open_fn = httpOpenKeepAlive;  // a viewer that left is found and closed
  if (httpd_start(&stream_httpd, &cfg) == ESP_OK) {
    httpd_uri_t stream_uri = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = NULL };
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}

// GET /sdlog?f=governor|battery&tail=<bytes>: the end of a log file on the SD card (read-only; only these two).
// The governor log has the chat's events (started / ended / why) - /log only says "a conversation".
esp_err_t sdlog_handler(httpd_req_t* req) {
  char q[64] = "", f[16] = "", tl[12] = "";
  httpd_req_get_url_query_str(req, q, sizeof(q));
  httpd_query_key_value(q, "f", f, sizeof(f));
  const char* path = !strcmp(f, "battery") ? "/logs/battery.csv" : "/logs/governor.csv";
  long tail = httpd_query_key_value(q, "tail", tl, sizeof(tl)) == ESP_OK ? atol(tl) : 60000;
  tail = constrain(tail, 1000L, 2000000L);
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  if (!xl::sdOn) return httpd_resp_sendstr(req, "no SD card");
  if (xl::sdDead) return httpd_resp_sendstr(req, "SD card not answering - the robot restarts once it's idle");
  static char* buf = nullptr;
  if (!buf) buf = (char*)heap_caps_malloc(2048, MALLOC_CAP_SPIRAM);
  if (!buf) return httpd_resp_sendstr(req, "no memory");
  size_t pos = 0, size = 0;
  {
    if (xSemaphoreTake(xl::flashMutex, pdMS_TO_TICKS(3000)) != pdTRUE) return httpd_resp_sendstr(req, "SD card busy");
    File fl = SD_MMC.open(path, FILE_READ);
    if (fl) { size = fl.size(); fl.close(); }
    xSemaphoreGive(xl::flashMutex);
  }
  pos = size > (size_t)tail ? size - tail : 0;
  while (pos < size) {  // a chunk at a time: the card is only held for one read
    size_t n = 0;
    if (xSemaphoreTake(xl::flashMutex, pdMS_TO_TICKS(3000)) != pdTRUE) break;
    File fl = SD_MMC.open(path, FILE_READ);
    if (fl && fl.seek(pos)) n = fl.read((uint8_t*)buf, min((size_t)2048, size - pos));
    if (fl) fl.close();
    xSemaphoreGive(xl::flashMutex);
    if (!n) break;
    if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) return ESP_FAIL;
    pos += n;
  }
  return httpd_resp_send_chunk(req, nullptr, 0);
}
esp_err_t shutdown_handler(httpd_req_t* req);  // (below: the page's Shut down)
void startServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.max_uri_handlers = 56;  // (the desk page and /clock need 42+)
  config.uri_match_fn = httpd_uri_match_wildcard;  // "/ai/*" (exact URIs still match exactly)
  config.stack_size = 8192;  // status JSON + snprintf headroom (6 KB leaves only ~1 KB at the deepest)
  // Browsers keep connections open (the page polls every 0.7 s); with all 7 sockets taken, a
  // new request waits until one times out (1-3 s lag). Close the least recently used instead.
  config.lru_purge_enable = true;
  // lwIP has 16 sockets for everything (this build can't raise it): 7 here + 4 video + 8 https run
  // out under the RC page - 5 kept-open is plenty with LRU purge
  config.max_open_sockets = 5;
  config.backlog_conn = 12;  // pending connections while the server task is busy
  config.open_fn = httpOpenKeepAlive;  // dead connections found and closed (talk.h)

  httpd_uri_t servo_uri = {
    .uri       = "/servo",
    .method    = HTTP_GET,
    .handler   = servo_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t cmd_uri = {
    .uri       = "/cmd",
    .method    = HTTP_GET,
    .handler   = cmd_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_handler,
    .user_ctx  = NULL
  };
  httpd_uri_t generate_204_uri = {
    .uri       = "/generate_204",
    .method    = HTTP_GET,
    .handler   = no_content_handler,  // a real 204: no "sign in to network" window
    .user_ctx  = NULL
  };
  httpd_uri_t delay_uri = {
    .uri       = "/setDelay",
    .method    = HTTP_GET,
    .handler   = delay_handler,
    .user_ctx  = NULL
  };
  httpd_uri_t mode_uri = {
    .uri       = "/setMode",
    .method    = HTTP_GET,
    .handler   = mode_handler,
    .user_ctx  = NULL
  };

  Serial.printf("Starting web server on port: '%d'\n", config.server_port);
  if (httpd_start(&cmd_httpd, &config) == ESP_OK) {
    // Every handler also goes on the HTTPS server (the same controller, secure: talk.h), once
    const bool toTls = tls_httpd && !tlsHandlersDone;
    auto regBoth = [toTls](httpd_uri_t* u) {
      httpd_register_uri_handler(cmd_httpd, u);
      if (toTls) httpd_register_uri_handler(tls_httpd, u);
    };
    regBoth(&servo_uri);
    regBoth(&cmd_uri);
    regBoth(&index_uri);
    regBoth(&generate_204_uri);
    regBoth(&delay_uri);
    regBoth(&mode_uri);
    
    httpd_uri_t tune_uri = {
      .uri       = "/tune_walk",
      .method    = HTTP_GET,
      .handler   = tune_walk_handler,
      .user_ctx  = NULL
    };
    regBoth(&tune_uri);

    httpd_uri_t mode_status_uri = {
      .uri       = "/modeStatus",
      .method    = HTTP_GET,
      .handler   = mode_status_handler,
      .user_ctx  = NULL
    };
    regBoth(&mode_status_uri);

    httpd_uri_t log_uri = {
      .uri       = "/log",
      .method    = HTTP_GET,
      .handler   = xlogHttp,
      .user_ctx  = NULL
    };
    regBoth(&log_uri);

    httpd_uri_t params_uri = {
      .uri       = "/exParams",
      .method    = HTTP_GET,
      .handler   = ex_params_handler,
      .user_ctx  = NULL
    };
    regBoth(&params_uri);

    // Settings panel (same API as the creator's firmware)
    httpd_uri_t get_settings_uri = { .uri = "/getSettings", .method = HTTP_GET, .handler = get_settings_handler, .user_ctx = NULL };
    httpd_uri_t set_settings_uri = { .uri = "/setSettings", .method = HTTP_GET, .handler = set_settings_handler, .user_ctx = NULL };
    httpd_uri_t wifi_status_uri = { .uri = "/api/wifi/status", .method = HTTP_GET, .handler = wifi_status_handler, .user_ctx = NULL };
    httpd_uri_t wifi_scan_uri = { .uri = "/api/wifi/scan", .method = HTTP_GET, .handler = wifi_scan_handler, .user_ctx = NULL };
    httpd_uri_t wifi_connect_uri = { .uri = "/api/wifi/connect", .method = HTTP_POST, .handler = wifi_connect_handler, .user_ctx = NULL };
    regBoth(&get_settings_uri);
    regBoth(&set_settings_uri);
    regBoth(&wifi_status_uri);
    regBoth(&wifi_scan_uri);
    regBoth(&wifi_connect_uri);

    // Camera view + head joystick (RC)
    httpd_uri_t cam_view_uri = { .uri = "/camView", .method = HTTP_GET, .handler = cam_view_handler, .user_ctx = NULL };
    httpd_uri_t head_uri = { .uri = "/head", .method = HTTP_GET, .handler = head_handler, .user_ctx = NULL };
    regBoth(&cam_view_uri);
    httpd_uri_t ping_uri = { .uri = "/ping", .method = HTTP_GET, .handler = ping_handler, .user_ctx = NULL };
    regBoth(&ping_uri);
    httpd_uri_t servo_cal_uri = { .uri = "/servoCal", .method = HTTP_GET, .handler = servo_cal_handler, .user_ctx = NULL };
    regBoth(&servo_cal_uri);
    httpd_uri_t calibrate_uri = { .uri = "/calibrate", .method = HTTP_GET, .handler = calibrate_page_handler, .user_ctx = NULL };
    regBoth(&calibrate_uri);
    httpd_uri_t servomap_uri = { .uri = "/servomap", .method = HTTP_GET, .handler = servomap_handler, .user_ctx = NULL };
    regBoth(&servomap_uri);
    httpd_uri_t head_cal_uri = { .uri = "/headCal", .method = HTTP_GET, .handler = head_cal_handler, .user_ctx = NULL };
    regBoth(&head_cal_uri);
    regBoth(&head_uri);
    httpd_uri_t audio_uri = { .uri = "/audio", .method = HTTP_GET, .handler = audio_handler, .user_ctx = NULL };
    regBoth(&audio_uri);
    httpd_uri_t gait_uri = { .uri = "/gait", .method = HTTP_GET, .handler = gait_handler, .user_ctx = NULL };
    regBoth(&gait_uri);
    httpd_uri_t shutdown_uri = { .uri = "/shutdown", .method = HTTP_GET, .handler = shutdown_handler, .user_ctx = NULL };
    regBoth(&shutdown_uri);
    httpd_uri_t sdlog_uri = { .uri = "/sdlog", .method = HTTP_GET, .handler = sdlog_handler, .user_ctx = NULL };
    regBoth(&sdlog_uri);
    httpd_uri_t desk_uri = { .uri = "/desk", .method = HTTP_GET, .handler = deskm::desk_handler, .user_ctx = NULL };
    regBoth(&desk_uri);
    httpd_uri_t life_uri = { .uri = "/life", .method = HTTP_GET, .handler = lifem::life_handler, .user_ctx = NULL };
    regBoth(&life_uri);
    httpd_uri_t clock_uri = { .uri = "/clock", .method = HTTP_GET, .handler = clk::clock_handler, .user_ctx = NULL };
    regBoth(&clock_uri);
    httpd_uri_t music_uri = { .uri = "/music", .method = HTTP_GET, .handler = mus::music_handler, .user_ctx = NULL };
    regBoth(&music_uri);
    httpd_uri_t sprites_uri = { .uri = "/sprites.webp", .method = HTTP_GET, .handler = sprites_handler, .user_ctx = NULL };
    regBoth(&sprites_uri);
    httpd_uri_t faces_uri = { .uri = "/faces", .method = HTTP_GET, .handler = fid::faces_handler, .user_ctx = NULL };
    regBoth(&faces_uri);
    httpd_uri_t gemini_uri = { .uri = "/gemini", .method = HTTP_GET, .handler = gemini_handler, .user_ctx = NULL };
    regBoth(&gemini_uri);
    httpd_uri_t gemini_post_uri = { .uri = "/gemini", .method = HTTP_POST, .handler = gemini_handler, .user_ctx = NULL };
    regBoth(&gemini_post_uri);
    httpd_uri_t sd_put_uri = { .uri = "/sdPut", .method = HTTP_POST, .handler = sd_put_handler, .user_ctx = NULL };
    regBoth(&sd_put_uri);
    httpd_uri_t sd_list_uri = { .uri = "/sdList", .method = HTTP_GET, .handler = sd_list_handler, .user_ctx = NULL };
    regBoth(&sd_list_uri);
    httpd_uri_t sd_get_uri = { .uri = "/sdGet", .method = HTTP_GET, .handler = sd_get_handler, .user_ctx = NULL };
    regBoth(&sd_get_uri);
    httpd_uri_t ota_uri = { .uri = "/ota", .method = HTTP_POST, .handler = ota_handler, .user_ctx = NULL };
    regBoth(&ota_uri);
    httpd_uri_t wifi_choice_uri = { .uri = "/wifi", .method = HTTP_GET, .handler = wifi_choice_handler, .user_ctx = NULL };
    regBoth(&wifi_choice_uri);
    httpd_uri_t gov_uri = { .uri = "/gov", .method = HTTP_GET, .handler = gov_handler, .user_ctx = NULL };
    regBoth(&gov_uri);
    httpd_uri_t tasks_uri = { .uri = "/tasks", .method = HTTP_GET, .handler = tasks_handler, .user_ctx = NULL };
    regBoth(&tasks_uri);
    httpd_uri_t oled_uri = { .uri = "/oled", .method = HTTP_GET, .handler = oled_handler, .user_ctx = NULL };
    regBoth(&oled_uri);
    httpd_uri_t face_uri = { .uri = "/face", .method = HTTP_GET, .handler = face_handler, .user_ctx = NULL };
    regBoth(&face_uri);
    httpd_uri_t ai_uri = { .uri = "/ai/*", .method = HTTP_GET, .handler = ai_file_handler, .user_ctx = NULL };
    regBoth(&ai_uri);
    httpd_uri_t talk_uri = {};  // WebSocket: the phone's mic (talk.h)
    talk_uri.uri = "/talk";
    talk_uri.method = HTTP_GET;
    talk_uri.handler = talk_ws_handler;
    talk_uri.is_websocket = true;
    regBoth(&talk_uri);
    
    httpd_register_err_handler(cmd_httpd, HTTPD_404_NOT_FOUND, my_404_handler);
    if (toTls) {
      httpd_uri_t vws_uri = {};  // the video, for the secure page
      vws_uri.uri = "/vws";
      vws_uri.method = HTTP_GET;
      vws_uri.handler = vws_handler;
      vws_uri.is_websocket = true;
      httpd_register_uri_handler(tls_httpd, &vws_uri);
      httpd_register_err_handler(tls_httpd, HTTPD_404_NOT_FOUND, my_404_handler);
      tlsHandlersDone = true;
    }
  }
}

static const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_EXT: return "reset pin";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "CRASH (panic)";
    case ESP_RST_INT_WDT: return "CRASH (interrupt watchdog)";
    case ESP_RST_TASK_WDT: return "CRASH (task watchdog)";
    case ESP_RST_WDT: return "CRASH (watchdog)";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_BROWNOUT: return "BROWNOUT (battery sagged)";
    case ESP_RST_SDIO: return "sdio";
    default: return "unknown";
  }
}

// ---- Power-down at empty: deep sleep until the charger is plugged in (or the head is touched) ----
static float quickPinMv() {  // the battery pin, as loop() reads it (32 samples, the middle 16 averaged)
  uint16_t rs[32];
  for (int i = 0; i < 32; i++) {
    const uint16_t v = analogRead(BATT_PIN);
    int k = i;
    while (k > 0 && rs[k - 1] > v) { rs[k] = rs[k - 1]; k--; }
    rs[k] = v;
  }
  long sum = 0;
  for (int i = 8; i < 24; i++) sum += rs[i];
  return (sum / 16.0f) / 4095.0f * 3300.0f;
}
[[noreturn]] static void sleepUntilCharged() {
  esp_sleep_enable_timer_wakeup(10ULL * 60 * 1000000);  // look every 10 minutes: plugged in?
  if (TOUCH_PIN >= 0) {  // a touch on the head wakes it at once
    rtc_gpio_pulldown_en((gpio_num_t)TOUCH_PIN);
    rtc_gpio_pullup_dis((gpio_num_t)TOUCH_PIN);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)TOUCH_PIN, 1);
  }
  pinMode(TFT_EN_PIN, OUTPUT);
  digitalWrite(TFT_EN_PIN, LOW);  // the display's power off, held off while asleep
  gpio_hold_en((gpio_num_t)TFT_EN_PIN);
  gpio_deep_sleep_hold_en();
  esp_deep_sleep_start();
}
// First thing in setup(): woken by the 10-minute timer and still not charging -> straight back to sleep
// (screen dark, nothing else started). Charging, a touch, or no battery -> a normal start.
void powerDownWakeCheck() {
  if (rtcSleepMagic != PWRDN_MAGIC) { gpio_hold_dis((gpio_num_t)TFT_EN_PIN); return; }
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
    analogReadResolution(12);
    const float mv = quickPinMv();
    const float packV = mv / 1000.0f * 11.0f * rtcBatCal;
    // A pack resting after the load is gone creeps up 0.1-0.2 V by itself: compare with the last check
    // (also resting), not with the moment it fell asleep. A charger lifts it ~0.3 V+ right away.
    const bool charging = packV >= 7.6f || (rtcRestPinMv > 0 && (mv - rtcRestPinMv) / 1000.0f * 11.0f * rtcBatCal >= 0.25f);
    if (!charging && packV >= 3.0f) {  // (below 3 V: no battery - USB power: start normally)
      rtcRestPinMv = mv;
      rtcSleepChecks++;
      sleepUntilCharged();
    }
  }
  rtcSleepMagic = 0;  // charging / touched / no battery: a normal start
  gpio_hold_dis((gpio_num_t)TFT_EN_PIN);
}
[[noreturn]] void powerDownNow() {
  xlogSys("BATTERY empty %d%% (%.2f V): powering down to protect the pack - plug in the charger (it checks every 10 min) "
          "or touch its head", (int)batPctSlow, batSlowV);
  govLog("battery", "empty: powering down (deep sleep until charging)");
  if (xSemaphoreTake(spriteMutex, pdMS_TO_TICKS(500)) == pdTRUE) {  // a last word on the screen
    tft.startWrite();
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(middle_center);
    tft.setTextSize(3);
    tft.drawString("charge me", 160, 100);
    tft.setTextSize(2);
    tft.drawString("battery empty", 160, 145);
    tft.endWrite();
    xSemaphoreGive(spriteMutex);
  }
  legsRelax();
  for (int i = 0; i < 16; i++) pwm.setPWM(i, 0, 4096);  // every channel off (head too)
  delay(2500);                                         // the message stays a moment; the log writes out
  pwm.sleep();                                         // the servo board's oscillator off
  oled.ssd1306_command(0xAE);                          // OLED off
  WiFi.mode(WIFI_OFF);
  rtcBatCal = BAT_CAL;
  rtcRestPinMv = 0;  // the first 10-minute check sets the resting level
  rtcSleepChecks = 0;
  rtcSleepMagic = PWRDN_MAGIC;
  sleepUntilCharged();
}
// The crab rave's song (movement-sequences.h): /music/crabrave.wav from the SD card, if it's there
bool crabMusicStart() {
  if (sdSizeMB < 0 || !SD_MMC.exists("/music/crabrave.wav")) return false;
  return aud::play("/music/crabrave.wav", 0.8f);
}
void crabMusicStop() {
  if (aud::playing) aud::stopReq = true;  // (fades out)
}
long crabMusicPosMs() { return aud::playing ? (long)aud::playPosMs : -1; }

// ---- The pat brain (see loop(); setting "patbrain", 0 = the simple hold-for-a-second "patted") ----
// Touches are caught by an interrupt, so pats while a reaction plays (loop() is busy then) still count.
// A contact: 30 ms .. 700 ms = a pat (tap); longer = a hand held there (petting). Pats less than 0.45 s
// apart are one burst: 1 = a tap, 2 = a double tap (play), 3+ = gentle patting (love). An affection
// level (+ each pat, fading over ~3 min) shades the choice; never the same reaction as the last three.
bool headTouched() { return TOUCH_PIN >= 0 && digitalRead(TOUCH_PIN) == HIGH; }
namespace pat {
volatile unsigned long riseMs = 0, lastEdgeMs = 0;
volatile bool isDown = false;
struct Contact { unsigned long start, dur; };
volatile Contact ring[16];
volatile uint8_t ringHead = 0;  // written by the interrupt
uint8_t ringTail = 0;           // read by loop()
void IRAM_ATTR edge() {
  const unsigned long now = millis();
  const bool hi = digitalRead(TOUCH_PIN) == HIGH;
  if (now - lastEdgeMs < 25 && hi == isDown) return;  // (bounce)
  lastEdgeMs = now;
  if (hi && !isDown) { isDown = true; riseMs = now; }
  else if (!hi && isDown) {
    isDown = false;
    const unsigned long d = now - riseMs;
    if (d >= 30) {
      ring[ringHead & 15].start = riseMs;
      ring[ringHead & 15].dur = d;
      ringHead++;
    }
  }
}
float affection = 0.3f;        // 0 .. 1
unsigned long affMs = 0, lastContactMs = 0, lastReactEndMs = 0, enoughMs = 0, lastActMs = 0;
unsigned long recent[16] = {0};
uint8_t recentN = 0;
const char* last3[3] = {"", "", ""};
bool started = false;
}  // namespace pat

void patBegin() {
  if (TOUCH_PIN < 0) return;
  attachInterrupt(digitalPinToInterrupt(TOUCH_PIN), pat::edge, CHANGE);
  pat::started = true;
}

void patUpdate() {
  using namespace pat;
  const unsigned long now = millis();
  static String cmdWas = "";
  if (currentCommand != cmdWas) {  // anything the robot did (a command, an emote) counts as company
    if (cmdWas.length() && !currentCommand.length()) lastReactEndMs = now;
    cmdWas = currentCommand;
    lastActMs = now;
  }

  if (!PAT_BRAIN || !started) {
    // ---- the simple way: hold for a second -> "patted", until a second after letting go ----
    static unsigned long touchStartTime = 0, touchReleaseTime = 0;
    static bool isTouching = false, isReleasing = false;
    if (headTouched() && activeMode == MODE_WALK && requestedMode == MODE_WALK) {
      isReleasing = false;
      if (!isTouching) { isTouching = true; touchStartTime = now; }
      else if (now - touchStartTime >= 1000 && currentCommand != "patted") currentCommand = "patted";
    } else if (isTouching) {
      isTouching = false;
      isReleasing = true;
      touchReleaseTime = now;
    } else if (isReleasing && now - touchReleaseTime >= 1000) {
      if (currentCommand == "patted") currentCommand = "";
      isReleasing = false;
    }
    ringTail = ringHead;  // (nothing queued up for when it's switched on)
    return;
  }

  // affection fades toward 0.3 (neutral) over ~3 minutes
  if (affMs) affection += (0.3f - affection) * fminf(1.0f, (now - affMs) / 180000.0f);
  affMs = now;

  // finished contacts (from the interrupt): count them
  int burst = 0;
  unsigned long burstEnd = 0;
  bool newOnes = false;
  while (ringTail != ringHead) {
    const Contact c = {ring[ringTail & 15].start, ring[ringTail & 15].dur};
    ringTail++;
    newOnes = true;
    recent[recentN++ & 15] = c.start;
    affection = fminf(1.0f, affection + (c.dur >= 700 ? 0.15f : 0.08f));
    lastContactMs = c.start + c.dur;
  }
  (void)newOnes;
  auto countSince = [&](unsigned long ms) {
    int n = 0;
    for (unsigned long r : recent) if (r && now - r < ms) n++;
    return n;
  };
  auto pick = [&](const char* const* opts, int n) {
    const char* ch = opts[random(n)];
    for (int tries = 0; tries < 10; tries++) {
      bool used = false;
      for (const char* l : last3) if (!strcmp(l, ch)) used = true;
      if (!used) break;
      ch = opts[random(n)];
    }
    last3[2] = last3[1];
    last3[1] = last3[0];
    last3[0] = ch;
    return ch;
  };

  const bool walkMode = activeMode == MODE_WALK && requestedMode == MODE_WALK && !aud::talkOn;
  const bool idle = walkMode && currentCommand == "";
  const bool lying = !legsPowered && legsPoseKnown;
  struct tm tmv;
  const bool night = getLocalTime(&tmv, 0) && (tmv.tm_hour >= 22 || tmv.tm_hour < 6);

  // a hand held on its head right now: petting (or waking up, lying down)
  if (isDown && now - riseMs >= 700 && idle && now - lastReactEndMs > 600) {
    const unsigned long since = now - lastActMs;
    currentCommand = lying ? "pat_wake" : "petting";
    xlogSys("PAT hold (affection %.2f, %lu s since anything) -> %s", affection, since / 1000, currentCommand.c_str());
    lastActMs = now;
    return;
  }
  // walking / driving: no stopping it - just a happy face
  if (isDown && now - riseMs >= 30 && walkMode && currentCommand.length() && currentCommand.startsWith("smooth")) {
    static unsigned long faceMs = 0;
    if (now - faceMs > 3000) { faceMs = now; setFace(affection > 0.6f ? "love" : "happy"); }
    return;
  }

  // the burst of pats is over (0.45 s since the last one, the hand gone): what was it?
  if (isDown || !lastContactMs || now - lastContactMs < 450) return;
  // the pats in this burst: the ones that started within the burst (each less than 0.45 s apart)
  {
    unsigned long t = lastContactMs + 1;
    for (int k = 0; k < 16; k++) {
      unsigned long best = 0;  // the latest start before t
      for (unsigned long r : recent) if (r && r < t && r > best) best = r;
      if (!best || t - best > 450 + 700) break;
      burst++;
      t = best;
    }
    burstEnd = lastContactMs;
  }
  lastContactMs = 0;  // handled
  if (burstEnd < lastReactEndMs) return;      // pats during the last reaction: counted, no new one
  if (!idle || now - burstEnd > 3000) return;  // busy (it was mid-reaction), or long ago: it still counted above
  if (now - lastReactEndMs < 600) return;      // just finished one: not straight into the next
  const int many = countSince(25000);
  const unsigned long since = now - lastActMs;
  const char* r;
  if (lying) r = "pat_wake";
  else if (enoughMs && now - enoughMs < 20000) {  // after "enough!": only a shy glance
    setFace("shy");
    xlogSys("PAT %d (after enough) -> shy look", burst);
    return;
  } else if (many >= 9) {
    r = "pat_enough";
    enoughMs = now;
    for (auto& x : recent) x = 0;
  } else if (since > 10UL * 60000 && millis() > 60000) r = "pat_hello";  // alone a long while (not just after a start)
  else if (night) { static const char* const O[] = {"pat_nuzzle", "petting", "pat_boop"}; r = pick(O, 3); }
  else if (burst >= 3) { static const char* const O[] = {"pat_nuzzle", "petting", "knead", "happy"}; r = pick(O, 4); }
  else if (burst == 2) {
    if (affection > 0.7f) { static const char* const O[] = {"playbow", "celebrate", "bounce", "wiggle"}; r = pick(O, 4); }
    else { static const char* const O[] = {"playbow", "wiggle", "bounce", "huh"}; r = pick(O, 4); }
  } else if (many >= 5) { static const char* const O[] = {"bounce", "wiggle", "happy", "excited"}; r = pick(O, 4); }
  else if (affection < 0.35f) { static const char* const O[] = {"pat_boop", "huh", "pat_boop"}; r = pick(O, 3); }
  else if (affection < 0.65f) { static const char* const O[] = {"pat_boop", "happy", "pat_nuzzle", "huh"}; r = pick(O, 4); }
  else { static const char* const O[] = {"pat_nuzzle", "wiggle", "happy", "pat_boop"}; r = pick(O, 4); }
  currentCommand = r;
  lastActMs = now;
  xlogSys("PAT %d tap%s (%d in 25 s, affection %.2f%s) -> %s", burst, burst > 1 ? "s" : "", many, affection, night ? ", night" : "", r);
}

// The page's "Shut down" (GET /shutdown): lie down gently, save waiting settings, write the log out and
// unmount the SD card, everything off, then deep sleep until the head is touched. Then the rocker
// switch can go off without the legs dropping or a half-written card / setting.
volatile bool shutdownReq = false;
[[noreturn]] void shutdownNow() {
  xlogSys("SHUTDOWN from the page: lying down, saving, card unmounted - safe to switch off");
  auto say = [](const char* big, const char* small) {
    if (xSemaphoreTake(spriteMutex, pdMS_TO_TICKS(500)) != pdTRUE) return;
    tft.startWrite();
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(middle_center);
    tft.setTextSize(3);
    tft.drawString(big, 160, 100);
    tft.setTextSize(2);
    tft.drawString(small, 160, 145);
    tft.endWrite();
    xSemaphoreGive(spriteMutex);
  };
  say("good night", "shutting down...");
  // The shutdown emote: sit, then the legs (knees R4 R3 L3 L4) to their min, the hips
  // (R1 R2 L1 L2) to their min, the head's pan to the middle and its tilt to its min - folded up for the night
  // (all in built-in angles: setServoAngle maps them onto the calibration)
  setFaceWithMode("sleepy", FACE_ANIM_ONCE);
  if (!robotFallen) {  // (lying on its side: no stand / sit / fold - the legs would push against the floor)
    runStandPose(0);
    moveSmoothHelper(600, -999, -999, -999, -999, REST_BACK[0], REST_BACK[1], REST_BACK[2], REST_BACK[3]);  // sit
    delay(300);
    moveSmoothHelper(700, -999, -999, -999, -999, (int)LEG_MIN0[4], (int)LEG_MIN0[5], (int)LEG_MIN0[6], (int)LEG_MIN0[7]);
    delay(200);
    moveSmoothHelper(700, (int)LEG_MIN0[0], (int)LEG_MIN0[1], (int)LEG_MIN0[2], (int)LEG_MIN0[3], -999, -999, -999, -999);
    delay(200);
  }
  auto headGlide = [](uint8_t pin, int& now, int to, int ms) {
    const int from = now, steps = max(1, ms / 20);
    for (int k = 1; k <= steps; k++) {
      const float u = (float)k / steps, e = u * u * (3 - 2 * u);
      setHeadServoF(pin, from + (to - from) * e);
      delay(20);
    }
    now = to;
  };
  panWho = "shutdown";
  int pn = emoPanNow, tl = emoTiltNow;
  headGlide(HEAD_PAN_PIN, pn, (int)lroundf(PAN_MID), 500);
  headGlide(HEAD_TILT_PIN, tl, (int)lroundf(TILT_MIN), 500);
  emoPanNow = pn;
  emoTiltNow = tl;
  delay(300);
  legsRelax();
  for (int i = 0; i < 16; i++) pwm.setPWM(i, 0, 4096);  // every channel off (head too)
  if (xl::lockFlash(3000)) { applyPendingPrefs(); xl::unlockFlash(); }  // settings still waiting
  delay(300);
  xl::shutdownFlush();
  say("safe to switch off", "touch my head to wake");
  delay(3000);
  pwm.sleep();
  oled.ssd1306_command(0xAE);
  WiFi.mode(WIFI_OFF);
  rtcSleepMagic = 0;  // a touch: a normal start
  clk::armWake();     // an alarm set: the timer wakes it ~2 min before (a normal start too)
  if (TOUCH_PIN >= 0) {
    rtc_gpio_pulldown_en((gpio_num_t)TOUCH_PIN);
    rtc_gpio_pullup_dis((gpio_num_t)TOUCH_PIN);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)TOUCH_PIN, 1);
  }
  pinMode(TFT_EN_PIN, OUTPUT);
  digitalWrite(TFT_EN_PIN, LOW);  // the display's power off
  gpio_hold_en((gpio_num_t)TFT_EN_PIN);
  gpio_deep_sleep_hold_en();
  esp_deep_sleep_start();
}
esp_err_t shutdown_handler(httpd_req_t* req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  if (otaActive) return httpd_resp_sendstr(req, "BUSY: updating");
  if (aud::talkOn) return httpd_resp_sendstr(req, "BUSY: chatting - end the chat first");
  shutdownReq = true;  // loop() does it (I2C, the screen and the legs are its)
  return httpd_resp_sendstr(req, "OK");
}

// loop(): at the stop % for 90 s with no charger (20 s below 6.4 V = 3.2 V/cell) -> power down.
// Never mid-chat, mid-RC or mid-update, and not in the first 2 minutes (the reading settles).
void powerDownCheck() {
  static unsigned long lowSince = 0;
  const unsigned long now = millis();
  const bool empty = BAT_SLEEP && batPctSlow >= 0 && batPctSlow <= BAT_STOP_PCT && !batCharging && now > 120000;
  if (!empty || otaActive || aud::talkOn || rcActive()) { lowSince = 0; return; }
  if (!lowSince) lowSince = now;
  if (now - lowSince < (batSlowV < 6.4f ? 20000UL : 90000UL)) return;
  powerDownNow();
}

void setup() {
  servoMapLoad();  // (before anything moves a servo)
  // currentCommand is set by the web server's task while loop() reads it: with room for any command made
  // once, an assignment copies in place - it never frees the buffer under a reader (which would crash)
  currentCommand.reserve(64);
  powerDownWakeCheck();  // asleep because the pack was empty: back to sleep unless it is charging
  // 32 KB, not 16: the raw frames (face tracking, Explore, desk) use the kept one too - a 32 KB block of their own,
  // made at every camera start, gets split up after a stop / restart and face mode can't start
  if (esp_camera_keep_dma) esp_camera_keep_dma(32768);  // the camera's DMA buffer, made while internal RAM is in one piece and kept (cam_hal.c)
  Serial.setRxBufferSize(16384);  // file uploads over the cable (serialCmdTask)
  // every log line is echoed here: without a buffer each one waits for the bytes to go out at 115200 baud
  // (~11 per ms) and the main loop pays for it
  Serial.setTxBufferSize(4096);
  Serial.begin(115200);
  while(!Serial);
  const bool logOk = xlogBegin();
  
  // GPIO 19/20 are the chip's native USB pins. The display's DC is on 20 and LovyanGFX sets pins
  // straight in the IO mux, which leaves the USB pad in charge of them - hand them to the GPIOs first.
  CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);

  pinMode(TFT_EN_PIN, OUTPUT);
  digitalWrite(TFT_EN_PIN, HIGH);  // display power - its RST wire is joined to this pin
  delay(150);                      // the panel's own power-on reset needs 120 ms before commands

  // Initialize Display. Its memory still holds whatever power-up left (random pixels: a half-lit,
  // distorted screen), so the init leaves the output off (Panel_ST7789_Quiet) until the screen is
  // cleared to black, then turns it on (0x29). No software reset needed: the panel's reset is wired to its power pin.
  tft.init();
  tft.setRotation(3);
  tft.setSwapBytes(false);
  tft.fillScreen(tft.color565(0, 0, 0));  // pitch black, as the boot screen
  tft.startWrite();
  tft.writeCommand(0x29);  // DISPON: a clean screen
  tft.endWrite();
  // Keep the 150KB face sprite in PSRAM so the camera/WiFi keep enough internal RAM
  sprite.setPsram(true);
  sprite.createSprite(320, 240);
  sprite.setSwapBytes(false);
  sprite2.setPsram(true);
  sprite2.createSprite(320, 240);
  sprite2.setSwapBytes(false);
  spriteMutex = xSemaphoreCreateMutex();


  // microSD at the standard 20 MHz clock (1-bit): at 40 MHz reads and writes
  // time out now and then (0x107) - the slot's wiring can't hold it. Never formats the card.
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
  // 10 files open at once: the log, governor/battery CSVs, music, a voice clip, a photo and a /log
  // download can be open together - and a failed open reads as "the card is gone"
  bool sdMounted = SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT, 10);
  if (sdMounted && SD_MMC.cardType() != CARD_NONE) sdSizeMB = (int)(SD_MMC.cardSize() >> 20);
  Serial.printf("SD card: %s\n", sdSizeMB >= 0 ? String(String(sdSizeMB) + " MB").c_str() : "none / not FAT32");
  if (sdSizeMB >= 0) {
    // a side log whose end is binary junk (a write cut by a power-off / restart): set aside, start a new one
    for (const char* lp : {"/logs/governor.csv", "/logs/battery.csv"}) {
      File f = SD_MMC.open(lp, FILE_READ);
      if (!f) continue;
      const size_t sz = f.size();
      bool junk = false;
      if (sz > 64 && f.seek(sz - 64)) {
        uint8_t t[64];
        const int n = f.read(t, 64);
        int bad = 0;
        for (int i = 0; i < n; i++) if ((t[i] < 9 || (t[i] > 13 && t[i] < 32) || t[i] > 126)) bad++;
        junk = bad > 8;
      }
      f.close();
      // too big (slow to read back), or stuck: an append that "works" but the file never grows (governor.csv
      // and battery.csv can stop growing with nothing failing): set aside, a new one
      bool stuck = sz > 3u * 1024 * 1024;
      if (!junk && !stuck) {
        File a = SD_MMC.open(lp, FILE_APPEND);
        if (a) { a.write('\n'); a.close(); }
        File b = SD_MMC.open(lp, FILE_READ);
        stuck = !b || b.size() <= sz;
        if (b) b.close();
      }
      if (junk || stuck) {
        char np[48];
        snprintf(np, sizeof(np), "%.*s-%s-%u.csv", (int)(strlen(lp) - 4), lp, junk ? "broken" : "old", (unsigned)esp_random() % 10000);
        SD_MMC.rename(lp, np);
        Serial.printf("LOG %s was broken: kept as %s, a new one started\n", lp, np);
      }
    }
    xlogUseSd();
  }  // the Explore log goes to the card: no camera pauses for log writes
  {
    // the animated face (face_engine_v2.h) renders from its own task; its style is saved in NVS.
    // Started after the card so /faces/*.txt packs load first.
    Preferences fp;
    fp.begin("explore", true);
    const int st = fp.getInt("faceStyle", 0);
    PLAY_EVERY_S = fp.getInt("playEvery", PLAY_EVERY_S);
    OLED_ML = fp.getInt("oledL", OLED_ML);
    OLED_MT = fp.getInt("oledT", OLED_MT);
    OLED_MR = fp.getInt("oledR", OLED_MR);
    OLED_MB = fp.getInt("oledB", OLED_MB);
    aud::masterVol = constrain(fp.getInt("volume", 70), 0, 100) / 100.0f;
    faceBright = constrain(fp.getInt("faceBright", 100), 10, 100) / 100.0f;
    props3d = fp.getBool("props3d", false);
    saverColor = (uint32_t)fp.getInt("saverCol", 0x5ADCFF) & 0xFFFFFF;
    playSound = fp.getBool("playSound", playSound);
    emoteSndOn = fp.getInt("emoSnd", 1) != 0;
    playAuto = fp.getBool("playAuto", playAuto);
    fp.end();
    faceBegin(st);
  }

  // (the screen: the boot screen, drawn by the face task - boot_screen.h)
  if (sdSizeMB >= 0) bootInfo.ok |= 1 << boot::B_SD; else bootInfo.fail |= 1 << boot::B_SD;

// Initialize Servos & I2C
  if (TOUCH_PIN >= 0) pinMode(TOUCH_PIN, INPUT_PULLDOWN);  // reads "not touched" if the sensor wire is off
  patBegin();  // the pat brain's touch interrupt
  Wire.begin(I2C_SDA, I2C_SCL);
  // 25 ms per I2C call, not the driver's 1 s: the MPU6050 clone now and then doesn't answer, and
  // each time the whole control loop would stand still for a second
  Wire.setTimeOut(25);
  legsFromServoBoard();  // before pwm.begin(): where the legs are
  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(50);
  bootInfo.ok |= 1 << boot::B_LEGS;
  
  if(!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
  } else {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 0);
    oled.println("Booting...");
    oled.display();
    i2cSpeedRestore();
  }

  imuOk = imuBegin();
  if (imuOk) bootInfo.ok |= 1 << boot::B_IMU; else bootInfo.fail |= 1 << boot::B_IMU;
  if (!imuOk) Serial.println("Failed to init MPU6050");

  // ToF on the head (Explore mode). Continuous ranging while it's used (tofPower), polled by loop().
  tofOk = lox.begin(0x29, false, &Wire);
  // 100 kHz (the 400 kHz pick - i2cPickSpeed() - is off: its test only checks that devices answer,
  // not that every byte arrives right)
  Wire.setClock(i2cHz = 100000);
  xlogSys("I2C 100 kHz (fixed)");
  if (tofOk) bootInfo.ok |= 1 << boot::B_TOF; else bootInfo.fail |= 1 << boot::B_TOF;
  if (!tofOk) {
    Serial.println(F("Failed to boot VL53L0X"));
  }

  prefs.begin("explore", false);
  walkDefaults(true, true);  // the walks: built in, or the ones saved from the page
  if (prefs.getBytesLength("walkF") == sizeof(walkFwd)) prefs.getBytes("walkF", &walkFwd, sizeof(walkFwd));
  legCalLoad();  // the legs' calibration (the calibration page)
  if (legsPowered)  // read back from the servo board as real angles: into built-in ones
    for (int i = 0; i < 8; i++) currentAngle[i] = calUnmap(i, currentAngle[i]);
  if (prefs.getBytesLength("walkB") == sizeof(walkBack)) prefs.getBytes("walkB", &walkBack, sizeof(walkBack));
  setHeadCal(prefs.getInt("panMin", (int)PAN_MIN0), prefs.getInt("panMid", (int)PAN_MID0), prefs.getInt("panMax", (int)PAN_MAX0),
             prefs.getInt("tiltMin", (int)TILT_MIN0), prefs.getInt("tiltMid", (int)TILT_MID0), prefs.getInt("tiltMax", (int)TILT_MAX0));
  exploreCamAssist = prefs.getBool("camAssist", true);
  panPlusIsLeft = prefs.getBool("panPlusLeft", true);
  staSearch = !prefs.getBool("hsOnly", false);  // "hotspot only" from the page, kept over a restart
  exEdgeEnabled = prefs.getBool("edge", true);
  exAvoidDark = prefs.getBool("avoidDark", true);
  camSharp = constrain(prefs.getInt("camSharp", -1), -1, 6);
  yawLeftSign = prefs.getInt("yawSign", 0);
  yawSignByHand = prefs.getInt("yawHand", 0) != 0;
  walkTrim = prefs.getInt("wTrim", 180) / 1000.0f;
  sideBal = prefs.getInt("sideBal", 0) / 1000.0f;
  turnLevel = prefs.getBool("turnLevel", true);
  sideLift = prefs.getInt("sideLift", 1000) / 1000.0f;
  balLift = prefs.getInt("balLift", 240) / 10.0f;
  balStride = prefs.getInt("balStride", 180) / 10.0f;
  trotSpeed = constrain(prefs.getInt("trotSpd", 67) / 100.0f, 0.0f, 1.0f);
  trotLift = constrain(prefs.getInt("trotLift", 250) / 10.0f, 5.0f, 45.0f);
  trotStride = constrain(prefs.getInt("trotStride", 320) / 10.0f, 8.0f, 40.0f);
  deskm::deskLoadSettings();
  clk::load();  // the alarms (desk_clock.h)
  if (prefs.getInt("deskMode", 0) == 1) requestedMode = MODE_DESK;  // it was on the desk before the restart
  gaitSteadyDpad = prefs.getBool("gaitSteady", false);
  levelTrim = prefs.getInt("lvlTrim", 0) / 10.0f;
  balanceOn = prefs.getInt("balOn", 0) != 0;
  balZeroSet = prefs.isKey("balP0");
  balPitch0 = prefs.getInt("balP0", 0) / 10.0f;
  balRoll0 = prefs.getInt("balR0", 0) / 10.0f;
  balPitchSign = prefs.getInt("balPS", 1) < 0 ? -1 : 1;
  balRollSign = prefs.getInt("balRS", 1) < 0 ? -1 : 1;
  {
    char sv[64] = "";
    prefs.getString("sitPose", sv, sizeof(sv));
    int a[8], n = 0;
    for (char* t = strtok(sv, ","); t && n < 8; t = strtok(nullptr, ",")) a[n++] = atoi(t);
    if (n == 8) memcpy(SIT_POSE, a, sizeof(a));
    char rb[40] = "";
    prefs.getString("restBack", rb, sizeof(rb));
    n = 0;
    for (char* t = strtok(rb, ","); t && n < 4; t = strtok(nullptr, ",")) a[n++] = atoi(t);
    if (n == 4) memcpy(REST_BACK, a, 4 * sizeof(int));
  }
  headHoldOn = prefs.getBool("hhOn", true);
  headHoldKp = prefs.getInt("hhKp", 35) / 1000.0f;
  headHoldKi = prefs.getInt("hhKi", 10) / 1000.0f;
  if (yawLeftSign != 1 && yawLeftSign != -1) yawLeftSign = 0;
  // Learned floor distances only count for the look-down angles they were learned at
  const bool floorSameAngles = prefs.getInt("floorSig", 0) == exFloorSig();
  for (int i = 0; i < 3; i++) {
    char key[8];
    snprintf(key, sizeof(key), "floor%d", i);
    exFloorSaved[i] = floorSameAngles ? prefs.getInt(key, EX_FLOOR_DEFAULT_MM[i]) : EX_FLOOR_DEFAULT_MM[i];
  }
  xl::onFlashSafe = applyPendingPrefs;
  exParamsInit();
  xlogSys("BOOT #%u reset=%s flash=%uMB psram=%u heap=%u log=%s imu=%d tof=%d sd=%dMB | edge=%d avoidDark=%d "
       "assist=%d flip=%d floor=%d/%d/%d",
       (unsigned)xl::bootNo, resetReasonName(esp_reset_reason()), (unsigned)(ESP.getFlashChipSize() >> 20),
       ESP.getFreePsram(), ESP.getFreeHeap(), xl::sdOn ? "SD card" : logOk ? "flash" : "SERIAL ONLY (no explog partition)",
       imuOk ? 1 : 0, tofOk ? 1 : 0, sdSizeMB, exEdgeEnabled ? 1 : 0, exAvoidDark ? 1 : 0, exploreCamAssist ? 1 : 0,
       panPlusIsLeft ? 0 : 1, exFloorSaved[0], exFloorSaved[1], exFloorSaved[2]);
  if (xl::recovered) xlog("LOG the lines just above were still waiting in memory when the chip reset");
  // A crash saved in flash (core dump): which task, where (decode the addresses with the firmware's .elf:
  // xtensa-esp32s3-elf-addr2line -pfiaC -e firmware.elf <pc> <bt...>), then erased so it's logged once
  {
    esp_core_dump_summary_t* cs = (esp_core_dump_summary_t*)heap_caps_malloc(sizeof(esp_core_dump_summary_t), MALLOC_CAP_SPIRAM);
    if (cs && esp_core_dump_image_check() == ESP_OK && esp_core_dump_get_summary(cs) == ESP_OK) {
      char bt[200];
      int o = 0;
      for (uint32_t i = 0; i < cs->exc_bt_info.depth && i < 16 && o < 180; i++) o += snprintf(bt + o, sizeof(bt) - o, " %08lx", (unsigned long)cs->exc_bt_info.bt[i]);
      bt[o] = 0;
      xlogSys("CRASH saved: task %s pc %08lx cause %lu addr %08lx | bt%s%s", cs->exc_task, (unsigned long)cs->exc_pc,
              (unsigned long)cs->ex_info.exc_cause, (unsigned long)cs->ex_info.exc_vaddr, bt, cs->exc_bt_info.corrupted ? " (corrupted)" : "");
      esp_core_dump_image_erase();
    }
    free(cs);
  }
  // WiFi events: why the router link dropped (reason code), a phone joining / leaving the hotspot
  WiFi.onEvent([](arduino_event_id_t e, arduino_event_info_t info) {
    static uint8_t lastReason = 0;
    static unsigned long lastMs = 0, repeats = 0;
    if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      const uint8_t r = info.wifi_sta_disconnected.reason;
      if (r == lastReason && millis() - lastMs < 30000) { repeats++; return; }  // (a failing join retries every few s)
      xlogSys("WIFI router link lost: reason %u%s (+%lu repeats)", (unsigned)r,
              r == 200 ? " beacon timeout" : r == 201 ? " no AP found" : r == 202 ? " auth fail" : r == 8 ? " left" : r == 4 ? " assoc expired" : "",
              repeats);
      lastReason = r; lastMs = millis(); repeats = 0;
    } else if (e == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      xlogSys("WIFI joined the router: rssi %d, channel %d", (int)WiFi.RSSI(), (int)WiFi.channel());
      lastReason = 0;
    } else if (e == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
      xlogSys("WIFI a phone joined the hotspot (..%02x:%02x), %d on it", info.wifi_ap_staconnected.mac[4], info.wifi_ap_staconnected.mac[5],
              (int)WiFi.softAPgetStationNum());
    } else if (e == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
      xlogSys("WIFI a phone left the hotspot (..%02x:%02x)", info.wifi_ap_stadisconnected.mac[4], info.wifi_ap_stadisconnected.mac[5]);
    }
  });
  {
    const String ch = exParamsChangedString();
    xlog("PARAMS %s", ch.length() ? ch.c_str() : "all defaults");
  }
  for (int i = 0; i < 16; i++) pendingServoDeg[i] = NAN;


  
  analogReadResolution(12);
  
  // No rest pose at start: the legs stay limp on power up
  


  // Start Access Point & Captive Portal alongside Station mode
  WiFi.persistent(false);  // the WiFi driver must not write flash later (camera, see xlog.h)
  // client only for now: the hotspot starts in bootWifiFinish() already named "Oreo" (starting it here
  // would broadcast the old name kept in the WiFi chip's flash for a few seconds).
  WiFi.mode(WIFI_STA);
  // No WiFi power saving: in modem sleep the radio dozes between router beacons and web
  // requests stall for 1-3 s (dropped TCP connects). A few mA - nothing next to the servos.
  WiFi.setSleep(false);
  {
    Preferences wp;
    wp.begin("explore", true);
    wifiBw = wp.getInt("wifiBw", wifiBw);
    wp.end();
  }
  esp_wifi_set_bandwidth(WIFI_IF_STA, wifiBw == 20 ? WIFI_BW_HT20 : WIFI_BW_HT40);
  // The hotspot on a plain 20 MHz channel. Its default is 40 MHz: fine while the robot is on a router
  // (then it takes the router's settings), but on its own the double-wide channel collides with the
  // neighbours' WiFi (25-50 % lost packets, RC freezing ~1 s at each loss).
  // (iPhones don't use 40 MHz on 2.4 GHz anyway.)
  // One scan first (~1.5 s): which known network is here and the quietest channel - then the hotspot starts
  // right on its final channel (moving it later kicks off a phone that already joined).
  prefs.getString("wifiS", savedSsid, sizeof(savedSsid));
  prefs.getString("wifiP", savedPass, sizeof(savedPass));
  // The scan (which remembered network is here, the quietest channel) takes seconds, so it runs in the
  // background: loop() starts the hotspot and the join when it's done
  // (bootWifiFinish) - the face is up meanwhile, the hotspot a few seconds later on its final channel.
  WiFi.setAutoReconnect(false);
  bootWifiPending = true;  // (loop starts the scan: started here it fails at once - the radio isn't ready)

  applyUtcOffset(prefs.getInt("tzOff", 0), false);  // (the saved timezone); the clock for night / morning play picks
  const unsigned long tWw0 = millis();
  ww::begin();  // the wake word (the model from the SD card)
  const unsigned long tWw1 = millis();
  xTaskCreatePinnedToCore(linkCheckTask, "linkchk", 3072, nullptr, 1, nullptr, 0);  // (WiFi: a dead link reconnects)
  startTlsServer();  // https://<robot>/ : the controller again, secure (the phone's mic works there)
  const unsigned long tTls = millis();
  startServer();
  bootInfo.finished = true;  // the boot screen: the eyes blink shut, the face takes over
  xlogSys("BOOT ready at %.1f s (wake word %.1f s, https %.1f s; the WiFi scan goes on)", millis() / 1000.0f,
          (tWw1 - tWw0) / 1000.0f, (tTls - tWw1) / 1000.0f);
  // (the "net" build: these two stacks in PSRAM - internal RAM for the camera next to a conversation;
  // neither writes to flash, which a PSRAM stack can't)
  static gem::PsTask psVws, psDec;
  gem::taskStart(psVws, vwsTask, "vws", 3072, 1, 0);
  
  // Start the face animation
  enterIdle();

  // Camera task idles until the web toggle switches to face tracking or explore
  jpgMutex = xSemaphoreCreateMutex();
  jpgFrame = (uint8_t*)heap_caps_malloc(JPG_MAX, MALLOC_CAP_SPIRAM);
  photoBuf = (uint8_t*)heap_caps_malloc(JPG_MAX, MALLOC_CAP_SPIRAM);
  xTaskCreatePinnedToCore(photoTask, "photo", 3072, nullptr, 1, &photoTaskHandle, 0);  // (~0.9 KB used)
  decIn = (uint8_t*)heap_caps_malloc(DEC_IN_MAX, MALLOC_CAP_SPIRAM);
  decBuf[0] = (uint8_t*)heap_caps_malloc(RAW_BYTES, MALLOC_CAP_SPIRAM);
  decBuf[1] = (uint8_t*)heap_caps_malloc(RAW_BYTES, MALLOC_CAP_SPIRAM);
  decFront = decBuf[0];
  decMutex = xSemaphoreCreateMutex();
  gem::taskStart(psDec, decoderTask, "jpgdec", 8192, 1, 1);
  decTaskHandle = psDec.h;
  startStreamServer();
  // the camera task's stack in PSRAM (it never writes the flash; internal RAM is for DMA)
  static gem::PsTask psVision;
  if (!gem::taskStart(psVision, visionTask, "Vision", 6144, 1, 0)) xTaskCreatePinnedToCore(visionTask, "Vision", 6144, NULL, 1, NULL, 0);
  fid::begin();  // who is this (the model loads 20 s later)
  xTaskCreatePinnedToCore(serialCmdTask, "serialCmd", 3072, NULL, 1, NULL, 0);  // SD uploads over USB
  xTaskCreate(webWatchdogTask, "webdog", 3072, NULL, 1, NULL);  // (~1.3 KB used)
  // The gait loop above the face (both on core 1: at the same priority the face's frames delay the steps
  // and the turns dip). loop() sleeps 10 ms a
  // pass, so the face still draws then
  vTaskPrioritySet(NULL, 2);
  xl::recording = false;  // boot lines are in; from now on only Explore is logged
}

// Battery empty: the TFT says so (drawn into the face sprite; the camera task shows it)
void showBatteryScreen() {
  if (!spriteMutex) return;
  xSemaphoreTake(spriteMutex, portMAX_DELAY);
  faceFront = &sprite;  // the battery screen is drawn into the first buffer
  sprite.fillScreen(TFT_BLACK);
  sprite.drawRoundRect(100, 50, 110, 56, 8, TFT_WHITE);
  sprite.drawRoundRect(101, 51, 108, 54, 7, TFT_WHITE);
  sprite.fillRect(210, 66, 10, 24, TFT_WHITE);
  sprite.fillRect(106, 56, 14, 44, TFT_RED);
  sprite.setTextSize(3);
  sprite.setTextColor(TFT_RED);
  sprite.setCursor(61, 130);
  sprite.print("BATTERY LOW");
  sprite.setTextSize(2);
  sprite.setTextColor(TFT_WHITE);
  sprite.setCursor(64, 175);
  sprite.print("please charge me");
  faceDirty = true;
  xSemaphoreGive(spriteMutex);
}

// ---- Play scenes: now and then, when it's idle, the robot plays by itself (a fly, a book, the
// piano ... see face_scenes.h). The face task runs the scene; here its head moves and sounds.
// A sound for a scene or an emote: "w:name|program" plays /sfx/name.wav from the card (a real
// sound file) or, if that isn't there, the synth program after the "|"
void playSfx(const char* prog) {
  if (!prog || !*prog || aud::talkOn) return;
  if (!strncmp(prog, "w:", 2)) {
    const char* bar = strchr(prog, '|');
    char path[48];
    snprintf(path, sizeof(path), "/sfx/%.*s.wav", (int)(bar ? bar - prog - 2 : strlen(prog) - 2), prog + 2);
    if (sdSizeMB >= 0 && SD_MMC.exists(path)) {
      if (aud::playing && !strncmp(aud::playPath, "/sfx/", 5)) {  // the last effect: cut it short
        aud::stopReq = true;
        for (int i = 0; i < 20 && aud::playing; i++) delay(5);
      }
      // at the volume the scene asked for ("w:page|v0.2 ..."): at one volume a soft page
      // turn or a sneeze comes out as a sudden blast. A face's voice clip (no "|v"): 0.6.
      float vol = 0.6f;
      const char* v = bar ? strstr(bar, "|v") : nullptr;
      if (v) vol = constrain(atof(v + 2) * 1.3f, 0.1f, 0.9f);
      aud::play(path, vol);
      return;
    }
    if (!bar) return;
    prog = bar + 1;
  }
  aud::sfx(prog);
}

// The robot's little voice (EMO style: squeaky calls, no words): when a face is
// picked - an emote, the page, a command - it says something that fits. At most one every 2.5 s.
// The props each emote shows (scene engine, overlay: the emote keeps its own face and head)
const char* emoteProps(const char* cmd) {
  static const struct { const char* cmd; const char* props; } M[] = {
    {"wave", "o:wave"}, {"wave2", "o:wave"}, {"dance", "o:notes"}, {"jam", "o:notes"}, {"proud", "o:sparkle"}, {"kiss", "o:sparkle"}, {"swim", "o:bubbles"}, {"pushup", "o:sweat"}, {"bow", "o:sparkle"},
    {"cute", "o:heart"}, {"love", "o:heart"}, {"patted", "o:heart"}, {"crabrave", "o:crab"}, {"musicdance", "o:notes"}, {"musicdance", "o:notes"}, {"crabrave1", "o:crab"}, {"petting", "o:heart"}, {"pat_nuzzle", "o:heart"}, {"shake", "o:shake"}, {"shrug", "o:shrug"},
    {"dead", "o:ghost"}, {"crab", "o:crab"}, {"point", "o:point"}, {"think", "o:idea"}, {"peekaboo", "o:peekaboo"},
    {"hiccup", "o:hiccups"}, {"sleepy", "o:dream"}, {"gocrazy", "o:sparkle"}, {"freaky", "o:sparkle"}, {"curious", "o:shrug"},
  };
  for (const auto& m : M) if (!strcmp(cmd, m.cmd)) return m.props;
  return nullptr;
}

// Every emote's own sound when it starts ("w:name|synth": the cute voice WAV from /sfx, a chip-tune if it's missing).
// Not here: the ones that make their own (sneeze: ah-ah-achoo; the music dances play songs) and walking.
const char* emoteSound(const char* cmd) {
  static const struct { const char* cmd; const char* snd; } M[] = {
    {"rest", "w:v_yawn|600~420:300"}, {"sit", "w:v_chat1|700~900:120"}, {"sit_elegant", "w:v_chat3|660~740:160 0:40 740~660:160"},
    {"stand", "w:v_chat2|500~800:140"}, {"wave", "w:v_hi|700~1000:140 0:50 900~1200:160"}, {"wave2", "w:v_hi|700~1000:140 0:50 900~1200:160"},
    {"dance", "w:v_yay|660~990:150 0:60 880~1320:200"}, {"swim", "w:bubble_pop|300~500:90 0:40 350~550:90 0:40 400~600:90"},
    {"point", "w:v_ooh|600~900:220"}, {"pushup", "w:v_chat1|250~200:150 0:150 250~200:150"}, {"bow", "w:v_tada|523:120 659:120 784:260"},
    {"cute", "w:v_hehe|900~1200:90 0:50 1000~1300:90"}, {"freaky", "w:v_eep|1200~400:300"}, {"worm", "w:v_hehe|500~700:100 0:40 600~800:100"},
    {"shake", "w:v_uhuh|400:80 500:80 400:80"}, {"shrug", "w:v_huh|500~700:200"}, {"dead", "w:lose|500~200:600"},
    {"crab", "w:v_hehe|800:70 0:30 800:70 0:30 900:90"}, {"gocrazy", "w:v_wow|400~1500:250 1500~400:250"},
    {"nodyes", "w:v_chat1|700~900:120"}, {"nodno", "w:v_uhuh|500~380:140 0:40 500~380:140"}, {"patted", "w:v_hehe|900~1200:90 0:50 1000~1300:90"},
    {"curious", "w:v_hmm|440~520:220"}, {"happy", "w:v_yay|660~990:150 0:60 880~1320:200"}, {"excited", "w:v_wow|500~1200:250"},
    {"stretch", "w:v_yawn|500~350:500"}, {"sleepy", "w:v_yawn|500~350:500"}, {"scared", "w:v_eep|1400~900:180"},
    {"love", "w:v_ooh|500~700:300"}, {"angry", "w:v_grr|150:300"}, {"sad", "w:v_aww|520~380:250 0:80 440~330:300"},
    {"think", "w:v_hmm|440~470:300"}, {"peekaboo", "w:v_hehe|900~1200:90 0:50 1000~1300:90"}, {"dizzy", "w:v_huh|600~400:150 400~600:150 600~400:150"},
    {"hiccup", "w:hic|900~1300:60 0:400 900~1300:60"}, {"yawn", "w:v_yawn|500~350:500"}, {"shiver", "w:v_eep|300:40 320:40 300:40 320:40 300:40"},
    {"celebrate", "w:party|523:120 659:120 784:260"}, {"tap", "w:v_uhuh|300:50 0:150 300:50 0:150 300:50"},
    {"bounce", "w:v_yay|400~800:120 0:40 400~800:120"}, {"wiggle", "w:v_hehe|700~900:80 900~700:80 700~900:80"},
    {"sniff", "w:v_hmm|n:80 0:80 n:80 0:80 n:80"}, {"laugh", "w:giggle|900~1200:90 0:50 1000~1300:90 0:50 900~1200:90"},
    {"pounce", "w:jump|300~1200:200"}, {"lookaround", "w:v_hmm|440~520:220"}, {"headbang", "w:v_yay|200:80 0:120 200:80 0:120 200:80"},
    {"faint", "w:v_uhoh|900~200:700"}, {"twirl", "w:whoosh|500~1100:400"}, {"yoga", "w:v_ooh|330~350:700"},
    {"sway", "w:v_chat3|440~520:300 520~440:300"}, {"robot", "w:v_chat_q|880:90 660:90 990:90 0:60 1320:120"},
    {"tada", "w:v_tada|523:120 659:120 784:260"}, {"playbow", "w:v_hehe|700~1000:120 0:50 800~1100:120"}, {"huh", "w:v_huh|500~750:200"},
    {"scratch", "w:v_uhuh|n:60 0:60 n:60 0:60 n:60"}, {"shakeoff", "w:whoosh|z:400"}, {"knead", "w:v_chat3|180:600"},
    {"beg", "w:v_aww|700~900:300"}, {"loaf", "w:snore|400~300:500"}, {"swat", "w:v_grr|300~800:90"}, {"startle", "w:v_eep|1400~900:180"},
    {"pat_boop", "w:v_hehe|900~1200:90"}, {"pat_nuzzle", "w:v_ooh|500~700:300"}, {"petting", "w:v_chat3|180:600"},
{"shy", "w:v_hehe|900~1200:90"}, {"hide", "w:v_eep|1400~900:180"}, {"sneaky", "w:v_hmm|330~360:300"}, {"proud", "w:v_tada|523:120 659:120 784:260"}, {"bored", "w:sigh|v0.16 440~300:500"}, {"jam", "w:v_yay|660~990:150 0:60 880~1320:200"}, {"sulk", "w:v_uhuh|500~380:200"}, {"kiss", "w:v_ooh|500~700:300"},
{"stomp", "w:v_uhuh|500~380:200"}, {"swaydance", "w:v_yay|660~990:150 0:60 880~1320:200"}, {"bob", "w:v_hehe|900~1200:90"}, {"peek", "w:v_hmm|330~360:300"}, {"doggyshake", "w:v_hehe|900~1200:90"}, {"bowwiggle", "w:v_yay|660~990:150 0:60 880~1320:200"}, {"highfive", "w:v_tada|523:120 659:120 784:260"}, {"march", "w:v_tada|523:120 659:120 784:260"}, {"hula", "w:v_yay|660~990:150 0:60 880~1320:200"}, {"bodynod", "w:v_hehe|900~1200:90"}, {"twistlook", "w:v_hmm|330~360:300"}, {"squats", "w:v_uhuh|500~380:200"}, {"shimmy", "w:v_hehe|900~1200:90"}, {"itchkick", "w:v_eep|1400~900:180"},
        {"pat_wake", "w:v_yawn|500~350:500"}, {"pat_hello", "w:v_hi|700~1000:140 0:50 900~1200:160"}, {"pat_enough", "w:v_uhuh|500~380:200"},
  };
  for (const auto& m : M) if (!strcmp(cmd, m.cmd)) return m.snd;
  return xEmoteSound(cmd);  // (the library's own)
}

const char* faceVoice(const char* face) {
  static const struct { const char* face; const char* snd; } M[] = {
    {"happy", "v_yay,v_chat1,v_hehe"}, {"excited", "v_wow,v_yay"}, {"joy", "v_yay"}, {"celebrate", "v_tada"}, {"proud", "v_tada"},
    {"success", "v_tada"}, {"party", "v_tada,v_yay"}, {"surprised", "v_wow"}, {"amazed", "v_wow,v_ooh"}, {"starstruck", "v_ooh"},
    {"awe", "v_ooh"}, {"love", "v_ooh"}, {"adoring", "v_ooh"}, {"lovestruck", "v_ooh"}, {"kiss", "v_ooh"}, {"curious", "v_hmm,v_chat_q"},
    {"thinking", "v_hmm"}, {"question", "v_huh,v_chat_q"}, {"confused", "v_huh"}, {"suspicious", "v_hmm"}, {"sad", "v_aww,v_chat_sad"},
    {"pout", "v_uhuh"}, {"sulking", "v_uhuh"}, {"stubborn", "v_uhuh"}, {"puffed", "v_uhuh"}, {"laugh", "v_hehe"}, {"giggle", "v_hehe"},
    {"ehehe", "v_hehe"}, {"playful", "v_hehe,v_chat2"}, {"cheeky", "v_hehe"}, {"mischievous", "v_hehe"}, {"scared", "v_eep"},
    {"shocked", "v_eep"}, {"startled", "v_eep"}, {"terrified", "v_eep"}, {"alarm", "v_eep"}, {"angry", "v_grr"}, {"furious", "v_grr"},
    {"grumpy", "v_grr"}, {"annoyed", "v_uhuh"}, {"sleepy", "v_yawn"}, {"yawn", "v_yawn"}, {"tired", "v_yawn"}, {"yum", "v_yum"},
    {"hungry", "v_yum"}, {"wave", "v_hi"}, {"hello", "v_hi"}, {"cute", "v_hehe,v_chat3"}, {"dance", "v_yay,v_chat3"},
    {"shy", "v_hehe"}, {"embarrassed", "v_eep"}, {"uh_oh", "v_uhoh"}, {"worried", "v_uhoh"}, {"nervous", "v_uhoh"},
  };
  char n[40];
  const char* f = !strncmp(face, "talk_", 5) ? face + 5 : face;
  strlcpy(n, f, sizeof(n));
  const size_t L = strlen(n);
  if (L > 5 && !strcmp(n + L - 5, "_mild")) n[L - 5] = 0;
  else if (L > 4 && !strcmp(n + L - 4, "_max")) n[L - 4] = 0;
  for (const auto& m : M) if (!strcmp(n, m.face)) return m.snd;
  return nullptr;
}

const char* perfNow() {
  if (otaActive) return "update";
  if (aud::talkOn) return "talk";
  if (rcActive()) return "rc";
  if (activeMode == MODE_EXPLORE) return "explore";
  if (activeMode == MODE_FACE) return "face tracking";
  if (xl::httpBusy) return "busy";
  return "idle";
}

void playUpdate() {
  const unsigned long now = millis();
  perfName = perfNow();
  // Governor: a play scene gives way at once to driving, talking, an update
  if (faceScenePlaying && strcmp(perfName, "idle") != 0) faceScene("stop");  // (an emote's props aren't a scene: they stay)
  const bool freeNow = activeMode == MODE_WALK && requestedMode == MODE_WALK && faceShowing && currentCommand == "" &&
                       !rcActive() && !rcHeadActive && !(headManualMs && now - headManualMs < 20000) && !camViewWanted && !calDirty && !aud::talkOn && !otaActive;
  // the head: follows the scene (eased, at most 150 deg/s), then back to the middle
  static bool headWas = false;
  static float hp = 0, ht = 0;
  static unsigned long lastMs = 0;
  const float dt = min(now - lastMs, 100UL) / 1000.0f;
  lastMs = now;
  // desk mode's scenes (the brain starts them): their head moves and sounds too
  const bool deskScene = activeMode == MODE_DESK && faceShowing && currentCommand == "" && !rcActive() && !aud::talkOn &&
                         !otaActive && faceScenePlaying;
  const bool headFree = (freeNow || deskScene) && !chickenOn;  // (the chicken head has it)
  const bool want = headFree && faceHeadOn;
  if (!headFree) headWas = false;  // someone else has the head
  else if (want || headWas) {
    headSlewCancel();  // the scene has the head
    static float vp = 0, vt = 0;
    if (!headWas) { hp = emoPanNow; ht = emoTiltNow; vp = vt = 0; }
    const float tp = constrain(want ? PAN_MID + faceHeadX * PLAY_HEAD_DEG * (panPlusIsLeft ? 1 : -1) : PAN_MID, PAN_MIN + 3, PAN_MAX - 3);
    const float tt = constrain(want ? TILT_MID + faceHeadY * PLAY_HEAD_DEG : TILT_MID, TILT_MIN + 3, TILT_MAX - 6);  // off the hard stops
    // A critically damped spring with speed and acceleration limits: it glides after a darting fly
    // instead of reversing at full speed (that makes the pan servo jerk). Sub-degree writes.
    auto follow = [&](float& x, float& v, float target) {
      const float w = 6;  // (stiffer settings read as random jerks at idle)
      const float a = constrain(w * w * (target - x) - 2 * w * v, -300.0f, 300.0f);
      v = constrain(v + a * dt, -60.0f, 60.0f);
      x += v * dt;
    };
    follow(hp, vp, tp);
    follow(ht, vt, tt);
    static float sentP = -999, sentT = -999;  // only when it moved (the I2C bus is shared)
    // only a real step (the servo board has 0.4 deg steps; smaller changes flip between two
    // of them and the servo hunts: jitter), and near the target only when it's settled
    panWho = "scene";
    if (fabsf(hp - sentP) >= 0.45f || (fabsf(vp) < 1 && fabsf(hp - sentP) >= 0.2f)) { setHeadServoF(HEAD_PAN_PIN, hp); sentP = hp; }
    if (fabsf(ht - sentT) >= 0.45f || (fabsf(vt) < 1 && fabsf(ht - sentT) >= 0.2f)) { setHeadServoF(HEAD_TILT_PIN, ht); sentT = ht; }
    emoPanNow = (int)lroundf(hp);
    emoTiltNow = (int)lroundf(ht);
    headWas = want || fabsf(hp - PAN_MID) > 0.6f || fabsf(ht - TILT_MID) > 0.6f;
  }
  // its sounds
  char sf[320];
  if (faceTakeSfx(sf, sizeof(sf)) && playSound && (freeNow || deskScene)) playSfx(sf);
  // its voice: a face was just picked
  static unsigned long voicedSetMs = 0, lastVoiceMs = 0;
  if (faceSetMs != voicedSetMs) {
    voicedSetMs = faceSetMs;
    const char* opts = faceVoice(currentFaceName);
    if (opts && playSound && activeMode == MODE_WALK && !faceScenePlaying && !aud::talkOn && now - lastVoiceMs > 2500 &&
        now - voiceLastMs > 2500) {
      char pick[16] = "";
      int n = 1;
      for (const char* c = opts; *c; c++) n += *c == ',';
      int k = random(n);
      const char* c = opts;
      while (k-- > 0) c = strchr(c, ',') + 1;
      snprintf(pick, sizeof(pick), "%.*s", (int)(strchr(c, ',') ? strchr(c, ',') - c : strlen(c)), c);
      char prog[24];
      snprintf(prog, sizeof(prog), "w:%s", pick);
      playSfx(prog);
      lastVoiceMs = voiceLastMs = now;
    }
  }
  // hello: once, a few seconds after it wakes up
  static bool saidHi = false;
  if (!saidHi && now > 6000 && faceShowing) {
    saidHi = true;
    if (playSound) playSfx("w:v_hi");
  }
  // start one by itself: idle for a random 0.5 .. 1.5 x PLAY_EVERY_S
  static unsigned long idleMs = 0, waitMs = 0;
  // (not within 30 s of a face being picked - on the page, by an emote - it isn't idle then)
  if (!freeNow || !playAuto || PLAY_EVERY_S <= 0 || faceScenePlaying || now - faceSetMs < 30000 || batEmpty) {
    idleMs = 0;
    playInS = -1;
  } else if (!idleMs) {
    idleMs = now;
    waitMs = (unsigned long)(PLAY_EVERY_S * 1000UL * (0.4f + 0.8f * random(1000) / 1000.0f));  // (mostly short moments)
  } else if (now - idleMs <= waitMs) {
    playInS = (long)((waitMs - (now - idleMs)) / 1000);
  } else if (now - idleMs > waitMs) {
    idleMs = 0;
    faceScene("random");
  }
  // the local hour for the play picks (night / morning); the clock comes from the internet
  static unsigned long hourMs = 0;
  if (now - hourMs > 10000) {  // (10 s: the clock scene shows the minute)
    hourMs = now;
    struct tm tmv;
    if (getLocalTime(&tmv, 5)) {
      faceEng.hourNow = tmv.tm_hour;
      faceEng.minuteNow = tmv.tm_min;
      faceEng.dayNow = tmv.tm_mday;  // (StandBy's calendar)
      faceEng.monthNow = tmv.tm_mon + 1;
      faceEng.yearNow = tmv.tm_year + 1900;
      faceEng.wdayNow = tmv.tm_wday;
    }
  }
}

#include "oled_ui.h"  // the chest display (uses the state above)
const char* oledKindName() { return OLED_KIND_NAMES[oledKindNow]; }

// ---- The governor's log: /logs/governor.csv on the card - every change of what it gives priority
// to (with what it did about it) and every 10 s how the robot is coping. For assessing it later.
// Written by the log task (xlog.h sdAppend): the loop never waits for the card.
void govLog(const char* kind, const char* note) {
  // the chat's and the WiFi's events in the main log too (/log): the governor log is only on the card, so
  // if the card breaks they'd leave no trace
  if (!strcmp(kind, "chat") || !strcmp(kind, "wifi")) xlogSys("%s %s", !strcmp(kind, "chat") ? "CHAT" : "WIFI", note);
  if (sdSizeMB < 0) return;
  char line[320];
  snprintf(line, sizeof(line), "%.1f,%s,%s,%.1f,%.1f,%d,%.1f,%d,%.1f,%.1f,%u,%u,%d,%d,%s,%d,%d,%s\n", millis() / 1000.0f, kind,
           perfName, (float)govLoopMaxMs, (float)govLoopAvgMs, (int)govJanks, (float)faceFpsNow, (int)faceFpsWanted,
           (float)faceDrawMs, (float)streamFps, ESP.getFreeHeap() / 1024, ESP.getMinFreeHeap() / 1024,
           WiFi.status() == WL_CONNECTED ? (int)WiFi.RSSI() : 0, WiFi.softAPgetStationNum(), oledKindName(),
           faceScenePlaying ? 1 : 0, aud::talkOn ? 1 : 0, note);
  sdAppend("/logs/governor.csv",
           "uptime_s,kind,perf,loop_max_ms,loop_avg_ms,janks,face_fps,face_fps_cap,draw_ms,stream_fps,heap_kb,min_heap_kb,rssi,ap_users,oled,scene,talk,note",
           line);
}

// ---- The OLED's health. It can go blank while the robot runs on:
// a dip on the 5 V rail resets its controller (display off, charge pump off), or a glitch leaves
// the I2C bus held. Every 2 s: does it answer? Every 20 s its setup goes again (the same values:
// no flicker; after a reset it comes back). No answer 3 times: the bus is freed (9 clocks + a
// stop) and started again. All I2C is on loop()'s core, so this is safe here.
void oledSetupAgain() {
  static const uint8_t cmds[] = {0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8,
                                 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF};
  for (uint8_t c : cmds) oled.ssd1306_command(c);
}

void i2cFreeBus() {
  Wire.end();
  pinMode(I2C_SDA, INPUT_PULLUP);
  pinMode(I2C_SCL, OUTPUT_OPEN_DRAIN);
  for (int i = 0; i < 9; i++) {  // a device stuck mid-byte lets go of SDA after at most 9 clocks
    digitalWrite(I2C_SCL, LOW);
    delayMicroseconds(6);
    digitalWrite(I2C_SCL, HIGH);
    delayMicroseconds(6);
  }
  pinMode(I2C_SDA, OUTPUT_OPEN_DRAIN);  // a stop: SDA low -> high while SCL is high
  digitalWrite(I2C_SDA, LOW);
  delayMicroseconds(6);
  digitalWrite(I2C_SDA, HIGH);
  delayMicroseconds(6);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setTimeOut(25);
  i2cSpeedRestore();
}

// The servo board (PCA9685) after a dip on its supply resets: asleep, every output off - the head
// hangs down, the legs go limp. Its MODE1 says so (SLEEP set,
// auto-increment gone): set it up again and put every servo back where it was.
void pcaHealth() {
  Wire.beginTransmission(0x40);
  Wire.write(0x00);  // MODE1
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom((uint8_t)0x40, (uint8_t)1) != 1) return;
  const uint8_t mode1 = Wire.read();
  if (!(mode1 & 0x10) && (mode1 & 0x20)) return;  // awake, set up by us (auto-increment on): fine
  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(50);
  if (legsPowered)
    for (int i = 0; i < 8; i++) {
      const int a = currentAngle[i];
      setServoAngle(i, a);
    }
  panWho = "board-reset";
  setHeadServoAngle(HEAD_PAN_PIN, emoPanNow);
  setHeadServoAngle(HEAD_TILT_PIN, emoTiltNow);
  pcaResets++;
  char note[72];
  snprintf(note, sizeof(note), "servo board had reset (MODE1 0x%02X): set up again, positions restored", mode1);
  govLog("i2c", note);
  xlogSys("I2C %s", note);
}

void oledHealth() {
  static unsigned long lastMs = 0, setupMs = 0;
  const unsigned long now = millis();
  if (now - lastMs < 2000) return;
  lastMs = now;
  pcaHealth();
  Wire.beginTransmission(0x3C);
  const uint8_t err = Wire.endTransmission();
  if (err == 0) {
    if (oledFails >= 3) govLog("i2c", "OLED answers again");
    // set up again every 20 s - and every 2 s for the first 30 s after a start: power-up noise (the
    // servos waking) sometimes scrambles its settings, and the picture sits too low until the next set-up
    if (oledFails >= 3 || now - setupMs > (now < 30000 ? 2000UL : 20000UL)) {
      setupMs = now;
      oledSetupAgain();
    }
    oledFails = 0;
    return;
  }
  if (++oledFails % 3 == 0) {  // 6 s without an answer (and every 6 s after)
    char note[64];
    snprintf(note, sizeof(note), "OLED not answering (I2C error %u): freeing the bus", err);
    govLog("i2c", note);
    xlogSys("I2C %s", note);
    i2cFreeBus();
    oledSetupAgain();
    oledRecoveries++;
  }
}

// Every loop: its period (from one start to the next) into the 10 s window; changes and the
// window go to the log
void govTick() {
  static unsigned long lastUs = 0, winMs = 0;
  static float maxMs = 0, sumMs = 0;
  static int n = 0, janks = 0;
  static const char* lastPerf = "";
  static bool booted = false;
  static char worst[72] = "";
  static unsigned long jankLogMs = 0;
  static float emoteMs = 0;
  if (i2cSlowNote[0]) {
    govLog("i2c", i2cSlowNote);
    i2cSlowNote[0] = 0;
  }
  static int viewersWas = 0;  // the video's viewers coming and going (a page reconnecting shows up here)
  if (streamClients != viewersWas) {
    char n[40];
    snprintf(n, sizeof(n), "video viewers %d -> %d", viewersWas, (int)streamClients);
    viewersWas = streamClients;
    if (millis() > 15000) govLog("cam", n);
  }
  govMark(GS_WAIT);  // the pause at the end of the last loop (other tasks ran)
  const unsigned long us = micros();
  if (lastUs) {
    const float ms = (us - lastUs) / 1000.0f;
    int w = 0;
    for (int i = 1; i < GS_N; i++) if (gsMs[i] > gsMs[w]) w = i;
    if (ms > 60 && w == GS_CMDS) {
      emoteMs += ms;  // an emote / a walk runs as one long step by design: its time, not a jank
    } else if (ms > 60) {
      janks++;
      if (ms > maxMs) snprintf(worst, sizeof(worst), "worst loop %.0f ms: %s %.0f ms", ms, GS_NAMES[w], gsMs[w]);
      // a long stall gets a line of its own at once (at most one every 5 s; emotes run for seconds)
      if (ms > 250 && millis() - jankLogMs > 5000 && millis() > 15000) {
        jankLogMs = millis();
        char note[72];
        snprintf(note, sizeof(note), "loop %.0f ms: %s %.0f ms", ms, GS_NAMES[w], gsMs[w]);
        govLog("jank", note);
      }
    }
    if (!(ms > 60 && w == GS_CMDS)) maxMs = max(maxMs, ms);
    sumMs += ms;
    n++;
  }
  for (int i = 0; i < GS_N; i++) gsMs[i] = 0;
  lastUs = us;
  // Internal RAM (the camera, the speaker's DMA, task stacks, WiFi need it; PSRAM can't stand in):
  // the window's lowest, and a line at once when it gets low (running out freezes the robot)
  static size_t intMin = SIZE_MAX;
  static unsigned long memLogMs = 0;
  const size_t intFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  if (intFree < intMin) intMin = intFree;
  if (intFree < 16 * 1024 && millis() - memLogMs > 5000 && millis() > 15000) {
    memLogMs = millis();
    char note[72];
    snprintf(note, sizeof(note), "internal RAM low: %u KB free (largest block %u KB)", (unsigned)(intFree / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
    govLog("mem", note);
  }
  const unsigned long now = millis();
  if (!booted && now > 15000) {
    booted = true;
    govLog("boot", "governor log started");
  }
  if (strcmp(lastPerf, perfName) != 0) {
    char note[80];
    const char* did = !strcmp(perfName, "rc") ? "scenes stopped; face 12 fps; OLED 1 Hz"
                    : !strcmp(perfName, "talk") ? "scenes stopped; face 15 fps; OLED 1 Hz"
                    : !strcmp(perfName, "update") ? "everything paused" : !strcmp(perfName, "idle") ? "scenes allowed; full face fps" : "scenes off";
    snprintf(note, sizeof(note), "from %s: %s", lastPerf[0] ? lastPerf : "start", did);
    lastPerf = perfName;
    if (booted) govLog("change", note);
  }
  if (now - winMs >= 10000) {
    govLoopMaxMs = maxMs;
    govLoopAvgMs = n ? sumMs / n : 0;
    govJanks = janks;
    strlcpy(govWorst, worst, sizeof(govWorst));
    maxMs = sumMs = 0;
    n = janks = 0;
    winMs = now;
    char note[200];
    char ap[96] = "";
    char phy[4] = "-";
    const int ar = apStaRssi(phy);
    if (ar) {
      const uint32_t n = phN, lost = phLost;
      snprintf(ap, sizeof(ap), "; phone %d dBm 11%s, frames %.0f ms, ping %u/%u ms lost %u/%u", ar, phy, (float)videoSendMs,
               (unsigned)(n ? phSum / n : 0), (unsigned)phMax, (unsigned)lost, (unsigned)(n + lost));
    }
    phN = phLost = phSum = phMax = 0;
    snprintf(note, sizeof(note), "internal min %u KB%s%s%s%s", (unsigned)(intMin / 1024), worst[0] ? "; " : "", worst,
             emoteMs > 0 ? "; emotes / walking" : "", ap);
    emoteMs = 0;
    if (booted) govLog("stat", note);
    worst[0] = 0;
    intMin = SIZE_MAX;
  }
}

// Fallen over (any mode): tilted past the fall angle for 0.8 s -> stop, legs off (no straining on
// its side), a dizzy face, "Help!" on the OLED. Upright again for 2 s -> "Thanks".
void fallUpdate() {
  static unsigned long tiltSince = 0, uprightSince = 0;
  if (!imuOk) return;
  const unsigned long now = millis();
  // The tilt NOW (the gyro-fed "up", chkG, against standing on the floor): the 0.8 s-smoothed imuTiltDeg plus a
  // 0.8 s wait notices a fall only ~1.5-2 s after it happens. Fast tilt past the angle for 0.25 s, or the
  // smooth one for 0.8 s (kept as a backup).
  // Against what "standing" means: gravBase (taken at the start / Explore's start), unless that looks wrong - switched
  // on while held tilted, gravBase is tilted and upright then read as a fall (or a fall as upright). The IMU sits
  // upside down in the body (z ~ -1 g standing), so the body's own up is the fallback.
  float ref[3] = {0, 0, gravBase[2] >= 0 ? 1.0f : -1.0f};  // (the sign from the reading itself: no mounting assumed)
  {
    const float gb = sqrtf(gravBase[0] * gravBase[0] + gravBase[1] * gravBase[1] + gravBase[2] * gravBase[2]);
    if (gb > 0.1f && fabsf(gravBase[2]) / gb > 0.906f)  // within ~25 deg of the body's up: the real floor's tilt, keep it
      for (int i = 0; i < 3; i++) ref[i] = gravBase[i] / gb;
  }
  auto angleTo = [&](const float v[3]) {
    const float n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (n < 0.1f) return 0.0f;
    return acosf(constrain((v[0] * ref[0] + v[1] * ref[1] + v[2] * ref[2]) / n, -1.0f, 1.0f)) * 57.2958f;
  };
  const float fastTilt = angleTo(chkG);       // gyro-fed: at once
  const float slowTilt = angleTo(gravSlow);   // ~0.8 s smoothed (the backup rule's)
  // The fall angle by direction: on its side it lies flat (past 90), on its nose or back the head/tail props it up
  // at ~40-50 deg (one 55 deg limit would catch forward / back falls late). y = pitch.
  float fallDeg = EX_FALL_DEG;
  {
    const float n = sqrtf(chkG[0] * chkG[0] + chkG[1] * chkG[1] + chkG[2] * chkG[2]);
    if (n > 0.1f) {
      const float dx = chkG[0] / n - ref[0], dy = chkG[1] / n - ref[1];
      const float h2 = dx * dx + dy * dy;
      if (h2 > 1e-4f) fallDeg = EX_FALL_DEG + (EX_FALL_PITCH_DEG - EX_FALL_DEG) * (dy * dy / h2);
    }
  }
  // The fast one alone overshoots when picked up / tilted by hand quickly (the gyro rate times a loop stall: a
  // small tilt can read 65-78 deg). It counts only with the ~0.3 s accelerometer agreeing.
  const float accTilt = angleTo(gravLp);
  const bool fast = fastTilt > fallDeg && accTilt > fallDeg - 10;
  if (fast || slowTilt > fallDeg) {
    uprightSince = 0;
    if (!tiltSince) tiltSince = now;
    if (!robotFallen && now - tiltSince > (fast ? 400UL : 800UL)) {
      robotFallen = true;
      xlogSys("FALL tilted %.0f deg (fast %.0f): legs off, asking for help", slowTilt, fastTilt);
      govLog("fall", "fell over: stopped, legs off");
      currentCommand = "";
      if (requestedMode == MODE_EXPLORE) requestedMode = MODE_WALK;
      if (legsPowered) legsRelax();
      legsPoseKnown = false;  // who knows how it lies: the next move goes straight to its target
      setFaceNamed("dizzy", true);
      if (playSound) playSfx("w:v_uhoh|v0.3 700~200:320");
    }
  } else {
    tiltSince = 0;
    if (robotFallen && slowTilt < 25) {
      if (!uprightSince) uprightSince = now;
      if (now - uprightSince > 2000) {
        robotFallen = false;
        thanksUntilMs = now + 4000;
        xlogSys("FALL upright again");
        govLog("fall", "upright again");
        setFaceNamed("relieved", true);
        if (playSound) playSfx("w:v_yay|v0.3 523:90 659:90 784:200");
      }
    }
  }
}

// Charging, from the voltage alone (the charger has no wire to the ESP):
//  - plugged in: the voltage jumps (the charge current through the pack, 0.15+ V within 20 s),
//    or climbs steadily (0.07 V over 4 minutes and still climbing in the last 2)
//  - unplugged: it drops 0.12 V within 20 s, or falls 0.03 V over 2 minutes
// Only once the legs have been off for a minute (their load comes and goes and fakes both) and
// not in the first 3 minutes after a start (the settling readings fake "charging" after
// every restart).
void chargeUpdate() {
  static float hist[16];  // the slow average every 15 s: 4 minutes
  static float fastHist[5];  // the ~2 s filtered voltage every 5 s: 20 s
  static int n = 0, idx = 0, fn = 0, fi = 0;
  static unsigned long ms = 0, fastMs = 0, legsOffMs = 0;
  const unsigned long now = millis();
  // (desk mode holds the sit pose all the time: 20 s without a body move counts as quiet legs, or the
  // charger would never be noticed there)
  const bool legsQuiet = !legsPowered || (activeMode == MODE_DESK && deskm::bodyQuietMs() > 20000);
  if (!legsQuiet) {
    // moving: it can't tell charging under the legs' load - and "charging" left over from before the
    // charger came off would keep desk mode asleep and the empty-pack power-down from ever firing
    // (20 s of it: a desk move on the charger - a pat's reaction - doesn't count)
    static unsigned long activeSince = 0;
    if (legsOffMs || !activeSince) activeSince = now;
    if (batCharging && now - activeSince > 20000) { batCharging = false; batFull = false; govLog("charge", "legs moving: charging state cleared"); }
    legsOffMs = 0;
  }
  else if (!legsOffMs) legsOffMs = now;
  // (the slow climb's history starts 30 s after a start - the slow average has settled by then. The
  // quick plug-in jump still waits 3 min: the settling readings would fake one after every restart.)
  if (!legsOffMs || now - legsOffMs < 60000 || now < 30000 || batSlowV < 5.9f) {
    n = fn = 0;  // the history starts again once it's quiet
    return;
  }
  const bool was = batCharging;
  if (now - fastMs >= 5000) {
    fastMs = now;
    fastHist[fi] = battVolts;
    fi = (fi + 1) % 5;
    if (fn < 5) fn++;
    if (fn == 5 && now >= 180000) {
      const float jump = battVolts - fastHist[fi];  // vs 20 s ago
      if (jump >= 0.15f) batCharging = true;
      else if (jump <= -0.12f) batCharging = false;
    }
  }
  if (now - ms >= 15000) {
    ms = now;
    hist[idx] = batSlowV;
    idx = (idx + 1) % 16;
    if (n < 16) n++;
    if (n == 16 && batSlowV - hist[idx] >= 0.07f && batSlowV - hist[(idx + 8) % 16] >= 0.025f) batCharging = true;
    if (n >= 9 && batSlowV - hist[(idx + 7) % 16] <= -0.03f) batCharging = false;  // vs 2 minutes ago
  }
  batFull = batCharging && batSlowV >= 8.36f;
  if (was != batCharging)
    govLog("charge", batCharging ? "charging (voltage jumped / climbing, legs off)" : "not charging (voltage dropped)");
}

// The battery % shown (OLED, page): follows the slow average's %, but down at most 1% a minute
// and up only while charging (3% a minute) - a load sag (legs, speaker) or a noisy reading
// doesn't make it jump. A clear rise without the charger (6+ points for 2 minutes: a sag that
// recovered) still gets through, 1% a minute.
void batShownUpdate() {
  static float shown = -1;
  static unsigned long lastMs = 0, highSince = 0;
  const int target = batPctSlow;
  const unsigned long now = millis();
  const float dtMin = lastMs ? (now - lastMs) / 60000.0f : 0;
  lastMs = now;
  if (target < 0) {
    shown = -1;
    batPctShown = -1;
    return;
  }
  if (shown < 0 || now < 30000) {
    shown = target;  // start: what it reads (the slow average settles in the first 30 s)
  } else if (target < shown) {
    shown = max((float)target, shown - 1.0f * dtMin);
    highSince = 0;
  } else if (target > shown) {
    if (batCharging) {
      shown = target;  // charging known: straight to the reading (3%/min would leave it ~10 points behind for minutes)
    } else if (target - shown >= 6) {
      if (!highSince) highSince = now;
      if (now - highSince > 120000) shown = min((float)target, shown + 1.0f * dtMin);
    } else {
      highSince = 0;
    }
  }
  batPctShown = (int)lroundf(shown);
}

// Battery saver face and nothing going on (no driving, talking, camera, Explore, emote, and the
// page not asking anything for 30 s): the OLED and loop() slow down. Anything happening: back to normal.
bool saverIdle = false;
void saverPower() {
  static unsigned long chkMs = 0;
  static int apWas = 0;
  if (millis() - chkMs > 250) {
    chkMs = millis();
    size_t n = 16;
    int fds[16];
    bool conn = cmd_httpd && httpd_get_client_list(cmd_httpd, &n, fds) == ESP_OK && n > 0;
    n = 16;
    if (!conn && tls_httpd) conn = httpd_get_client_list(tls_httpd, &n, fds) == ESP_OK && n > 0;
    const int ap = WiFi.softAPgetStationNum();
    if (conn || streamClients > 0 || ap > apWas) webSeenMs = millis();  // someone's there: full speed
    apWas = ap;
  }
  saverIdle = faceSaver && !strcmp(perfName, "idle") && !faceScenePlaying && millis() - webSeenMs > 30000 &&
              !camViewWanted && !otaActive && currentCommand.length() == 0;
  // (The CPU stays at 240 MHz: the clock is the whole chip's - at 80 MHz the WiFi slows down too
  // and RC lags. The saving is in drawing less, and idle cores sleep anyway.)
}

// ---- Balance: standing still in walk mode, the body stays parallel to the ground on a sloping floor ----
// The tilt (the IMU's gravity, ~0.3 s smoothed) against "level" (/gait?balzero=1 on a flat floor) is slowly undone with
// the four knees: nose down -> the front corners up, the back ones down; leaning to a side -> that side up. Each
// direction checks itself every 2 s: a correction that made the tilt worse is flipped (and saved). Only while
// standing with the legs on and nothing else moving them; anything else (a walk, an emote, rest) takes over.
void balanceUpdate() {
  static unsigned long lastMs = 0, chkMs = 0, logMs = 0, satPMs = 0, satRMs = 0;
  static float sent[4] = {NAN, NAN, NAN, NAN};  // knee targets sent: FR FL BR BL
  static bool was = false;
  static float chkErrP = 0, chkOffP = 0, chkErrR = 0, chkOffR = 0, satErrP = 0, satErrR = 0;
  const unsigned long now = millis();
  const bool want = balanceOn && imuOk && activeMode == MODE_WALK && requestedMode == MODE_WALK && currentCommand.length() == 0 &&
                    legsPowered && legsPoseKnown && !robotFallen && !batEmpty && !calDirty && !chickenOn;
  if (!want) {
    if (was) {
      was = false;
      balOffP = balOffR = 0;
      for (int i = 0; i < 4; i++) sent[i] = NAN;
    }
    lastMs = now;
    return;
  }
  // the tilt: gyro + gravity fused (chkG - quick; a 0.3 s average is slow to react)
  const float pitch = asinf(constrain(chkG[1], -1.0f, 1.0f)) * 57.2958f;  // + = nose down
  const float roll = asinf(constrain(chkG[0], -1.0f, 1.0f)) * 57.2958f;
  if (!balZeroSet) {  // never told what level is: this stance (a wrong "level" winds it up to its limits)
    balPitch0 = pitch;
    balRoll0 = roll;
    balZeroSet = true;
    prefPut("balP0", (int32_t)lroundf(balPitch0 * 10), false);
    prefPut("balR0", (int32_t)lroundf(balRoll0 * 10), false);
    xlogSys("BALANCE level taken from how it stands now: pitch %.1f roll %.1f", balPitch0, balRoll0);
  }
  if (!was) {  // starting: from the stand
    was = true;
    lastMs = chkMs = now;
    satPMs = satRMs = 0;
    chkErrP = chkErrR = 0;
    chkOffP = balOffP;
    chkOffR = balOffR;
  }
  const float dt = min(now - lastMs, 100UL) / 1000.0f;
  lastMs = now;
  const float eP = balPitchSign * (pitch - balPitch0), eR = balRollSign * (roll - balRoll0);
  const float LIM = 16;
  auto step = [&](float& off, float e) {
    if (fabsf(e) < 0.6f) return;  // (a deadband: no hunting around level)
    off = constrain(off + 15.0f * e * dt, -LIM, LIM);  // ~15 deg/s per degree off (slower can't follow a tilt)
  };
  step(balOffP, eP);
  step(balOffR, eR);
  // stuck at a limit for 3 s and still off: the wrong way (or a slope too steep) - back to zero, the other way
  auto stuck = [&](float& off, float e, unsigned long& since, float& eAt, int& sign, const char* key, const char* what) {
    if (fabsf(off) < LIM - 0.5f || fabsf(e) < 2) { since = 0; return; }
    if (!since) { since = now; eAt = fabsf(e); return; }
    if (now - since > 3000 && fabsf(e) >= eAt - 0.5f) {
      sign = -sign;
      off = 0;
      since = 0;
      prefPut(key, sign, false);
      xlog("BALANCE %s at its limit and not getting level (%.1f deg off) - the other way now (%d)", what, e, sign);
    }
  };
  stuck(balOffP, eP, satPMs, satErrP, balPitchSign, "balPS", "front/back");
  stuck(balOffR, eR, satRMs, satErrR, balRollSign, "balRS", "side");
  if (now - chkMs > 2000) {  // made it worse? the other way, then
    if (fabsf(balOffP - chkOffP) > 3 && fabsf(eP) > fabsf(chkErrP) + 1.5f && fabsf(eP) > 3) {
      balPitchSign = -balPitchSign;
      balOffP = 0;
      prefPut("balPS", balPitchSign, false);
      xlog("BALANCE front/back correction went the wrong way - flipped (%d)", balPitchSign);
    }
    if (fabsf(balOffR - chkOffR) > 3 && fabsf(eR) > fabsf(chkErrR) + 1.5f && fabsf(eR) > 3) {
      balRollSign = -balRollSign;
      balOffR = 0;
      prefPut("balRS", balRollSign, false);
      xlog("BALANCE side correction went the wrong way - flipped (%d)", balRollSign);
    }
    chkMs = now;
    chkErrP = eP;
    chkErrR = eR;
    chkOffP = balOffP;
    chkOffR = balOffR;
  }
  if (now - logMs > 5000) {
    logMs = now;
    xlog("BALANCE tilt %.1f / %.1f deg (level %.1f / %.1f), correction %.1f / %.1f", pitch, roll, balPitch0, balRoll0, balOffP, balOffR);
  }
  // each corner's height change (+ = that corner lower): FR FL BR BL
  const float h[4] = {-balOffP - balOffR, -balOffP + balOffR, balOffP - balOffR, balOffP + balOffR};
  static const int K[4] = {5, 6, 4, 7};  // their knees: R3 L3 R4 L4
  for (int i = 0; i < 4; i++) {
    const int k = K[i];
    const float down = POSE_REST[k] > POSE_STAND[k] ? 1.0f : -1.0f;  // this knee's "lower the corner" way
    const float a = POSE_STAND[k] + down * h[i];
    if (isnan(sent[i]) || fabsf(a - sent[i]) >= 0.5f) {
      setServoAngle(k, (int)lroundf(a));
      sent[i] = a;
    }
  }
}

void loop() {
  xl::holdFlash = streamClients > 0 || rcActive();  // settings wait: no camera pause under a viewer
  // driving / talking / walking (any leg command, Explore): the face stays awake, no dozing off mid-walk
  faceKeepAwake = rcActive() || aud::talkOn || currentCommand.length() > 0 || activeMode == MODE_EXPLORE || activeMode == MODE_DESK || activeMode == MODE_LIFE;
  govTick();
  gem::update();  // the conversation: faces, start / stop, the quiet timeout
  saverPower();
  dnsServer.processNextRequest();
  
  // --- Mode switching: walk engine / face tracking / explore ---
  int wantMode = requestedMode;
  if (wantMode != activeMode) {
    if (wantMode == MODE_WALK) {
      // At once: the camera task owns the TFT and shows the face itself (waiting for
      // the camera to stop would hang with the RC video on: it never stops).
      enterMode(MODE_WALK);
    } else if (camReady || camFailed) {
      enterMode(wantMode);
    }
  }

  // The TFT belongs to the camera task while a camera mode is running
  bool loopOwnsDisplay = (activeMode == MODE_WALK) || activeMode == MODE_DESK || activeMode == MODE_LIFE || (activeMode == MODE_EXPLORE && camFailed);  // (desk: the face, the camera only peeks)
  static bool hadDisplay = true;
  if (loopOwnsDisplay && !hadDisplay) {
    currentFaceFrames = nullptr;  // force a face redraw over the last camera frame
    enterIdle();
  }
  hadDisplay = loopOwnsDisplay;
  if (batEmpty) {
    static unsigned long batScreenMs = 0;
    if (millis() - batScreenMs > 3000) { batScreenMs = millis(); showBatteryScreen(); }
  }
  faceShowing = loopOwnsDisplay && !batEmpty;
  // While the RC video streams the camera task also pushes the face; fewer face frames keep the
  // video fast. Otherwise ~30 a second for smooth eyes.
  // the Battery Saver face's mouth: none outside a chat, a line while it listens, open with the voice
  saverMouth = gem::state == gem::S_OFF ? -1.0f : gem::state == gem::S_SPEAKING ? fminf(1.0f, gem::outLvl * 6) : 0.0f;
  faceFpsWanted = camViewWanted || rcActive() ? min(12, faceFps) : gem::state == gem::S_SPEAKING ? min(20, faceFps) : aud::talkOn ? min(15, faceFps) : faceFps;  // (talking: 20 for the lips)
  // A full-frame style (every style but Battery Saver: 153 KB to the screen each frame) at 30 a second never lets the
  // face task rest: the web server's replies jump from ~90 ms to over 2 s (20 fps and below:
  // ~100 ms). Capped at 15, 12 during a chat (its audio and TLS need the room).
  if (faceEng.style != fe::ST_SAVER) faceFpsWanted = min((int)faceFpsWanted, gem::state != gem::S_OFF ? 12 : 15);
  govMark(GS_MODES);

  updateWifiSetup();  // a network picked on the Settings page
  if (bootWifiPending) bootWifiFinish();  // the boot scan: hotspot + join when it's done
  else wifiKeepUp();  // the home network: never searching while the hotspot is in use
  phonePingUpdate();  // the hotspot phone's link, for the governor's log
  govMark(GS_WIFI);

  // Queued /servo writes (calibrator)
  // (with ms=: the robot glides there itself, eased, a step every loop - big steps from the page
  //  make the legs judder)
  {
    static float gFrom[16], gTo[16];
    static bool gOn[16] = {false};
    static unsigned long gT0 = 0, gDur = 0;
    if (pendingServoAny) {
      pendingServoAny = false;
      gT0 = millis();
      gDur = pendingServoMs;
      for (int i = 0; i < 16; i++) {
        const float to = pendingServoDeg[i];
        if (isnan(to)) continue;
        pendingServoDeg[i] = NAN;
        gFrom[i] = servoRawKnown[i] ? servoRawNow[i] : to;
        gTo[i] = to;
        gOn[i] = true;
      }
    }
    const unsigned long nowG = millis();
    for (int i = 0; i < 16; i++) {
      if (!gOn[i]) continue;
      const float u = gDur ? min(1.0f, (nowG - gT0) / (float)gDur) : 1.0f, e = u * u * (3 - 2 * u);
      const float a = gFrom[i] + (gTo[i] - gFrom[i]) * e;
      pwm.writeMicroseconds(i, (uint16_t)lroundf(732 + a * (2929 - 732) / 180.0f));
      servoRawNow[i] = a;
      servoRawKnown[i] = true;
      calPinMs[i] = nowG ? nowG : 1;
      for (int c = 0; c < 8; c++)
        if (mappedPins[c] == i) { currentAngle[c] = calUnmap(c, a); legsPowered = legsPoseKnown = true; }
      if (u >= 1) gOn[i] = false;
    }
  }

  pollTof();
  imuUpdate();
  yawSignFromTurns();
  govMark(GS_SENSORS);

  // RC head (walk mode): follow the joystick, at most ~360 deg/s so it doesn't jerk
  if (calDirty && activeMode == MODE_WALK) {  // head calibration panel
    calDirty = false;
    headSlewTo(calPan, calTilt);  // glides there
  }
  // Chicken head: the head holds its world direction - level while the body pitches, its heading while the body
  // turns - quick (gyro-fed) and smooth (a stiff critically damped spring). Walk mode, standing or walking; an
  // emote, the RC joystick or the calibration panel take the head back.
  {
    static bool chkWas = false;
    const bool chkRun = chickenOn && activeMode == MODE_WALK && requestedMode == MODE_WALK && !rcHeadActive && !calDirty &&
                        (currentCommand == "" || currentCommand.startsWith("smooth_") || currentCommand.startsWith("step_") || currentCommand.startsWith("sharp_"));
    static float yaw0 = 0, hp = 0, ht = 0, vp = 0, vt = 0, sentP = -999, sentT = -999;
    static unsigned long lastMs = 0;
    const unsigned long now = millis();
    if (chkRun) {
      headSlewCancel();
      const float yawL = yawLeftSign ? yawLeftSign * imuYawAngle : 0;  // + = the body turned left
      if (!chkWas) { yaw0 = yawL; hp = emoPanNow; ht = emoTiltNow; vp = vt = 0; lastMs = now; sentP = sentT = -999; }
      const float dt = constrain((now - lastMs) / 1000.0f, 0.001f, 0.1f);
      lastMs = now;
      if (fabsf(yawL - yaw0) > 75) yaw0 += (yawL - yaw0) > 0 ? (yawL - yaw0 - 75) : (yawL - yaw0 + 75);  // past the neck's reach: follow
      const float pitch = asinf(constrain(chkG[1], -1.0f, 1.0f)) * 57.2958f;  // + = nose down (the walk's convention)
      const float tp = constrain(PAN_MID - (yawL - yaw0) * (panPlusIsLeft ? 1 : -1), PAN_MIN + 3, PAN_MAX - 3);
      const float tt = constrain(TILT_MID - (pitch - levelTrim), TILT_MIN + 3, TILT_MAX - 6);  // nose down -> head up
      auto spring = [&](float& x, float& v, float target) {
        const float w = 22;  // stiff: ~0.15 s to settle
        const float acc = constrain(w * w * (target - x) - 2 * w * v, -4000.0f, 4000.0f);
        v = constrain(v + acc * dt, -500.0f, 500.0f);
        x += v * dt;
      };
      spring(hp, vp, tp);
      spring(ht, vt, tt);
      panWho = "chicken";
      if (fabsf(hp - sentP) >= 0.3f) { setHeadServoF(HEAD_PAN_PIN, hp); sentP = hp; }
      if (fabsf(ht - sentT) >= 0.3f) { setHeadServoF(HEAD_TILT_PIN, ht); sentT = ht; }
      emoPanNow = (int)lroundf(hp);
      emoTiltNow = (int)lroundf(ht);
    } else if (chkWas && !chickenOn && activeMode == MODE_WALK && !rcHeadActive) {
      headSlewTo(PAN_MID, TILT_MID);  // switched off: glide back to straight ahead
    }
    chkWas = chkRun;
  }
  if (activeMode == MODE_WALK && rcHeadActive) {
    headSlewCancel();  // the joystick has the head
    static float rcPan = PAN_MID, rcTilt = TILT_MID;
    static unsigned long rcLastMs = 0;
    const unsigned long now = millis();
    if (now - rcLastMs > 200) {  // (re)starting: from where the head is now
      rcPan = emoPanNow;
      rcTilt = emoTiltNow;
    }
    const float step = 0.36f * min(now - rcLastMs, 50UL);  // (eased 150 deg/s is not responsive enough)
    rcLastMs = now;
    rcPan += constrain(rcPanTarget - rcPan, -step, step);
    rcTilt += constrain(rcTiltTarget - rcTilt, -step, step);
    const int p = (int)lroundf(rcPan), tl = (int)lroundf(rcTilt);
    panWho = "rc";
    if (p != emoPanNow) setHeadServoAngle(HEAD_PAN_PIN, p);
    if (tl != emoTiltNow) setHeadServoAngle(HEAD_TILT_PIN, tl);
    emoPanNow = p;
    emoTiltNow = tl;
    if (fabsf(rcPanTarget - rcPan) < 0.5f && fabsf(rcTiltTarget - rcTilt) < 0.5f) rcHeadActive = false;
  }

  if (activeMode == MODE_FACE && headTargetDirty) {
    // Apply the tracker's latest head target (I2C stays on this core)
    headTargetDirty = false;
    headSlewTo(headPanTarget, headTiltTarget);  // glides after the face (steps of up to 9 deg per frame)
  }
  if (activeMode == MODE_EXPLORE) exploreUpdate();
  if (activeMode == MODE_DESK && !robotFallen) deskm::deskTick(millis());  // (fallen: its body moves would push the legs)
  else if (activeMode == MODE_DESK) deskbody::now = deskbody::MV_NONE;
  if (activeMode == MODE_LIFE) lifem::lifeTick(millis());  // life mode: Oreo on its own (life_mode.h)
  headSlewUpdate();  // the eased head moves asked for above (and by Explore)
  govMark(GS_HEAD);
  playUpdate();
  fallUpdate();
  chargeUpdate();
  govMark(GS_PLAY);

  // Telemetry for the log: every second while exploring, every 30 s otherwise
  // RC mode on the page: the log pauses so the camera never stops for a flash write
  static bool rcWas = false;
  const bool rcNow = rcActive();
  if (rcNow != rcWas) {
    rcWas = rcNow;
    if (rcNow && xl::sdOn) {
      xlog("RC mode (log on the SD card: keeps logging, the camera doesn't pause)");
    } else if (rcNow) {
      xlog("RC mode: log paused (the camera would have to stop for every flash write)");
      xl::skippedLines = 0;
      xl::paused = true;
    } else {
      rcSpeed = 1.0f;  // the throttle belongs to RC mode: normal speed again when it closes
      xl::paused = false;
      xlog("RC mode ended: log resumed (%u lines not logged)", (unsigned)xl::skippedLines);
    }
  }

  static unsigned long lastTelemMs = 0;
  if (millis() - lastTelemMs >= (activeMode == MODE_EXPLORE ? 2000UL : 30000UL)) {
    lastTelemMs = millis();
    if (activeMode == MODE_EXPLORE) {
      xlog("T %s tof=%d raw=%d/%d spd=%d tilt=%.0f fast=%.0f yaw=%.0f rate=%.0f shake=%.2f jolt=%.2f "
           "mot=%d %.1f/%.1f/%.1f free=%.2f/%.2f/%.2f light=%.0f/%.0f/%.0f dark=%d veer=%d caution=%d "
           "fails=%d bat=%.2fV heap=%u camgap=%lu",
           (const char*)exploreStateName, (int)tofMm, (int)tofRawMm, (int)tofRawStatus,
           (int)(cpgSpeed * 100 + 0.5f), imuTiltDeg, imuTiltFastDeg, imuYawAngle, imuYawRate,
           imuShakeRms, imuJolt, (int)motVerdict, (float)motDiffT, (float)motDiff2T, (float)motNoise,
           (float)camFree[camLeftBand()], (float)camFree[1], (float)camFree[camRightBand()],
           (float)camLumaAll, (float)camLumaAhead, (float)camLumaNear, exCamDark ? 1 : 0, exVeer,
           exCaution() ? 1 : 0, exFails, (float)battVolts, ESP.getFreeHeap(), (unsigned long)xl::lastCamGapMs);
    } else {
      // every 30 s, in every mode (xlogSys: walk mode isn't 'recording'): what the robot is coping with
      xlogSys("T mode=%s cmd=%s bat=%.2fV temp=%.0fC | ram int %u min %u big %u dma %u psram %u | web %d tls %d video %d hotspot %d | "
              "wifi %s rssi %d ch %d | cam %d %.0ffps video %.0ffps | loop max %.0fms face %.0ffps | chat %d talk %d rc %d",
              MODE_NAMES[activeMode], currentCommand.length() ? currentCommand.c_str() : "-", (float)battVolts, temperatureRead(),
              (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
              (unsigned)ESP.getFreePsram(), clientCount(cmd_httpd), clientCount(tls_httpd), clientCount(stream_httpd),
              (int)WiFi.softAPgetStationNum(), WiFi.status() == WL_CONNECTED ? "up" : "down",
              WiFi.status() == WL_CONNECTED ? (int)WiFi.RSSI() : 0, (int)WiFi.channel(), camReady ? 1 : 0, (float)camFps, (float)streamFps,
              (float)govLoopMaxMs, (float)faceFpsNow, (int)gem::state, aud::talkOn ? 1 : 0, rcActive() ? 1 : 0);
    }
  }

  govMark(GS_TELEM);
  // Battery + OLED status only 5x per second (a full OLED refresh blocks ~25 ms,
  // which would make every loop - CPG, IMU, ToF - slow)
  static unsigned long lastOledMs = 0;
  const bool oledLive = gem::state == gem::S_LISTENING;  // (a chat listening: the live "hearing you" meter, 5 Hz)
  if (millis() - lastOledMs >= (!oledLive && (rcActive() || aud::talkOn || saverIdle) ? 1000UL : 200UL)) {  // (governor: driving first; saver)
    lastOledMs = millis();
    // Read Battery with EMA filter
    static float filteredVoltage = 0;
    // The reading: raw analogRead,
    // 1:11 divider on GPIO 3, times "batcal" (1.166 by default, the creator's value).
    // /exParams?batnow=8.25 can still set batcal from a known voltage.
    // 32 readings, the 8 lowest and 8 highest thrown away, the middle 16 averaged: the divider
    // (100k / 10k) is a weak source for the ADC, single readings jump by tens of mV at the pin.
    // (A 100 nF capacitor from GPIO 3 to GND steadies it in hardware.)
    uint16_t rs[32];
    for (int i = 0; i < 32; i++) {
      const uint16_t v = analogRead(BATT_PIN);
      int k = i;
      while (k > 0 && rs[k - 1] > v) { rs[k] = rs[k - 1]; k--; }
      rs[k] = v;
    }
    long rawBatt = 0;
    for (int i = 8; i < 24; i++) rawBatt += rs[i];
    batPinMv = (rawBatt / 16.0f) / 4095.0f * 3300.0f;  // uncalibrated pin mV (what batcal scales)
    float voltage = batPinMv / 1000.0f * 11.0f * BAT_CAL;
    filteredVoltage = (filteredVoltage == 0 || millis() < 4000) ? voltage : (filteredVoltage * 0.9 + voltage * 0.1);  // settle after boot
    battVolts = filteredVoltage;
    // Battery log on the SD card, one line a minute (a full-charge drain test): uptime, volts
    // (30 s average), %, mode, legs powered, the command, face fps. A "boot" line at each start.
    static unsigned long batLogMs = 0;
    if (sdSizeMB >= 0 && millis() > 20000 && (batLogMs == 0 || millis() - batLogMs >= 60000)) {
      static const char* BAT_HDR = "uptime_s,volts,pct,mode,legs_powered,command,face_fps";
      if (batLogMs == 0) sdAppend("/logs/battery.csv", BAT_HDR, "boot,,,,,,\n");
      char line[120];
      snprintf(line, sizeof(line), "%lu,%.2f,%d,%s,%d,%.40s,%.0f\n", millis() / 1000, batSlowV, (int)batPctSlow,
               MODE_NAMES[activeMode], legsPowered ? 1 : 0, currentCommand.length() ? currentCommand.c_str() : "-",
               (float)faceFpsNow);
      sdAppend("/logs/battery.csv", BAT_HDR, line);  // the log task writes it: the loop never waits for the card
      batLogMs = millis();
    }
  
    // 2S LiPo: BAT_EMPTY_V (empty for this robot - it browns out below) to 8.4 V (full)
    int battPct = batPercentOf(filteredVoltage);
    if(battPct > 100) battPct = 100;
    if(battPct < 0) battPct = 0;
    battPercent = battPct;
    batteryUpdate(voltage);
    batShownUpdate();
    powerDownCheck();
    if (shutdownReq) shutdownNow();

    // Tilt from the MPU6050 (m/s^2)
    const float ax = imuAcc[0] * 9.80665f, ay = imuAcc[1] * 9.80665f;
    String tiltState = "LEVEL";
    if (!imuOk) tiltState = "NO IMU";
    else if (ay > 5.0) tiltState = "FRONT";
    else if (ay < -5.0) tiltState = "BACK";
    else if (ax > 5.0) tiltState = "LEFT";
    else if (ax < -5.0) tiltState = "RIGHT";
  
    oledHealth();  // answers? set up again now and then; frees a stuck bus
    oledDraw(batPctShown >= 0 ? batPctShown : battPct, filteredVoltage);  // the chest display (oled_ui.h)
  }
  govMark(GS_OLED);

  // Head pats (the touch sensor): a "pat brain" instead of one fixed pose. It tells a tap,
  // a double tap and a hand held on its head (petting) apart, remembers the last pats and picks a reaction
  // that fits - never the same one twice in a row: a boop / head tilt / wiggle / nuzzle for a tap, play for a
  // double tap, leaning into the hand and melting for petting, more and more excited with many pats, "ok,
  // enough!" past ~8 in 25 s, "oh, you're here!" after a long while, and a groggy wake-up when lying down.
  // Timers / alarms / sleep timer (desk_clock.h): while one rings, a touch on the head is its (snooze / stop),
  // not a pat
  // RC deadman: a walk from the RC screen lives only while the page keeps saying "still holding" (hold=1 every
  // 300 ms), so a lost "stop" on a bad link can't leave it walking. 1.2 s: one missed
  // refresh on this link is common - 1 s would stutter.
  if (rcDriveLease && millis() - rcDriveMs > 1200) {
    rcDriveLease = false;
    if (currentCommand.length()) {
      xlogSys("RC lease expired: stopped (%s - the page went quiet for 1.2 s)", currentCommand.c_str());
      currentCommand = "";
    }
  }
  legsMoving = currentCommand.length() > 0;  // (for the web watchdog's reboot check, read from another task)
  clk::tick(millis());
  if (clk::ringing() || clk::touchDownMs) pat::ringTail = pat::ringHead;
  mus::tick(millis());  // the next song, bobbing along (music.h)
  patUpdate();

  govMark(GS_TOUCH);
  static String lastCommand = "";

  // Emotes only run in walk mode (they draw on the TFT); Explore drives the smooth_ gaits
  // and the non-blocking step_ turns
  bool commandsAllowed = (activeMode == MODE_WALK) || activeMode == MODE_DESK || activeMode == MODE_LIFE ||  // (desk: poses + emotes; gaits refused below)
                         (activeMode == MODE_EXPLORE &&
                          (currentCommand.startsWith("smooth_") || currentCommand.startsWith("step_") || currentCommand.startsWith("sharp_")));
  // Walk mode: the smooth gait's speed follows the RC speed slider, eased at 1.5 per second.
  // It's the CPG clock's rate, not the gait frequency: the stride stays where it is, it just
  // runs faster or slower - no jump when the slider moves mid-step.
  if (activeMode == MODE_WALK) {
    static unsigned long spdMs = 0;
    const unsigned long nowS = millis();
    const float dts = spdMs ? min((nowS - spdMs) / 1000.0f, 0.1f) : 0.0f;
    spdMs = nowS;
    cpgSpeed += constrain(rcSpeed - cpgSpeed, -1.5f * dts, 1.5f * dts);
  }
  // Emotes get matching props on the screen (the scene engine, props only): started with the
  // emote, stopped when it ends
  // Lying on its side / back: no leg command runs (the page, RC, voice, a pat's reaction) - the legs would push
  // against the floor. Upright again (fallUpdate) and they work again.
  if (robotFallen && currentCommand != "") {
    static unsigned long refusedMs = 0;
    if (millis() - refusedMs > 3000) { refusedMs = millis(); xlog("FALL: '%s' refused - lying on its side", currentCommand.c_str()); }
    currentCommand = "";
  }
  static String ovCmd = "";
  static bool ovOn = false;
  if (currentCommand != ovCmd) {
    if (ovOn) { faceScene("stop"); ovOn = false; }
    ovCmd = currentCommand;
    const char* ov = emoteProps(currentCommand.c_str());
    if (ov && commandsAllowed && activeMode == MODE_WALK) { faceScene(ov); ovOn = true; }
    // the emote's own sound (the page's "Emote sounds" switch); skipped while talking / RC talk / a song
    const char* snd = emoteSound(currentCommand.c_str());
    if (snd && emoteSndOn && commandsAllowed && !aud::talkOn && !aud::playing) {
      playSfx(snd);
      voiceLastMs = millis();
    }
  }
  if (currentCommand != "" && commandsAllowed) {
    if (!currentCommand.startsWith("smooth_") && !currentCommand.startsWith("step_") && !currentCommand.startsWith("sharp_")) headSlewCancel();  // an emote has the head
    if (gaitSteadyDpad && (currentCommand == "forward" || currentCommand == "backward"))
      currentCommand = currentCommand == "forward" ? "smooth_steady" : "smooth_steady_back";  // (the page's switch)
    // desk mode never walks: any gait command is refused (on the desk only the body's small moves)
    if (activeMode == MODE_DESK && !deskWalkActive && (currentCommand.startsWith("smooth_") || currentCommand.startsWith("step_") || currentCommand.startsWith("sharp_") ||
                                    currentCommand == "forward" || currentCommand == "backward" || currentCommand == "left" ||
                                    currentCommand == "right" || currentCommand.startsWith("creator"))) {
      xlog("DESK gait refused: %s", currentCommand.c_str());
      currentCommand = "";
    }
    // desk mode with the legs folded (the page's "Fold legs"): no leg moves at all - emotes, poses, pats
    if (activeMode == MODE_DESK && deskbody::folded && currentCommand.length()) {
      xlog("DESK legs folded: %s skipped", currentCommand.c_str());
      currentCommand = "";
    }
    String cmd = currentCommand;
    if (cmd == "forward") runWalkPose();
    else if (cmd == "backward") runWalkBackward();
    else if (cmd == "left") runTurnLeft();
    else if (cmd == "right") runTurnRight();
    else if (cmd == "rest") {
      runRestPose();
      delay(400);    // settle on the belly
      legsRelax();   // then no power to the legs while lying
      if (currentCommand == "rest") currentCommand = "";
    }
    else if (cmd == "stand") { runStandPose(1); emoHead(0, 0); if (currentCommand == "stand") currentCommand = ""; }
    else if (cmd == "sit") {  // a dog's sit: front legs standing, back legs down as in rest (the dead emote's pose, no face)
      runStandPose(0);
      moveSmoothHelper(600, -999, -999, -999, -999, REST_BACK[0], REST_BACK[1], REST_BACK[2], REST_BACK[3]);
      if (currentCommand == "sit") currentCommand = "";
    }
    else if (cmd == "sit_elegant") {
      setFaceWithMode("content", FACE_ANIM_ONCE);
      // front legs first, then the back ones (like the cute emote: all at once is a belly flop)
      // the front four at their MIN (setServoAngle maps the built-in min onto the calibration page's)
      moveSmoothHelper(700, (int)LEG_MIN0[0], (int)LEG_MIN0[1], (int)LEG_MIN0[2], (int)LEG_MIN0[3], -999, -999, -999, -999);
      moveSmoothHelper(800, -999, -999, -999, -999, REST_BACK[0], REST_BACK[1], REST_BACK[2], REST_BACK[3]);
      if (currentCommand == "sit_elegant") currentCommand = "";
    }
    else if (cmd == "curious") runCuriousEmote();
    else if (cmd == "happy") runHappyEmote();
    else if (cmd == "excited") runExcitedEmote();
    else if (cmd == "stretch") runStretchEmote();
    else if (cmd == "sleepy") runSleepyEmote();
    else if (cmd == "scared") runScaredEmote();
    else if (cmd == "sneeze") runSneezeEmote();
    else if (cmd == "love") runLoveEmote();
    else if (cmd == "angry") runAngryEmote();
    else if (cmd == "sad") runSadEmote();
    else if (cmd == "think") runThinkEmote();
    else if (cmd == "peekaboo") runPeekabooEmote();
    else if (cmd == "dizzy") runDizzyEmote();
    else if (cmd == "hiccup") runHiccupEmote();
    else if (cmd == "yawn") runYawnEmote();
    else if (cmd == "shiver") runShiverEmote();
    else if (cmd == "celebrate") runCelebrateEmote();
    else if (cmd == "tap") runTapFootEmote();
    else if (cmd == "bounce") runBounceEmote();
    else if (cmd == "wiggle") runWiggleEmote();
    else if (cmd == "sniff") runSniffEmote();
    else if (cmd == "laugh") runLaughEmote();
    else if (cmd == "pounce") runPounceEmote();
    else if (cmd == "lookaround") runLookAroundEmote();
    else if (cmd == "headbang") runHeadbangEmote();
    else if (cmd == "faint") runFaintEmote();
    else if (cmd == "twirl") runTwirlEmote();
    else if (cmd == "yoga") runYogaEmote();
    else if (cmd == "sway") runSwayEmote();
    else if (cmd == "robot") runRobotEmote();
    else if (cmd == "tada") runTadaEmote();
    else if (cmd == "playbow") runPlayBowEmote();
    else if (cmd == "huh") runHuhEmote();
    else if (cmd == "scratch") runScratchEmote();
    else if (cmd == "shakeoff") runShakeOffEmote();
    else if (cmd == "knead") runKneadEmote();
    else if (cmd == "beg") runBegEmote();
    else if (cmd == "loaf") runLoafEmote();
    else if (cmd == "swat") runSwatEmote();
    else if (cmd == "startle") runStartleEmote();
    else if (cmd == "crabrave") runCrabRaveEmote();
    else if (cmd == "crabrave1") runCrabRave1Emote();
    else if (cmd == "musicdance") runMusicDanceEmote();
    else if (cmd == "pat_boop") runBoopEmote();
    else if (cmd == "pat_nuzzle") runNuzzleEmote();
    else if (cmd == "petting") runPettingEmote();
    else if (cmd == "pat_wake") runPatWakeEmote();
    else if (cmd == "pat_hello") runPatHelloEmote();
    else if (cmd == "pat_enough") runPatEnoughEmote();
    else if (cmd == "wave") runWavePose(false);
    else if (cmd == "wave2") runWavePose(true);
    else if (cmd == "dance") runDancePose();
    else if (cmd == "shy") runShyEmote();
    else if (cmd == "hide") runHideEmote();
    else if (cmd == "sneaky") runSneakyEmote();
    else if (cmd == "proud") runProudEmote();
    else if (cmd == "bored") runBoredEmote();
    else if (cmd == "jam") runJamEmote();
    else if (cmd == "sulk") runSulkEmote();
    else if (cmd == "kiss") runKissEmote();
    else if (cmd == "stomp") runStompEmote();
    else if (cmd == "swaydance") runSwayDanceEmote();
    else if (cmd == "bob") runBobEmote();
    else if (cmd == "peek") runPeekEmote();
    else if (cmd == "doggyshake") runDoggyShakeEmote();
    else if (cmd == "bowwiggle") runBowWiggleEmote();
    else if (cmd == "highfive") runHighFiveEmote();
    else if (cmd == "march") runMarchEmote();
    else if (cmd == "hula") runHulaEmote();
    else if (cmd == "bodynod") runBodyNodEmote();
    else if (cmd == "twistlook") runTwistLookEmote();
    else if (cmd == "squats") runSquatsEmote();
    else if (cmd == "shimmy") runShimmyEmote();
    else if (cmd == "itchkick") runItchKickEmote();
    else if (xEmoteFind(cmd.c_str()) >= 0) runXEmote(xEmoteFind(cmd.c_str()));  // the emote library
    else if (cmd == "swim") runSwimPose();
    else if (cmd == "point") runPointPose();
    else if (cmd == "pushup") runPushupPose();
    else if (cmd == "bow") runBowPose();
    else if (cmd == "cute") runCutePose();
    else if (cmd == "freaky") runFreakyPose();
    else if (cmd == "worm") runWormPose();
    else if (cmd == "shake") runShakePose();
    else if (cmd == "shrug") runShrugPose();
    else if (cmd == "dead") runDeadPose();
    else if (cmd == "crab") runCrabPose();
    else if (cmd == "nodyes") runNodYes();
    else if (cmd == "nodno") runNodNo();
    else if (cmd == "gocrazy") runGoCrazyPose();
    else if (cmd == "patted") runPattedPose();
    else if (cmd == "step_left") stepTurnUpdate(true);    // creator's turn, one frame per pass
    else if (cmd == "step_right") stepTurnUpdate(false);
    else if (cmd == "sharp_left") sharpTurnUpdate(true);    // the page's Sharp L / R: the creator's turn, smooth
    else if (cmd == "sharp_right") sharpTurnUpdate(false);
    else if (cmd == "smooth_walk") { loadWalkPreset(0); }
    else if (cmd == "smooth_walk_back") { loadWalkPreset(1); }
    else if (cmd == "smooth_steady") { loadWalkPreset(0); }  // (the same tuned trot - and smooth_walk is one too when "everywhere" is on)
    else if (cmd == "smooth_steady_back") { loadWalkPreset(1); }
    else if (cmd == "smooth_steady_level") { loadWalkPreset(0); }       // the steady level-gait
    else if (cmd == "smooth_steady_level_back") { loadWalkPreset(1); }
    else if (cmd == "smooth_walk_left") { loadWalkPreset(2); }
    else if (cmd == "smooth_walk_right") { loadWalkPreset(3); }
    else if (cmd == "smooth_sharp_left") { loadWalkPreset(4); }
    else if (cmd == "smooth_sharp_right") { loadWalkPreset(5); }
    else if (cmd == "smooth_fwd_left") { loadWalkPreset(6); }
    else if (cmd == "smooth_fwd_right") { loadWalkPreset(7); }
    else if (cmd == "smooth_creator_left") { loadWalkPreset(8); }
    else if (cmd == "smooth_creator_right") { loadWalkPreset(9); }
    else if (cmd == "smooth_side_left") { loadWalkPreset(10); }   // sideways, still facing ahead
    else if (cmd == "smooth_balanced") { loadWalkPreset(12); }     // the balanced walk (symmetric trot, IMU level)
    else if (cmd == "smooth_balanced_back") { loadWalkPreset(13); }
    else if (cmd == "smooth_trot") { loadWalkPreset(14); }          // the diagonal trot (speed / lift / hip swing: /gait)
    else if (cmd == "smooth_trot_back") { loadWalkPreset(15); }
    else if (cmd == "smooth_nav") { loadWalkPreset(0); sw_frequency = (navVx < -0.01f ? -1.0f : 1.0f) * fabsf(walkFwd.freq); }
    else if (cmd == "smooth_side_right") { loadWalkPreset(11); }
    else if (cmd == "smooth_climb") {  // Explore: forward walk, legs lift higher and swing further
      loadWalkPreset(0);
      for (int i = 0; i < 8; i++) sw_amp_mapped[i] *= EX_CLIMB_AMP;
    }
    // smooth_tuned: uses params from /tune_walk, no preset override

    // D-pad walk / the creator's spin let go (RC: button released): stand at once. These
    // return mid-step when the command changes; the stand further down only comes if the
    // command had already run for a whole loop pass before (one call runs several cycles), so
    // after a single press the legs would stay where the step happened to be.
    if ((cmd == "forward" || cmd == "backward" || cmd == "left" || cmd == "right") && currentCommand != cmd) {
      runStandPose(currentCommand == "" ? 1 : 0);
    }
    
    // Run CPG loop for any smooth walk variant
    if (cmd.startsWith("smooth_")) {
      unsigned long currentMillis = millis();
      // Starting, or another walk (forward -> a turn ...): blend from where the legs are into the
      // gait over 400 ms. The gait's first frame is up to ~40 deg from the stand pose (and from the
      // last walk): snapping all 8 legs there at once would jolt.
      static String blendCmd = "";
      static unsigned long blendMs = 0;
      static float blendFrom[8];
      // Switching gaits WHILE walking (straight <-> curve: Explore does it every second or two): cross-fade from the
      // old gait still running to the new one, both on the same clock (fading from a frozen snapshot of the
      // legs makes them barely move for ~150 ms: a visible hitch at every steering change).
      static bool liveBlend = false;
      static float lbBase[8], lbAmp[8], lbPhase[8], lbFreq = 0;
      if (sw_oldMillis == 0 || cmd != blendCmd) {
        liveBlend = sw_oldMillis != 0 && cpgLastValid;
        if (liveBlend) {
          memcpy(lbBase, cpgLastBase, sizeof(lbBase));
          memcpy(lbAmp, cpgLastAmp, sizeof(lbAmp));
          memcpy(lbPhase, cpgLastPhase, sizeof(lbPhase));
          lbFreq = cpgLastFreq;
        }
        blendCmd = cmd;
        blendMs = currentMillis;
        for (int i = 0; i < 8; i++) blendFrom[i] = currentAngle[sw_servos[i]];
      }
      const float bu = min(1.0f, (currentMillis - blendMs) / 400.0f), be = bu * bu * (3 - 2 * bu);
      if (sw_oldMillis == 0) sw_oldMillis = currentMillis;
      sw_innerTime += (currentMillis - sw_oldMillis) * (double)cpgSpeed;
      sw_oldMillis = currentMillis;

      // Walking straight: hold the heading the walk started with (gyro). Hips: 0, 1 = left side, 4, 5 = right
      float scaleL = 1, scaleR = 1;
      const bool sideCmd = cmd == "smooth_side_left" || cmd == "smooth_side_right";
      const bool balCmd = cmd.startsWith("smooth_balanced") || cmd.startsWith("smooth_trot");  // (the trot: symmetric too)
      const bool straightCmd = cmd == "smooth_walk" || cmd == "smooth_walk_back" || cmd.startsWith("smooth_steady") || cmd == "smooth_climb" || sideCmd || balCmd;
      const bool straight = headHoldOn && straightCmd;
      // the walk's own pull, taken out up front (forward only; + = the left strides shorter), so the gyro
      // doesn't have to steer against it all the time. GET /gait?trim=
      const float trimF = (straightCmd && !sideCmd && !balCmd && !cmd.endsWith("_back")) ? walkTrim : 0.0f;  // (sideways: sideBal; balanced: symmetric)
      static String holdCmd = "";
      static float hdg0 = 0, hdgInt = 0;
      static unsigned long holdMs = 0;
      if (straight && yawLeftSign != 0) {
        const float hdg = yawLeftSign * imuYawAngle;  // + = left
        if (cmd != holdCmd || be < 0.05f) { holdCmd = cmd; hdg0 = hdg; hdgInt = 0; holdMs = currentMillis; }
        const float hdt = min(currentMillis - holdMs, 100UL) / 1000.0f;
        holdMs = currentMillis;
        const float err = hdg0 - hdg;  // + : it drifted right, steer left
        hdgInt = constrain(hdgInt + err * hdt, -40.0f, 40.0f);
        float c = constrain(trimF + headHoldKp * err + headHoldKi * hdgInt, -0.45f, 0.45f);
        if (cmd.endsWith("_back")) c = -c;  // walking backward, the same stride change turns the other way
        if (c > 0) scaleL = 1 - c; else scaleR = 1 + c;  // (only shorter strides: never past the tuned ones)
        headErrNow = err;
        headCorrNow = c;
        headHoldActive = true;
      } else {
        holdCmd = "";
        if (trimF > 0) scaleL = 1 - trimF; else scaleR = 1 + trimF;  // (the gyro off: just the trim)
        headErrNow = headCorrNow = 0;
        headHoldActive = false;
      }
      // The level-gait: the body parallel to the ground
      const bool levelCmd = cmd.startsWith("smooth_steady_level") || balCmd;
      if (levelCmd && imuOk) {
        static unsigned long lvlMs = 0;
        static String lvlCmd = "";
        const bool lvlGap = currentMillis - lvlMs > 500;  // (stopped in between: a fresh start)
        const float ldt = min(currentMillis - lvlMs, 100UL) / 1000.0f;
        lvlMs = currentMillis;
        const float pitch = asinf(constrain(imuAcc[1], -1.0f, 1.0f)) * 57.2958f;  // + = nose down (ay > 0: tipped forward)
        static float lvlI = 0;
        if (cmd != lvlCmd || lvlGap) { lvlCmd = cmd; levelPitch = pitch; lvlI = 0; }  // (the last walk's correction: not carried into this one)
        levelPitch += (pitch - levelPitch) * min(1.0f, ldt / 0.5f);  // (smoothed: the steps shake it)
        const float err = levelPitch - levelTrim;
        lvlI = constrain(lvlI + 1.2f * err * ldt, -15.0f, 15.0f);  // slow and steady: ~1.2 deg/s per deg off
        levelCorr = constrain(lvlI + 0.25f * err, -15.0f, 15.0f);
        for (int i = 0; i < 8; i++) {
          levelOff[i] = 0;
          if (i != 2 && i != 3 && i != 6 && i != 7) continue;  // knees: 2 L3 front, 3 L4 back, 6 R3 front, 7 R4 back
          const int e = sw_servos[i];
          const float down = POSE_REST[e] > POSE_STAND[e] ? 1.0f : -1.0f;  // this knee's "lower the corner" way
          const bool front = i == 2 || i == 6;
          levelOff[i] = (front ? -down : down) * levelCorr * 0.5f;  // nose down: front up, back down
        }
      } else if (!levelCmd) {
        for (int i = 0; i < 8; i++) levelOff[i] = 0;
      }
      // The steady gait's soft start: the strides grow over ~1.2 s (eased)
      float env = 1;
      if (isSteady(cmd)) {
        const float u = min(1.0f, (currentMillis - blendMs) / 1200.0f);
        env = u * u * (3 - 2 * u);
      }
      // walking sideways the "left" / "right" sides of the gait (for the heading hold) are the back / front pairs:
      // going left the back legs play the gait's left side, going right the front legs (sideGaitFill's roles)
      const bool sideL = cmd == "smooth_side_left";
      for (int i = 0; i < 8; i++) {
        float amp = sw_amp_mapped[i] * env;
        if (cmd == "smooth_nav") {  // skid steer by (navVx, navWz), the whole stride faded in by the command's size
          const float way = navVx < -0.01f ? -1.0f : 1.0f;
          const float v = fabsf(navVx) / NAV_VX_FULL, w = navWz / NAV_WZ_FULL * way;
          const float gate = min(1.0f, sqrtf(navVx * navVx + 0.0625f * navWz * navWz) / NAV_GATE_FULL);
          if (i == 0 || i == 1) amp *= constrain(v - w, -1.0f, 1.0f);
          else if (i == 4 || i == 5) amp *= constrain(v + w, -1.0f, 1.0f);
          amp *= gate;
        }
        if (sideCmd) {
          const bool back = i == 1 || i == 5, front = i == 0 || i == 4;
          if ((sideL && back) || (!sideL && front)) amp *= scaleL;
          if ((sideL && front) || (!sideL && back)) amp *= scaleR;
        } else {
          if (i == 0 || i == 1) amp *= scaleL;
          if (i == 4 || i == 5) amp *= scaleR;
        }
        float cw = cos(sw_frequency * sw_innerTime + sw_phase[i]);
        if (sw_rect[i] && cw < 0) cw = 0;
        if (sw_snap[i]) cw = sqrtf(cw);
        float angle = sw_base[i] + levelOff[i] + amp * cw;
        if (be < 1) {
          const float from = liveBlend ? lbBase[i] + lbAmp[i] * cos(lbFreq * sw_innerTime + lbPhase[i]) : blendFrom[i];
          angle = from + (angle - from) * be;
        }
        setServoAngle(sw_servos[i], (int)lroundf(angle));
      }
      // Gait phase for the Explore camera motion check
      double ph = fmod((double)sw_frequency * (double)sw_innerTime, 6.283185307);
      if (ph < 0) ph += 6.283185307;
      cpgPhase = (float)ph;
      cpgPeriodMs = 6.283185307f / fabsf(sw_frequency) / cpgSpeed;
      // the gait just used (for the next switch's live cross-fade)
      memcpy(cpgLastBase, sw_base, sizeof(cpgLastBase));
      memcpy(cpgLastAmp, sw_amp_mapped, sizeof(cpgLastAmp));
      memcpy(cpgLastPhase, sw_phase, sizeof(cpgLastPhase));
      cpgLastFreq = sw_frequency;
      cpgLastValid = true;
    }
  }
  if (activeMode != MODE_EXPLORE) cpgGait = GAIT_NONE;
  else if (exHeadAway() || millis() - exHeadMovedMs < 250) cpgGait = GAIT_GLANCE;  // head not straight ahead / moving
  else cpgGait = (currentCommand.startsWith("smooth_") || currentCommand.startsWith("step_") || currentCommand.startsWith("sharp_")) ? exGaitId : GAIT_NONE;

  // The steady gait let go: the strides shrink to nothing over 0.5 s (the CPG keeps its rhythm), then
  // the stand below - not a snap from mid-step
  static String gaitWas = "";
  // (fallen: none of the stop moves below - the fall turned the legs off, a stand pose would power them up again on its side)
  if (isSteady(gaitWas) && !currentCommand.startsWith("smooth_") && sw_oldMillis != 0 && !robotFallen) {
    const unsigned long t0 = millis();
    unsigned long last = t0;
    while (millis() - t0 < 500 && currentCommand == "") {
      const unsigned long nowMs = millis();
      sw_innerTime += (nowMs - last) * (double)cpgSpeed;
      last = nowMs;
      const float u = 1.0f - (nowMs - t0) / 500.0f, e = u * u * (3 - 2 * u);
      for (int i = 0; i < 8; i++) {
        float cw = cos(sw_frequency * sw_innerTime + sw_phase[i]);
        if (sw_rect[i] && cw < 0) cw = 0;
        if (sw_snap[i]) cw = sqrtf(cw);
        setServoAngle(sw_servos[i], (int)lroundf(sw_base[i] + levelOff[i] * e + sw_amp_mapped[i] * e * cw));
      }
      vTaskDelay(pdMS_TO_TICKS(15));
    }
  }
  gaitWas = currentCommand;
  bool isWalking = currentCommand.startsWith("smooth_");
  if (!isWalking) headHoldActive = false;
  if (!isWalking) {
    if (sw_oldMillis != 0 && !robotFallen) {
      runStandPose(0);
    }
    sw_oldMillis = 0;
    cpgLastValid = false;
  }
  if (sharpActive && !currentCommand.startsWith("sharp_")) {  // Sharp L / R let go: ease back to the stand
    sharpActive = false;
    if (currentCommand == "" && !robotFallen) runStandPose(0);
  }
  if (stepTurnFrame >= 0 && !currentCommand.startsWith("step_")) {
    stepTurnFrame = -1;
    if (!isWalking && !robotFallen) runStandPose(0);  // the creator's turn ends standing, like the D-pad
  }
  
  if (currentCommand == "" && activeMode == MODE_WALK && !robotFallen) {
    if (lastCommand == "forward" || lastCommand == "backward" || lastCommand == "left" || lastCommand == "right" || lastCommand == "patted") {
      runStandPose(1);
    }
  }
  lastCommand = currentCommand;
  balanceUpdate();
  govMark(GS_CMDS);

  vTaskDelay(pdMS_TO_TICKS(saverIdle ? 30 : 10));  // battery saver at rest: fewer loop passes
}
