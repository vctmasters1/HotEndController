/*
 * ssd1306.h — SSD1306 OLED driver (software I2C, frame-buffered)
 *
 * Designed for ESP32-C3 boards with a built-in SSD1306 OLED.
 * Uses software (bit-bang) I2C so the hardware I2C peripheral
 * remains free for the I2C-slave link to the EBB2209/RP2040.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* ── Display dimensions ──────────────────────────────────────
 * Default: 128×64 (0.96″ OLED).
 * For the 0.42″ variant change to 72 × 40.                  */
#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT  64

/* ── Software-I2C GPIO pins (built-in OLED) ──────────────── */
#define OLED_SDA_PIN    5
#define OLED_SCL_PIN    6

/* ── SSD1306 I2C address (7-bit) ─────────────────────────── */
#define OLED_I2C_ADDR   0x3C

/* ── Public API ──────────────────────────────────────────── */

/** Initialise GPIOs, software I2C bus, and the SSD1306.     */
void ssd1306_init(void);

/** Clear the local frame buffer (all pixels off).           */
void ssd1306_clear(void);

/** Flush the frame buffer to the display over software I2C. */
void ssd1306_update(void);

/** Set or clear a single pixel in the frame buffer.         */
void ssd1306_draw_pixel(int x, int y, bool on);

/** Draw a horizontal line.                                  */
void ssd1306_draw_hline(int x, int y, int width);

/**
 * Draw a character at (x, y) using the built-in 5×7 font.
 * @param scale  1 = normal (6×8), 2 = double (12×16), etc.
 */
void ssd1306_draw_char(int x, int y, char c, int scale);

/**
 * Draw a null-terminated string.
 * @param scale  1 = normal, 2 = double size, …
 */
void ssd1306_draw_string(int x, int y, const char *str, int scale);
