# ESP32-C3 Independent Hotend Controller — Pin & Wiring Reference

The ESP32-C3 SuperMini (with built-in OLED) acts as a **self-contained
heater controller** for the secondary hotend.  It:

1. Receives the **set temperature** from Klipper via I2C
2. Reads the **actual temperature** from a PT1000 via its own ADC
3. Runs a **PID control loop** with LEDC PWM to drive a MOSFET / SSR
4. **Reports actual temp** back to Klipper via I2C read
5. Displays both temperatures and PID power % on the OLED

Device: [ESP32-C3 SuperMini with OLED](https://www.aliexpress.us/item/3256807156068355.html)

## ESP32-C3 Pin Assignments

| Function          | GPIO | Notes                                  |
|-------------------|------|----------------------------------------|
| OLED SDA (master) | 5    | Software I2C — built-in OLED           |
| OLED SCL (master) | 6    | Software I2C — built-in OLED           |
| I2C SDA (slave)   | 8    | Hardware I2C — from EBB2209/RP2040     |
| I2C SCL (slave)   | 9    | Hardware I2C — from EBB2209/RP2040     |
| MOSFET / SSR OUT  | 10   | Drives heater MOSFET/SSR (active HIGH) |
| PT1000 ADC        | 2    | ADC1_CH2 — voltage divider input       |

* OLED: SSD1306, 128×64 or 72×40, I2C address **0x3C**
* ESP32-C3 I2C slave address: **0x50**

## PT1000 Wiring (Voltage Divider)

```
3.3 V ──┬── [1 kΩ reference] ──┬── [PT1000] ── GND
        │                      │
        │                   GPIO2 (ADC)
        │
      (optional 100 nF cap to GND for noise filtering)
```

At 0 °C the PT1000 is 1000 Ω → mid-point ≈ 1.65 V.
At 300 °C it is ≈ 2120 Ω → ≈ 2.24 V.
The Callendar–Van Dusen equation converts resistance to °C.

ADC readings are passed through an **EMA filter** (α = 0.3) in software
to smooth noise before feeding the PID loop.  The ESP32-C3 does not have
a hardware IIR filter on its ADC (ESP32-C6/S3 do).

## EBB2209 (RP2040) → ESP32-C3 I2C Wiring

| Function | RP2040 GPIO | Klipper pin name |
|----------|-------------|------------------|
| I2C SDA  | gpio4       | `EBBCan:gpio4`   |
| I2C SCL  | gpio5       | `EBBCan:gpio5`   |

```
EBB2209 RP2040            ESP32-C3
──────────────            ────────
gpio4 (SDA) ───────────── GPIO8 (SDA)
gpio5 (SCL) ───────────── GPIO9 (SCL)
GND         ───────────── GND
3V3 (opt)   ───────────── 3V3 (if not self-powered)
```

> 4.7 kΩ pull-ups on SDA and SCL to 3.3 V (many dev boards
> already have on-board pull-ups).

## I2C Protocol (Klipper ↔ ESP32-C3)

**Bidirectional**: Klipper writes the target temp and reads the actual temp.

### Write (Klipper → ESP32)

| Register | Bytes | Description                                    |
|----------|-------|------------------------------------------------|
| 0x01     | 2     | Set temperature (°C × 10, uint16 BE). 0 = OFF |

Example: SET = 200.0 °C → `[0x01, 0x07, 0xD0]` (2000 = 0x07D0)
Example: heater OFF      → `[0x01, 0x00, 0x00]`

### Read (ESP32 → Klipper)

Master reads 2 bytes from the slave (no register address needed):

| Byte 0  | Byte 1 | Description                              |
|---------|--------|------------------------------------------|
| act_hi  | act_lo | Actual temp (°C × 10, uint16 BE)         |

Special value: `0xFFFF` = sensor fault.
Example: actual = 195.5 °C → `[0x07, 0x9B]` (1955 = 0x079B)

## MOSFET / SSR Notes

GPIO10 drives the heater MOSFET gate or SSR input (active HIGH).
The ESP32's PID controller outputs a 10 Hz PWM signal via the
LEDC peripheral with 8-bit resolution (0-255 duty).

Default PID: Kp=14.7, Ki=6.5, Kd=8.3 (Qidi Max3 factory PID_CALIBRATE values).
Anti-windup on integral. Derivative on measurement (no kick). **Tune for your hotend.**

> **WARNING:** GPIO10 is 3.3 V / ~12 mA — use it to drive an SSR
> signal input or a MOSFET gate driver, **never** the heater directly.

## Safety

* If no I2C message is received for **30 seconds**, the ESP32
  automatically sets the target to 0 (heater off) as a watchdog.
* Maximum temperature limit: **360 °C** (configurable in firmware).
* **Thermal runaway — not heating**: if duty ≥ 50 % for 120 s and
  temperature hasn’t risen at least 10 °C, the heater is faulted OFF.
  (broken element, bad MOSFET, wiring fault)
* **Thermal runaway — falling away**: if actual temp drops > 15 °C
  below target for 20+ s while actively heating, the heater is
  faulted OFF. (thermistor detached, heater block loose)
* All faults are **latching** — heater stays off until power cycle.
