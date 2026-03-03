# HotEndController

ESP32-C3 autonomous secondary hotend controller for a retrofitted **Qidi Max3** with an **EBB2209 (RP2040) CAN-bus toolhead**.

Both heaters run simultaneously — Klipper controls the primary extruder as normal, while the ESP32-C3 independently reads a PT1000, runs PID, and drives a second heater via MOSFET/SSR.

## Hardware

- **Qidi Max3** — MKS SKIPR (STM32F407) main board
- **EBB2209 (RP2040)** — CAN-bus toolhead MCU
- **ESP32-C3 SuperMini with OLED** — [AliExpress](https://www.aliexpress.us/item/3256807156068355.html)
- **PT1000 RTD** — secondary hotend temperature sensor
- **MOSFET or SSR** — driven by ESP32 GPIO10 (3.3 V logic)
- 1 kΩ reference resistor for PT1000 voltage divider
- 4.7 kΩ pull-ups on I2C SDA/SCL (if not on dev board)

See [ESP32C3wOLED/pinout.md](ESP32C3wOLED/pinout.md) for full wiring diagrams.

## Repository Structure

```
├── printer.cfg                     Entry point — select Factory, Stock, or MyMod
├── Factory/                        Original Qidi Max3 configs (REFERENCE ONLY)
├── MyMod-03012026/                 Modular Klipper config for EBB2209 toolhead
│   ├── Qidi-Max3-MyMod-EBB-RP-double.cfg  Dual heater (EBB2209 + ESP32 hotend2)
│   ├── Qidi-Max3-MyMod-EBB-RP-single.cfg  Single heater (EBB2209 only)
│   ├── esp32_temp_display.py       Klipper extra — bidirectional I2C with ESP32
│   ├── ESP-PT1000-2.cfg            Hotend2 config + M104/M109 macros
│   └── …                          Kinematics, heaters, fans, sensors, macros
└── ESP32C3wOLED/                   ESP-IDF firmware for the ESP32-C3
    ├── AI-Instruct.md              Detailed project instructions
    ├── pinout.md                   Wiring, I2C protocol, PT1000 circuit
    └── main/                       Firmware source (PID, I2C, ADC, OLED)
```

## Installation on the Printer

### 1. Copy config files to the printer

SSH into your Qidi Max3 (default user `mks`, typically at `mks@your-printer-ip`):

```bash
# From your PC, copy the config directories to the printer
scp -r MyMod-03012026 mks@<printer-ip>:~/klipper_config/
scp -r Factory mks@<printer-ip>:~/klipper_config/
```

### 2. Replace printer.cfg

Back up the existing printer.cfg first, then copy ours:

```bash
# On the printer (via SSH)
cd ~/klipper_config
cp printer.cfg printer.cfg.bak       # backup original
```

```bash
# From your PC
scp printer.cfg mks@<printer-ip>:~/klipper_config/printer.cfg
```

The new `printer.cfg` is a selector — it includes **one** of four configurations:

| Option | Line to uncomment | Description |
|--------|-------------------|-------------|
| Factory | `[include Factory/printer.cfg]` | Original monolithic Qidi config |
| MyMod (dual) | `[include MyMod-03012026/Qidi-Max3-MyMod-EBB-RP-double.cfg]` | EBB2209 CAN toolhead + ESP32 hotend2 |
| MyMod (single) | `[include MyMod-03012026/Qidi-Max3-MyMod-EBB-RP-single.cfg]` | EBB2209 CAN toolhead only — no ESP32 |
| Stock | `[include MyMod-03012026/Qidi-Max3-STOCK-main.cfg]` | Original MKS_THR toolhead (modular split) |

By default, **MyMod dual** (EBB2209 + ESP32) is active. To switch, edit `printer.cfg` and uncomment the desired `[include]` line (comment out the others).

The second heating element is designed to be **easy to attach and remove**.
When it is physically connected, use MyMod (dual).  When it is removed,
switch to MyMod (single) to avoid I2C errors.

> **Note:** Factory mode is standalone — comment out the `[mcu]` and `[include Factory/Adaptive_Mesh.cfg]` shared section when using it.

### 3. Install the Klipper extra (dual-heater mode only)

The ESP32 hotend controller needs a custom Klipper module.
**Skip this step if using MyMod (single) or Stock mode.**

```bash
# On the printer (via SSH)
cp ~/klipper_config/MyMod-03012026/esp32_temp_display.py ~/klipper/klippy/extras/
sudo systemctl restart klipper
```

### 4. Build and flash the ESP32-C3 firmware

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/get-started/) (v5.x recommended):

```bash
cd ESP32C3wOLED
idf.py set-target esp32c3
idf.py build
idf.py -p COMx flash monitor    # replace COMx with your serial port
```

### 5. Wire the ESP32-C3 to the EBB2209

Connect the I2C bus between the EBB2209 and ESP32-C3:

| EBB2209 RP2040 | ESP32-C3 | Function |
|-----------------|----------|----------|
| gpio4 | GPIO8 | I2C SDA |
| gpio5 | GPIO9 | I2C SCL |
| GND | GND | Ground |
| 3V3 (optional) | 3V3 | Power (if not self-powered) |

See [pinout.md](ESP32C3wOLED/pinout.md) for the PT1000 and MOSFET wiring.

### 6. Verify

After restarting Klipper:

```
# In Fluidd/Mainsail console:
SET_HOTEND2_TEMP              # should report actual=xx.x°C target=0.0°C
SET_HOTEND2_TEMP S=100        # set secondary hotend to 100°C
HOTEND2_OFF                   # turn it off
```

Hotend2 temperature should appear in the Fluidd/Mainsail dashboard.

## GCode Commands

| Command | Description |
|---------|-------------|
| `SET_HOTEND2_TEMP S=200` | Set secondary hotend target (non-blocking) |
| `SET_HOTEND2_TEMP` | Query current temps |
| `HOTEND2_OFF` | Turn off secondary hotend |
| `M104_HOTEND2 S=200` | Non-blocking set (like M104) |
| `M109_HOTEND2 S=200` | Blocking wait until reached (like M109) |
| `WAIT_HOTEND2` | Block until current target ±3°C |

### Slicer start G-code

Set both heaters in your slicer's start G-code:

```gcode
M104 S250                ; primary extruder
M104_HOTEND2 S=200       ; secondary hotend
M140 S60                 ; bed
M109 S250                ; wait for primary
M109_HOTEND2 S=200       ; wait for secondary
```

## Safety Features

- **I2C watchdog** — heater off after 30 s with no communication
- **Over-temperature** — hard limit at 360°C
- **Sensor fault** — open/short circuit detection
- **Thermal runaway (not heating)** — faults if duty ≥50% for 120 s with <10°C rise
- **Thermal runaway (falling away)** — faults if temp drops >15°C below target for 20 s (after initial reach)
- All faults are **latching** — heater stays off until power cycle

## License

Private project.
