#include <Arduino.h>
#include <HX711.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <Preferences.h>
#include <nvs_flash.h>
#include <esp_err.h>
#include <esp_system.h>
#include "rom/ets_sys.h"

#include <cmath>
#include <cstring>

#include "hardware.h"
#include "lv_conf.h"

HX711 scale;
TFT_eSPI tft = TFT_eSPI();

int32_t g_baseline = 0;
int32_t g_counts_at_cal = COUNTS_AT_CAL;

static lv_obj_t* g_status_label = nullptr;
static uint32_t g_last_lv_tick_ms = 0;
static uint32_t g_last_ui_update_ms = 0;
static bool g_touch_was_down = false;
static uint32_t g_last_touch_log_ms = 0;

static Preferences g_prefs;
// Touchscreen calibration intentionally removed.

static void log_touch_position(bool touched, uint16_t x, uint16_t y) {
  const uint32_t now = millis();
  if (touched) {
    if (!g_touch_was_down || (now - g_last_touch_log_ms >= 200)) {
      Serial.print(F("touch:x="));
      Serial.print(x);
      Serial.print(F(",y="));
      Serial.println(y);
      g_last_touch_log_ms = now;
    }
    g_touch_was_down = true;
  } else if (g_touch_was_down) {
    Serial.println(F("touch:release"));
    g_touch_was_down = false;
  }
}

// Must match tft.setRotation(): rotation 1 = landscape 320x240 on 240x320 panels.
static constexpr uint16_t kScreenWidth = 320;
static constexpr uint16_t kScreenHeight = 240;
static constexpr uint16_t kLvglDrawRows = 20;
static constexpr uint32_t kLvColorBytesPerPixel = 2;  // RGB565

// LVGL v9 checks draw buffer alignment in lv_display_set_buffers().
// Keep explicit byte buffers with LV_ATTRIBUTE_MEM_ALIGN to satisfy alignment asserts.
static LV_ATTRIBUTE_MEM_ALIGN uint8_t g_lv_buf1[kScreenWidth * kLvglDrawRows * kLvColorBytesPerPixel];
static LV_ATTRIBUTE_MEM_ALIGN uint8_t g_lv_buf2[kScreenWidth * kLvglDrawRows * kLvColorBytesPerPixel];
#if LVGL_VERSION_MAJOR >= 9
static lv_draw_buf_t g_draw_buf1_v9;
static lv_draw_buf_t g_draw_buf2_v9;
#endif

enum class SystemState : uint8_t {
  IDLE,
  LOAD_DETECTED,
  HEATING_MONITORING,
  EVAPORATING,
  WARNING,
  SHUTDOWN,
  PAUSED_BY_USER,
  FAULT
};

enum class ShutdownReason : uint8_t {
  None,
  DryDetected,
  RapidMassLoss,
  VesselRemovedHot,
  SensorFault,
  WarningExpired,
  PauseExpired
};

struct SensorSnapshot {
  int32_t raw_delta_counts = 0;
  float raw_rate_counts_per_min = 0.0f;
  float mass_g = 0.0f;
  float mass_rate_g_per_min = 0.0f;
  float plate_temp_C = 0.0f;
  float temp_rate_C_per_min = 0.0f;
  bool heater_current_on = false;
  bool valid = true;
  uint32_t time_ms = 0;
};

struct DerivedSignals {
  bool mass_present = false;
  bool heating_active = false;
  bool mass_stable = false;
  bool rapid_mass_drop = false;
  bool drying_detected = false;
  bool temp_rising = false;
  bool near_zero_rate_while_hot = false;
  bool dry_condition = false;
  bool load_removed_while_hot = false;
  bool warning_timer_expired = false;
  bool sensor_fault = false;
};

static SystemState g_state = SystemState::IDLE;
static ShutdownReason g_shutdown_reason = ShutdownReason::None;
static uint32_t g_state_enter_ms = 0;
static uint32_t g_load_stable_start_ms = 0;
static uint32_t g_idle_stable_start_ms = 0;
static uint32_t g_warning_start_ms = 0;
static uint32_t g_pause_start_ms = 0;
static int32_t g_baseline_load_counts = 0;

static SensorSnapshot g_sensor;
static DerivedSignals g_derived;

