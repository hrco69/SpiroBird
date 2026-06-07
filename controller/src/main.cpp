// ============================================================================
// SpiroBird — Controller / Master ESP32-S3 firmware
//
// Reads the potentiometer (breath emulator), runs the exercise state machine,
// drives buzzer + vibration motor, persists the high score, and (from Faza 4)
// broadcasts live SpiroPackets over ESP-NOW and POSTs results to the backend.
//
// Design rules (course requirements):
//  - NO blocking delay() in core logic — everything is millis() scheduled
//  - ISRs only set volatile flags; all work happens in loop()
//  - every feature sits behind an ENABLE_* flag in config.h
//  - motor pin is forced LOW as the very first statement of setup()
// ============================================================================
#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "protocol.h"
#include "BreathSensor.h"
#include "ExerciseLogic.h"
#include "Haptics.h"
#include "Storage.h"

#if ENABLE_SLEEP_MODE
  #include <esp_sleep.h>
  #include <driver/rtc_io.h>
#endif

#if ENABLE_WIFI
  #include "WifiProvisioning.h"   // Faza 4
#endif
#if ENABLE_ESPNOW
  #include "EspNowSender.h"       // Faza 4
#endif
#if ENABLE_SERVER_POST
  #include "ServerClient.h"       // Faza 4
#endif

// ----------------------------------------------------------------------------
// Modules
// ----------------------------------------------------------------------------
static BreathSensor  sensor;
static ExerciseLogic logic;
static Haptics       haptics;
static Storage       storage;

#if ENABLE_WIFI
static WifiProvisioning wifiProv;
#endif
#if ENABLE_ESPNOW
static EspNowSender espNow;
#endif
#if ENABLE_SERVER_POST
static ServerClient serverClient;
#endif

// ----------------------------------------------------------------------------
// ISR flags — ISRs do NOTHING except set these (safe ISR design)
// ----------------------------------------------------------------------------
static volatile bool g_buttonIsrFlag = false;

static void IRAM_ATTR onButtonIsr() {
  g_buttonIsrFlag = true;
}

#if ENABLE_SAMPLE_TIMER_ISR
static hw_timer_t   *g_sampleTimer  = nullptr;
static volatile bool g_sampleFlag   = false;

static void IRAM_ATTR onSampleTimerIsr() {
  g_sampleFlag = true;   // ADC read + math happen in loop(), never here
}
#endif

// ----------------------------------------------------------------------------
// Scheduler bookkeeping
// ----------------------------------------------------------------------------
static uint32_t g_lastSampleMs     = 0;
static uint32_t g_lastDebugMs      = 0;
static uint32_t g_lastButtonMs     = 0;
static uint32_t g_lastActivityMs   = 0;
static uint16_t g_sleepRefAdc      = 0;     // pot position when pseudo sleep began
static bool     g_pendingResultSideEffects = false;

#if ENABLE_ESPNOW
static uint32_t g_lastEspNowMs = 0;
static uint32_t g_packetSeq    = 0;
#endif

// ----------------------------------------------------------------------------
// Status LED (onboard WS2812 via the core's neopixelWrite helper)
// ----------------------------------------------------------------------------
static void setStatusLed(ExerciseState s) {
#if ENABLE_STATUS_LED
  uint8_t r = 0, g = 0, b = 0;
  switch (s) {
    case STATE_IDLE:        b = 8;          break;  // dim blue
    case STATE_CALIBRATING: r = 8; b = 8;   break;  // purple
    case STATE_READY:       r = 8; g = 8;   break;  // yellow
    case STATE_ACTIVE:      g = 12;         break;  // green
    case STATE_SUCCESS:     g = 40;         break;  // bright green
    case STATE_FAIL:        r = 40;         break;  // red
    case STATE_RESULT:      g = 4; b = 4;   break;  // teal
    case STATE_SLEEP:       /* off */       break;
  }
  neopixelWrite(PIN_STATUS_LED, r, g, b);
#else
  (void)s;
#endif
}

