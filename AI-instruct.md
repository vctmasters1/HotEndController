https://github.com/vctmasters1/HotEndController.git

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

| Mode | Include line | Toolhead | Hotend2 | Description |
|------|-------------|----------|---------|-------------|
| **Factory** | `[include Factory/printer.cfg]` | MKS_THR (USB) | — | Original monolithic Qidi config — standalone, no shared section needed |
| **Stock** | `[include MyMod-03012026/Qidi-Max3-STOCK-main.cfg]` | MKS_THR (USB) | — | Modular split of stock config — same hardware, cleaner organisation |
| **MyMod (single)** | `[include MyMod-03012026/Qidi-Max3-MyMod-EBB-RP-single.cfg]` | EBB2209 (CAN) | — | EBB2209/RP2040 CAN-bus toolhead, single heater only |
| **MyMod (dual)** | `[include MyMod-03012026/Qidi-Max3-MyMod-EBB-RP-double.cfg]` | EBB2209 (CAN) | ESP32-C3 | EBB2209/RP2040 CAN-bus toolhead + ESP32-C3 secondary hotend |

To switch modes, edit `printer.cfg` and uncomment exactly **one**
`[include ...]` line.  Factory mode is standalone; Stock and both MyMod
variants require the shared `[mcu]` and `[include Factory/Adaptive_Mesh.cfg]`
section to be active.

The second heating element is designed to be easy to attach and remove.
When it is **physically connected**, use MyMod (dual).  When it is
**removed**, switch to MyMod (single) — this avoids I2C errors from
trying to talk to a missing ESP32.

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
│   ├── Qidi-Max3-MyMod-EBB-RP-double.cfg  MyMod DUAL entry (EBB2209 + ESP32)
│   ├── Qidi-Max3-MyMod-EBB-RP-single.cfg  MyMod SINGLE entry (EBB2209 only)
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
│   ├── ESP-PT1000-2.cfg               MyMod dual-only: ESP32 hotend2 config
│   ├── esp32_temp_display.py           MyMod dual-only: Klipper extra module
│   │
│   └── MY-Qidi-Max3-STOCK-printer.cfg reference: stock as single file
│
└── ESP32C3wOLED/                   ← ESP-IDF firmware project
    └── (see ESP32C3wOLED/AI-Instruct.md)
```

---

## Include Chain

### MyMod — dual heater (EBB2209 CAN bus + ESP32-C3)

```
printer.cfg
 ├─ Factory/Adaptive_Mesh.cfg
 └─ MyMod-03012026/Qidi-Max3-MyMod-EBB-RP-double.cfg
     ├─ Qidi-Max3-kinematics.cfg
     ├─ Qidi-Max3-heaters.cfg
     ├─ Qidi-Max3-fans.cfg
     ├─ Qidi-Max3-sensors.cfg
     ├─ Qidi-max3-macros.cfg
     ├─ Qidi-Max3-EBB2209-RP2040.cfg    ← CAN toolhead
     └─ ESP-PT1000-2.cfg                ← ESP32-C3 hotend2 controller
```

### MyMod — single heater (EBB2209 CAN bus only)

```
printer.cfg
 ├─ Factory/Adaptive_Mesh.cfg
 └─ MyMod-03012026/Qidi-Max3-MyMod-EBB-RP-single.cfg
     ├─ Qidi-Max3-kinematics.cfg
     ├─ Qidi-Max3-heaters.cfg
     ├─ Qidi-Max3-fans.cfg
     ├─ Qidi-Max3-sensors.cfg
     ├─ Qidi-max3-macros.cfg
     ├─ Qidi-Max3-EBB2209-RP2040.cfg    ← CAN toolhead
     └─ (no ESP32 — hotend2 macros are no-op stubs)
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
     └─ Qidi-Max3-STOCK.cfg             ← USB toolhead + HOTEND2_OFF stub
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
  used by Stock and both MyMod variants — changes apply to all.
- **Toolhead-specific** files (`EBB2209-RP2040.cfg` vs `STOCK.cfg`)
  are only loaded by their respective main config.
- **ESP32 hotend2** files (`ESP-PT1000-2.cfg`, `esp32_temp_display.py`)
  are only loaded by the MyMod **dual** config.
- **Hotend2 no-op stubs** (`HOTEND2_OFF`, `SET_HOTEND2_TEMP`, etc.)
  are defined in both `Qidi-Max3-MyMod-EBB-RP-single.cfg` and
  `Qidi-Max3-STOCK.cfg` so that shared macros (e.g. `PRINT_END`)
  can safely call `HOTEND2_OFF` regardless of mode.
- When adding a new shared module, include it in **all** main configs.
- Klipper's `SAVE_CONFIG` block goes at the bottom of the active main
  config (e.g., `Qidi-Max3-MyMod-EBB-RP-double.cfg`), not `printer.cfg`.