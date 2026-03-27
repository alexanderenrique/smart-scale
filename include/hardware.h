#pragma once

#include <stdint.h>

// HX711 wiring: DOUT -> PIN_HX711_DT, PD_SCK -> PIN_HX711_SCK
// Defaults: XIAO ESP32C3 D2/D3 (GPIO4/GPIO5). Change to match your board.
#define PIN_HX711_DT 4
#define PIN_HX711_SCK 15

#define SERIAL_BAUD 115200

#define BASELINE_SECONDS 10
#define CALIBRATION_SECONDS 10
// Time between HX711 polls and between serial weight lines (2 Hz).
#define SAMPLE_DELAY_MS 500
#define HEARTBEAT_INTERVAL_MS 1000

// Known calibration mass (grams).
static constexpr int32_t CAL_REFERENCE_G = 250;
// Fallback if live calibration delta is invalid; optional manual tune.
static constexpr int32_t COUNTS_AT_CAL = 200000;
