/*
 * main.c — ESP32-C3 Independent Hotend Controller
 *
 * Self-contained secondary heater: reads a PT1000 via ADC,
 * receives the set temperature via I2C from Klipper/EBB2209,
 * runs PID heater control via LEDC PWM, and displays status
 * on the built-in SSD1306 OLED.
 *
 * Architecture:
 *   - PID task   (priority 5) — 4 Hz, never blocked by OLED
 *   - OLED task  (priority 2) — updates only when idle CPU is available
 *
 *   ┌────────────────────────┐
 *   │ SET:         PWR:45%   │  ← labels (1×)
 *   │ 200.0°C                │  ← 2× value
 *   │────────────────────────│  ← separator
 *   │ ACT:                   │
 *   │ 195.5°C                │
 *   └────────────────────────┘
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "ssd1306.h"
#include "i2c_slave_handler.h"
#include "pt1000.h"
#include "heater_control.h"

static const char *TAG = "main";

/* 0x7F is mapped to the ° glyph in our font table */
#define DEG_CHAR "\x7F"

/* Comms watchdog: shut off heater if no I2C in 30 seconds */
#define WATCHDOG_TIMEOUT_US  (30 * 1000000LL)

/* ── EMA (exponential moving average) for PT1000 noise ──── */
#define EMA_ALPHA  0.3f              /* 0.0 = frozen, 1.0 = no filter */
static float ema_temp = -999.0f;     /* initialised on first valid read */
static bool  ema_primed = false;

static float ema_filter(float raw)
{
    if (raw < -900.0f) return raw;   /* pass through fault sentinel */
    if (!ema_primed) {
        ema_temp   = raw;            /* seed with first good reading */
        ema_primed = true;
        return raw;
    }
    ema_temp = EMA_ALPHA * raw + (1.0f - EMA_ALPHA) * ema_temp;
    return ema_temp;
}

/* ── Shared state: PID task → OLED task (spinlock-protected) ── */
typedef struct {
    float set_temp;
    float act_temp;
    float duty_pct;
    bool  heater_on;
    bool  faulted;
} display_state_t;

static display_state_t g_disp = {0};
static portMUX_TYPE    g_disp_mux = portMUX_INITIALIZER_UNLOCKED;

/* ───────────────────────────────────────────────────────────
 * OLED task — low priority, runs only when PID task sleeps
 * ─────────────────────────────────────────────────────────── */
static void format_temp(char *buf, size_t len, float temp)
{
    if (temp < -900.0f)
        snprintf(buf, len, "ERR");
    else
        snprintf(buf, len, "%.1f" DEG_CHAR "C", temp);
}

static void oled_task(void *arg)
{
    (void)arg;
    char buf[16];
    float prev_set  = -999.0f;
    float prev_act  = -999.0f;
    bool  prev_htr  = false;
    bool  prev_flt  = false;
    bool  first_draw = true;

    for (;;) {
        /* Snapshot shared state under spinlock */
        display_state_t snap;
        taskENTER_CRITICAL(&g_disp_mux);
        snap = g_disp;
        taskEXIT_CRITICAL(&g_disp_mux);

        /* Only redraw when something changed */
        if (snap.set_temp != prev_set || snap.act_temp != prev_act
            || snap.heater_on != prev_htr || snap.faulted != prev_flt
            || first_draw) {
            prev_set  = snap.set_temp;
            prev_act  = snap.act_temp;
            prev_htr  = snap.heater_on;
            prev_flt  = snap.faulted;
            first_draw = false;

            ssd1306_clear();

            /* Top half: SET temperature + heater/power indicator */
            ssd1306_draw_string(0, 0, "SET:", 1);
            if (snap.faulted) {
                ssd1306_draw_string(72, 0, "FAULT!", 1);
            } else if (snap.heater_on) {
                char pwr[12];
                snprintf(pwr, sizeof(pwr), "PWR:%d%%",
                         (int)(snap.duty_pct + 0.5f));
                ssd1306_draw_string(72, 0, pwr, 1);
            } else {
                ssd1306_draw_string(78, 0, "PWR:--", 1);
            }

            format_temp(buf, sizeof(buf), snap.set_temp);
            ssd1306_draw_string(0, 12, buf, 2);

            /* Separator */
            ssd1306_draw_hline(0, 32, SSD1306_WIDTH);

            /* Bottom half: ACTUAL temperature */
            ssd1306_draw_string(0, 36, "ACT:", 1);
            format_temp(buf, sizeof(buf), snap.act_temp);
            ssd1306_draw_string(0, 48, buf, 2);

            ssd1306_update();  /* ~80 ms bit-bang — fine, we're low priority */
        }

        vTaskDelay(pdMS_TO_TICKS(500));  /* OLED at ~2 Hz max */
    }
}

/* ───────────────────────────────────────────────────────────
 * app_main — starts PID (high priority) and OLED (low priority)
 * ─────────────────────────────────────────────────────────── */
void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-C3 Hotend Controller ===");

    /* ── 1. Initialise peripherals ─────────────────────── */
    ssd1306_init();
    ssd1306_clear();
    ssd1306_draw_string(4, 16, "HOTEND CTRL", 1);
    ssd1306_draw_string(16, 32, "Starting...", 1);
    ssd1306_update();

    pt1000_init();
    heater_init();
    i2c_slave_init();

    /* ── 2. Launch OLED task at LOW priority ────────────── */
    xTaskCreate(oled_task, "oled", 2048, NULL, 2, NULL);

    /* ── 3. PID loop (this task, priority 5 = default main) ─ */
    for (;;) {
        /* ── I2C: receive target temperature ────────────── */
        i2c_slave_process();
        float set = i2c_slave_get_set_temp();

        /* ── Comms watchdog ─────────────────────────────── */
        int64_t now_us = esp_timer_get_time();
        int64_t elapsed = now_us - i2c_slave_last_rx_time();
        if (set > 0.0f && elapsed > WATCHDOG_TIMEOUT_US) {
            ESP_LOGW(TAG, "I2C watchdog: no comms for %.0f s — heater OFF",
                     elapsed / 1e6f);
            set = 0.0f;
        }

        /* ── PT1000: read actual temperature (EMA filtered) ── */
        float raw_act = pt1000_read_temp();
        float act     = ema_filter(raw_act);

        /* ── I2C TX: update actual temp for Klipper read ─── */
        i2c_slave_set_actual_temp(act);

        /* ── Heater: update PID control loop ────────────── */
        heater_set_target(set);
        heater_update(act);

        /* ── Update shared display state (spinlock) ──────── */
        taskENTER_CRITICAL(&g_disp_mux);
        g_disp.set_temp  = set;
        g_disp.act_temp  = act;
        g_disp.duty_pct  = heater_get_duty_pct();
        g_disp.heater_on = heater_is_on();
        g_disp.faulted   = heater_is_faulted();
        taskEXIT_CRITICAL(&g_disp_mux);

        vTaskDelay(pdMS_TO_TICKS(250));  /* PID at 4 Hz — uninterrupted */
    }
}
