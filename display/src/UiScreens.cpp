#include "UiScreens.h"

static const uint16_t COL_BG      = 0x0926;   // dark blue
static const uint16_t COL_BAR_BG  = 0x10A2;

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------

void UiScreens::centerText(const char *txt, int y, uint8_t font, uint16_t color) {
  lgfx::LovyanGFX *g = _r->gfx();
  g->setTextFont(font);
  g->setTextDatum(TC_DATUM);
  g->setTextColor(color, COL_BG);
  g->drawString(txt, GameRenderer::SCREEN_W / 2, y);
}

void UiScreens::titleScreen(uint16_t bg, const char *title, uint16_t titleColor) {
  _r->clear(bg);
  lgfx::LovyanGFX *g = _r->gfx();
  g->setTextFont(4);
  g->setTextDatum(TC_DATUM);
  g->setTextColor(titleColor, bg);
  g->drawString(title, GameRenderer::SCREEN_W / 2, 18);
}

const char *UiScreens::wifiStatusText(uint8_t s) {
  switch (s) {
    case WIFI_ST_DISABLED:     return "WiFi: off";
    case WIFI_ST_CONNECTING:   return "WiFi: connecting...";
    case WIFI_ST_SETUP_PORTAL: return "WiFi: setup mode";
    case WIFI_ST_CONNECTED:    return "WiFi: connected";
    case WIFI_ST_OFFLINE:      return "PLAYING OFFLINE / LOCAL MODE";
    case WIFI_ST_DECISION:     return "WiFi: choose mode (button)";
  }
  return "WiFi: ?";
}

const char *UiScreens::failReasonText(uint8_t r) {
  switch (r) {
    case FAIL_OVER_1200: return "Flow over 1200 ml/s!";
    case FAIL_UNSTABLE:  return "Flow was not stable";
    case FAIL_COLLISION: return "Bird crashed";
    case FAIL_TIMEOUT:   return "Time ran out";
  }
  return "";
}

// ----------------------------------------------------------------------------
// Connection screens
// ----------------------------------------------------------------------------

void UiScreens::drawScanning(const EspNowReceiver &rx, uint32_t nowMs) {
  titleScreen(COL_BG, "SpiroBird", TFT_YELLOW);
  char buf[40];
  centerText("NO SIGNAL", 78, 4, TFT_RED);
  snprintf(buf, sizeof(buf), "SCANNING CH %u", rx.currentChannel());
  centerText(buf, 112, 4, TFT_WHITE);
  centerText("Waiting for Controller...", 150, 2, TFT_SILVER);
  // Animated dots so it is obvious the Display is alive.
  int dots = (nowMs / 400) % 4;
  char d[8] = "";
  for (int i = 0; i < dots; i++) d[i] = '.';
  centerText(d, 172, 2, TFT_SILVER);
}

void UiScreens::drawNoSignal(const EspNowReceiver &rx, uint32_t nowMs) {
  (void)nowMs;
  titleScreen(COL_BG, "SpiroBird", TFT_YELLOW);
  centerText("NO ESP-NOW SIGNAL", 84, 4, TFT_RED);
  char buf[48];
  snprintf(buf, sizeof(buf), "Last data %lu s ago (CH %u)",
           (unsigned long)((millis() - rx.lastPacketMs()) / 1000), rx.currentChannel());
  centerText(buf, 122, 2, TFT_WHITE);
  centerText("Waiting for Controller", 146, 2, TFT_SILVER);
}

// ----------------------------------------------------------------------------
// Controller-state screens
// ----------------------------------------------------------------------------

void UiScreens::drawIdle(const SpiroPacket &p) {
  titleScreen(COL_BG, "SpiroBird", TFT_YELLOW);
  centerText("Breath trainer game", 50, 2, TFT_SILVER);
  centerText("Press the button or blow", 100, 4, TFT_WHITE);
  centerText("to start the exercise", 128, 4, TFT_WHITE);
  centerText("Goal: hold 900-1200 ml/s for 5 s", 168, 2, TFT_GREEN);
}

