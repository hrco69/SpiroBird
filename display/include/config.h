// ============================================================================
// SpiroBird Display — central configuration
// (hardware/TFT pins live in display_config.h, protocol in protocol.h)
// ============================================================================
#pragma once
#include <Arduino.h>

// ============================================================================
// FEATURE FLAGS
// ============================================================================

#define ENABLE_DEBUG_SERIAL            1
#define ENABLE_ESPNOW_CHANNEL_SCAN     1   // scan 1-13 until valid packets found
#define ENABLE_DISPLAY_FAKE_DATA_MODE  1   // demo animation when no Controller
                                           // (bring-up step 5: display standalone)
#define ENABLE_DISPLAY_PSEUDO_SLEEP    1   // backlight dim/off — radio keeps listening
#define ENABLE_DISPLAY_TOUCH_WAKE      1   // FT6336G touch wakes the screen
#define ENABLE_DISPLAY_DEEP_SLEEP      0   // MUST stay 0: deep sleep kills the
                                           // ESP-NOW receiver (see display_config.h)

// Pseudo-sleep timing: full -> dim after inactivity -> backlight OFF.
// "Activity" = game states / portal / decision on the Controller, or a touch.
#define DISPLAY_DIM_AFTER_MS           60000   // 1 min idle -> dim
#define DISPLAY_OFF_AFTER_DIM_MS       15000   // +15 s dimmed -> backlight off
#define DISPLAY_BRIGHTNESS_FULL        255
#define DISPLAY_BRIGHTNESS_DIM         40
#define TOUCH_POLL_INTERVAL_MS         50

// ============================================================================
// RENDERING
// ============================================================================

#define DISPLAY_ROTATION          1     // 1 = landscape 320x240 (3 if upside down)
#define FRAME_INTERVAL_MS         33    // ~30 FPS, millis() scheduled
#define DEBUG_PRINT_INTERVAL_MS   1000

// ============================================================================
// ESP-NOW CHANNEL SCAN / LOCK (docs/protocol.md)
// ============================================================================

#define CHANNEL_SCAN_MIN          1
#define CHANNEL_SCAN_MAX          13    // EU; use 11 for US regulatory domain
#define CHANNEL_SCAN_DWELL_MS     300   // listen time per channel while scanning
#define LOCK_VALID_PACKETS        3     // valid packets on one channel -> lock
#define SIGNAL_STALE_MS           1000  // no packet for this long -> "NO SIGNAL"
#define SIGNAL_LOST_RESCAN_MS     3000  // no packet for this long -> re-scan

// Fake-data demo starts if no valid packet ever arrived within this time:
#define FAKE_DATA_START_MS        8000

// ============================================================================
// GAME VISUALS / PHYSICS (display-side only — Controller is source of truth)
// ============================================================================

// Flow thresholds — MUST match controller/include/config.h:
#define FLOW_MAX_ML_S             1400.0f
#define FLOW_LINE_LOW_ML_S        600.0f
#define FLOW_TARGET_MIN_ML_S      900.0f
#define FLOW_TARGET_MAX_ML_S      1200.0f
#define STABLE_SUCCESS_MS         5000

// Bird inertia: vel += (targetY - y) * spring; vel *= damping; y += vel
#define BIRD_X                    70
#define BIRD_SPRING               0.08f
#define BIRD_DAMPING              0.86f

// Cosmetic scrolling pipes (collision is NOT judged here):
#define PIPE_SPEED_PX             2     // px per frame
#define PIPE_SPACING_PX           120
#define PIPE_WIDTH_PX             26
#define PIPE_GAP_MARGIN_PX        14   // extra opening beyond the target band

// ============================================================================
// DEBUG MACROS
// ============================================================================

#if ENABLE_DEBUG_SERIAL
  #define DBG(...)   Serial.printf(__VA_ARGS__)
#else
  #define DBG(...)   do {} while (0)
#endif