static bool g_serial_ack = false;
static bool g_relay_enabled = false;
static bool g_heating_info = USE_DUMMY_TEMPERATURE;
static float g_smoothed_raw_delta_counts = 0.0f;
static uint32_t g_last_sensor_publish_ms = 0;
static int32_t g_prev_raw_delta_counts = 0;
static float g_prev_mass_g = 0.0f;
static float g_prev_temp_C = 0.0f;
static uint32_t g_prev_sample_ms = 0;
static bool g_first_sample = true;

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
  log_touch_position(touched, x, y);
  if (touched) {
    g_serial_ack = true;
  }
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
  log_touch_position(touched, x, y);
  if (touched) {
    g_serial_ack = true;
  }
  data->state = touched ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
  if (touched) {
    data->point.x = x;
    data->point.y = y;
  }
  (void)indev;
}
#endif

static const char* state_name(SystemState s) {
  switch (s) {
    case SystemState::IDLE: return "IDLE";
    case SystemState::LOAD_DETECTED: return "LOAD_DETECTED";
    case SystemState::HEATING_MONITORING: return "HEATING_MONITORING";
    case SystemState::EVAPORATING: return "EVAPORATING";
    case SystemState::WARNING: return "WARNING";
    case SystemState::SHUTDOWN: return "SHUTDOWN";
    case SystemState::PAUSED_BY_USER: return "PAUSED_BY_USER";
    case SystemState::FAULT: return "FAULT";
  }
  return "UNKNOWN";
}

static const char* reason_name(ShutdownReason r) {
  switch (r) {
    case ShutdownReason::None: return "none";
    case ShutdownReason::DryDetected: return "dry_detected";
    case ShutdownReason::RapidMassLoss: return "rapid_mass_loss";
    case ShutdownReason::VesselRemovedHot: return "vessel_removed_hot";
    case ShutdownReason::SensorFault: return "sensor_fault";
    case ShutdownReason::WarningExpired: return "warning_expired";
    case ShutdownReason::PauseExpired: return "pause_expired";
  }
  return "unknown";
}

static void set_relay_enabled(bool enabled) {
  g_relay_enabled = enabled;
  digitalWrite(PIN_HEATER_RELAY, enabled ? RELAY_ACTIVE_LEVEL : (RELAY_ACTIVE_LEVEL == HIGH ? LOW : HIGH));
  Serial.print(F("relay="));
  Serial.println(enabled ? F("ON") : F("OFF"));
}

static void log_event(const char* tag) {
  Serial.print(F("event="));
  Serial.print(tag);
  Serial.print(F(",t_ms="));
  Serial.print(g_sensor.time_ms);
  Serial.print(F(",state="));
  Serial.print(state_name(g_state));
  Serial.print(F(",raw_counts="));
  Serial.print(g_sensor.raw_delta_counts);
  Serial.print(F(",raw_rate_cpm="));
  Serial.print(g_sensor.raw_rate_counts_per_min, 1);
  Serial.print(F(",temp_C="));
  Serial.print(g_sensor.plate_temp_C, 2);
  Serial.print(F(",shutdown_reason="));
  Serial.println(reason_name(g_shutdown_reason));
}

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
  Serial.println(F("[BOOT][UI] enter init_display_ui"));
  Serial.println(F("[BOOT][UI] tft.init start"));
  tft.init();
  Serial.println(F("[BOOT][UI] tft.init done"));
  Serial.println(F("[BOOT][UI] tft.setRotation start"));
  tft.setRotation(1);
  Serial.println(F("[BOOT][UI] tft.setRotation done"));
  Serial.println(F("[BOOT][UI] tft.fillScreen start"));
  tft.fillScreen(TFT_BLACK);
  Serial.println(F("[BOOT][UI] tft.fillScreen done"));

  Serial.println(F("[BOOT][UI] lv_init start"));
  lv_init();
  Serial.println(F("[BOOT][UI] lv_init done"));
  g_last_lv_tick_ms = millis();
  Serial.println(F("[BOOT][UI] tick baseline set"));