// ----------------------------------------------------------------------------
// Boot diagnostics
// ----------------------------------------------------------------------------
static const char *resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "POWERON (normal power-up)";
    case ESP_RST_EXT:       return "EXT (external reset pin)";
    case ESP_RST_SW:        return "SW (software restart)";
    case ESP_RST_PANIC:     return "PANIC (crash/exception!)";
    case ESP_RST_INT_WDT:   return "INT_WDT (interrupt watchdog!)";
    case ESP_RST_TASK_WDT:  return "TASK_WDT (task watchdog!)";
    case ESP_RST_WDT:       return "WDT (other watchdog!)";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP (wake from deep sleep)";
    case ESP_RST_BROWNOUT:  return "BROWNOUT (supply voltage dipped! check wiring/shorts)";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
  }
}

static void printBootInfo() {
  DBG("\n==============================================\n");
  DBG(" SpiroBird Controller (MASTER) booting\n");
  DBG("==============================================\n");
  DBG("[boot] reset reason: %s\n", resetReasonName(esp_reset_reason()));
  DBG("[boot] chip: %s rev%d, %d cores @ %d MHz\n",
      ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(), ESP.getCpuFreqMHz());
  DBG("[boot] flash: %u KB, free heap: %u KB\n",
      ESP.getFlashChipSize() / 1024, ESP.getFreeHeap() / 1024);
  DBG("[boot] pins: POT=%d BUZZER=%d MOTOR=%d WAKE_BTN=%d LED=%d\n",
      PIN_POT_ADC, PIN_BUZZER, PIN_MOTOR, PIN_WAKE_BUTTON, PIN_STATUS_LED);
  DBG("[boot] flags: WIFI=%d WIFI_MGR=%d SERVER=%d ESPNOW=%d MOTOR=%d BUZZER=%d SLEEP=%d\n",
      ENABLE_WIFI, ENABLE_WIFI_MANAGER, ENABLE_SERVER_POST, ENABLE_ESPNOW,
      ENABLE_MOTOR, ENABLE_BUZZER, ENABLE_SLEEP_MODE);
  DBG("[boot] sizeof(SpiroPacket)=%u bytes (must match Display!)\n",
      (unsigned)sizeof(SpiroPacket));

#if ENABLE_SLEEP_MODE
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  switch (cause) {
    case ESP_SLEEP_WAKEUP_EXT0:
      DBG("[boot] wake reason: EXT0 — wake button pressed\n"); break;
    case ESP_SLEEP_WAKEUP_TIMER:
      DBG("[boot] wake reason: timer\n"); break;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
    default:
      DBG("[boot] wake reason: normal power-on / reset\n"); break;
  }
#endif
}

// ----------------------------------------------------------------------------
// Activity tracking (drives pseudo/deep sleep)
// ----------------------------------------------------------------------------
static void markActivity(uint32_t nowMs) {
  g_lastActivityMs = nowMs;
}

#if ENABLE_ESPNOW
static void buildAndSendPacket(uint32_t nowMs, bool deepSleepPending = false);
#endif

// ----------------------------------------------------------------------------
// Deep sleep entry — everything off, wake on the rocker switch (EXT0, LOW)
// ----------------------------------------------------------------------------
#if ENABLE_SLEEP_MODE && ENABLE_CONTROLLER_DEEP_SLEEP
static void goToDeepSleep() {
  DBG("[sleep] entering DEEP SLEEP — press wake button (GPIO%d) to wake\n",
      PIN_WAKE_BUTTON);

  haptics.allOff();
  digitalWrite(PIN_MOTOR, LOW);              // belt and suspenders
#if ENABLE_STATUS_LED
  neopixelWrite(PIN_STATUS_LED, 0, 0, 0);
#endif

#if ENABLE_ESPNOW
  // Final packet so the Display can show its sleep screen (deepSleepPending).
  buildAndSendPacket(millis(), /*deepSleepPending=*/true);
  delay(20);   // single tiny wait so the radio flushes the last packet —
               // acceptable: we are about to power down anyway
#endif

  // High score is already persisted by Storage on every change (NVS).
  Serial.flush();

  // Hold the wake pin high through deep sleep so the LOW press wakes us.
  rtc_gpio_pullup_en((gpio_num_t)PIN_WAKE_BUTTON);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_WAKE_BUTTON);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_WAKE_BUTTON, 0 /* wake on LOW */);

  esp_deep_sleep_start();
}
#endif

