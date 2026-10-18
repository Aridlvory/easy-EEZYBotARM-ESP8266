# easyEEZYbotARM · ESP8266 WiFi Control

Control an **EEZYbotARM Mk1** robot arm from your phone's browser. The ESP8266 creates its own WiFi network and serves a touch-friendly control page. No router, laptop, or app install needed.

This project adapts the kinematics from [meisben/easyEEZYbotARM](https://github.com/meisben/easyEEZYbotARM), which used Python on a laptop talking to an Arduino over USB. Here the kinematics run in **JavaScript on your phone**, and the **ESP8266 drives the servos**, connected over WiFi.

<!-- Add screenshots here, e.g.: ![UI](docs/images/ui.png) -->

---

## Contents

- [Features](#features)
- [How it works](#how-it-works)
- [Repository layout](#repository-layout)
- [Hardware](#hardware)
- [Wiring](#wiring)
- [Build and flash](#build-and-flash)
- [Using the web interface](#using-the-web-interface)
- [HTTP API](#http-api)
- [Kinematics](#kinematics)
- [Configuration and limits](#configuration-and-limits)
- [Calibration](#calibration)
- [Troubleshooting](#troubleshooting)
- [Known limitations](#known-limitations)
- [Credits and license](#credits-and-license)

---

## Features

- ESP8266 runs as a **WiFi access point** and hosts the control page from flash (no filesystem needed)
- **Joint control** tab with sliders for base, main arm, horarm, and gripper
- **Tap-to-aim X/Y/Z control**: tap a **top view** to set direction and reach, tap a **side view** to set height
- **Workspace check**: the target dot turns green if the point is reachable and red if not. The *Go to position* button stays disabled until the point is reachable, and the reason is shown when it isn't
- Forward and inverse kinematics ported from the original Python library
- Serial debug logging of every request, servo mapping, and servo command
- Two firmware variants: **PCA9685 driver board** or **direct GPIO servos**

---

## How it works

```
  Phone browser (JavaScript)                 ESP8266 (C++)
 ┌───────────────────────────┐   WiFi     ┌────────────────────────────┐
 │ forward / inverse         │  HTTP GET  │ web server on port 80      │
 │ kinematics                │ ─────────► │ /move?q1&q2&q3&ee          │
 │ workspace check           │            │ kinematic → servo angles   │
 │ top + side view canvases  │ ◄───────── │ drives servos              │
 └───────────────────────────┘    JSON    └─────────────┬──────────────┘
                                                        │
                                          PCA9685 (I2C)  or  direct GPIO
                                                        │
                                                   4 servos (SG90)
```

The phone works out joint angles and the ESP8266 only turns them into servo commands. This keeps the ESP8266 light and matches the Python/Arduino split of the original project.

---

## Repository layout

```
.
├── firmware-pca9685/       PlatformIO project: servos via PCA9685 (I2C)
│   ├── platformio.ini
│   └── src/main.cpp
├── firmware-gpio/          PlatformIO project: servos direct on ESP8266 GPIO
│   ├── platformio.ini
│   └── src/main.cpp
├── tools/
│   └── servo_diagnostic_sweep_test.cpp   standalone PCA9685 sweep test
├── LICENSE
└── README.md
```

The web page is embedded in `main.cpp` as a raw string literal (`INDEX_HTML`). Both firmware variants contain the same page.

**Which one should I use?**

| | `firmware-pca9685` | `firmware-gpio` |
|---|---|---|
| Extra hardware | PCA9685 board | none |
| PWM generation | hardware (steady) | software (can jitter under WiFi load) |
| I2C wiring | yes | no |
| Pins used | D1, D2 (I2C) | D1, D2, D5, D6 |

If the PCA9685 is available and working, it is the more robust choice. The GPIO variant is simpler and works well for four light hobby servos.

---

## Hardware

- EEZYbotARM **Mk1** (3D printed) with 4 servos (SG90 or similar)
- ESP8266 board (developed on NodeMCU v2)
- **PCA9685** 16-channel servo driver (PCA9685 variant only)
- 5–6 V servo supply, for example 4×AA (NiMH gives ~4.8 V, alkaline ~6 V)
- Separate USB power for the ESP8266 (power bank or charger)
- Jumper wires

> ⚠️ **Never power servos from the ESP8266's 3V3/5V pins.** Servos draw current spikes that will brown out the microcontroller. Use a separate supply and share a common ground.

---

## Wiring

### PCA9685 variant (`firmware-pca9685`)

| From | To | Note |
|---|---|---|
| ESP8266 `D2` | PCA9685 `SDA` | I2C data |
| ESP8266 `D1` | PCA9685 `SCL` | I2C clock |
| ESP8266 `3V3` | PCA9685 `VCC` | logic power |
| ESP8266 `GND` | PCA9685 `GND` | **common ground, required** |
| Battery pack `+` | PCA9685 `V+` (screw terminal) | servo power |
| Battery pack `−` | PCA9685 `GND` (screw terminal) | servo return |
| PCA9685 `CH0` | Gripper servo | |
| PCA9685 `CH1` | q1 base servo | |
| PCA9685 `CH2` | q2 main arm servo | |
| PCA9685 `CH3` | q3 horarm servo | |
| USB power | ESP8266 `USB`/`VIN` | powered separately from the servos |

### Direct GPIO variant (`firmware-gpio`)

| Servo | Signal | V+ (red) | GND (brown/black) |
|---|---|---|---|
| q1 base | `D1` | battery `+` | common GND |
| q2 main arm | `D2` | battery `+` | common GND |
| q3 horarm | `D5` | battery `+` | common GND |
| Gripper | `D6` | battery `+` | common GND |

The battery `−`, all servo grounds, and the ESP8266 `GND` must share one ground node. Power the ESP8266 from USB.

---

## Build and flash

Requires [PlatformIO](https://platformio.org/) (VS Code extension or CLI).

```bash
# choose a variant
cd firmware-pca9685        # or: cd firmware-gpio

# build and upload
pio run -t upload

# watch the debug log
pio device monitor         # 115200 baud
```

`platformio.ini` targets `nodemcuv2`. Change the `board` line for other boards (`d1_mini`, `esp12e`, ...). The PCA9685 variant pulls in the Adafruit PWM Servo Driver library automatically through `lib_deps`.

---

## Using the web interface

1. Power the arm and the ESP8266.
2. On your phone, join the WiFi network:
   - SSID: `EEZYbotARM`
   - Password: `eezybot123` (change it in `main.cpp`)
3. Open **http://192.168.4.1** in the browser. The serial monitor prints the exact address at boot.

**Joint control tab**: drag the sliders, then press **Send to arm**.

**X / Y / Z tab**:
1. Tap the **top view** to aim left/right and set reach. The shaded wedge is the reachable sector.
2. Tap the **side view** to set the height.
3. The dot turns **green** when reachable. Press **Go to position** to move the arm there.

The top view remembers your aim direction, so a side-view tap only changes reach and height.

At boot the arm moves to a home pose: q1 = 0°, q2 = 90°, q3 = −100°, gripper = 90°.

---

## HTTP API

The page uses two endpoints, which you can also call directly (curl, scripts, other apps).

| Method | Path | Description |
|---|---|---|
| GET | `/` | serves the control page |
| GET | `/move?q1=..&q2=..&q3=..&ee=..` | moves the arm |

`/move` takes **kinematic** joint angles in degrees. `q1`, `q2`, `q3` are required and `ee` (gripper) is optional, defaulting to 90.

```bash
curl "http://192.168.4.1/move?q1=0&q2=90&q3=-100&ee=90"
```

Response (servo angles actually commanded):

```json
{"ok":true,"servo_q1":91.73,"servo_q2":90.00,"servo_q3":125.00,"servo_ee":90.00}
```

A missing required parameter returns HTTP 400.

---

## Kinematics

The Mk1 link lengths (mm) are `L1 = 61`, `L2 = 80`, `L3 = 80`, `L4 = 57`.

**Forward kinematics** (joint angles → gripper position):

```
x = cos(q1) · (L2·cos(q2) + L3·cos(q2+q3) + L4)
y = sin(q1) · (L2·cos(q2) + L3·cos(q2+q3) + L4)
z = L1 + L2·sin(q2) + L3·sin(q2+q3)
```

**Inverse kinematics** (position → joint angles) is closed-form. It uses `atan2` for q1, then the law of cosines on the L2/L3 triangle for q2 and q3. There is no iteration and no Jacobian.

**Workspace check** rejects a point if:
1. the L2/L3 triangle cannot close (the point is too far or too close), or
2. the resulting q1, q2 or q3 falls outside its joint limit.

**Kinematic → servo angle mapping** (from the original library's `map_kinematicsToServoAngles`):

```
servo_q1 = -2.0497 · q1 + 91.726
servo_q2 = 180 − q2
servo_q3 = q2 − 45 + (180 + q3)
```

---

## Configuration and limits

All in `src/main.cpp`.

| Setting | Where | Default |
|---|---|---|
| WiFi SSID / password | `AP_SSID`, `AP_PASS` | `EEZYbotARM` / `eezybot123` |
| q1 limit | slider `id="q1"` **and** `Q1_MIN/Q1_MAX` | −20° to 30° |
| q2 limit | slider `id="q2"` **and** `Q2_MIN/Q2_MAX` | 39° to 120° |
| q3 limit | slider `id="q3"` **and** `q3Limits()` | depends on q2 |
| Gripper range | slider `id="ee"` | 10° to 100° |
| Servo pulse range (PCA9685) | `SERVOMIN`, `SERVOMAX` | 150 / 600 |
| Servo pins (GPIO variant) | `PIN_Q1`, `PIN_Q2`, `PIN_Q3`, `PIN_EE` | D1, D2, D5, D6 |

Limits live in two places that must be kept in sync: the **slider attributes** limit manual dragging, while the **JS constants** limit the X/Y/Z workspace check.

> **Widening a limit needs recalibration.** The mapping formulas were fitted for the original ranges. For q1 in particular, the slope of about 2 means a kinematic range of ±90° would ask the servo for angles far outside 0–180°. Check the mechanical range by hand first, then recalibrate the mapping across the new range.

---

## Calibration

If the arm does not reach the angles you command:

1. **Pulse range** (PCA9685): adjust `SERVOMIN`/`SERVOMAX` until 0° and 180° match a protractor.
2. **Mapping formulas**: command several known angles, measure the real angle, fit a straight line (`measured = m·commanded + b`), and update the matching `mapQ*ToServo()` function.
3. See the "Calibrating the EEZYbotARM" section of the [original README](https://github.com/meisben/easyEEZYbotARM#calibrating-the-eezybotarm) for the servo installation procedure.

---

## Troubleshooting

Open the serial monitor at 115200 baud. Every request is logged.

| Symptom | Likely cause |
|---|---|
| Can't see the `EEZYbotARM` network | ESP8266 not powered or crashed in `setup()`. Check the serial log |
| Page loads but the arm doesn't move | Servo power (`V+`) missing or low, or no common ground |
| Page says "OK" but nothing moves | The response only confirms the request and command were issued, not physical motion. Check power first |
| PCA9685 not found in I2C scan | Check SDA→D2, SCL→D1, VCC→3V3, common GND |
| I2C scan finds `0x40` but servos stay silent | Check servo power, then run `tools/servo_diagnostic_sweep_test.cpp`. If no channel moves, suspect the PCA9685 board itself. Try the `firmware-gpio` variant to confirm |
| Servos twitch or jitter | Almost always power. Use a separate, adequately sized supply. With 4 servos, AA cells hold up better than AAA |
| "Outside workspace" for a point that looks fine | Check the joint limits and that the target is inside the shaded top-view wedge |
| Garbled text at the start of the serial log | Normal ESP8266 boot output at a different baud rate |

**Diagnostic sweep**: copy `tools/servo_diagnostic_sweep_test.cpp` to `firmware-pca9685/src/main.cpp` (keep a backup of the original). It sweeps one channel with no WiFi or kinematics involved, isolating the hardware signal path.

---

## Known limitations

- **No authentication.** Anyone who joins the WiFi network can move the arm. Change the default password.
- **No server-side joint limits.** Joint limits are enforced in the browser only. The firmware clamps final servo angles to 0–180° but does not check kinematic limits, so a raw `/move` request can command poses outside the safe range.
- **Mk1 geometry only.** Link lengths are hard-coded for the Mk1.
- **Side-view reach envelope is approximate.** The drawn arcs are a guide. The exact check is the workspace test that runs on each tap.
- **GPIO variant timing.** Software PWM shares the CPU with the WiFi stack and can jitter under load.
- Hobby servos heat up and lose accuracy under load, as noted in the original project.

---

## Credits and license

- Kinematics, servo mapping formulas, and project concept: [**meisben/easyEEZYbotARM**](https://github.com/meisben/easyEEZYbotARM) (MIT)
- Robot arm design: Carlo Franciscone, [EEZYbotARM](http://www.eezyrobots.it/eba_mk2.html)
- [Adafruit PWM Servo Driver Library](https://github.com/adafruit/Adafruit-PWM-Servo-Driver-Library)

Released under the **MIT License**, see [LICENSE](LICENSE). It retains the original copyright notice as MIT requires. Replace `YOUR NAME HERE` with your name.
