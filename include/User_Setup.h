#pragma once

// Project-local TFT_eSPI setup for 240x320 ILI9341 + XPT2046 touch.
// Adjust these pin assignments to match your wiring.

#define ILI9341_DRIVER

#define TFT_WIDTH 240
#define TFT_HEIGHT 320

// Panel sends BGR order; without this, red/blue (or green/blue) look swapped — same approach as Monitor-Project.
#define TFT_RGB_ORDER TFT_BGR

// DMA for faster SPI block writes (optional parity with Monitor-Project).
#define ESP32_DMA_CHANNEL 1

// ESP32 DevKit: explicit SPI lines (shared by TFT + XPT2046 touch).
#define TFT_MOSI 26
#define TFT_MISO 32
#define TFT_SCLK 25

// Display control pins.
#define TFT_CS 13
#define TFT_DC 27
#define TFT_RST 14

// Touch controller pins (XPT2046).
#define TOUCH_CS 33
// T_IRQ not used.
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
