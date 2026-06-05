// ============================================================================
// SpiroBird Controller — central configuration
//
// ALL pins, thresholds, timeouts and feature flags live here.
// Tune values here instead of digging through the code.
// ============================================================================
#pragma once
#include <Arduino.h>

// Server URL / device ID live in secrets.h (gitignored). The example file
// provides safe defaults so the project always compiles.
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #include "secrets.example.h"
#endif

// ============================================================================
// FEATURE FLAGS — the core game MUST work with any combination of these.
//
// BRING-UP PRESETS (docs/testing-plan.md):
//   Step 1 (Controller standalone): WIFI 0, ESPNOW 0, MOTOR 0, BUZZER 0
//   Step 2 (Buzzer):                + BUZZER 1
//   Step 3 (Motor):                 + MOTOR 1   (ONLY after wiring verified!)
//   Step 4 (Backend):               + WIFI 1, SERVER_POST 1
//   Step 6 (ESP-NOW):               + ESPNOW 1
// ============================================================================

#define ENABLE_WIFI               1   // captive portal provisioning + reconnect
#define ENABLE_WIFI_MANAGER       1   // 1=tzapu/WiFiManager, 0=built-in fallback portal
#define ENABLE_SERVER_POST        1   // HTTP POST results to backend after attempts
#define ENABLE_ESPNOW             1   // broadcast SpiroPacket to Display
#define ENABLE_OFFLINE_MODE       1   // game works without Wi-Fi/server
#define ENABLE_BUZZER             1
#define ENABLE_MOTOR              0   // !! keep 0 until transistor wiring is verified !!
#define ENABLE_STATUS_LED         1   // onboard WS2812 RGB (neopixelWrite)
#define ENABLE_SLEEP_MODE         0   // pseudo + deep sleep (enable once core works)
#define ENABLE_DEBUG_SERIAL       1
#define ENABLE_SAMPLE_TIMER_ISR   0   // hw-timer ISR sets sample flag (else millis())

// ============================================================================
// PIN MAPPING — Controller ESP32-S3 (see docs/wiring.md)
// Avoid: GPIO0/45/46 (strapping), GPIO26-37 (flash/octal PSRAM), GPIO19/20 (USB)
// ============================================================================

#define PIN_POT_ADC       4    // ADC1 — potentiometer wiper
#define PIN_BUZZER        15   // passive buzzer, LEDC PWM tone
#define PIN_MOTOR         17   // 1 kOhm -> NPN base (NEVER the motor directly)
#define PIN_WAKE_BUTTON   21   // momentary rocker -> GND, INPUT_PULLUP, RTC-capable
#define PIN_STATUS_LED    48   // onboard WS2812 RGB

// ============================================================================
// LOOP TIMING (millis() scheduling, no blocking delay() in core logic)
// ============================================================================

#define SENSOR_SAMPLE_INTERVAL_MS   10    // 100 Hz ADC sampling + logic update
#define ESPNOW_SEND_INTERVAL_MS     25    // 40 Hz live packets to Display
#define DEBUG_PRINT_INTERVAL_MS     500   // periodic Serial status line
#define BUTTON_DEBOUNCE_MS          200

// ============================================================================
// ADC / POTENTIOMETER (12-bit, 0-4095)
// ============================================================================

#define ADC_MIN_USABLE            200     // ESP32 ADC is nonlinear near the rails
#define ADC_MAX_USABLE            3900
#define ADC_DEADZONE              70      // counts around calibrated center -> flow 0
#define ADC_SAMPLES_PER_READ      4       // averaged per update() call
#define CALIBRATION_DURATION_MS   1000    // averaging window at startup/rest
// Expected center is ~2048 but it is ALWAYS calibrated, never hardcoded.

// ============================================================================
// FLOW MAPPING & FILTERING (ml/s)
// ============================================================================

#define FLOW_MAX_ML_S             1400.0f // max deviation maps to this
#define EMA_ALPHA                 0.20f   // filtered = a*raw + (1-a)*prev

