#pragma once

/* Tell LVGL this file was included successfully. */
#ifndef LV_CONF_H
#define LV_CONF_H
#endif

/*
 * Minimal LVGL project configuration for ESP32C3 + TFT_eSPI.
 * Keep this intentionally small for initial integration.
 */

#define LV_COLOR_DEPTH 16

/* Byte swap for RGB565; Monitor-Project uses 1 — tune with TFT_BGR if hues look wrong */
#define LV_COLOR_16_SWAP 1

#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (32U * 1024U)

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 0
#define LV_USE_ASSERT_MALLOC 0
#define LV_USE_ASSERT_STYLE 0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ 0

#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

#define LV_USE_THEME_DEFAULT 1
#define LV_USE_LABEL 1

/* Fonts */
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_DEFAULT &lv_font_montserrat_18
