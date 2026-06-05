// ============================================================================
// SpiroBird Display — TFT_eSPI hardware configuration for LCDWiki ES3C28P
//
// This file is force-included into EVERY translation unit via platformio.ini
// (-include include/display_config.h), so TFT_eSPI compiles with these pins
// without editing the library's User_Setup.h.
// !! Pure #defines only — no C/C++ code, it is included into C files too. !!
//
// Pin source: official LCDWiki page "2.8inch ESP32-S3 Display" (ES3C28P/ES3N28P)
//   https://www.lcdwiki.com/2.8inch_ESP32-S3_Display
//
// TODO(hardware bring-up): verify these against the LCDWiki demo package for
// YOUR exact board revision (Program download -> Arduino demo -> setup file).
// If the screen stays white/black or colors are inverted, see the notes at
// the bottom of this file.
// ============================================================================
#pragma once

#define USER_SETUP_LOADED 1

// ---- Driver ----------------------------------------------------------------
// Panel is ILI9341V. Standard ILI9341 driver works for it in TFT_eSPI.
// TODO: if colors/orientation look wrong, try ILI9341_2_DRIVER instead.
#define ILI9341_DRIVER 1

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// ---- Pins (LCDWiki ES3C28P I/O allocation) ----------------------------------
#define TFT_CS   10
#define TFT_DC   46
#define TFT_RST  -1     // LCD reset is tied to the ESP32-S3 reset line
#define TFT_SCLK 12
#define TFT_MOSI 11
#define TFT_MISO 13
#define TFT_BL   45     // backlight
#define TFT_BACKLIGHT_ON HIGH

// Capacitive touch (FT6336G: SDA=16, SCL=15, RST=18, INT=17) is NOT used in
// v1 — the Display has no touch functions in this project by design.

// ---- SPI --------------------------------------------------------------------
// 40 MHz is usually fine for ILI9341 on short on-board traces.
// TODO: drop to 27000000 if you see flicker/corruption.
#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  20000000

// ---- Fonts ------------------------------------------------------------------
#define LOAD_GLCD   1   // 8px adafruit font
#define LOAD_FONT2  1   // 16px
#define LOAD_FONT4  1   // 26px
#define LOAD_FONT7  1   // 48px 7-segment (big flow number)
#define SMOOTH_FONT 1

// ============================================================================
// Troubleshooting notes (docs/troubleshooting.md has the long version):
//  - white screen        -> wrong CS/DC/SCLK/MOSI, or backlight pin/polarity
//  - mirrored/rotated    -> change DISPLAY_ROTATION in include/config.h (1<->3)
//  - inverted colors     -> add: #define TFT_INVERSION_ON 1   (or _OFF)
//  - garbage pixels      -> lower SPI_FREQUENCY to 27000000
// ============================================================================
