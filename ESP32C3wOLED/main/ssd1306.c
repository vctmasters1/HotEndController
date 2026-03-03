/*
 * ssd1306.c — SSD1306 OLED driver implementation
 *
 * Software (bit-bang) I2C master on GPIO5 (SDA) / GPIO6 (SCL).
 * Frame-buffered: draw into RAM, then call ssd1306_update() to
 * push the entire buffer to the display in one I2C transaction.
 */

#include "ssd1306.h"
#include "font5x7.h"

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include <string.h>

/* ================================================================
 *  Frame buffer — one bit per pixel, organised in 8-row "pages".
 *  Byte index = page * WIDTH + column.
 *  Within a byte: bit 0 = top row of the page, bit 7 = bottom.
 * ================================================================ */
static uint8_t framebuf[SSD1306_WIDTH * (SSD1306_HEIGHT / 8)];

/* ================================================================
 *  Software I2C  (bit-bang, master-only, write-only)
 * ================================================================ */

/* ~4 µs half-period → ~125 kHz bus clock (well within SSD1306 spec) */
static inline void sw_i2c_delay(void)
{
    esp_rom_delay_us(4);
}

static inline void sda_high(void) { gpio_set_level(OLED_SDA_PIN, 1); }
static inline void sda_low(void)  { gpio_set_level(OLED_SDA_PIN, 0); }
static inline void scl_high(void) { gpio_set_level(OLED_SCL_PIN, 1); }
static inline void scl_low(void)  { gpio_set_level(OLED_SCL_PIN, 0); }

static void sw_i2c_start(void)
{
    sda_high(); sw_i2c_delay();
    scl_high(); sw_i2c_delay();
    sda_low();  sw_i2c_delay();
    scl_low();  sw_i2c_delay();
}

static void sw_i2c_stop(void)
{
    sda_low();  sw_i2c_delay();
    scl_high(); sw_i2c_delay();
    sda_high(); sw_i2c_delay();
}

/* Shift out one byte MSB-first; ignores the ACK bit. */
static void sw_i2c_write_byte(uint8_t data)
{
    for (int i = 7; i >= 0; i--) {
        if (data & (1 << i))
            sda_high();
        else
            sda_low();
        sw_i2c_delay();
        scl_high(); sw_i2c_delay();
        scl_low();  sw_i2c_delay();
    }
    /* ACK clock — release SDA, pulse SCL, ignore ACK value */
    sda_high(); sw_i2c_delay();
    scl_high(); sw_i2c_delay();
    scl_low();  sw_i2c_delay();
}

/* ── GPIO set-up for the software I2C bus ────────────────── */
static void sw_i2c_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << OLED_SDA_PIN) | (1ULL << OLED_SCL_PIN),
        .mode         = GPIO_MODE_INPUT_OUTPUT_OD,   /* open-drain */
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    /* Bus idle: both lines high */
    sda_high();
    scl_high();
}

/* ================================================================
 *  SSD1306 low-level helpers
 * ================================================================ */

/* Send a stream of command bytes (control byte 0x00 = Co=0, D/C#=0). */
static void ssd1306_send_cmds(const uint8_t *cmds, int len)
{
    sw_i2c_start();
    sw_i2c_write_byte((OLED_I2C_ADDR << 1) | 0);  /* address + W */
    sw_i2c_write_byte(0x00);                        /* command stream */
    for (int i = 0; i < len; i++)
        sw_i2c_write_byte(cmds[i]);
    sw_i2c_stop();
}

/* Send a single command byte. */
static void ssd1306_cmd(uint8_t cmd)
{
    ssd1306_send_cmds(&cmd, 1);
}

/* ================================================================
 *  Public API
 * ================================================================ */