#if LVGL_VERSION_MAJOR >= 9
  Serial.println(F("[BOOT][UI] lv_display_create start"));
  lv_display_t* display = lv_display_create(kScreenWidth, kScreenHeight);
  Serial.println(F("[BOOT][UI] lv_display_create done"));
  if (display == nullptr) {
    Serial.println(F("[ERROR][UI] lv_display_create returned nullptr"));
    return;
  }
  Serial.println(F("[BOOT][UI] lv_display_set_color_format start"));
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  Serial.println(F("[BOOT][UI] lv_display_set_color_format done"));
  Serial.println(F("[BOOT][UI] lv_display_set_flush_cb start"));
  lv_display_set_flush_cb(display, lvgl_flush_cb);
  Serial.println(F("[BOOT][UI] lv_display_set_flush_cb done"));
  Serial.println(F("[BOOT][UI] lv_draw_buf_init #1 start"));
  lv_result_t buf1_ok = lv_draw_buf_init(
      &g_draw_buf1_v9,
      kScreenWidth,
      kLvglDrawRows,
      LV_COLOR_FORMAT_RGB565,
      0,
      g_lv_buf1,
      sizeof(g_lv_buf1));
  Serial.print(F("[BOOT][UI] lv_draw_buf_init #1 result="));
  Serial.println(static_cast<int>(buf1_ok));
  if (buf1_ok != LV_RESULT_OK) {
    Serial.println(F("[ERROR][UI] draw buffer #1 init failed"));
    return;
  }

  Serial.println(F("[BOOT][UI] lv_draw_buf_init #2 start"));
  lv_result_t buf2_ok = lv_draw_buf_init(
      &g_draw_buf2_v9,
      kScreenWidth,
      kLvglDrawRows,
      LV_COLOR_FORMAT_RGB565,
      0,
      g_lv_buf2,
      sizeof(g_lv_buf2));
  Serial.print(F("[BOOT][UI] lv_draw_buf_init #2 result="));
  Serial.println(static_cast<int>(buf2_ok));
  if (buf2_ok != LV_RESULT_OK) {
    Serial.println(F("[ERROR][UI] draw buffer #2 init failed"));
    return;
  }

  Serial.println(F("[BOOT][UI] lv_display_set_draw_buffers start"));
  lv_display_set_draw_buffers(display, &g_draw_buf1_v9, &g_draw_buf2_v9);
  Serial.println(F("[BOOT][UI] lv_display_set_draw_buffers done"));
  Serial.println(F("[BOOT][UI] lv_display_set_render_mode start"));
  lv_display_set_render_mode(display, LV_DISPLAY_RENDER_MODE_PARTIAL);
  Serial.println(F("[BOOT][UI] lv_display_set_render_mode done"));

  Serial.println(F("[BOOT][UI] lv_indev_create start"));
  lv_indev_t* indev = lv_indev_create();
  Serial.println(F("[BOOT][UI] lv_indev_create done"));
  Serial.println(F("[BOOT][UI] lv_indev_set_type start"));
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  Serial.println(F("[BOOT][UI] lv_indev_set_type done"));
  Serial.println(F("[BOOT][UI] lv_indev_set_read_cb start"));
  lv_indev_set_read_cb(indev, lvgl_touch_read_cb);
  Serial.println(F("[BOOT][UI] lv_indev_set_read_cb done"));
#else
  Serial.println(F("[BOOT][UI] lv_disp_draw_buf_init start"));
  lv_disp_draw_buf_init(
      &g_draw_buf,
      reinterpret_cast<lv_color_t*>(g_lv_buf1),
      reinterpret_cast<lv_color_t*>(g_lv_buf2),
      static_cast<uint32_t>(kScreenWidth) * kLvglDrawRows);
  Serial.println(F("[BOOT][UI] lv_disp_draw_buf_init done"));
  Serial.println(F("[BOOT][UI] lv_disp_drv_init start"));
  lv_disp_drv_init(&g_disp_drv);
  Serial.println(F("[BOOT][UI] lv_disp_drv_init done"));
  g_disp_drv.hor_res = kScreenWidth;
  g_disp_drv.ver_res = kScreenHeight;
  g_disp_drv.flush_cb = lvgl_flush_cb;
  g_disp_drv.draw_buf = &g_draw_buf;
  Serial.println(F("[BOOT][UI] lv_disp_drv_register start"));
  lv_disp_drv_register(&g_disp_drv);
  Serial.println(F("[BOOT][UI] lv_disp_drv_register done"));

  Serial.println(F("[BOOT][UI] lv_indev_drv_init start"));
  lv_indev_drv_init(&g_indev_drv);
  Serial.println(F("[BOOT][UI] lv_indev_drv_init done"));
  g_indev_drv.type = LV_INDEV_TYPE_POINTER;
  g_indev_drv.read_cb = lvgl_touch_read_cb;
  Serial.println(F("[BOOT][UI] lv_indev_drv_register start"));
  lv_indev_drv_register(&g_indev_drv);
  Serial.println(F("[BOOT][UI] lv_indev_drv_register done"));
