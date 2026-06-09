# CamCar wiring

The camera is **onboard** — no wiring. What you wire is the drive train and
peripherals: two DC motors through an **L298N** H-bridge, two servos for the
camera gimbal, a headlight LED, and (S3 only) a battery-voltage divider.

`src/board_config.h` is the single source of truth for every GPIO; the tables
below mirror it. All logic is **3.3 V**; the L298N accepts 3.3 V logic on its
inputs.

## Bill of materials

- ESP32 board: **Freenove ESP32-S3-WROOM CAM** (primary) or AI-Thinker ESP32-CAM
- **L298N** dual H-bridge motor driver module
- 2× DC gear motors (the drive tracks/wheels)
- 2× hobby servos (pan + tilt)
- 1× LED + ~220 Ω resistor (headlight)
- **2S LiPo** (7.4 V nominal, 6.0–8.4 V) + the firmware's voltage range
- S3 only: 2 resistors for the battery divider — **200 kΩ** (top) + **100 kΩ** (bottom)
- Recommended: a separate **5 V BEC** for the servos (see power notes)

## Power distribution

```
  2S LiPo (6.0–8.4 V)
     │ +            ┌──────────────── L298N ────────────────┐
     ├──────────────► +12V (motor supply)                   │
     │              │   on-board 5 V reg ── 5V out ──┐       │
     │              └───────────────────────────────│───────┘
     │                                               │
     │                              ┌────────────────┴───────────┐
     │                              ▼                            ▼
     │                       ESP 5V pin (or USB)          servo + (see note)
     │ −
     └──────────────────── COMMON GROUND ───────────────────────┐
        (LiPo −, L298N GND, ESP GND, servo GND, LED −, divider −)┘
```

**Notes**
- Tie **all grounds together** — battery, L298N, ESP, servos, LED, divider. A
  floating ground between the ESP and the L298N is the #1 cause of erratic drive.
- The L298N's on-board 5 V regulator (7805) is weak (~0.5 A) and **browns out
  under servo stall current**. Prefer a **separate 5 V BEC** for the two servos,
  or power the ESP over USB and only run motors off the LiPo. Don't hang both
  servos *and* the ESP off the L298N's 5 V pin.
- Keep the L298N's **5 V-enable jumper ON** only when the motor supply is ≤ 12 V
  (it is here). The motor supply feeds the on-board regulator.

## Motors (L298N, sign-magnitude PWM)

Each motor's speed is PWM on whichever **input** matches its direction — the
firmware PWMs the active input and holds the other low (`driveMotor()`), so there
is **no separate enable channel**. Tie the L298N **ENA/ENB enables HIGH** (leave
the module's ENA/ENB jumpers on); their GPIO is freed.

```
   ESP GPIO ──► L298N IN1 ─┐
   ESP GPIO ──► L298N IN2 ─┴─► OUT1/OUT2 ──► RIGHT motor
   ESP GPIO ──► L298N IN3 ─┐
   ESP GPIO ──► L298N IN4 ─┴─► OUT3/OUT4 ──► LEFT motor
   3.3 V ─────► ENA, ENB (jumpers ON = always enabled)
```

If a motor runs backwards, swap its two OUT wires (or its two IN GPIOs).

## Servos (pan + tilt)

Standard 3-wire hobby servos:

```
   signal ──► ESP GPIO (pan or tilt)
   +5V    ──► 5 V rail  (BEC recommended — see power notes)
   GND    ──► common ground
```

## Headlight

```
   ESP GPIO ──► [220 Ω] ──►|LED|──► GND
```

Driven on/off (`digitalWrite`) by the UI's Light toggle. On the AI-Thinker the
"headlight" is the very bright onboard flash LED (GPIO 4) — no external LED
needed there.

## Battery sense (S3 only)

A divider scales the pack voltage into the ADC range (≤ 3.3 V):

```
   Vbat ──[ R1 200k ]──┬──[ R2 100k ]── GND
                       │
                       └──► GPIO1 (ADC1_CH0)
```

`Vadc = Vbat × 100k/(100k+200k) = Vbat / 3` → 8.4 V maps to 2.8 V, safely under
the ADC ceiling. The firmware constants must match the parts:
`BATTERY_DIVIDER = 3.0`, `BATTERY_VMIN = 6.0`, `BATTERY_VMAX = 8.4` (in
`CamCar.ino`). The AI-Thinker has **no free ADC1 pin**, so it has no battery
sense.

## GPIO map — Freenove ESP32-S3 (primary)

| Signal | GPIO | Connect to |
|---|---|---|
| Right motor IN1 / IN2 | 41 / 42 | L298N IN1 / IN2 |
| Left motor IN1 / IN2 | 40 / 39 | L298N IN3 / IN4 |
| Motor enable | — | tie L298N ENA/ENB **HIGH** (jumpers on) |
| Pan servo | 47 | servo signal |
| Tilt servo | 21 | servo signal |
| Headlight | 14 | LED + 220 Ω resistor |
| Battery sense | 1 | divider midpoint (ADC1_CH0) |
| Status LED | 2 | onboard (no wiring) |

## GPIO map — AI-Thinker ESP32-CAM (legacy)

| Signal | GPIO | Connect to | Caveat |
|---|---|---|---|
| Right motor IN1 / IN2 | 13 / 15 | L298N IN1 / IN2 | |
| Left motor IN1 / IN2 | 14 / 2 | L298N IN3 / IN4 | GPIO2 is a strapping pin (idles low — OK) |
| Pan servo | 12 | servo signal | **strapping pin (MTDI): must be LOW at boot** |
| Tilt servo | 3 | servo signal | U0RXD — sacrifices serial **input**; servo twitches during flashing |
| Headlight | 4 | onboard flash LED | very bright; no external LED |
| Status LED | 33 | onboard red LED | active **LOW** |
| Battery sense | — | — | no free ADC1 pin |

**AI-Thinker boot caveats:** GPIO 12 (pan) must not be held high at reset or the
board picks the wrong flash voltage — a servo signal line idles low, so it's
fine, but don't tie it high. GPIO 3 (tilt) is the serial RX line; you can still
flash over USB, but the serial *monitor input* is gone and the tilt servo
twitches while flashing. The microSD slot is sacrificed (its pins are the left
motor).

## Full system

```
                     2S LiPo (6.0–8.4 V)
                       │+                              │−
                       ▼                               │
      ┌──────────────── L298N ─────────────────┐       │
      │ +12V   ENA┐                  5V   GND   │       │
      │        ENB┘(HIGH)             │    │    │       │
      │ IN1 IN2 IN3 IN4    OUT1 OUT2 OUT3 OUT4  │       │
      └──┬───┬───┬───┬──────┬────┬────┬────┬────┘       │
         │   │   │   │       └─R motor┘ └─L motor┘       │
   GPIO  41  42  40  39                                 │
         │   │   │   │     ┌── 5V rail (BEC) ──┐         │
   ┌─────┴───┴───┴───┴ ESP32-S3 ──────┐        │         │
   │  47 ─ pan servo signal           │   pan +5V◄┘  pan GND ─┐
   │  21 ─ tilt servo signal          │   tilt+5V◄┘ tilt GND ─┤
   │  14 ─[220Ω]─►|LED|─ headlight ── GND ───────────────────┤
   │   1 ◄─ divider midpoint (R1 200k / R2 100k) ── Vbat/GND ─┤
   │  5V ◄─ from L298N 5V or USB                              │
   │ GND ─────────────────── COMMON GROUND ──────────────────┘
   └──────────────────────────────────────────┘
```
