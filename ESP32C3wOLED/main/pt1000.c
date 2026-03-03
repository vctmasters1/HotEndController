/*
 * pt1000.c — PT1000 ADC driver implementation
 *
 * Voltage divider: 3.3 V → [1 kΩ] → ADC → [PT1000] → GND
 *
 *   V_adc = VCC × R_pt / (R_ref + R_pt)
 *   R_pt  = R_ref × V_adc / (VCC − V_adc)
 *
 * Callendar–Van Dusen (IEC 60751, PT1000):
 *   R(T) = R0 × (1 + A·T + B·T²)       for T ≥ 0 °C
 *   R0 = 1000 Ω, A = 3.9083e-3, B = -5.775e-7
 *
 * We solve for T using the quadratic formula.
 */

#include "pt1000.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "pt1000";

/* ── Callendar–Van Dusen coefficients (IEC 60751) ────────── */
#define R0      1000.0f           /* PT1000 at 0 °C  */
#define CVD_A   3.9083e-3f
#define CVD_B  (-5.775e-7f)

/* ── ADC handles ─────────────────────────────────────────── */
static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t         cali_handle = NULL;

/* Number of samples to average per read */
#define NUM_SAMPLES 16

/* ================================================================ */

void pt1000_init(void)
{
    /* ── ADC unit ───────────────────────────────────────── */
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id  = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    /* ── Channel config (GPIO2 = ADC1_CH2) ──────────────── */
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_12,   /* 0–3.3 V range */
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle,
                                                ADC_CHANNEL_2,
                                                &chan_cfg));

    /* ── Calibration (curve fitting on ESP32-C3) ────────── */
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .chan     = ADC_CHANNEL_2,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_cfg,
                                                          &cali_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "ADC calibration: curve fitting OK");
    } else {
        ESP_LOGW(TAG, "ADC calibration unavailable (%s)", esp_err_to_name(ret));
        cali_handle = NULL;
    }
#else
    cali_handle = NULL;
    ESP_LOGW(TAG, "ADC calibration not supported on this chip");
#endif

    ESP_LOGI(TAG, "PT1000 on GPIO%d  R_ref=%d Ω  ready",
             PT1000_ADC_PIN, (int)PT1000_REF_OHM);
}

/* ── Convert resistance to °C via Callendar–Van Dusen ────── */
static float resistance_to_celsius(float r_pt)
{
    /*
     * R = R0 (1 + A·T + B·T²)
     * B·T² + A·T + (1 − R/R0) = 0
     * T = (−A + √(A² − 4·B·(1 − R/R0))) / (2·B)
     */
    float ratio = r_pt / R0;
    float disc  = CVD_A * CVD_A - 4.0f * CVD_B * (1.0f - ratio);
    if (disc < 0.0f) return -999.0f;   /* shouldn't happen for valid R */
    return (-CVD_A + sqrtf(disc)) / (2.0f * CVD_B);
}

float pt1000_read_temp(void)
{
    int raw_sum = 0;
    int good    = 0;

    for (int i = 0; i < NUM_SAMPLES; i++) {
        int raw;
        if (adc_oneshot_read(adc_handle, ADC_CHANNEL_2, &raw) == ESP_OK) {
            raw_sum += raw;
            good++;
        }
    }
    if (good == 0) {
        ESP_LOGE(TAG, "ADC read failed");
        return -999.0f;
    }

    float v_adc;
    int avg_raw = raw_sum / good;

    if (cali_handle) {
        int mv;
        adc_cali_raw_to_voltage(cali_handle, avg_raw, &mv);
        v_adc = mv / 1000.0f;
    } else {
        /* Uncalibrated fallback: linear 0–3.3 V over 12-bit range */
        v_adc = (avg_raw / 4095.0f) * PT1000_VCC;
    }

    /* Avoid division by zero if sensor is disconnected (V → VCC) */
    if (v_adc >= PT1000_VCC - 0.01f) {
        ESP_LOGW(TAG, "Sensor open? V_adc=%.3f V", v_adc);
        return -999.0f;
    }
    /* Sensor shorted to GND? */
    if (v_adc < 0.05f) {
        ESP_LOGW(TAG, "Sensor short? V_adc=%.3f V", v_adc);
        return -999.0f;
    }

    /* R_pt = R_ref × V_adc / (VCC − V_adc) */
    float r_pt = PT1000_REF_OHM * v_adc / (PT1000_VCC - v_adc);
    float temp  = resistance_to_celsius(r_pt);

    ESP_LOGD(TAG, "raw=%d  V=%.3f  R=%.1f  T=%.1f",
             avg_raw, v_adc, r_pt, temp);

    return temp;
}