#endif

  Serial.println(F("[BOOT][UI] screen styling start"));
  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x001122), 0);
  g_status_label = lv_label_create(lv_scr_act());
  lv_label_set_long_mode(g_status_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(g_status_label, kScreenWidth - 20);
  lv_obj_align(g_status_label, LV_ALIGN_TOP_LEFT, 10, 8);
  lv_obj_set_style_text_font(g_status_label, LV_FONT_DEFAULT, 0);
  lv_label_set_text(g_status_label, "FSM booting...");
  Serial.println(F("[BOOT][UI] first lv_timer_handler start"));
  lv_timer_handler();
  Serial.println(F("[BOOT][UI] init_display_ui complete"));
}

static void set_status_text(const char* text) {
  if (g_status_label == nullptr) return;
  lv_label_set_text(g_status_label, text);
  lv_timer_handler();
}

static void set_status_colors(lv_color_t bg, lv_color_t text) {
  if (g_status_label == nullptr) return;
  lv_obj_set_style_bg_color(lv_scr_act(), bg, 0);
  lv_obj_set_style_text_color(g_status_label, text, 0);
  lv_timer_handler();
}

static void collect_baseline();
static void collect_calibration_250g();

static bool load_scale_calibration(int32_t* baseline, int32_t* counts_at_cal) {
  if (!g_prefs.begin("smart-scale", true)) {
    Serial.println(F("[WARN][SCALE] prefs begin failed (ro)"));
    return false;
  }
  const bool ok = g_prefs.getBool("scaleCalOk", false);
  const int32_t b = g_prefs.getInt("baseline", 0);
  const int32_t c = g_prefs.getInt("countsCal", 0);
  g_prefs.end();
  if (!ok || c <= 0) {
    return false;
  }
  *baseline = b;
  *counts_at_cal = c;
  return true;
}

static void save_scale_calibration(int32_t baseline, int32_t counts_at_cal) {
  if (!g_prefs.begin("smart-scale", false)) {
    Serial.println(F("[WARN][SCALE] prefs begin failed (rw)"));
    return;
  }
  g_prefs.putBool("scaleCalOk", true);
  g_prefs.putInt("baseline", baseline);
  g_prefs.putInt("countsCal", counts_at_cal);
  g_prefs.end();
}

static void init_nvs_storage() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    (void)nvs_flash_erase();
    err = nvs_flash_init();
  }
  if (err != ESP_OK) {
    Serial.print(F("[WARN][NVS] init failed err="));
    Serial.println(static_cast<int>(err));
    Serial.print(F("[WARN][NVS] "));
    Serial.println(esp_err_to_name(err));
  } else {
    Serial.println(F("[BOOT][NVS] init ok"));
  }
}

static void wait_for_touch() {
  g_serial_ack = false;
  set_status_text("First boot calibration\n\n1) Remove all weight\n2) Touch screen to start tare");
  while (!g_serial_ack) {
    lvgl_tick();
    lv_timer_handler();
    delay(10);
  }
  g_serial_ack = false;
  set_status_text("Taring... do not touch the scale");
}

static void run_first_boot_tare() {
  // No "touch to start" gate — run immediately on first boot.
  set_status_colors(lv_color_hex(0xFFFFFF), lv_color_hex(0x000000));
  set_status_text("First boot calibration\n\nRemove all weight\nStarting tare...");
  lv_timer_handler();
  collect_baseline();
  collect_calibration_250g();
  save_scale_calibration(g_baseline, g_counts_at_cal);
  set_status_text("Calibration complete");
}

static void collect_baseline() {
  Serial.println(F("Averaging empty baseline — do not touch the scale..."));
  int64_t sum = 0;
  uint32_t n = 0;
  const uint32_t t0 = millis();
  const uint32_t deadline = t0 + static_cast<uint32_t>(BASELINE_SECONDS) * 1000UL;
  uint32_t last_ui_second = 0;
  while (millis() < deadline) {
    if (scale.is_ready()) {
      sum += scale.read();
      ++n;
    }
    if (g_status_label != nullptr) {
      const uint32_t elapsed_s = (millis() - t0) / 1000U;
      if (elapsed_s != last_ui_second) {
        last_ui_second = elapsed_s;
        const uint32_t remaining = (BASELINE_SECONDS > elapsed_s) ? (BASELINE_SECONDS - elapsed_s) : 0;
        char buf[128];
        snprintf(buf, sizeof(buf), "First boot calibration\n\nRemove all weight\nTaring... %lus", static_cast<unsigned long>(remaining));
        set_status_text(buf);
      }
    }
    delay(SAMPLE_DELAY_MS);
  }
  if (n == 0) {
    Serial.println(F("ERROR: no HX711 baseline samples."));
    while (true) {
      delay(500);
    }
  }
  g_baseline = static_cast<int32_t>(sum / static_cast<int64_t>(n));
  Serial.print(F("Baseline raw average: "));
  Serial.println(g_baseline);
}

