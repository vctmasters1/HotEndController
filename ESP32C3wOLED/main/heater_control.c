/*
 * heater_control.c — PID heater controller with LEDC PWM
 *
 * Uses the ESP32-C3 LEDC peripheral for hardware PWM on the
 * MOSFET/SSR output.  PID loop runs at ~4 Hz from main loop.
 *
 * PID features:
 *   - Anti-windup: integral clamped when output saturates
 *   - Derivative on measurement: avoids kick on setpoint change
 *   - Smooth ramp-down when approaching target
 */

#include "heater_control.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>

static const char *TAG = "heater";

/* ── PID state ───────────────────────────────────────────── */
static float target      = 0.0f;
static bool  faulted     = false;

static float kp          = PID_KP_DEFAULT;
static float ki          = PID_KI_DEFAULT;
static float kd          = PID_KD_DEFAULT;

static float integral    = 0.0f;
static float prev_temp   = 0.0f;  /* for derivative on measurement */
static int64_t prev_us   = 0;
static bool  pid_primed  = false;  /* wait for first valid reading */

/* ── Thermal runaway state ───────────────────────────────── */
static float   runaway_base_temp = 0.0f;   /* temp when high-duty watch began */
static int64_t runaway_start_us  = 0;      /* when high-duty period began     */
static bool    runaway_watching  = false;   /* currently tracking heat-up?     */

static int64_t drop_start_us     = 0;      /* when drop-below-target began    */
static bool    drop_watching     = false;   /* currently tracking temp drop?   */
static bool    reached_target    = false;   /* has temp ever reached target?   */

/* ── PWM state ───────────────────────────────────────────── */
#define DUTY_MAX  ((1 << HEATER_PWM_BITS) - 1)   /* 255 */
static uint32_t cur_duty  = 0;

/* ── LEDC channel config ────────────────────────────────── */
#define LEDC_TIMER     LEDC_TIMER_0
#define LEDC_CHANNEL   LEDC_CHANNEL_0
#define LEDC_MODE      LEDC_LOW_SPEED_MODE

/* ================================================================ */

void heater_init(void)
{
    /* ── Configure LEDC timer ─────────────────────────────── */
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_MODE,
        .duty_resolution = HEATER_PWM_BITS,
        .timer_num       = LEDC_TIMER,
        .freq_hz         = HEATER_PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    /* ── Configure LEDC channel ───────────────────────────── */
    ledc_channel_config_t ch_cfg = {
        .gpio_num   = HEATER_OUT_PIN,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CHANNEL,
        .timer_sel  = LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
        .intr_type  = LEDC_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));

    target   = 0.0f;
    faulted  = false;
    cur_duty = 0;

    /* reset PID state */
    integral   = 0.0f;
    prev_temp  = 0.0f;
    prev_us    = 0;
    pid_primed = false;

    /* reset runaway state */
    runaway_watching = false;
    drop_watching    = false;
    reached_target   = false;

    ESP_LOGI(TAG, "PID heater GPIO%d  %dHz %d-bit  Kp=%.1f Ki=%.1f Kd=%.1f",
             HEATER_OUT_PIN, HEATER_PWM_FREQ_HZ, HEATER_PWM_BITS, kp, ki, kd);
}

