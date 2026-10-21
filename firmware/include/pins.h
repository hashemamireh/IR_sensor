#pragma once

// --- OPT101 Photodiode ---
#define PIN_OPT101_OUT      4   // GPIO4 — ADC1_CH3

// --- I2C Bus (shared: OLED 0x3C + MAX17043 0x36) ---
#define PIN_I2C_SDA         23  // GPIO23
#define PIN_I2C_SCL         24  // GPIO24

// --- Rotary Encoder (KY-040) ---
#define PIN_ENC_CLK         8   // GPIO8  — rotation signal A
#define PIN_ENC_DT          10  // GPIO10 — rotation signal B
#define PIN_ENC_SW          5   // GPIO5  — button (LP_GPIO5, deep sleep wake)

// --- Onboard WS2812B RGB LED ---
#define PIN_RGB_LED         27  // GPIO27

// --- ADC ---
#define OPT101_ADC_CHANNEL  ADC_CHANNEL_3   // ADC1_CH3 = GPIO4
#define OPT101_ADC_UNIT     ADC_UNIT_1

// --- I2C Addresses ---
#define I2C_ADDR_OLED       0x3C
#define I2C_ADDR_MAX17043   0x36
