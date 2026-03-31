#pragma once

#include <stdint.h>

// HX711 wiring: DOUT -> PIN_HX711_DT, PD_SCK -> PIN_HX711_SCK
// Defaults: XIAO ESP32C3 D2/D3 (GPIO4/GPIO5). Change to match your board.
#define PIN_HX711_DT 4
#define PIN_HX711_SCK 15

#define SERIAL_BAUD 115200

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

// Plate temperature: Adafruit K-type analog amplifier (e.g. AD8495 breakout) -> ESP32 ADC.
// Output is ~5 mV/°C with ~1.25 V at 0 °C; trim TC_ADC_MV_AT_0C against ice water / known temp if needed.
static constexpr bool USE_DUMMY_TEMPERATURE = false;
static constexpr float DUMMY_TEMP_C = 95.0f;
static constexpr int PIN_THERMOCOUPLE_ADC = 34;
static constexpr float TC_ADC_MV_PER_DEGC = 5.0f;
static constexpr float TC_ADC_MV_AT_0C = 1250.0f;
static constexpr int TC_ADC_SAMPLES = 8;
// Minimum positive temperature rate (°C/min) considered "still heating up" and used to defer
// mass-stable shutdowns so we do not time out while the plate is ramping.
static constexpr float TEMP_RISING_MIN_C_PER_MIN = 0.5f;

// FSM thresholds in raw HX711 units (counts and counts/min).
static constexpr int32_t LOAD_PRESENT_THRESHOLD_COUNTS = 16000;
static constexpr float STABLE_RATE_THRESHOLD_COUNTS_PER_MIN = 4000.0f;
// Sudden-loss trigger in raw HX711 counts/min (primary safety signal).
static constexpr float RAPID_DROP_THRESHOLD_COUNTS_PER_MIN = -400000.0f;
// Require this long below RAPID_DROP_THRESHOLD before shutdown (filters one-off SPI/garbage reads).
static constexpr uint32_t RAPID_MASS_LOSS_CONFIRM_MS = 2500;
// Reject one HX711 median if |delta - smoothed_delta| exceeds this (counts); stops single blips from moving EMA.
static constexpr int32_t MAX_HX711_DELTA_STEP_COUNTS = 80000;
// Faster than normal evaporation (~few k cpm); beaker removal / lift-off (see logs ~-150k cpm).
static constexpr float VESSEL_REMOVAL_RATE_THRESHOLD_COUNTS_PER_MIN = -80000.0f;
// Sustained mass loss (counts/min): slow boil-off is often only a few hundred cpm negative on HX711.
static constexpr float DRYING_RATE_THRESHOLD_COUNTS_PER_MIN = -100.0f;
// Smoothed rate above this (counts/min, apparent mass gain) zeros evaporation dwell — slosh / rebound, not slow loss.
static constexpr float EVAPORATION_POSITIVE_CANCEL_COUNTS_PER_MIN = 200.0f;
// Total ms spent at or below DRYING_RATE_THRESHOLD (accumulated; middling rates pause without resetting) before EVAPORATING.
static constexpr uint32_t EVAPORATION_DETECT_DWELL_MS = 15000;
// Plate temp (°C) at or above this counts as "heating" for the FSM when not using dummy temp
// (e.g. hotplate on but relay not driven from this firmware).
static constexpr float HEATING_INFER_TEMP_C = 55.0f;

// Dryness thresholds.
// WARNING threshold: "nearly gone".
static constexpr float DRY_THRESHOLD = 0.10f;
// Hard shutdown dryness threshold (more severe than warning threshold).
static constexpr float DRY_SHUTDOWN_THRESHOLD = 0.03f;
static constexpr float NEAR_ZERO_RATE_THRESHOLD_COUNTS_PER_MIN = 2500.0f;
// Recovery margin in WARNING before resuming (fraction of baseline load).
static constexpr float WARNING_RECOVERY_MARGIN_RATIO = 0.10f;

// Timing configuration.
static constexpr uint32_t STABLE_TIME_MS = 15000;
static constexpr uint32_t WARNING_COUNTDOWN_MS = 60000;
static constexpr uint32_t PAUSE_GRACE_MS = 120000;
// SHUTDOWN: |mass rate| below STABLE_RATE for this long while heating (evaporation effectively stopped).
static constexpr uint32_t MASS_STABLE_SHUTDOWN_MS = 120000;
static constexpr uint32_t AUTO_TARE_DWELL_MS = 30000;

// UI and runtime cadence.
static constexpr uint32_t UI_UPDATE_MS = 200;
// How often committed mass (and derived rate) updates. Longer = calmer derivative, slower UI/step response.
static constexpr uint32_t SENSOR_PUBLISH_INTERVAL_MS = 2000;
// EMA on HX711 delta each SAMPLE_DELAY_MS tick: lower α = heavier smoothing (slower mass to settle).
static constexpr float RAW_COUNTS_EMA_ALPHA = 0.06f;
// EMA on counts/min (and g/min) from publish-interval deltas: lower α = calmer rate, slower alarms/UI rate.
static constexpr float MASS_RATE_EMA_ALPHA = 0.10f;
static constexpr float TEMP_RATE_EMA_ALPHA = 0.10f;
