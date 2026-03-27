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

// Safety relay output (wired to a hotplate power relay module).
static constexpr int PIN_HEATER_RELAY = 12;
// Relay active level: HIGH for active-high modules, LOW for active-low modules.
static constexpr uint8_t RELAY_ACTIVE_LEVEL = 1;

// Dummy temperature input for v1 testing (until thermocouple/ADC is wired).
static constexpr bool USE_DUMMY_TEMPERATURE = true;
static constexpr float DUMMY_TEMP_C = 95.0f;

// FSM thresholds (starting values from spec).
static constexpr float MASS_PRESENT_THRESHOLD_G = 20.0f;
static constexpr float TEMP_HEATING_THRESHOLD_C = 40.0f;
static constexpr float STABLE_RATE_THRESHOLD_G_PER_MIN = 0.05f;
static constexpr float RAPID_DROP_THRESHOLD_G_PER_MIN = -50.0f;
static constexpr float DRYING_RATE_THRESHOLD_G_PER_MIN = -0.2f;
static constexpr float TEMP_RISE_THRESHOLD_C_PER_MIN = 0.5f;

// Dryness thresholds.
// WARNING threshold: "nearly gone".
static constexpr float DRY_THRESHOLD = 0.10f;
// Hard shutdown dryness threshold (more severe than warning threshold).
static constexpr float DRY_SHUTDOWN_THRESHOLD = 0.03f;
static constexpr float NEAR_ZERO_RATE_THRESHOLD_G_PER_MIN = 0.03f;

// Timing configuration.
static constexpr uint32_t STABLE_TIME_MS = 15000;
static constexpr uint32_t WARNING_COUNTDOWN_MS = 60000;
static constexpr uint32_t PAUSE_GRACE_MS = 120000;
static constexpr uint32_t AUTO_TARE_DWELL_MS = 30000;

// UI and runtime cadence.
static constexpr uint32_t UI_UPDATE_MS = 200;
static constexpr float MASS_RATE_EMA_ALPHA = 0.35f;
static constexpr float TEMP_RATE_EMA_ALPHA = 0.35f;