void UiScreens::drawCalibrating(const SpiroPacket &p) {
  titleScreen(COL_BG, "Calibration", TFT_CYAN);
  centerText("CENTER the knob", 84, 4, TFT_WHITE);
  centerText("and hold it for 2 seconds...", 118, 2, TFT_SILVER);
  // Live coaching: which way to turn (rawAdc comes in every packet).
  const int low = 1700, high = 2300;   // POT_CENTER_ADC +/- TOLERANCE
  if (p.rawAdc < low)       centerText(">>> turn UP <<<",   150, 2, TFT_ORANGE);
  else if (p.rawAdc > high) centerText(">>> turn DOWN <<<", 150, 2, TFT_ORANGE);
  else                      centerText("hold it right there!", 150, 2, TFT_GREEN);
}

void UiScreens::drawReady(const SpiroPacket &p) {
  (void)p;
  titleScreen(0x0280, "GET READY", TFT_WHITE);
  centerText("Start blowing!", 100, 4, TFT_YELLOW);
  centerText("Keep the flow between 900 and 1200", 150, 2, TFT_WHITE);
}

void UiScreens::drawSuccess(const SpiroPacket &p) {
  titleScreen(0x0280, "SUCCESS!", TFT_GREEN);
  char buf[40];
  snprintf(buf, sizeof(buf), "Volume: %.2f L", p.volumeMl / 1000.0f);
  centerText(buf, 90, 4, TFT_WHITE);
  snprintf(buf, sizeof(buf), "Stable: %.1f s", p.stableTimeMs / 1000.0f);
  centerText(buf, 122, 4, TFT_WHITE);
}

void UiScreens::drawFail(const SpiroPacket &p) {
  titleScreen(0x4000, "FAILED", TFT_RED);
  centerText(failReasonText(p.failReason), 95, 4, TFT_WHITE);
  centerText("Try again!", 140, 2, TFT_SILVER);
}

void UiScreens::drawResult(const SpiroPacket &p) {
  titleScreen(COL_BG, p.success ? "RESULT - SUCCESS" : "RESULT - FAIL",
              p.success ? TFT_GREEN : TFT_RED);
  char buf[48];
  lgfx::LovyanGFX *g = _r->gfx();
  g->setTextFont(2);
  g->setTextDatum(TL_DATUM);
  g->setTextColor(TFT_WHITE, COL_BG);
  int y = 64;
  snprintf(buf, sizeof(buf), "Volume:      %.2f L", p.volumeMl / 1000.0f);
  g->drawString(buf, 70, y); y += 22;
  snprintf(buf, sizeof(buf), "Max flow:    %.0f ml/s", p.maxFlowMlS);
  g->drawString(buf, 70, y); y += 22;
  snprintf(buf, sizeof(buf), "Avg flow:    %.0f ml/s", p.avgFlowMlS);
  g->drawString(buf, 70, y); y += 22;
  snprintf(buf, sizeof(buf), "Stable time: %.1f s", p.stableTimeMs / 1000.0f);
  g->drawString(buf, 70, y); y += 22;
  if (!p.success) {
    g->setTextColor(TFT_RED, COL_BG);
    g->drawString(failReasonText(p.failReason), 70, y);
  }
}

void UiScreens::drawSleep(const SpiroPacket &p) {
  titleScreen(TFT_BLACK, "", TFT_BLACK);
  centerText("Controller sleeping", 90, 4, 0x4208);
  centerText("Press wake switch", 124, 4, 0x4208);
  if (p.deepSleepPending) centerText("(deep sleep)", 156, 2, 0x2104);
}

