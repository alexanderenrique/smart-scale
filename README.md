# smart-scale

Smart load scale firmware for lab safety (PlatformIO / Arduino). This project is intended to use a thermocouple in combination with a scale to measure hot plate conditions and determine whether the hot plate should be automatically shut down in the event of a boil-off or liquid spillage.

## Quick start

From the project folder (`smart-scale`):

## Clean build artifacts

```bash
pio run -t clean
```

Removes the `seeed_xiao_esp32c3` build directory under `.pio/build/`. Next `pio run` is a full recompile.

## Build (after clean or anytime)

```bash
pio run
```

## Upload firmware

```bash
pio run -t upload
```

Ensure the XIAO ESP32C3 is connected by USB and the correct serial port is available (PlatformIO usually auto-picks it).

## Clean + build + upload (one shot)

```bash
pio run -t clean && pio run -t upload && pio device monitor
```

## Serial monitor (115200 baud)

`platformio.ini` sets `monitor_speed = 115200`.

```bash
pio device monitor
```

Or: **PlatformIO: Upload and Monitor** from the command palette (builds/uploads/opens monitor if your environment supports it).

## Display stack (TFT + LVGL)

This firmware now includes:
- `TFT_eSPI` for 240x320 ILI9341 TFT drawing and XPT2046 touch reads
- `lvgl` for UI rendering

Configuration files:
- Display + touch driver/pins: `include/User_Setup.h`
- LVGL config: `include/lv_conf.h`

`src/main.cpp` now initializes TFT + LVGL during `setup()` and services LVGL in the main `loop()` with `lv_tick_inc(...)` and `lv_timer_handler()`.

If your wiring differs, update the pin defines in `include/User_Setup.h` before upload.

## Calibration / interaction (on boot)

1. Remove all weight from the platform, then press Enter.
2. Place the 250 g calibration weight on the platform, then press Enter.

After that, it prints lines at ~2 Hz:

`raw_delta_g delta_g grams`

## HX711 wiring defaults

From `include/hardware.h`:
- `DOUT -> PIN_HX711_DT` (default `9`)
- `PD_SCK -> PIN_HX711_SCK` (default `10`)

