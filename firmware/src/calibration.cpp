#include "calibration.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

#include "config.h"
#include "pins.h"

static const char* TAG = "calibration";

static adc_cali_handle_t cali = nullptr;

void calibration_init() {
    adc_cali_curve_fitting_config_t cfg = {
        .unit_id  = OPT101_ADC_UNIT,
        .chan     = OPT101_ADC_CHANNEL,
        .atten    = OPT101_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_12,
    };
    esp_err_t err = adc_cali_create_scheme_curve_fitting(&cfg, &cali);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Curve-fitting calibration unavailable: %s", esp_err_to_name(err));
        cali = nullptr;
        return;
    }
    ESP_LOGI(TAG, "Curve-fitting calibration ready (%d dB attenuation)", OPT101_ADC_ATTEN_TENTHS_DB / 10);
}

bool calibration_available() { return cali != nullptr; }

bool calibration_raw_to_mv(int raw, int* mv) {
    return cali && adc_cali_raw_to_voltage(cali, raw, mv) == ESP_OK;
}
