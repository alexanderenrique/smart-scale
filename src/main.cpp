#include <Arduino.h>
#include <HX711.h>
#include <TFT_eSPI.h>
#include <lvgl.h>

#include "hardware.h"
#include "lv_conf.h"

HX711 scale;
TFT_eSPI tft = TFT_eSPI();

int32_t g_baseline;
int32_t g_counts_at_cal;

static lv_obj_t* g_weight_label = nullptr;
static uint32_t g_last_lv_tick_ms = 0;
static uint32_t g_last_ui_update_ms = 0;

static constexpr uint16_t kScreenWidth = 240;
static constexpr uint16_t kScreenHeight = 320;
static constexpr uint16_t kLvglDrawRows = 20;

static lv_color_t g_lv_buf1[kScreenWidth * kLvglDrawRows];
static lv_color_t g_lv_buf2[kScreenWidth * kLvglDrawRows];

#if LVGL_VERSION_MAJOR >= 9
static void lvgl_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  const uint16_t w = static_cast<uint16_t>(area->x2 - area->x1 + 1);
  const uint16_t h = static_cast<uint16_t>(area->y2 - area->y1 + 1);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushPixels(reinterpret_cast<uint16_t*>(px_map), static_cast<uint32_t>(w) * h);
  tft.endWrite();
  lv_display_flush_ready(disp);
}

static void lvgl_touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
  uint16_t x = 0;
  uint16_t y = 0;
  const bool touched = tft.getTouch(&x, &y, 600);
  data->state = touched ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  if (touched) {
    data->point.x = x;
    data->point.y = y;
  }
  (void)indev;
}
#else
static lv_disp_draw_buf_t g_draw_buf;
static lv_disp_drv_t g_disp_drv;
static lv_indev_drv_t g_indev_drv;

static void lvgl_flush_cb(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p) {
  const uint16_t w = static_cast<uint16_t>(area->x2 - area->x1 + 1);
  const uint16_t h = static_cast<uint16_t>(area->y2 - area->y1 + 1);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors(reinterpret_cast<uint16_t*>(color_p), static_cast<int32_t>(w) * h, true);
  tft.endWrite();
  lv_disp_flush_ready(disp);
}

static void lvgl_touch_read_cb(lv_indev_drv_t* indev, lv_indev_data_t* data) {
  uint16_t x = 0;
  uint16_t y = 0;
  const bool touched = tft.getTouch(&x, &y, 600);
  data->state = touched ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
  if (touched) {
    data->point.x = x;
    data->point.y = y;
  }
  (void)indev;
}
#endif

static void lvgl_tick() {
  const uint32_t now = millis();
  if (g_last_lv_tick_ms == 0) {
    g_last_lv_tick_ms = now;
    return;
  }
  const uint32_t delta = now - g_last_lv_tick_ms;
  if (delta > 0) {
    lv_tick_inc(delta);
    g_last_lv_tick_ms = now;
  }
}

static void init_display_ui() {
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  lv_init();
  g_last_lv_tick_ms = millis();

#if LVGL_VERSION_MAJOR >= 9
  lv_display_t* display = lv_display_create(kScreenWidth, kScreenHeight);
  lv_display_set_flush_cb(display, lvgl_flush_cb);
  lv_display_set_buffers(
      display, g_lv_buf1, g_lv_buf2, sizeof(g_lv_buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, lvgl_touch_read_cb);
#else
  lv_disp_draw_buf_init(
      &g_draw_buf, g_lv_buf1, g_lv_buf2, static_cast<uint32_t>(kScreenWidth) * kLvglDrawRows);
  lv_disp_drv_init(&g_disp_drv);
  g_disp_drv.hor_res = kScreenWidth;
  g_disp_drv.ver_res = kScreenHeight;
  g_disp_drv.flush_cb = lvgl_flush_cb;
  g_disp_drv.draw_buf = &g_draw_buf;
  lv_disp_drv_register(&g_disp_drv);

  lv_indev_drv_init(&g_indev_drv);
  g_indev_drv.type = LV_INDEV_TYPE_POINTER;
  g_indev_drv.read_cb = lvgl_touch_read_cb;
  lv_indev_drv_register(&g_indev_drv);
#endif

  g_weight_label = lv_label_create(lv_scr_act());
  lv_label_set_text(g_weight_label, "Scale booting...");
  lv_obj_align(g_weight_label, LV_ALIGN_CENTER, 0, 0);
  lv_timer_handler();
}

static void update_weight_ui(long raw, int32_t delta, float grams) {
  if (g_weight_label == nullptr) {
    return;
  }
  if (millis() - g_last_ui_update_ms < 200) {
    return;
  }
  g_last_ui_update_ms = millis();

  char text[96];
  snprintf(text, sizeof(text), "Raw: %ld\nDelta: %ld\nWeight: %.2f g", raw, static_cast<long>(delta), grams);
  lv_label_set_text(g_weight_label, text);
}

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

  init_display_ui();

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
  lvgl_tick();
  lv_timer_handler();

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

  update_weight_ui(raw, delta, grams);

  Serial.print(raw);
  Serial.print(' ');
  Serial.print(delta);
  Serial.print(' ');
  Serial.println(grams, 2);

  delay(SAMPLE_DELAY_MS);
}
