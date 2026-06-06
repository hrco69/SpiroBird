// ============================================================================
// SpiroBird Display — LovyanGFX device configuration for LCDWiki ES3C28P
//
// WHY LovyanGFX (not TFT_eSPI): TFT_eSPI v2.5.x crashes with
// "StoreProhibited EXCVADDR 0x00000010" in tft.init() on ESP32-S3 with
// Arduino core >= 2.0.14 — a known unresolved issue (Bodmer/TFT_eSPI #3743),
// reproduced on this exact board during bring-up step 5. LovyanGFX handles
// the S3 (incl. pins > 31 like DC=46/BL=45) correctly.
//
// Pin source: official LCDWiki page "2.8inch ESP32-S3 Display" (ES3C28P/ES3N28P)
//   https://www.lcdwiki.com/2.8inch_ESP32-S3_Display
// ============================================================================
#pragma once
#include <LovyanGFX.hpp>

// ---- LCD pins (LCDWiki I/O allocation) --------------------------------------
#define LCD_PIN_CS    10
#define LCD_PIN_DC    46
#define LCD_PIN_RST   -1     // LCD reset tied to the ESP32-S3 reset line
#define LCD_PIN_SCLK  12
#define LCD_PIN_MOSI  11
#define LCD_PIN_MISO  13
#define LCD_PIN_BL    45     // backlight (PWM-dimmable)

// Capacitive touch (FT6336G: SDA=16, SCL=15, RST=18, INT=17) is NOT used in
// v1 — the Display has no touch functions in this project by design.

// ILI9341V panel, 240x320, 4-wire SPI.
class LGFX_ES3C28P : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
  lgfx::Light_PWM     _light_instance;

public:
  LGFX_ES3C28P() {
    {  // SPI bus
      auto cfg = _bus_instance.config();
      cfg.spi_host    = SPI2_HOST;       // FSPI on the S3
      cfg.spi_mode    = 0;
      cfg.freq_write  = 40000000;        // drop to 27000000 if you see glitches
      cfg.freq_read   = 16000000;
      cfg.spi_3wire   = false;
      cfg.use_lock    = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk    = LCD_PIN_SCLK;
      cfg.pin_mosi    = LCD_PIN_MOSI;
      cfg.pin_miso    = LCD_PIN_MISO;
      cfg.pin_dc      = LCD_PIN_DC;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {  // panel
      auto cfg = _panel_instance.config();
      cfg.pin_cs           = LCD_PIN_CS;
      cfg.pin_rst          = LCD_PIN_RST;
      cfg.pin_busy         = -1;
      cfg.panel_width      = 240;
      cfg.panel_height     = 320;
      cfg.offset_x         = 0;
      cfg.offset_y         = 0;
      cfg.offset_rotation  = 0;
      cfg.readable         = true;
      cfg.invert           = false;   // flip to true if colors look negative
      cfg.rgb_order        = false;   // flip if red/blue are swapped
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = false;
      _panel_instance.config(cfg);
    }
    {  // backlight
      auto cfg = _light_instance.config();
      cfg.pin_bl      = LCD_PIN_BL;
      cfg.invert      = false;
      cfg.freq        = 44100;
      cfg.pwm_channel = 7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }
    setPanel(&_panel_instance);
  }
};

// ============================================================================
// Troubleshooting (docs/troubleshooting.md has the long version):
//  - white/black screen  -> check CS/DC/SCLK/MOSI pins above vs your board rev
//  - backlight dead      -> LCD_PIN_BL / cfg.invert in the light section
//  - mirrored/rotated    -> DISPLAY_ROTATION in include/config.h (1 <-> 3)
//  - negative colors     -> cfg.invert = true
//  - red/blue swapped    -> cfg.rgb_order = true
//  - glitchy pixels      -> cfg.freq_write = 27000000
// ============================================================================