static void collect_calibration_250g() {
  g_counts_at_cal = COUNTS_AT_CAL;
  Serial.print(F("Calibration step disabled; using COUNTS_AT_CAL="));
  Serial.println(g_counts_at_cal);
}

static void process_serial_commands() {
  static char line[40];
  static size_t len = 0;
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      // Enter alone is treated as acknowledge/resume/reset.
      if (len == 0) {
        g_serial_ack = true;
      } else {
        line[len] = '\0';
        if (strcmp(line, "ack") == 0 || strcmp(line, "resume") == 0) {
          g_serial_ack = true;
        } else if (strcmp(line, "status") == 0) {
          log_event("status");
        } else if (strcmp(line, "heating_on") == 0 || strcmp(line, "heat_on") == 0) {
          g_heating_info = true;
          Serial.println(F("heating_info=ON"));
        } else if (strcmp(line, "heating_off") == 0 || strcmp(line, "heat_off") == 0) {
          g_heating_info = false;
          Serial.println(F("heating_info=OFF"));
        }
      }
      len = 0;
    } else if (len < sizeof(line) - 1) {
      line[len++] = c;
    }
  }
}

static void updateSensors() {
  g_sensor.time_ms = millis();
  g_sensor.valid = true;

  if (!scale.is_ready()) {
    g_sensor.valid = false;
    return;
  }

  const long raw = scale.read();
  const int32_t delta = static_cast<int32_t>(raw - g_baseline);
  g_smoothed_raw_delta_counts =
      (g_first_sample ? static_cast<float>(delta)
                      : (RAW_COUNTS_EMA_ALPHA * static_cast<float>(delta)) +
                            ((1.0f - RAW_COUNTS_EMA_ALPHA) * g_smoothed_raw_delta_counts));

  const float tempC = USE_DUMMY_TEMPERATURE ? DUMMY_TEMP_C : 25.0f;
  g_sensor.plate_temp_C = tempC;

  if (g_first_sample) {
    g_sensor.raw_delta_counts = static_cast<int32_t>(lroundf(g_smoothed_raw_delta_counts));
    const int32_t divisor = (g_counts_at_cal > 0) ? g_counts_at_cal : 1;
    float grams = g_sensor.raw_delta_counts * (static_cast<float>(CAL_REFERENCE_G) / static_cast<float>(divisor));
    if (grams < 0.0f) {
      grams = 0.0f;
    }
    g_sensor.mass_g = grams;

    g_prev_raw_delta_counts = g_sensor.raw_delta_counts;
    g_prev_mass_g = g_sensor.mass_g;
    g_prev_temp_C = g_sensor.plate_temp_C;
    g_prev_sample_ms = g_sensor.time_ms;
    g_last_sensor_publish_ms = g_sensor.time_ms;
    g_sensor.raw_rate_counts_per_min = 0.0f;
    g_sensor.mass_rate_g_per_min = 0.0f;
    g_sensor.temp_rate_C_per_min = 0.0f;
    g_first_sample = false;
  } else {
    const uint32_t since_publish_ms = g_sensor.time_ms - g_last_sensor_publish_ms;
    if (since_publish_ms >= SENSOR_PUBLISH_INTERVAL_MS) {
      g_sensor.raw_delta_counts = static_cast<int32_t>(lroundf(g_smoothed_raw_delta_counts));
      g_last_sensor_publish_ms = g_sensor.time_ms;

      const int32_t divisor = (g_counts_at_cal > 0) ? g_counts_at_cal : 1;
      float grams = g_sensor.raw_delta_counts * (static_cast<float>(CAL_REFERENCE_G) / static_cast<float>(divisor));
      if (grams < 0.0f) {
        grams = 0.0f;
      }
      g_sensor.mass_g = grams;

      const uint32_t dt_ms = g_sensor.time_ms - g_prev_sample_ms;
      const float dt_min = static_cast<float>(dt_ms) / 60000.0f;
      if (dt_min > 0.0001f) {
        const float raw_rate = (static_cast<float>(g_sensor.raw_delta_counts - g_prev_raw_delta_counts)) / dt_min;
        const float mass_rate_raw = (g_sensor.mass_g - g_prev_mass_g) / dt_min;
        const float temp_rate_raw = (g_sensor.plate_temp_C - g_prev_temp_C) / dt_min;
        g_sensor.raw_rate_counts_per_min =
            (MASS_RATE_EMA_ALPHA * raw_rate) + ((1.0f - MASS_RATE_EMA_ALPHA) * g_sensor.raw_rate_counts_per_min);
        g_sensor.mass_rate_g_per_min =
            (MASS_RATE_EMA_ALPHA * mass_rate_raw) + ((1.0f - MASS_RATE_EMA_ALPHA) * g_sensor.mass_rate_g_per_min);
        g_sensor.temp_rate_C_per_min =
            (TEMP_RATE_EMA_ALPHA * temp_rate_raw) + ((1.0f - TEMP_RATE_EMA_ALPHA) * g_sensor.temp_rate_C_per_min);
      }
      g_prev_raw_delta_counts = g_sensor.raw_delta_counts;
      g_prev_mass_g = g_sensor.mass_g;
      g_prev_temp_C = g_sensor.plate_temp_C;
      g_prev_sample_ms = g_sensor.time_ms;
    }
  }

  g_sensor.heater_current_on = g_relay_enabled;

  if (isnan(g_sensor.mass_g) || isnan(g_sensor.mass_rate_g_per_min) || isnan(g_sensor.plate_temp_C) ||
      isnan(g_sensor.temp_rate_C_per_min)) {
    g_sensor.valid = false;
  }
}

