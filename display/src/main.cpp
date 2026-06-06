// ============================================================================
// SpiroBird — Display / Slave ESP32-S3 firmware (LCDWiki ES3C28P)
//
// Receives live SpiroPackets from the Controller over ESP-NOW (channel
// scan/lock, no Wi-Fi connection, no credentials) and renders the game at
// ~30 FPS with sprite double buffering.
//
// This device NEVER decides the exercise outcome — the Controller is the
// single source of truth. It also never deep-sleeps (ESP-NOW cannot wake a
// sleeping ESP32); a "Controller sleeping" pseudo-sleep screen is shown
// instead while the radio keeps listening.
//
// Bring-up step 5 (display standalone): with ENABLE_DISPLAY_FAKE_DATA_MODE 1
// a fake exercise demo starts when no Controller is heard for 8 s, so the
// panel/driver/orientation can be verified without any other hardware.
// ============================================================================
#include <Arduino.h>
#include "config.h"
#include "protocol.h"
#include "EspNowReceiver.h"
#include "GameRenderer.h"
#include "UiScreens.h"

static EspNowReceiver receiver;
static GameRenderer   renderer;
static UiScreens      ui;

static uint32_t g_lastFrameMs = 0;
static uint32_t g_lastDebugMs = 0;
static uint8_t  g_lastState   = 255;   // to reset bird physics on ACTIVE entry

// Pseudo-sleep power state (the ESP-NOW receiver NEVER sleeps):
//   ON -> (1 min idle) -> DIM -> (+15 s) -> OFF (backlight 0, touch-sensitive)
enum DisplayPower : uint8_t { DISP_ON = 0, DISP_DIM = 1, DISP_OFF = 2 };
static DisplayPower g_power = DISP_ON;
static uint32_t g_lastUiActivityMs = 0;

// ----------------------------------------------------------------------------
// Fake-data demo (no Controller needed) — cycles through a whole exercise
// ----------------------------------------------------------------------------
#if ENABLE_DISPLAY_FAKE_DATA_MODE
static bool g_fakeActive = false;

static SpiroPacket makeFakePacket(uint32_t nowMs) {
  static uint32_t cycleStart = 0;
  static float volume = 0, maxFlow = 0, flowSum = 0;
  static uint32_t flowN = 0, stableStart = 0;

  if (cycleStart == 0) cycleStart = nowMs;
  uint32_t t = nowMs - cycleStart;

  SpiroPacket p = {};
  p.seq = nowMs / FRAME_INTERVAL_MS;
  p.timestampMs = nowMs;
  p.wifiStatus = WIFI_ST_OFFLINE;
  p.serverStatus = SERVER_ST_DISABLED;
  p.espNowChannel = 0;

  // Phase plan: 2 s calibrating -> 2 s ready -> active -> success -> result
  if (t < 2000) {
    p.state = STATE_CALIBRATING;
    volume = maxFlow = flowSum = 0; flowN = 0; stableStart = 0;
  } else if (t < 4000) {
    p.state = STATE_READY;
  } else if (t < 24000) {
    p.state = STATE_ACTIVE;
    // Wobbly flow that settles into the target zone.
    float wobble = (t < 9000) ? 350.0f : 90.0f;
    float flow = 1050.0f + wobble * sinf(t * 0.004f) + 25.0f * sinf(t * 0.027f);
    if (flow < 0) flow = 0;
    p.flowMlS = p.filteredFlowMlS = flow;
    volume += flow * (FRAME_INTERVAL_MS / 1000.0f);
    flowSum += flow; flowN++;
    if (flow > maxFlow) maxFlow = flow;
    p.targetZone = flow >= FLOW_TARGET_MIN_ML_S && flow <= FLOW_TARGET_MAX_ML_S;
    p.dangerZone = flow > FLOW_TARGET_MAX_ML_S;
    if (p.targetZone) {
      if (stableStart == 0) stableStart = t;
      uint32_t st = t - stableStart;
      p.stableTimeMs = (uint16_t)min(st, (uint32_t)STABLE_SUCCESS_MS);
      if (st >= STABLE_SUCCESS_MS) {
        // jump the cycle forward to the success phase
        cycleStart = nowMs - 24000;
      }
    } else {
      stableStart = 0;
    }
  } else if (t < 26000) {
    p.state = STATE_SUCCESS;
    p.success = true;
    p.stableTimeMs = STABLE_SUCCESS_MS;
  } else if (t < 31000) {
    p.state = STATE_RESULT;
    p.success = true;
    p.stableTimeMs = STABLE_SUCCESS_MS;
  } else {
    cycleStart = nowMs;   // restart the demo loop
    p.state = STATE_IDLE;
  }

  p.volumeMl = volume;
  p.maxFlowMlS = maxFlow;
  p.avgFlowMlS = flowN ? flowSum / flowN : 0;
  return p;
}
#endif