// Medical thresholds — fixed by the project specification:
#define FLOW_LINE_LOW_ML_S        600.0f  // lower reference line
#define FLOW_TARGET_MIN_ML_S      900.0f  // target zone start
#define FLOW_TARGET_MAX_ML_S      1200.0f // target zone end / instant fail above
#define FLOW_FAIL_RAW_ML_S        1250.0f // unfiltered flow above this -> fail too
#define FLOW_WARN_ML_S            1150.0f // approaching danger -> haptic warning

#define FLOW_START_THRESHOLD_ML_S 150.0f  // READY -> ACTIVE when flow rises above

// ============================================================================
// STABILITY & EXERCISE RULES
// ============================================================================

#define STABILITY_WINDOW_MS       500     // ring buffer of filtered flow
#define STABILITY_P2P_MAX_ML_S    180.0f  // peak-to-peak limit inside the window
#define STABLE_SUCCESS_MS         5000    // continuous stable time for success

#define ATTEMPT_TIMEOUT_MS        60000UL // ACTIVE longer than this -> FAIL_TIMEOUT
#define SUCCESS_TO_RESULT_MS      1200    // SUCCESS/FAIL screen flash before RESULT
#define RESULT_DURATION_MS        5000    // RESULT screen, then back to IDLE

// ============================================================================
// HAPTICS — BUZZER (passive, LEDC tone) & MOTOR (NPN low-side, 5 V supply)
// ============================================================================

#define BUZZER_LEDC_CHANNEL       0       // used only on Arduino core 2.x API

// Motor safety: 3 V motor on 5 V supply -> SHORT pulses only, hard guards.
#define MOTOR_PWM_DUTY_MAX        90      // of 255 (~35 %) if PWM is ever used
#define MOTOR_MAX_PULSE_MS        250     // hard upper limit, enforced in update()
#define MOTOR_COOLDOWN_MS         500     // minimum off-time between pulses
#define MOTOR_PULSE_WARN_MS       80      // entering danger / leaving zone
#define MOTOR_PULSE_COLLISION_MS  150
#define MOTOR_PULSE_FAIL_MS       250

// ============================================================================
// SLEEP / POWER MANAGEMENT (Controller only — Display never deep-sleeps)
// ============================================================================

#define ENABLE_CONTROLLER_PSEUDO_SLEEP  1
#define ENABLE_CONTROLLER_DEEP_SLEEP    1
#define PSEUDO_SLEEP_TIMEOUT_MS         60000UL   // 60 s idle -> pseudo sleep
#define DEEP_SLEEP_TIMEOUT_MS           180000UL  // 3 min idle -> real deep sleep
#define POT_MOVEMENT_WAKE_ADC           150       // raw delta to wake from pseudo sleep

// ============================================================================
// WI-FI / SERVER
// ============================================================================

#define WIFI_AP_NAME              "SpiroBird-Setup"
#define WIFI_CONNECT_TIMEOUT_MS   8000
#define WIFI_PORTAL_TIMEOUT_SEC   180
#define WIFI_RECONNECT_INTERVAL_MS 30000UL  // periodic non-blocking reconnect
#define HTTP_TIMEOUT_MS           1500
#define SERVER_RESULTS_PATH       "/api/results"
#define SERVER_HEALTH_PATH        "/health"
#define SERVER_POST_RETRIES       3
#define SERVER_RETRY_INTERVAL_MS  5000UL

// ESP-NOW channel when Wi-Fi is disabled/offline (with Wi-Fi connected the
// active Wi-Fi channel is used automatically — see docs/protocol.md).
#define ESPNOW_FIXED_CHANNEL      1
// How often the Display is told about the open setup portal:
#define PORTAL_STATUS_SEND_INTERVAL_MS 250

// ============================================================================
// DEBUG MACROS
// ============================================================================

#if ENABLE_DEBUG_SERIAL
  #define DBG(...)   Serial.printf(__VA_ARGS__)
#else
  #define DBG(...)   do {} while (0)
#endif
