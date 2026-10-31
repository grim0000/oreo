# Oreo gait kit

`oreo_gait.h` is Oreo's walk on its own: the gait engine, the tuned walks, and the calibration mapping. It's for anyone
who wants to improve the gait without the rest of the firmware (camera, display, WiFi, voice).

## How a walk is described

Every leg servo follows one wave:

```
angle = base + amp * cos(freq * t + phase)
```

| | meaning |
|---|---|
| `base` | the leg's middle angle during the walk (deg) |
| `amp` | how far it swings each way (deg); the sign sets the direction |
| `phase` | its place in the cycle (rad). Diagonal legs in step = a trot |
| `freq` | rad per ms. 0.012 = one step cycle in about 524 ms; negative = the same walk played backward |
| `kneeLiftOnly` | the knee only lifts (half wave): up while the foot swings forward, flat while it pushes |

The tables are in gait order: FL hip, BL hip, FL knee, BL knee, FR hip, BR hip, FR knee, BR knee.

The angles are Oreo's built-in angles. Each one is mapped onto your own calibration (the min / mid / max you set on the
calibration page, in `gait.cal`), so a walk tuned on one robot works on another.

Built in: `WALK_FORWARD`, `WALK_BACKWARD`, `WALK_LEFT` / `WALK_RIGHT` (curves), `TURN_LEFT` / `TURN_RIGHT` (on the
spot) and `balancedTrot(lift, stride)`. The engine cross-fades between gaits over 400 ms and grows the strides in over
1.2 s from a stand. `speed` scales the clock, and `strideLeft` / `strideRight` shorten one side to steer.

## Servo board or straight to the microcontroller

The gait itself doesn't care how the servos are driven. Pick the output before the include:

| | |
|---|---|
| `#define OREO_GAIT_PCA9685` | servos on a PCA9685 board over I2C, like Oreo. Needs the *Adafruit PWM Servo Driver Library*. Set `oreo::legChannel` to your wiring |
| `#define OREO_GAIT_DIRECT` | servos on ESP32 GPIOs using LEDC PWM. One channel per leg; the ESP32-S3 has 8, enough for the legs. Set `oreo::legPin` |
| neither | write your own `void oreoGaitWriteUs(uint8_t leg, uint16_t us)` |

Oreo itself needs the PCA9685. Its ESP32-S3 CAM board has almost no free pins: the camera, display, SD card, I2S
audio and I2C use them. On a bare ESP32 board, direct drive is fine. Either way:
- power the servos from their own 5–6 V supply, never the board's 5V or 3V3 pin;
- tie the grounds together.

The MG90S accepts the 3.3 V signal.

## Try it

`examples/walk_pca9685` and `examples/walk_direct` walk from the serial monitor (115200):

| key | action |
|---|---|
| `w` / `s` | forward / backward |
| `a` / `d` | curve left / right |
| `q` / `e` | turn left / right |
| `t` | balanced trot |
| `x` | stand |
| `+` / `-` | speed |

Put `oreo_gait.h` next to the sketch (Arduino IDE), or add this folder to the include path (PlatformIO
`-I gait`). Test on the floor: the robot walks.

A gait you like goes back into the firmware as numbers. `WALK_FWD_*`, `WALK_BACK_*` and the turn tables at the top of
`firmware/src/main.cpp` hold the same values. Or save it from the robot's page: Smooth walk settings, then
"Save as forward / backward walk".
