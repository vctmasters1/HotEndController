# Qidi Max3 — Modular Klipper Configuration Architecture

> Project-level instructions for AI assistants and contributors.
> For the ESP32-C3 hotend controller sub-project, see
> [ESP32C3wOLED/AI-Instruct.md](ESP32C3wOLED/AI-Instruct.md).

---

## Design Goal

A single repository that supports **three configuration modes** for the
Qidi Max3, selectable by editing one file (`printer.cfg`).  All modular
configs live in a date-stamped subdirectory and share common modules so
changes to kinematics, heaters, fans, sensors, or macros apply to both
Stock and MyMod profiles.

---

## Configuration Modes

| Mode | Include line | Toolhead | Description |
|------|-------------|----------|-------------|
| **Factory** | `[include Factory/printer.cfg]` | MKS_THR (USB) | Original monolithic Qidi config — standalone, no shared section needed |
| **Stock** | `[include MyMod-03012026/Qidi-Max3-STOCK-main.cfg]` | MKS_THR (USB) | Modular split of stock config — same hardware, cleaner organisation |
| **MyMod** | `[include MyMod-03012026/Qidi-Max3-MyMod-EBB-RP-main.cfg]` | EBB2209 (CAN) | Retrofit EBB2209/RP2040 CAN-bus toolhead + ESP32-C3 secondary hotend |

To switch modes, edit `printer.cfg` and uncomment exactly **one**
`[include ...]` line.  Factory mode is standalone; Stock and MyMod
require the shared `[mcu]` and `[include Factory/Adaptive_Mesh.cfg]`
section to be active.

---

## Directory Layout

```
printer.cfg                         ← entry point (mode selector)
│
├── Factory/                        ← REFERENCE ONLY — do NOT modify
│   ├── printer.cfg                    original monolithic Qidi config
│   ├── Adaptive_Mesh.cfg              shared by Stock/MyMod modes
│   ├── MKS_THR.cfg                    stock toolhead MCU config
│   ├── config.mksini                  MKS board config
│   ├── fluidd.cfg                     Fluidd web interface
│   ├── moonraker.conf                 Moonraker API server
│   ├── KlipperScreen.conf             touchscreen UI
│   └── webcam.txt                     webcam config
│
├── MyMod-03012026/                 ← modular config modules
│   ├── Qidi-Max3-MyMod-EBB-RP-main.cfg   MyMod entry (EBB2209 CAN)
│   ├── Qidi-Max3-STOCK-main.cfg          Stock entry (MKS_THR USB)
│   │
│   ├── Qidi-Max3-kinematics.cfg       shared: steppers, bed mesh, homing
│   ├── Qidi-Max3-heaters.cfg          shared: extruder, bed, chamber heater
│   ├── Qidi-Max3-fans.cfg             shared: part cooling, hotend, exhaust
│   ├── Qidi-Max3-sensors.cfg          shared: filament sensor, endstops
│   ├── Qidi-max3-macros.cfg           shared: PRINT_END, PAUSE, RESUME, etc.
│   │
│   ├── Qidi-Max3-EBB2209-RP2040.cfg   MyMod-only: CAN toolhead MCU
│   ├── Qidi-Max3-STOCK.cfg            Stock-only: USB toolhead MCU
│   │
│   ├── ESP-PT1000-2.cfg               MyMod-only: ESP32 hotend2 config
│   ├── esp32_temp_display.py           MyMod-only: Klipper extra module
│   │
│   └── MY-Qidi-Max3-STOCK-printer.cfg reference: stock as single file
│
└── ESP32C3wOLED/                   ← ESP-IDF firmware project
    └── (see ESP32C3wOLED/AI-Instruct.md)
```

---

## Include Chain

### MyMod (EBB2209 CAN bus)

```
printer.cfg
 ├─ Factory/Adaptive_Mesh.cfg
 └─ MyMod-03012026/Qidi-Max3-MyMod-EBB-RP-main.cfg
     ├─ Qidi-Max3-kinematics.cfg
     ├─ Qidi-Max3-heaters.cfg
     ├─ Qidi-Max3-fans.cfg
     ├─ Qidi-Max3-sensors.cfg
     ├─ Qidi-max3-macros.cfg
     ├─ Qidi-Max3-EBB2209-RP2040.cfg    ← CAN toolhead
     └─ ESP-PT1000-2.cfg                ← ESP32-C3 hotend2 controller
```

### Stock (MKS_THR USB)

```
printer.cfg
 ├─ Factory/Adaptive_Mesh.cfg
 └─ MyMod-03012026/Qidi-Max3-STOCK-main.cfg
     ├─ Qidi-Max3-kinematics.cfg
     ├─ Qidi-Max3-heaters.cfg
     ├─ Qidi-Max3-fans.cfg
     ├─ Qidi-Max3-sensors.cfg
     ├─ Qidi-max3-macros.cfg
     └─ Qidi-Max3-STOCK.cfg             ← USB toolhead
```

### Factory (standalone)

```
printer.cfg
 └─ Factory/printer.cfg                 ← everything in one file
```

---

## Editing Guidelines

- **Factory/** is read-only reference.  Never edit these files.
- **Shared modules** (kinematics, heaters, fans, sensors, macros) are
  used by both Stock and MyMod — changes apply to both.
- **Toolhead-specific** files (`EBB2209-RP2040.cfg` vs `STOCK.cfg`)
  are only loaded by their respective main config.
- **ESP32 hotend2** files (`ESP-PT1000-2.cfg`, `esp32_temp_display.py`)
  are only loaded by the MyMod config.
- When adding a new shared module, include it in **both** main configs.
- Klipper's `SAVE_CONFIG` block goes at the bottom of the active main
  config (e.g., `Qidi-Max3-MyMod-EBB-RP-main.cfg`), not `printer.cfg`.