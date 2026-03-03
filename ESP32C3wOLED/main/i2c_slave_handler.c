/*
 * i2c_slave_handler.c — Hardware I2C slave implementation
 *
 * Bidirectional:
 *   - RX: receives target/set temperature from Klipper
 *   - TX: pre-loads actual temperature for Klipper to read
 *
 * The ESP32-C3 reads the actual temperature itself (PT1000 ADC)
 * and runs its own PID heater control loop.
 */

#include "i2c_slave_handler.h"

#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "i2c_slave";

#define I2C_SLAVE_PORT  I2C_NUM_0
#define RX_BUF_LEN      128
#define TX_BUF_LEN      16       /* we transmit actual temp */

/* ── Shared state ────────────────────────────────────────── */
static float   set_temp     = 0.0f;
static int64_t last_rx_us   = 0;

/* ================================================================ */

void i2c_slave_init(void)
{
    i2c_config_t conf = {
        .mode         = I2C_MODE_SLAVE,
        .sda_io_num   = I2C_SLAVE_SDA_PIN,
        .scl_io_num   = I2C_SLAVE_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .slave = {
            .addr_10bit_en = 0,
            .slave_addr    = I2C_SLAVE_ADDR,
            .maximum_speed = 100000,
        },
    };

    ESP_ERROR_CHECK(i2c_param_config(I2C_SLAVE_PORT, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_SLAVE_PORT,
                                       I2C_MODE_SLAVE,
                                       RX_BUF_LEN,
                                       TX_BUF_LEN,
                                       0));

    last_rx_us = esp_timer_get_time();

    /* Pre-load TX buffer with 0xFFFF (fault / no data yet) */
    uint8_t init_tx[2] = { 0xFF, 0xFF };
    i2c_slave_write_buffer(I2C_SLAVE_PORT, init_tx, 2, pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "I2C slave ready  addr=0x%02X  SDA=%d  SCL=%d",
             I2C_SLAVE_ADDR, I2C_SLAVE_SDA_PIN, I2C_SLAVE_SCL_PIN);
}

bool i2c_slave_process(void)
{
    uint8_t buf[16];
    int len = i2c_slave_read_buffer(I2C_SLAVE_PORT, buf, sizeof(buf),
                                    pdMS_TO_TICKS(10));
    if (len <= 0) return false;

    bool got_new = false;
    int pos = 0;

    while (pos < len) {
        uint8_t reg = buf[pos++];

        if (reg == 0x01 && pos + 2 <= len) {
            /* Set temperature: uint16 BE, °C × 10 */
            uint16_t st = ((uint16_t)buf[pos] << 8) | buf[pos + 1];
            set_temp = st / 10.0f;
            pos += 2;
            last_rx_us = esp_timer_get_time();
            got_new = true;
            ESP_LOGI(TAG, "RX SET=%.1f °C", set_temp);

        } else {
            /* Unknown register or incomplete data — discard rest */
            ESP_LOGW(TAG, "unknown reg 0x%02X at pos %d (len %d)",
                     reg, pos - 1, len);
            break;
        }
    }

    return got_new;
}

float   i2c_slave_get_set_temp(void)  { return set_temp; }
int64_t i2c_slave_last_rx_time(void)  { return last_rx_us; }

void i2c_slave_set_actual_temp(float actual_c)
{
    uint16_t raw;
    if (actual_c < -900.0f) {
        raw = 0xFFFF;   /* sensor fault indicator */
    } else {
        int val = (int)(actual_c * 10.0f + 0.5f);
        if (val < 0) val = 0;
        if (val > 0xFFFE) val = 0xFFFE;
        raw = (uint16_t)val;
    }

    /*
     * Reset TX FIFO and preload fresh data.
     * When the master does a read, it gets these 2 bytes.
     */
    i2c_reset_tx_fifo(I2C_SLAVE_PORT);
    uint8_t tx[2] = { (raw >> 8) & 0xFF, raw & 0xFF };
    i2c_slave_write_buffer(I2C_SLAVE_PORT, tx, 2, pdMS_TO_TICKS(10));
}
