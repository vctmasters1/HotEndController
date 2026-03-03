# ESP32-C3 Independent Hotend Controller — Project Instructions

> **For AI assistants and humans alike.**  This file is the single source of
> truth for the ESP32-C3 secondary hotend sub-project.  Read it first.

---

## Overview

An [ESP32-C3 SuperMini with built-in OLED](https://www.aliexpress.us/item/3256807156068355.html)
serves as a **self-contained secondary hotend controller** on a retrofitted
Qidi Max3 running Klipper with an EBB2209 (RP2040) CAN-bus toolhead.

**Dual-heater operation** — both heaters run simultaneously:

| Heater   | Controlled by       | Set via            |
|----------|---------------------|--------------------|
| Primary  | Klipper (extruder)  | `M104` / `M109`   |
| Hotend 2 | ESP32-C3 (this fw)  | `M104_HOTEND2` / `M109_HOTEND2` |

The slicer start g-code must set temps for **both** heaters.

---

## What the ESP32 Does (Autonomously)

1. **Receives set temperature** from Klipper via I2C slave (addr 0x50, register 0x01)
2. **Reads actual temperature** from a PT1000 RTD via ADC (GPIO2, 1 kΩ voltage divider)
3. **EMA filters** the ADC reading (α = 0.3) to smooth noise before PID sees it
4. **Runs PID heater control** — LEDC PWM at 10 Hz / 8-bit driving MOSFET/SSR (GPIO10)
5. **Reports actual temperature** back to Klipper via I2C read (2 bytes, uint16 BE, °C × 10)
6. **Displays on OLED** — SET temp, ACT temp, PWR % (or FAULT)
7. **Safety** — 30 s I2C watchdog, 360 °C over-temp limit, sensor-fault detection, thermal runaway detection, latching fault

---

## Workspace & Directory Layout

```
3d-Qidi-Max3-MyMod/                ← workspace root
│
├── Factory/                        ← ⚠ REFERENCE ONLY — do NOT modify
│   ├── printer.cfg                    (stock Qidi Max3 Klipper config)
│   └── …                             (factory configs, moonraker, etc.)
│
├── MyMod-03012026/                 ← active Klipper config modules
│   ├── esp32_temp_display.py          Klipper extra (→ ~/klipper/klippy/extras/)
│   ├── ESP-PT1000-2.cfg              [esp32_temp_display] + convenience macros
│   ├── Qidi-Max3-MyMod-EBB-RP-double.cfg  main config (includes ESP-PT1000-2.cfg)
│   ├── Qidi-max3-macros.cfg          PRINT_END calls HOTEND2_OFF
│   └── …                             (kinematics, fans, heaters, sensors, etc.)
│
└── ESP32C3wOLED/                   ← THIS directory: ESP-IDF firmware project
    ├── AI-Instruct.md                 (this file)
    ├── pinout.md                      wiring, I2C protocol, PT1000 circuit
    ├── CMakeLists.txt
    ├── sdkconfig.defaults
    └── main/
        ├── main.c                     PID task (4 Hz) + OLED task (low priority)
        ├── heater_control.c/.h        PID controller + LEDC PWM
        ├── i2c_slave_handler.c/.h     bidirectional I2C slave (hw peripheral)
        ├── pt1000.c/.h                PT1000 ADC + Callendar-Van Dusen
        ├── ssd1306.c/.h               OLED driver (sw bit-bang I2C)
        └── font5x7.h                  5×7 bitmap font
```

> **Factory/ is read-only reference.**  Those files are the original Qidi Max3
> factory Klipper configuration, kept for comparison.  Never edit them.

---

## Klipper Integration

### Files to install
| File                    | Destination                        |
|-------------------------|------------------------------------|
| `esp32_temp_display.py` | `~/klipper/klippy/extras/`         |
| `ESP-PT1000-2.cfg`      | included via `Qidi-Max3-MyMod-EBB-RP-double.cfg` |

### I2C bus
Routed through the EBBCan toolhead MCU:
- SDA = `EBBCan:gpio4` → ESP32 GPIO8
- SCL = `EBBCan:gpio5` → ESP32 GPIO9

### GCode commands
| Command                  | Behaviour                                |
|--------------------------|------------------------------------------|
| `SET_HOTEND2_TEMP S=200` | Set target (non-blocking)                |
| `SET_HOTEND2_TEMP`       | Query current temps (no S param)         |
| `HOTEND2_OFF`            | Turn off heater (target = 0)             |
| `M104_HOTEND2 S=200`     | Alias — non-blocking, like M104         |
| `M109_HOTEND2 S=200`     | **Blocking wait** until temp reached     |
| `WAIT_HOTEND2`           | Block until current target ±3 °C         |

### Dashboard
Hotend2 actual temp appears in Fluidd/Mainsail via `get_status()`.

### Print lifecycle
- **PRINT_END** → calls `HOTEND2_OFF`
- **PAUSE** → hotend2 keeps its target (ESP32 watchdog stays fed by 1 Hz re-send)
- **RESUME** → no special action needed; primary extruder re-heats via Klipper

---

## I2C Protocol (Klipper ↔ ESP32)

| Direction | Register | Bytes | Encoding                     | Notes              |
|-----------|----------|-------|------------------------------|---------------------|
| Write     | 0x01     | 2     | uint16 BE, °C × 10           | 0 = heater off      |
| Read      | —        | 2     | uint16 BE, °C × 10           | 0xFFFF = sensor fault|

Klipper re-sends the target at 1 Hz; the ESP32 treats this as a watchdog keep-alive.
If no write arrives for 30 s the ESP32 sets target to 0 (heater off).

---

## PID Tuning

| Parameter | Default | Source                                |
|-----------|---------|---------------------------------------|
| Kp        | 14.7    | Qidi Max3 factory `PID_CALIBRATE`     |
| Ki        | 6.5     | (extruder, from `Factory/printer.cfg`)|
| Kd        | 8.3     |                                       |

- Output range: 0–255 (Klipper-compatible scaling)
- Anti-windup on integral; derivative on measurement (no kick)
- **Tune for your specific hotend** — these are starting defaults

---

## Pin Summary

| GPIO | Function               | Notes                         |
|------|------------------------|-------------------------------|
| 2    | PT1000 ADC input       | ADC1_CH2, 1 kΩ voltage divider|
| 5    | OLED SDA               | Software bit-bang I2C         |
| 6    | OLED SCL               | Software bit-bang I2C         |
| 8    | I2C slave SDA          | Hardware I2C peripheral       |
| 9    | I2C slave SCL          | Hardware I2C peripheral       |
| 10   | MOSFET/SSR gate        | LEDC PWM, 10 Hz, 8-bit       |

---

## Build & Flash

```bash
cd ESP32C3wOLED
idf.py set-target esp32c3
idf.py build
idf.py -p COMx flash monitor
```

---

## Key Design Decisions (quick reference)

- **Two FreeRTOS tasks** — PID at priority 5 (4 Hz, never blocked), OLED at priority 2 (runs only during PID idle time). The ~80 ms OLED bit-bang transfer cannot delay PID.
- **EMA filter on ADC** — α = 0.3, software low-pass (ESP32-C3 has no hardware IIR filter; ESP32-C6/S3 do). Smooths ±1–2 °C ADC jitter without hiding real temp changes.
- **Software I2C for OLED** — frees the single hardware I2C peripheral for slave mode
- **PID, not bang-bang** — 115 W element needs proportional control to avoid overshoot
- **Bidirectional I2C** — master writes target, reads actual (TX FIFO preloaded each cycle)
- **4 Hz control loop** — 250 ms period; PID computes `dt` from wall-clock for jitter tolerance
- **Autonomous operation** — ESP32 maintains temp independently; Klipper only sets the target

