/*
 * heater_control.h — PID heater controller with LEDC PWM
 *
 * Software PID drives a MOSFET/SSR via LEDC PWM at ~10 Hz.
 * Call heater_update() from the main loop at ~4 Hz.
 *
 * Safety features:
 *   - Max temperature limit (default 360 °C)
 *   - Sensor fault detection (open / short)
 *   - Thermal runaway detection (not heating / falling away)
 *   - All faults latch the heater OFF until reset
 *   - Anti-windup on integral term
 *   - Derivative on measurement (no derivative kick)
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* ── MOSFET / SSR output GPIO ────────────────────────────── */
#define HEATER_OUT_PIN      10

/* ── Safety limits ───────────────────────────────────────── */
#define HEATER_MAX_TEMP     360.0f  /* absolute safety limit  */

/* ── Thermal runaway detection ───────────────────────────── */
/* "Not heating": duty > threshold for N seconds with < M °C rise */
#define RUNAWAY_DUTY_THRESH  50.0f  /* % duty to start watching      */
#define RUNAWAY_HEAT_TIME_S 120     /* seconds at high duty          */
#define RUNAWAY_MIN_RISE_C   10.0f  /* must rise at least this much  */

/* "Falling away": temp drops > N °C below target for M seconds    */
#define RUNAWAY_DROP_THRESH  15.0f  /* °C below target               */
#define RUNAWAY_DROP_TIME_S  20     /* seconds of sustained drop     */

/* ── PID defaults ────────────────────────────────────────── */
/* Based on Qidi Max3 factory Klipper PID_CALIBRATE results. */
/* Tune for YOUR hotend: run Klipper PID_CALIBRATE or use    */
/* Ziegler-Nichols method and update these values.           */
#define PID_KP_DEFAULT      14.7f
#define PID_KI_DEFAULT       6.5f
#define PID_KD_DEFAULT       8.3f

/* ── PWM configuration ───────────────────────────────────── */
#define HEATER_PWM_FREQ_HZ  10      /* 10 Hz — fine for heaters */
#define HEATER_PWM_BITS      8      /* 0-255 duty resolution    */

/** Initialise the LEDC PWM on HEATER_OUT_PIN (starts OFF). */
void heater_init(void);

/**
 * Set the target temperature (°C).
 * Pass 0.0 to turn the heater off.
 */
void heater_set_target(float target_c);

/** Get the current target temperature. */
float heater_get_target(void);

/**
 * Run one PID control cycle.
 * @param actual_temp  current temperature from PT1000 (°C).
 *                     Pass -999.0 to signal a sensor fault.
 */
void heater_update(float actual_temp);

/**
 * Returns true if the heater is outputting any power (duty > 0).
 */
bool heater_is_on(void);

/** Returns true if a safety fault has latched the heater off. */
bool heater_is_faulted(void);

/** Clear a latched fault (heater stays off until next set_target). */
void heater_clear_fault(void);

/** Get current PWM duty cycle as 0-100 percent. */
float heater_get_duty_pct(void);
