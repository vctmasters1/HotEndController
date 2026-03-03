/*
 * pt1000.h — PT1000 temperature sensor via ESP32-C3 ADC
 *
 * Reads a PT1000 RTD through a voltage divider:
 *   3.3 V → [R_ref 1 kΩ] → ADC pin → [PT1000] → GND
 *
 * Uses the Callendar–Van Dusen equation to convert
 * measured resistance to temperature in °C.
 */
#pragma once

/* ── ADC GPIO pin ────────────────────────────────────────── */
#define PT1000_ADC_PIN      2       /* GPIO2 = ADC1_CH2 */

/* ── Reference resistor in the voltage divider (Ω) ──────── */
#define PT1000_REF_OHM      1000.0f

/* ── Supply voltage (V) ─────────────────────────────────── */
#define PT1000_VCC          3.3f

/** Initialise the ADC for PT1000 reading. */
void pt1000_init(void);

/**
 * Read the current PT1000 temperature.
 * Returns temperature in °C (averaged over multiple samples).
 * Returns -999.0 on read error.
 */
float pt1000_read_temp(void);