// ----------------------------------------------------------------------------
// Frame rendering — picks the right screen for the current situation
// ----------------------------------------------------------------------------
static void renderFrame(uint32_t nowMs) {
  SpiroPacket p = {};
  bool live = false;
  bool fake = false;

  if (receiver.everReceived()) {
    p = receiver.lastPacket();
    live = receiver.isFresh(nowMs);
  }

#if ENABLE_DISPLAY_FAKE_DATA_MODE
  // Fake demo only if NO Controller was ever heard (first real packet wins).
  if (!receiver.everReceived() && nowMs >= FAKE_DATA_START_MS) {
    p = makeFakePacket(nowMs);
    live = fake = true;
    if (!g_fakeActive) {
      g_fakeActive = true;
      DBG("[ui] no Controller heard for %d ms -> FAKE DATA demo\n", FAKE_DATA_START_MS);
    }
  }
#endif

  if (!live) {
    // No fresh data. Stale data is NEVER rendered as a live game.
    if (receiver.everReceived() && p.state == STATE_SLEEP) {
      ui.drawSleep(p);            // controller sleeps: keep the sleep screen
    } else if (receiver.everReceived()) {
      ui.drawNoSignal(receiver, nowMs);
    } else {
      ui.drawScanning(receiver, nowMs);
    }
    renderer.present();
    return;
  }

  // Reset bird physics whenever a new attempt starts.
  if (p.state == STATE_ACTIVE && g_lastState != STATE_ACTIVE) renderer.resetGame();
  g_lastState = p.state;

  if (p.wifiStatus == WIFI_ST_SETUP_PORTAL) {
    ui.drawWifiSetup(p);          // portal open on the Controller
  } else if (p.wifiStatus == WIFI_ST_DECISION) {
    ui.drawWifiDecision(p);       // offline vs portal choice on the button
  } else {
    switch ((ExerciseState)p.state) {
      case STATE_IDLE:        ui.drawIdle(p);          break;
      case STATE_CALIBRATING: ui.drawCalibrating(p);   break;
      case STATE_READY:       ui.drawReady(p);         break;
      case STATE_ACTIVE:      renderer.drawGame(p, nowMs); break;
      case STATE_SUCCESS:     ui.drawSuccess(p);       break;
      case STATE_FAIL:        ui.drawFail(p);          break;
      case STATE_RESULT:      ui.drawResult(p);        break;
      case STATE_SLEEP:       ui.drawSleep(p);         break;
      default:                ui.drawIdle(p);          break;
    }
  }

  if (p.state != STATE_ACTIVE) ui.drawStatusBar(p, receiver);
  if (fake) ui.drawFakeBanner();

  renderer.present();
}