static void computeDerivedSignals() {
  g_derived.mass_present = g_sensor.raw_delta_counts > LOAD_PRESENT_THRESHOLD_COUNTS;
  g_derived.heating_active = g_heating_info || g_sensor.heater_current_on;
  g_derived.mass_stable = fabsf(g_sensor.raw_rate_counts_per_min) < STABLE_RATE_THRESHOLD_COUNTS_PER_MIN;
  g_derived.rapid_mass_drop = g_sensor.raw_rate_counts_per_min < RAPID_DROP_THRESHOLD_COUNTS_PER_MIN;
  g_derived.drying_detected = g_sensor.raw_rate_counts_per_min < DRYING_RATE_THRESHOLD_COUNTS_PER_MIN;
  g_derived.temp_rising = false;
  g_derived.near_zero_rate_while_hot =
      g_derived.heating_active &&
      (fabsf(g_sensor.raw_rate_counts_per_min) < NEAR_ZERO_RATE_THRESHOLD_COUNTS_PER_MIN);

  float ratio = 1.0f;
  if (g_baseline_load_counts > 0) {
    ratio = static_cast<float>(g_sensor.raw_delta_counts) / static_cast<float>(g_baseline_load_counts);
  }
  g_derived.dry_condition = ratio < DRY_SHUTDOWN_THRESHOLD;
  g_derived.load_removed_while_hot = (!g_derived.mass_present) && g_derived.heating_active;
  g_derived.warning_timer_expired =
      (g_state == SystemState::WARNING) && (g_sensor.time_ms - g_warning_start_ms >= WARNING_COUNTDOWN_MS);
  g_derived.sensor_fault = !g_sensor.valid;
}

static bool unified_shutdown_trigger(ShutdownReason* reason) {
  if (g_derived.sensor_fault) {
    *reason = ShutdownReason::SensorFault;
    return true;
  }
  if (g_derived.rapid_mass_drop) {
    *reason = ShutdownReason::RapidMassLoss;
    return true;
  }
  if (g_derived.warning_timer_expired) {
    *reason = ShutdownReason::WarningExpired;
    return true;
  }
  return false;
}