// ----------------------------------------------------------------------------
// ESP-NOW live packet (Faza 4 — compiled only with ENABLE_ESPNOW)
// ----------------------------------------------------------------------------
#if ENABLE_ESPNOW
static void buildAndSendPacket(uint32_t nowMs, bool deepSleepPending) {
  SpiroPacket p = {};
  p.seq             = g_packetSeq++;
  p.timestampMs     = nowMs;
  p.rawAdc          = sensor.rawAdc();
  p.deviationAdc    = sensor.deviationAdc();
  p.flowMlS         = sensor.flowMlS();
  p.filteredFlowMlS = sensor.filteredFlowMlS();
  p.volumeMl        = logic.volumeMl();
  p.maxFlowMlS      = logic.maxFlowMlS();
  p.avgFlowMlS      = logic.avgFlowMlS();
  p.stableTimeMs    = logic.stableTimeMs();
#if ENABLE_WIFI
  // During the boot decision phase stableTimeMs is unused by the game, so it
  // carries the live button-hold time -> the Display draws a hold progress bar.
  if (wifiProv.status() == WIFI_ST_DECISION) {
    p.stableTimeMs = (uint16_t)min(wifiProv.decisionHeldMs(), (uint32_t)65535);
  }
#endif
  p.state           = (uint8_t)logic.state();
  p.failReason      = (uint8_t)logic.failReason();
  p.targetZone      = sensor.inTargetZone();
  p.dangerZone      = sensor.inDangerZone();
  // success/fail describe the ATTEMPT OUTCOME, not the momentary state —
  // they must stay correct through STATE_RESULT (the Display result screen
  // renders from them). Source of truth: the stored AttemptResult, same as
  // the server POST. (HW-test bug: state()==STATE_SUCCESS turned false in
  // STATE_RESULT, so the result screen always said FAIL.)
  {
    const ExerciseState s = logic.state();
    const bool outcomePhase =
        (s == STATE_SUCCESS || s == STATE_FAIL || s == STATE_RESULT);
    const AttemptResult &r = logic.result();
    p.success = outcomePhase && r.valid && r.success;
    p.fail    = outcomePhase && r.valid && !r.success;
  }
  p.deepSleepPending = deepSleepPending;
#if ENABLE_WIFI
  p.wifiStatus      = (uint8_t)wifiProv.status();
#else
  p.wifiStatus      = WIFI_ST_DISABLED;
#endif
#if ENABLE_SERVER_POST
  p.serverStatus    = (uint8_t)serverClient.status();
#else
  p.serverStatus    = SERVER_ST_DISABLED;
#endif
  p.espNowChannel   = espNow.channel();

  // EspNowSender stamps espNowChannel and finalizes (magic/version/checksum).
  espNow.send(p);
}

// While the setup portal is open, keep telling the Display about it so it can
// show "Connect to SpiroBird-Setup / Open 192.168.4.1 / press button to skip".
#if ENABLE_WIFI
static void portalTick() {
  static uint32_t lastMs = 0;
  uint32_t now = millis();
  if (now - lastMs >= PORTAL_STATUS_SEND_INTERVAL_MS) {
    lastMs = now;
    buildAndSendPacket(now, false);
  }
}
#endif
#endif

