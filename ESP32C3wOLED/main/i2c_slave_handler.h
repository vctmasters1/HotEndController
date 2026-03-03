/*
 * i2c_slave_handler.h — Hardware I2C slave for bidirectional
 *                       communication with Klipper
 *
 * Protocol (master writes to slave at address 0x50):
 *   Register 0x01 — set temperature (2 bytes, uint16 BE, °C × 10)
 *                   Send 0 to turn the heater off.
 *
 *   Example: 200.0 °C → [0x01, 0x07, 0xD0]
 *   Example: OFF      → [0x01, 0x00, 0x00]
 *
 * Protocol (master reads from slave at address 0x50):
 *   The slave pre-loads its TX buffer with 2 bytes:
 *     [actual_hi, actual_lo]  — uint16 BE, °C × 10
 *   The master can read at any time to get latest actual temp.
 *   A read value of 0xFFFF indicates a sensor fault.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* ── I2C slave GPIO pins (from EBB2209/RP2040) ───────────── */
#define I2C_SLAVE_SDA_PIN   8
#define I2C_SLAVE_SCL_PIN   9

/* ── I2C slave address (7-bit) ───────────────────────────── */
#define I2C_SLAVE_ADDR      0x50

/** Install the hardware I2C slave driver. */
void i2c_slave_init(void);

/**
 * Drain the I2C slave RX buffer and parse register writes.
 * Call this periodically from the main loop.
 * Returns true if a new set-point was received this cycle.
 */
bool i2c_slave_process(void);

/** Last received set-point temperature (°C). */
float i2c_slave_get_set_temp(void);

/**
 * Microsecond timestamp of the last valid I2C message.
 * Used for the communications watchdog.
 */
int64_t i2c_slave_last_rx_time(void);

/**
 * Update the TX buffer with the current actual temperature.
 * Call from main loop after reading PT1000.
 * The master can read these 2 bytes at any time.
 * Pass -999.0 to indicate sensor fault (TX will be 0xFFFF).
 */
void i2c_slave_set_actual_temp(float actual_c);
