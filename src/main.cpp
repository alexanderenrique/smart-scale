#include <Arduino.h>
#include <HX711.h>

#include "hardware.h"

HX711 scale;

int32_t g_baseline;
int32_t g_counts_at_cal;

static void wait_for_enter(const __FlashStringHelper* prompt) {
  Serial.println(prompt);
  Serial.println(F("Press Enter when ready."));
  Serial.flush();
  for (;;) {
    if (Serial.available() > 0) {
      const int c = Serial.read();
      if (c == '\n' || c == '\r') {
        delay(5);
        while (Serial.available() > 0) {
          Serial.read();
        }
        return;
      }
    }
    delay(10);
  }
}

static void collect_baseline() {
  Serial.println(F("Averaging empty baseline — do not touch the scale..."));
  int64_t sum = 0;
  uint32_t n = 0;
  const unsigned long t0 = millis();
  const unsigned long deadline = t0 + static_cast<unsigned long>(BASELINE_SECONDS) * 1000UL;
  unsigned long next_hb = t0;
  while (millis() < deadline) {
    const unsigned long now = millis();
    if (now >= next_hb) {
      next_hb = now + HEARTBEAT_INTERVAL_MS;
      const unsigned sec = (now - t0) / 1000UL;
      Serial.print(F("[baseline "));
      Serial.print(sec);
      Serial.print(F("s samples="));
      Serial.print(n);
      Serial.println(F("]"));
      Serial.flush();
    }
    if (scale.is_ready()) {
      sum += scale.read();
      ++n;
    }
    delay(SAMPLE_DELAY_MS);
  }
  if (n == 0) {
    Serial.println(F("ERROR: no HX711 samples during baseline window."));
    while (true) {
      delay(500);
    }
  }
  g_baseline = static_cast<int32_t>(sum / static_cast<int64_t>(n));
  Serial.print(F("Baseline raw average: "));
  Serial.println(g_baseline);
  Serial.flush();
}

static void collect_calibration_250g() {
  Serial.println(F("Averaging with calibration weight — keep 250 g steady..."));
  int64_t sum = 0;
  uint32_t n = 0;
  const unsigned long t0 = millis();
  const unsigned long deadline = t0 + static_cast<unsigned long>(CALIBRATION_SECONDS) * 1000UL;
  unsigned long next_hb = t0;
  while (millis() < deadline) {
    const unsigned long now = millis();
    if (now >= next_hb) {
      next_hb = now + HEARTBEAT_INTERVAL_MS;
      const unsigned sec = (now - t0) / 1000UL;
      Serial.print(F("[cal "));
      Serial.print(sec);
      Serial.print(F("s samples="));
      Serial.print(n);
      Serial.println(F("]"));
      Serial.flush();
    }
    if (scale.is_ready()) {
      sum += scale.read();
      ++n;
    }
    delay(SAMPLE_DELAY_MS);
  }
  if (n == 0) {
    Serial.println(F("ERROR: no HX711 samples during calibration window."));
    g_counts_at_cal = COUNTS_AT_CAL;
    Serial.print(F("Using fallback COUNTS_AT_CAL from firmware: "));
    Serial.println(g_counts_at_cal);
    return;
  }
  const int32_t avg_raw = static_cast<int32_t>(sum / static_cast<int64_t>(n));
  const int32_t measured = avg_raw - g_baseline;
  Serial.print(F("Calibration raw average: "));
  Serial.println(avg_raw);
  Serial.print(F("Measured delta (counts for "));
  Serial.print(CAL_REFERENCE_G);
  Serial.print(F(" g): "));
  Serial.println(measured);

  if (measured > 100) {
    g_counts_at_cal = measured;
    Serial.println(F("Calibration OK — using measured counts for this session."));
  } else {
    g_counts_at_cal = COUNTS_AT_CAL;
    Serial.println(F("Calibration delta too small or wrong sign; using COUNTS_AT_CAL from hardware.h."));
  }
  Serial.print(F("Optional: set COUNTS_AT_CAL in hardware.h to "));
  Serial.print(g_counts_at_cal);
  Serial.println(F(" to persist."));
  Serial.flush();
}

void setup() {
  static_assert(COUNTS_AT_CAL > 0, "COUNTS_AT_CAL must be positive");

  Serial.begin(SERIAL_BAUD);
  delay(200);
  Serial.println();
  Serial.println(F("smart-scale: boot"));
  Serial.flush();

  scale.begin(PIN_HX711_DT, PIN_HX711_SCK);
  Serial.println(F("smart-scale: HX711 begin OK"));
  Serial.flush();

  wait_for_enter(
      F(">>> Step 1 — Empty scale: remove all weight from the platform."));
  collect_baseline();

  wait_for_enter(F(">>> Step 2 — Place the 250 g calibration weight on the platform."));
  g_counts_at_cal = COUNTS_AT_CAL;
  collect_calibration_250g();

  Serial.println(F("--- Running (2 Hz) — columns: raw delta_g grams ---"));
  Serial.flush();
}

void loop() {
  static unsigned long last_hb = 0;
  const unsigned long now = millis();
  if (now - last_hb >= HEARTBEAT_INTERVAL_MS) {
    last_hb = now;
    Serial.print(F("[hb ms="));
    Serial.print(now);
    Serial.print(F(" ready="));
    Serial.print(scale.is_ready() ? 1 : 0);
    Serial.println(F("]"));
    Serial.flush();
  }

  if (!scale.is_ready()) {
    delay(SAMPLE_DELAY_MS);
    return;
  }

  const long raw = scale.read();
  const int32_t delta = static_cast<int32_t>(raw - g_baseline);
  const int32_t divisor = (g_counts_at_cal > 0) ? g_counts_at_cal : 1;
  float grams = delta * (static_cast<float>(CAL_REFERENCE_G) / static_cast<float>(divisor));
  if (grams < 0.f) {
    grams = 0.f;
  }

  Serial.print(raw);
  Serial.print(' ');
  Serial.print(delta);
  Serial.print(' ');
  Serial.println(grams, 2);

  delay(SAMPLE_DELAY_MS);
}