// Saved Wi-Fi unreachable: the Controller waits for the user's choice.
// p.stableTimeMs carries the live button-hold time (0..1000 ms) here.
void UiScreens::drawWifiDecision(const SpiroPacket &p) {
  titleScreen(COL_BG, "Wi-Fi unavailable", TFT_ORANGE);
  centerText("Saved network is not reachable.", 52, 2, TFT_SILVER);
  centerText("SHORT press = play OFFLINE", 84, 2, TFT_WHITE);
  centerText("LONG press (1 s) = Wi-Fi setup portal", 106, 2, TFT_WHITE);
  centerText("(no input 60 s = offline)", 128, 2, TFT_SILVER);

  // Hold progress bar: fills while the button is held toward the long press.
  const int barW = 200, barH = 14;
  const int barX = (GameRenderer::SCREEN_W - barW) / 2, barY = 160;
  lgfx::LovyanGFX *g = _r->gfx();
  g->drawRect(barX, barY, barW, barH, TFT_WHITE);
  float t = (float)p.stableTimeMs / 1000.0f;   // WIFI_DECISION_LONGPRESS_MS
  if (t > 1.0f) t = 1.0f;
  if (t > 0.0f) {
    g->fillRect(barX + 2, barY + 2, (int)((barW - 4) * t), barH - 4, TFT_CYAN);
    centerText("keep holding for portal...", 182, 2, TFT_CYAN);
  } else {
    centerText("waiting for your choice", 182, 2, TFT_SILVER);
  }
}

void UiScreens::drawWifiSetup(const SpiroPacket &p) {
  (void)p;
  titleScreen(COL_BG, "Wi-Fi Setup", TFT_CYAN);
  centerText("Connect your phone to:", 64, 2, TFT_SILVER);
  centerText("SpiroBird-Setup", 86, 4, TFT_YELLOW);
  centerText("then open:", 120, 2, TFT_SILVER);
  centerText("http://192.168.4.1", 140, 4, TFT_WHITE);
  centerText("Or press the button on the", 178, 2, TFT_SILVER);
  centerText("Controller to skip (play offline)", 196, 2, TFT_SILVER);
}

// ----------------------------------------------------------------------------
// Overlays
// ----------------------------------------------------------------------------

void UiScreens::drawFakeBanner() {
  lgfx::LovyanGFX *g = _r->gfx();
  g->fillRect(0, 0, GameRenderer::SCREEN_W, 14, TFT_MAROON);
  g->setTextFont(1);
  g->setTextDatum(TC_DATUM);
  g->setTextColor(TFT_WHITE, TFT_MAROON);
  g->drawString("FAKE DATA DEMO - no Controller connected", GameRenderer::SCREEN_W / 2, 3);
}

// Bottom status bar with live connection/Wi-Fi/server info.
void UiScreens::drawStatusBar(const SpiroPacket &p, const EspNowReceiver &rx) {
  lgfx::LovyanGFX *g = _r->gfx();
  const int y = GameRenderer::SCREEN_H - 24;
  g->fillRect(0, y, GameRenderer::SCREEN_W, 24, COL_BAR_BG);

  g->setTextFont(2);
  g->setTextDatum(TL_DATUM);

  // ESP-NOW link info (left)
  char buf[40];
  snprintf(buf, sizeof(buf), "CH%u %lup/s", rx.currentChannel(),
           (unsigned long)rx.rxPerSecond());
  g->setTextColor(rx.scanState() == EspNowReceiver::LOCKED ? TFT_GREEN : TFT_ORANGE,
                  COL_BAR_BG);
  g->drawString(buf, 4, y + 4);

  // Wi-Fi status of the CONTROLLER (center)
  g->setTextDatum(TC_DATUM);
  g->setTextColor(p.wifiStatus == WIFI_ST_OFFLINE ? TFT_ORANGE : TFT_SILVER, COL_BAR_BG);
  g->drawString(wifiStatusText(p.wifiStatus), GameRenderer::SCREEN_W / 2, y + 4);

  // Server status (right)
  g->setTextDatum(TR_DATUM);
  const char *srv = p.serverStatus == SERVER_ST_ONLINE  ? "SRV: online"
                  : p.serverStatus == SERVER_ST_OFFLINE ? "SRV: offline"
                                                        : "SRV: -";
  g->setTextColor(p.serverStatus == SERVER_ST_ONLINE ? TFT_GREEN : TFT_SILVER, COL_BAR_BG);
  g->drawString(srv, GameRenderer::SCREEN_W - 4, y + 4);
}
