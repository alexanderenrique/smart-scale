#pragma once

// Project-local TFT_eSPI setup for 240x320 ILI9341 + XPT2046 touch.
// Adjust these pin assignments to match your wiring.

#define ILI9341_DRIVER

#define TFT_WIDTH 240
#define TFT_HEIGHT 320

// XIAO ESP32C3 explicit SPI pin mapping.
#define TFT_MOSI 7
#define TFT_MISO 6
#define TFT_SCLK 8

// Display control pins.
#define TFT_CS 4
#define TFT_DC 5
#define TFT_RST 21

// Touch controller pins (XPT2046).
#define TOUCH_CS 20
#define TOUCH_IRQ -1

// Fonts and optional rendering support.
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF

// SPI clock rates. Reduce if wiring/noise causes instability.
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 20000000
#define SPI_TOUCH_FREQUENCY 2500000
