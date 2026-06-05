// ============================================================================
// WifiProvisioning — Wi-Fi connection + captive-portal setup (Controller only!)
//
// The Controller has no screen, so Wi-Fi selection happens via a phone/laptop
// connecting to the AP "SpiroBird-Setup" (captive portal). The Display NEVER
// selects Wi-Fi — it only renders the WifiStatus we broadcast.
//
// Boot flow (blocking is allowed ONLY here, never during the game):
//   1. try saved credentials (WIFI_CONNECT_TIMEOUT_MS)
//   2. on failure: start the portal, status = WIFI_ST_SETUP_PORTAL
//   3. portal exits on: success / WIFI_PORTAL_TIMEOUT_SEC / wake-button skip
//   4. not connected -> WIFI_ST_OFFLINE, game runs locally ("Playing Offline")
//
// Two compile-time implementations (config.h):
//   ENABLE_WIFI_MANAGER 1 -> tzapu/WiFiManager (non-blocking process loop)
//   ENABLE_WIFI_MANAGER 0 -> minimal fallback portal (WebServer + DNSServer +
//                            network scan page, creds in Preferences)
// ============================================================================
#pragma once
#include <Arduino.h>
#include "config.h"
#include "protocol.h"

#if ENABLE_WIFI

class WifiProvisioning {
public:
  // Called repeatedly while the portal is open (rate-limit inside the
  // callback). main.cpp uses it to broadcast WIFI_ST_SETUP_PORTAL packets so
  // the Display can show "Connect to SpiroBird-Setup".
  typedef void (*PortalTickFn)();
  void setPortalTick(PortalTickFn fn) { _portalTick = fn; }

  void begin();   // BOOT ONLY — may block while the portal is open

  // Non-blocking upkeep: reconnect attempts every WIFI_RECONNECT_INTERVAL_MS.
  // allowReconnect is false during STATE_ACTIVE (never disturb sampling).
  void update(uint32_t nowMs, bool allowReconnect);

  WifiStatus status() const { return _status; }
  bool isConnected() const;

private:
  bool tryConnectSaved();
  bool waitForConnection(uint32_t timeoutMs);
  bool buttonSkipPressed();
  void goOffline(const char *reason);

#if ENABLE_WIFI_MANAGER
  void runWiFiManagerPortal();
#else
  void runFallbackPortal();
#endif

  WifiStatus   _status = WIFI_ST_CONNECTING;
  PortalTickFn _portalTick = nullptr;
  uint32_t     _lastReconnectMs = 0;
};

#endif // ENABLE_WIFI