// ============================================================================
void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) { /* boot-only USB CDC wait */ }

  DBG("\n==============================================\n");
  DBG(" SpiroBird Display (SLAVE) booting\n");
  DBG("==============================================\n");
  DBG("[boot] sizeof(SpiroPacket)=%u bytes (must match Controller!)\n",
      (unsigned)sizeof(SpiroPacket));
  DBG("[boot] flags: CHANNEL_SCAN=%d FAKE_DATA=%d DEEP_SLEEP=%d (must be 0)\n",
      ENABLE_ESPNOW_CHANNEL_SCAN, ENABLE_DISPLAY_FAKE_DATA_MODE,
      ENABLE_DISPLAY_DEEP_SLEEP);

  renderer.begin();
  ui.begin(&renderer);

  // Splash while ESP-NOW comes up.
  renderer.clear(0x0926);
  renderer.present();

  receiver.begin();
  DBG("[boot] setup complete\n");
}

// Pseudo-sleep policy. Activity = an engaged Controller (anything except
// IDLE/SLEEP), the fake demo, or a touch. The radio keeps listening in every
// power state — only the backlight changes.
static void updateDisplayPower(uint32_t nowMs) {
#if ENABLE_DISPLAY_PSEUDO_SLEEP
  bool activity = false;

#if ENABLE_DISPLAY_TOUCH_WAKE
  static uint32_t lastTouchPollMs = 0;
  if (nowMs - lastTouchPollMs >= TOUCH_POLL_INTERVAL_MS) {
    lastTouchPollMs = nowMs;
    if (renderer.touched()) activity = true;
  }
#endif

  if (receiver.isFresh(nowMs)) {
    SpiroPacket p = receiver.lastPacket();
    const bool engaged =
        (p.state >= STATE_CALIBRATING && p.state <= STATE_RESULT) ||
        p.wifiStatus == WIFI_ST_SETUP_PORTAL ||
        p.wifiStatus == WIFI_ST_DECISION;
    if (engaged) activity = true;
  }
#if ENABLE_DISPLAY_FAKE_DATA_MODE
  if (g_fakeActive) activity = true;   // the standalone demo never sleeps
#endif

  if (activity) {
    g_lastUiActivityMs = nowMs;
    if (g_power != DISP_ON) {
      g_power = DISP_ON;
      renderer.setBrightness(DISPLAY_BRIGHTNESS_FULL);
      DBG("[power] wake -> full brightness\n");
    }
    return;
  }

  const uint32_t idle = nowMs - g_lastUiActivityMs;
  if (g_power == DISP_ON && idle >= DISPLAY_DIM_AFTER_MS) {
    g_power = DISP_DIM;
    renderer.setBrightness(DISPLAY_BRIGHTNESS_DIM);
    DBG("[power] %lu s idle -> DIM\n", (unsigned long)(DISPLAY_DIM_AFTER_MS / 1000));
  } else if (g_power == DISP_DIM &&
             idle >= DISPLAY_DIM_AFTER_MS + DISPLAY_OFF_AFTER_DIM_MS) {
    g_power = DISP_OFF;
    renderer.setBrightness(0);
    DBG("[power] backlight OFF (touch to wake, ESP-NOW still listening)\n");
  }
#else
  (void)nowMs;
#endif
}

void loop() {
  const uint32_t now = millis();

  receiver.update(now);
  updateDisplayPower(now);

  // With the backlight off there is nothing to see — skip rendering and let
  // the loop spin faster on the receiver + touch polling.
  if (g_power != DISP_OFF && now - g_lastFrameMs >= FRAME_INTERVAL_MS) {
    g_lastFrameMs = now;
    renderFrame(now);
  }

  if (now - g_lastDebugMs >= DEBUG_PRINT_INTERVAL_MS) {
    g_lastDebugMs = now;
    SpiroPacket p = receiver.lastPacket();
    DBG("[dbg] %s ch=%u rx=%lu (%lu/s) bad=%lu fresh=%d state=%u flow=%.0f\n",
        receiver.scanState() == EspNowReceiver::LOCKED ? "LOCKED" : "SCANNING",
        receiver.currentChannel(),
        (unsigned long)receiver.validCount(),
        (unsigned long)receiver.rxPerSecond(),
        (unsigned long)receiver.invalidCount(),
        receiver.isFresh(now), p.state, p.filteredFlowMlS);
  }
}