static SystemState evaluateTransitions() {
  ShutdownReason reason = ShutdownReason::None;
  if (unified_shutdown_trigger(&reason)) {
    g_shutdown_reason = reason;
    return (reason == ShutdownReason::SensorFault) ? SystemState::FAULT : SystemState::SHUTDOWN;
  }

  switch (g_state) {
    case SystemState::IDLE:
      if (g_derived.mass_present) return SystemState::LOAD_DETECTED;
      if (g_derived.heating_active) return SystemState::HEATING_MONITORING;
      break;

    case SystemState::LOAD_DETECTED:
      if (!g_derived.mass_present) return SystemState::IDLE;
      if (g_derived.heating_active) return SystemState::HEATING_MONITORING;
      if (g_derived.mass_stable) {
        if (g_load_stable_start_ms == 0) {
          g_load_stable_start_ms = g_sensor.time_ms;
        } else if (g_sensor.time_ms - g_load_stable_start_ms >= STABLE_TIME_MS) {
          g_baseline_load_counts = g_sensor.raw_delta_counts;
        }
      } else {
        g_load_stable_start_ms = 0;
      }
      break;

    case SystemState::HEATING_MONITORING:
      if (!g_derived.mass_present) return SystemState::PAUSED_BY_USER;
      if (g_derived.drying_detected) return SystemState::EVAPORATING;
      if (!g_derived.heating_active) return SystemState::LOAD_DETECTED;
      break;

    case SystemState::EVAPORATING: {
      const float ratio =
          (g_baseline_load_counts > 0)
              ? (static_cast<float>(g_sensor.raw_delta_counts) / static_cast<float>(g_baseline_load_counts))
              : 1.0f;
      if (ratio < DRY_THRESHOLD || g_derived.near_zero_rate_while_hot) return SystemState::WARNING;
      break;
    }

    case SystemState::WARNING:
      if (g_serial_ack) return SystemState::HEATING_MONITORING;
      if (g_baseline_load_counts > 0) {
        const int32_t resume_threshold = static_cast<int32_t>(
            static_cast<float>(g_baseline_load_counts) * (1.0f + WARNING_RECOVERY_MARGIN_RATIO));
        if (g_sensor.raw_delta_counts > resume_threshold) return SystemState::HEATING_MONITORING;
      }
      break;

    case SystemState::PAUSED_BY_USER:
      if (g_derived.mass_present && (g_sensor.time_ms - g_pause_start_ms < PAUSE_GRACE_MS)) {
        return SystemState::HEATING_MONITORING;
      }
      if (g_sensor.time_ms - g_pause_start_ms >= PAUSE_GRACE_MS) {
        g_shutdown_reason = ShutdownReason::PauseExpired;
        return SystemState::SHUTDOWN;
      }
      break;

    case SystemState::SHUTDOWN:
      if (g_serial_ack) {
        if (g_derived.mass_present && g_derived.mass_stable) return SystemState::LOAD_DETECTED;
        if (!g_derived.mass_present) return SystemState::IDLE;
      }
      break;

    case SystemState::FAULT:
      if (g_serial_ack && !g_derived.sensor_fault) return SystemState::IDLE;
      break;
  }

  return g_state;
}

static void applyStateActions(SystemState new_state) {
  if (new_state != g_state) {
    g_state = new_state;
    g_state_enter_ms = g_sensor.time_ms;
    log_event("state_change");
    if (g_state == SystemState::WARNING) {
      g_warning_start_ms = g_sensor.time_ms;
    }
    if (g_state == SystemState::PAUSED_BY_USER) {
      g_pause_start_ms = g_sensor.time_ms;
    }
    if (g_state == SystemState::LOAD_DETECTED) {
      g_load_stable_start_ms = 0;
    }
  }

  if (g_state == SystemState::IDLE && g_derived.mass_stable && !g_derived.heating_active) {
    if (g_idle_stable_start_ms == 0) {
      g_idle_stable_start_ms = g_sensor.time_ms;
    } else if (g_sensor.time_ms - g_idle_stable_start_ms >= AUTO_TARE_DWELL_MS) {
      g_baseline = g_baseline + g_sensor.raw_delta_counts;
      g_idle_stable_start_ms = g_sensor.time_ms;
      log_event("auto_tare");
    }
  } else {
    g_idle_stable_start_ms = 0;
  }

  // Safety-first heater policy:
  // Only energize relay in active heating phases.
  const bool should_heat =
      (g_state == SystemState::HEATING_MONITORING) ||
      (g_state == SystemState::EVAPORATING) ||
      (g_state == SystemState::WARNING);
  if (g_relay_enabled != should_heat) {
    set_relay_enabled(should_heat);
  }
}

static lv_color_t state_color(SystemState s) {
  switch (s) {
    case SystemState::IDLE: return lv_color_hex(0x0E8E3A);
    case SystemState::LOAD_DETECTED:
    case SystemState::HEATING_MONITORING: return lv_color_hex(0x1B65D6);
    case SystemState::EVAPORATING: return lv_color_hex(0xB28A00);
    case SystemState::WARNING: return lv_color_hex(0xD67A00);
    case SystemState::SHUTDOWN:
    case SystemState::FAULT: return lv_color_hex(0xB11818);
    case SystemState::PAUSED_BY_USER: return lv_color_hex(0x4444AA);
  }
  return lv_color_hex(0x222222);
}

