// ============================================================================
// UiScreens — every non-game screen the Display can show
//
// The Display only RENDERS what the Controller broadcasts: scanning/no-signal
// states come from EspNowReceiver, everything else from SpiroPacket fields.
// No touch, no input, no Wi-Fi selection on this device.
// ============================================================================
#pragma once
#include <Arduino.h>
#include "config.h"
#include "protocol.h"
#include "GameRenderer.h"
#include "EspNowReceiver.h"

class UiScreens {
public:
  void begin(GameRenderer *renderer) { _r = renderer; }

  // Connection states
  void drawScanning(const EspNowReceiver &rx, uint32_t nowMs);   // NO SIGNAL / SCANNING CH n
  void drawNoSignal(const EspNowReceiver &rx, uint32_t nowMs);   // locked but stale

  // Controller-reported states
  void drawIdle(const SpiroPacket &p);
  void drawCalibrating(const SpiroPacket &p);
  void drawReady(const SpiroPacket &p);
  void drawSuccess(const SpiroPacket &p);
  void drawFail(const SpiroPacket &p);
  void drawResult(const SpiroPacket &p);
  void drawSleep(const SpiroPacket &p);
  void drawWifiSetup(const SpiroPacket &p);    // portal open on Controller
  void drawWifiDecision(const SpiroPacket &p); // offline vs portal choice

  // Overlays
  void drawFakeBanner();                                   // FAKE DATA demo mode
  void drawStatusBar(const SpiroPacket &p, const EspNowReceiver &rx);

private:
  void titleScreen(uint16_t bg, const char *title, uint16_t titleColor);
  void centerText(const char *txt, int y, uint8_t font, uint16_t color);
  static const char *wifiStatusText(uint8_t s);
  static const char *failReasonText(uint8_t r);

  GameRenderer *_r = nullptr;
};