// ----------------------------------------------------------------------------
// One-shot logic events -> side effects (haptics, storage, network)
// ----------------------------------------------------------------------------
static void handleLogicEvents(uint32_t nowMs) {
  LogicEvent e;
  while ((e = logic.nextEvent()) != LogicEvent::None) {
    switch (e) {
      case LogicEvent::CalibrationStarted:
        DBG("[main] calibrating — keep the potentiometer at rest\n");
        break;

      case LogicEvent::ReadyToStart:
        haptics.startBeep();
        break;

      case LogicEvent::AttemptStarted:
        markActivity(nowMs);
        break;

      case LogicEvent::EnteredZone:
        haptics.zoneBeep();
        break;

      case LogicEvent::LeftZone:
        haptics.motorWarn();    // small nudge: you slipped out of the zone
        break;

      case LogicEvent::DangerWarning:
        haptics.motorWarn();    // approaching 1200 ml/s
        break;

      case LogicEvent::Success:
        haptics.successMelody();            // no-op while buzzer is disabled
        haptics.motorCollision();           // 150 ms tactile "win" pulse
        storage.recordAttempt(logic.result());
        g_pendingResultSideEffects = true;   // POST after the melody starts
        markActivity(nowMs);
        break;

      case LogicEvent::Fail:
        haptics.errorBeep();
        haptics.motorFail();
        storage.recordAttempt(logic.result());
        g_pendingResultSideEffects = true;
        markActivity(nowMs);
        break;

      case LogicEvent::ResultDone:
        storage.printStats();
        break;

      default:
        break;
    }
  }

  // Server POST happens OUTSIDE the ACTIVE sampling phase, in SUCCESS/FAIL/
  // RESULT, and never blocks longer than HTTP_TIMEOUT_MS.
  if (g_pendingResultSideEffects) {
    g_pendingResultSideEffects = false;
#if ENABLE_SERVER_POST
    serverClient.queueResult(logic.result());
#endif
  }
}

// ----------------------------------------------------------------------------
// Sleep policy (pseudo sleep -> deep sleep), guarded by ENABLE_SLEEP_MODE
// ----------------------------------------------------------------------------
#if ENABLE_SLEEP_MODE
static void updateSleepPolicy(uint32_t nowMs) {
  const bool sleeping = logic.state() == STATE_SLEEP;

  // Inactivity is INPUT-based ONLY (knob movement, button press, attempt
  // start/success/fail events). No state keeps the device awake by itself —
  // HW-test decision: 60 s without touching anything -> pseudo sleep from ANY
  // state (even READY/CALIBRATING), 3 min total -> deep sleep.

  if (!sleeping) {
#if ENABLE_CONTROLLER_PSEUDO_SLEEP
    if (nowMs - g_lastActivityMs >= PSEUDO_SLEEP_TIMEOUT_MS) {
      DBG("[sleep] %lu s idle -> pseudo sleep (radio stays on)\n",
          (unsigned long)(PSEUDO_SLEEP_TIMEOUT_MS / 1000));
      haptics.allOff();
      logic.enterSleep(nowMs);
      g_sleepRefAdc = sensor.rawAdc();
    }
#endif
  } else {
    // Wake instantly on potentiometer movement (button wake is handled in the
    // button section of loop()).
    int delta = abs((int)sensor.rawAdc() - (int)g_sleepRefAdc);
    if (delta > POT_MOVEMENT_WAKE_ADC) {
      DBG("[sleep] potentiometer moved (delta=%d) -> waking up\n", delta);
      logic.wakeUp(nowMs);
      markActivity(nowMs);
    }

#if ENABLE_CONTROLLER_DEEP_SLEEP
    if (nowMs - g_lastActivityMs >= DEEP_SLEEP_TIMEOUT_MS) {
      goToDeepSleep();   // never returns
    }
#endif
  }
}
#endif