static void set_duty(uint32_t duty)
{
    if (duty > DUTY_MAX) duty = DUTY_MAX;
    cur_duty = duty;
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

void heater_set_target(float target_c)
{
    if (target_c < 0.0f) target_c = 0.0f;
    if (target_c > HEATER_MAX_TEMP) target_c = HEATER_MAX_TEMP;

    if (target_c != target) {
        target = target_c;
        /* Reset integral on target change to avoid windup surge */
        integral = 0.0f;
        /* Reset runaway watchers on target change */
        runaway_watching = false;
        drop_watching    = false;
        reached_target   = false;
        ESP_LOGI(TAG, "Target = %.1f °C", target);
    }

    /* If target set to 0, shut off immediately */
    if (target == 0.0f) {
        integral   = 0.0f;
        pid_primed = false;
        runaway_watching = false;
        drop_watching    = false;
        reached_target   = false;
        set_duty(0);
    }
}

float heater_get_target(void)
{
    return target;
}

void heater_update(float actual_temp)
{
    /* ── Fault conditions (latching) ────────────────────── */
    if (faulted) {
        set_duty(0);
        return;
    }

    /* Sensor error */
    if (actual_temp < -900.0f) {
        ESP_LOGE(TAG, "FAULT: sensor error (%.1f)", actual_temp);
        faulted = true;
        set_duty(0);
        return;
    }

    /* Over-temperature */
    if (actual_temp > HEATER_MAX_TEMP) {
        ESP_LOGE(TAG, "FAULT: over-temp %.1f > %.1f",
                 actual_temp, HEATER_MAX_TEMP);
        faulted = true;
        target  = 0.0f;
        set_duty(0);
        return;
    }

    /* ── Heater off ─────────────────────────────────────── */
    if (target == 0.0f) {
        if (cur_duty > 0) set_duty(0);
        return;
    }

    /* ── PID calculation ────────────────────────────────── */
    int64_t now_us = esp_timer_get_time();

    if (!pid_primed) {
        /* First cycle — just record baseline, no output */
        prev_temp  = actual_temp;
        prev_us    = now_us;
        pid_primed = true;
        return;
    }

    float dt = (now_us - prev_us) / 1000000.0f;   /* seconds */
    if (dt < 0.01f) dt = 0.01f;                     /* guard  */
    prev_us = now_us;

    float error = target - actual_temp;

    /* ── Proportional ───────────────────────────────────── */
    float p_term = kp * error;

    /* ── Integral (with anti-windup) ────────────────────── */
    integral += error * dt;
    /* Clamp integral so it can't exceed full output on its own */
    float i_max = (float)DUTY_MAX / (ki > 0.001f ? ki : 1.0f);
    if (integral >  i_max) integral =  i_max;
    if (integral < -i_max) integral = -i_max;
    float i_term = ki * integral;

    /* ── Derivative (on measurement, not error) ─────────── */
    float d_meas = -(actual_temp - prev_temp) / dt;
    float d_term = kd * d_meas;
    prev_temp = actual_temp;

    /* ── Sum & clamp ────────────────────────────────────── */
    float output = p_term + i_term + d_term;
    if (output < 0.0f)           output = 0.0f;
    if (output > (float)DUTY_MAX) output = (float)DUTY_MAX;

    uint32_t duty = (uint32_t)(output + 0.5f);
    set_duty(duty);

    /* ── Thermal runaway: not-heating check ──────────────── */
    float duty_pct = (duty * 100.0f) / (float)DUTY_MAX;
    if (duty_pct >= RUNAWAY_DUTY_THRESH) {
        if (!runaway_watching) {
            runaway_watching  = true;
            runaway_start_us  = now_us;
            runaway_base_temp = actual_temp;
        } else {
            float elapsed_s = (now_us - runaway_start_us) / 1e6f;
            float rise      = actual_temp - runaway_base_temp;
            if (elapsed_s >= RUNAWAY_HEAT_TIME_S && rise < RUNAWAY_MIN_RISE_C) {
                ESP_LOGE(TAG, "FAULT: thermal runaway — duty %.0f%% for %.0fs "
                         "but only +%.1f °C rise",
                         duty_pct, elapsed_s, rise);
                faulted = true;
                target  = 0.0f;
                set_duty(0);
                return;
            }
        }
    } else {
        /* Duty dropped below threshold — reset watcher */
        runaway_watching = false;
    }

    /* ── Thermal runaway: falling-away check ─────────────── */
    /* Only active after temp has first reached the target range.
     * This prevents false-triggering during the initial heat-up
     * ramp when actual is far below target by definition.        */
    if (!reached_target) {
        if (actual_temp >= target - RUNAWAY_DROP_THRESH) {
            reached_target = true;
        }
    }
    if (reached_target && actual_temp < target - RUNAWAY_DROP_THRESH) {
        if (!drop_watching) {
            drop_watching = true;
            drop_start_us = now_us;
        } else {
            float drop_s = (now_us - drop_start_us) / 1e6f;
            if (drop_s >= RUNAWAY_DROP_TIME_S) {
                ESP_LOGE(TAG, "FAULT: temp falling away — %.1f °C, "
                         "target %.1f °C, for %.0f s",
                         actual_temp, target, drop_s);
                faulted = true;
                target  = 0.0f;
                set_duty(0);
                return;
            }
        }
    } else {
        drop_watching = false;
    }

    ESP_LOGD(TAG, "PID: err=%.1f P=%.1f I=%.1f D=%.1f -> duty=%lu/255",
             error, p_term, i_term, d_term, (unsigned long)duty);
}

bool heater_is_on(void)      { return cur_duty > 0; }
bool heater_is_faulted(void) { return faulted; }

void heater_clear_fault(void)
{
    faulted    = false;
    target     = 0.0f;
    integral   = 0.0f;
    pid_primed = false;
    runaway_watching = false;
    drop_watching    = false;
    reached_target   = false;
    set_duty(0);
    ESP_LOGI(TAG, "Fault cleared, heater OFF");
}

float heater_get_duty_pct(void)
{
    return (cur_duty * 100.0f) / (float)DUTY_MAX;
}