static const char* status_line() {
  switch (g_state) {
    case SystemState::IDLE: return "Ready";
    case SystemState::LOAD_DETECTED: return "Load detected, stabilizing";
    case SystemState::HEATING_MONITORING: return "Monitoring heat and count rate";
    case SystemState::EVAPORATING: return "Evaporation detected";
    case SystemState::WARNING: return "Liquid nearly gone - touch screen";
    case SystemState::SHUTDOWN: return reason_name(g_shutdown_reason);
    case SystemState::PAUSED_BY_USER: return "Load removed - waiting";
    case SystemState::FAULT: return "Sensor fault - touch screen to reset";
  }
  return "";
}

static void updateUI() {
  if (g_status_label == nullptr) return;
  if (millis() - g_last_ui_update_ms < UI_UPDATE_MS) return;
  g_last_ui_update_ms = millis();

  lv_obj_set_style_bg_color(lv_scr_act(), state_color(g_state), 0);
  lv_obj_set_style_text_color(g_status_label, lv_color_hex(0xFFFFFF), 0);

  char text[320];
  snprintf(
      text,
      sizeof(text),
      "STATE: %s\nHeater: %s\n\nCounts: %ld\nRate: %.1f counts/min\nTemp: %.1f C\n\nStatus: %s",
      state_name(g_state),
      g_relay_enabled ? "ON" : "OFF",
      static_cast<long>(g_sensor.raw_delta_counts),
      g_sensor.raw_rate_counts_per_min,
      g_sensor.plate_temp_C,
      status_line());
  lv_label_set_text(g_status_label, text);
}

void setup() {
  ets_printf("[EARLY] setup entered\n");
  static_assert(COUNTS_AT_CAL > 0, "COUNTS_AT_CAL must be positive");

  pinMode(PIN_HEATER_RELAY, OUTPUT);
  // Keep heater OFF until state machine explicitly allows heating.
  digitalWrite(PIN_HEATER_RELAY, (RELAY_ACTIVE_LEVEL == HIGH ? LOW : HIGH));
  g_relay_enabled = false;
  ets_printf("[EARLY] relay forced OFF\n");

  Serial.begin(SERIAL_BAUD);
  ets_printf("[EARLY] Serial.begin done\n");
  delay(200);
  Serial.println(F("[BOOT] smart-scale setup start"));
  Serial.print(F("[BOOT] reset_reason="));
  Serial.println(static_cast<uint32_t>(esp_reset_reason()));
  Serial.print(F("[BOOT] SERIAL_BAUD="));
  Serial.println(SERIAL_BAUD);

  Serial.println(F("[BOOT] init_display_ui start"));
  init_display_ui();
  Serial.println(F("[BOOT] init_display_ui done"));

  init_nvs_storage();

  // Touchscreen calibration intentionally removed.

  Serial.println(F("[BOOT] HX711 begin start"));
  scale.begin(PIN_HX711_DT, PIN_HX711_SCK);
  Serial.println(F("HX711 begin OK"));
  Serial.print(F("[BOOT] HX711 pins dt="));
  Serial.print(PIN_HX711_DT);
  Serial.print(F(", sck="));
  Serial.println(PIN_HX711_SCK);

  {
    int32_t saved_baseline = 0;
    int32_t saved_counts = 0;
    if (load_scale_calibration(&saved_baseline, &saved_counts)) {
      g_baseline = saved_baseline;
      g_counts_at_cal = saved_counts;
      Serial.println(F("[BOOT][SCALE] calibration loaded"));
    } else {
      Serial.println(F("[BOOT][SCALE] calibration missing; running first-boot tare"));
      run_first_boot_tare();
    }
  }

  g_state_enter_ms = millis();
  log_event("boot_complete");
  Serial.println(F("[BOOT] setup complete"));
}

void loop() {
  lvgl_tick();
  lv_timer_handler();

  process_serial_commands();

  updateSensors();
  computeDerivedSignals();

  const SystemState next = evaluateTransitions();
  applyStateActions(next);
  updateUI();

  if (g_serial_ack) {
    g_serial_ack = false;
  }

  static uint32_t last_hb = 0;
  if (millis() - last_hb >= HEARTBEAT_INTERVAL_MS) {
    last_hb = millis();
    log_event("hb");
  }

  delay(SAMPLE_DELAY_MS);
}