// ============================================================================
// setup()
// ============================================================================
void setup() {
  // ------- SAFETY: motor OFF before anything else can go wrong -------------
  pinMode(PIN_MOTOR, OUTPUT);
  digitalWrite(PIN_MOTOR, LOW);

  Serial.begin(115200);
#if ENABLE_DEBUG_SERIAL && DEBUG_MIRROR_UART0
  Serial0.begin(115200);   // CH340 "COM" port mirror (see config.h)
#endif
#if ENABLE_DEBUG_SERIAL
  // Native USB CDC: give the host a moment to enumerate, but never hang if
  // no monitor is attached (the cap keeps this boot-time-only wait short).
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) { /* boot-only wait, max 1.5 s */ }
#endif

  printBootInfo();

  // Wake/start button: pull-up, pressed = LOW, ISR only sets a flag.
  pinMode(PIN_WAKE_BUTTON, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_WAKE_BUTTON), onButtonIsr, FALLING);

  storage.begin();
  sensor.begin();
  haptics.begin();
  haptics.bootChirp();   // audible hardware self-test (no-op, buzzer disabled)
#if ENABLE_MOTOR && MOTOR_BOOT_SELFTEST
  // Single short pulse so the transistor circuit is verified at every boot.
  // SYNCHRONOUS on purpose: setup() blocks on Wi-Fi right after this (up to
  // WIFI_PORTAL_TIMEOUT_SEC!), so the non-blocking watchdog in loop() cannot
  // be trusted yet. A 120 ms delay() is acceptable in the boot phase.
  // (HW-test bug: the async pulse stayed ON through the whole Wi-Fi portal.)
  DBG("[haptics] motor boot self-test pulse (%d ms, synchronous)\n",
      MOTOR_PULSE_WARN_MS);
  digitalWrite(PIN_MOTOR, HIGH);
  delay(MOTOR_PULSE_WARN_MS);
  digitalWrite(PIN_MOTOR, LOW);
#endif
  logic.begin(&sensor);

  // ESP-NOW first, so the Display can already be told about the setup portal.
#if ENABLE_ESPNOW
  espNow.begin();
#endif

#if ENABLE_WIFI
  // Blocking is allowed ONLY here, during boot/setup — never during the game.
#if ENABLE_ESPNOW
  wifiProv.setPortalTick(portalTick);
#endif
  wifiProv.begin();
  g_buttonIsrFlag = false;   // a portal-skip press must not also start the game
#endif

#if ENABLE_ESPNOW
  espNow.syncChannel();      // Wi-Fi may have moved us to its channel
#endif

#if ENABLE_SERVER_POST
  serverClient.begin();
#endif

#if ENABLE_SAMPLE_TIMER_ISR
  // Optional course demo: hardware timer fires at 100 Hz, ISR sets a flag.
  #if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
    g_sampleTimer = timerBegin(1000000);                       // 1 MHz base
    timerAttachInterrupt(g_sampleTimer, &onSampleTimerIsr);
    timerAlarm(g_sampleTimer, SENSOR_SAMPLE_INTERVAL_MS * 1000ULL, true, 0);
  #else
    g_sampleTimer = timerBegin(0, 80, true);                   // 80 MHz / 80 = 1 MHz
    timerAttachInterrupt(g_sampleTimer, &onSampleTimerIsr, true);
    timerAlarmWrite(g_sampleTimer, SENSOR_SAMPLE_INTERVAL_MS * 1000ULL, true);
    timerAlarmEnable(g_sampleTimer);
  #endif
  DBG("[boot] sample timer ISR enabled (%d ms period)\n", SENSOR_SAMPLE_INTERVAL_MS);
#endif

  uint32_t now = millis();
  markActivity(now);
  setStatusLed(logic.state());

  // Auto-start calibration on boot so the demo needs no extra button press.
  logic.requestStart(now);

  DBG("[boot] setup complete, entering main loop\n");
}