void ssd1306_init(void)
{
    sw_i2c_init();

    /* Small delay to let the display power-up */
    esp_rom_delay_us(50000);   /* 50 ms */

    static const uint8_t init_seq[] = {
        0xAE,             /* Display OFF                        */
        0xD5, 0x80,       /* Set display clock div/ratio        */
        0xA8, (SSD1306_HEIGHT - 1),  /* Multiplex ratio          */
        0xD3, 0x00,       /* Display offset = 0                 */
        0x40,             /* Start line = 0                     */
        0x8D, 0x14,       /* Charge pump ON (internal VCC)      */
        0x20, 0x00,       /* Horizontal addressing mode         */
        0xA1,             /* Segment re-map (col 127 = SEG0)    */
        0xC8,             /* COM output scan direction (rem.)   */
#if SSD1306_HEIGHT == 64
        0xDA, 0x12,       /* COM pins config — 128×64           */
#elif SSD1306_HEIGHT == 40
        0xDA, 0x12,       /* COM pins config — 72×40            */
#else
        0xDA, 0x02,       /* COM pins config — fallback         */
#endif
        0x81, 0xCF,       /* Contrast                           */
        0xD9, 0xF1,       /* Pre-charge period                  */
        0xDB, 0x40,       /* VCOMH deselect level               */
        0xA4,             /* Entire display ON (follow RAM)     */
        0xA6,             /* Normal (non-inverted) display      */
        0xAF,             /* Display ON                         */
    };
    ssd1306_send_cmds(init_seq, sizeof(init_seq));

    ssd1306_clear();
    ssd1306_update();
}

void ssd1306_clear(void)
{
    memset(framebuf, 0, sizeof(framebuf));
}

void ssd1306_update(void)
{
    /* Set column address range */
    uint8_t col_cmd[] = { 0x21, 0x00, (uint8_t)(SSD1306_WIDTH - 1) };
    ssd1306_send_cmds(col_cmd, sizeof(col_cmd));

    /* Set page address range */
    uint8_t page_cmd[] = { 0x22, 0x00, (uint8_t)((SSD1306_HEIGHT / 8) - 1) };
    ssd1306_send_cmds(page_cmd, sizeof(page_cmd));

    /* Stream pixel data (control byte 0x40 = Co=0, D/C#=1) */
    sw_i2c_start();
    sw_i2c_write_byte((OLED_I2C_ADDR << 1) | 0);
    sw_i2c_write_byte(0x40);       /* data stream */
    for (int i = 0; i < (int)sizeof(framebuf); i++)
        sw_i2c_write_byte(framebuf[i]);
    sw_i2c_stop();
}

/* ── Drawing primitives ──────────────────────────────────── */

void ssd1306_draw_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT)
        return;

    int page = y / 8;
    int bit  = y % 8;
    int idx  = page * SSD1306_WIDTH + x;

    if (on)
        framebuf[idx] |=  (1 << bit);
    else
        framebuf[idx] &= ~(1 << bit);
}

void ssd1306_draw_hline(int x, int y, int width)
{
    for (int i = 0; i < width; i++)
        ssd1306_draw_pixel(x + i, y, true);
}

void ssd1306_draw_char(int x, int y, char c, int scale)
{
    /* Map to font table index; out-of-range → space */
    int idx = (int)c - 0x20;
    if (idx < 0 || idx >= (int)(sizeof(font5x7) / sizeof(font5x7[0])))
        idx = 0;      /* space */

    for (int col = 0; col < 5; col++) {
        uint8_t column_data = font5x7[idx][col];
        for (int row = 0; row < 7; row++) {
            if (column_data & (1 << row)) {
                /* Draw a scale×scale block for this "on" pixel */
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        ssd1306_draw_pixel(x + col * scale + sx,
                                           y + row * scale + sy, true);
            }
        }
    }
}

void ssd1306_draw_string(int x, int y, const char *str, int scale)
{
    const int char_w = 6 * scale;   /* 5 px glyph + 1 px gap */

    while (*str) {
        ssd1306_draw_char(x, y, *str, scale);
        x += char_w;
        str++;
    }
}