// ============================================================================
// loop() — pure millis() scheduling, zero blocking
// ============================================================================
void loop() {
  const uint32_t now = millis();
  static ExerciseState lastLedState = STATE_SLEEP;  // force first LED update

  // ---- 1) Button (ISR flag + debounce + level check) -----------------------
  if (g_buttonIsrFlag) {
    g_buttonIsrFlag = false;
    if (now - g_lastButtonMs >= BUTTON_DEBOUNCE_MS &&
        digitalRead(PIN_WAKE_BUTTON) == LOW) {
      g_lastButtonMs = now;
      DBG("[main] button pressed\n");
      markActivity(now);
      if (logic.state() == STATE_SLEEP) {
        logic.wakeUp(now);
      } else {
        logic.onButtonPressed(now);
      }
    }
  }

  // ---- 2) Sensor + logic tick @ 100 Hz -------------------------------------
#if ENABLE_SAMPLE_TIMER_ISR
  bool sampleDue = g_sampleFlag;
  if (sampleDue) g_sampleFlag = false;
#else
  bool sampleDue = (now - g_lastSampleMs >= SENSOR_SAMPLE_INTERVAL_MS);
  if (sampleDue) g_lastSampleMs = now;
#endif

  if (sampleDue) {
    sensor.update(now);
    logic.update(now);
    handleLogicEvents(now);

    // Activity = the knob MOVED, not "flow is nonzero": a knob parked away
    // from center would otherwise keep the device awake forever and the
    // sleep timers could never fire.
    static uint16_t lastActivityRaw = 0;
    if (abs((int)sensor.rawAdc() - (int)lastActivityRaw) > POT_MOVEMENT_WAKE_ADC) {
      lastActivityRaw = sensor.rawAdc();
      markActivity(now);
    }
  }

  // ---- 3) Haptics (non-blocking melodies + motor pulse watchdog) -----------
  haptics.update(now);

  // ---- 4) ESP-NOW live packets @ 40 Hz (Faza 4) -----------------------------
#if ENABLE_ESPNOW
  if (now - g_lastEspNowMs >= ESPNOW_SEND_INTERVAL_MS) {
    g_lastEspNowMs = now;
    buildAndSendPacket(now);
  }
#endif

  // ---- 5) Network upkeep (reconnects, queued POSTs) -------------------------
  // Both are gated so they can never disturb ACTIVE sampling.
  const bool networkAllowed = logic.state() != STATE_ACTIVE;
#if ENABLE_WIFI
  wifiProv.update(now, networkAllowed);
#endif
#if ENABLE_SERVER_POST
  serverClient.update(now, networkAllowed);
#endif
#if !ENABLE_WIFI && !ENABLE_SERVER_POST
  (void)networkAllowed;
#endif

  // Defensive watchdog tick: the server POST above may block up to
  // HTTP_TIMEOUT_MS — if a motor pulse was running, end it on time using a
  // FRESH timestamp (`now` is stale after a blocking call).
  haptics.update(millis());

  // ---- 6) Sleep policy ------------------------------------------------------
#if ENABLE_SLEEP_MODE
  updateSleepPolicy(now);
#endif

  // ---- 7) Status LED on state change ----------------------------------------
  if (logic.state() != lastLedState) {
    lastLedState = logic.state();
    setStatusLed(lastLedState);
  }

  // ---- 8) Periodic debug line ------------------------------------------------
  if (now - g_lastDebugMs >= DEBUG_PRINT_INTERVAL_MS) {
    g_lastDebugMs = now;
    DBG("[dbg] %s raw=%4u off=%4u dev=%5d flow=%6.1f filt=%6.1f p2p=%5.1f vol=%7.1f stable=%4u/%u\n",
        ExerciseLogic::stateName(logic.state()),
        sensor.rawAdc(), sensor.offsetAdc(), sensor.deviationAdc(),
        sensor.flowMlS(), sensor.filteredFlowMlS(), sensor.peakToPeak(),
        logic.volumeMl(), logic.stableTimeMs(), STABLE_SUCCESS_MS);
  }
}
