#include "WifiProvisioning.h"

#if ENABLE_WIFI

#include <WiFi.h>
#include <esp_wifi.h>

#if ENABLE_WIFI_MANAGER
  #include <WiFiManager.h>          // tzapu/WiFiManager
#else
  #include <WebServer.h>
  #include <DNSServer.h>
  #include <Preferences.h>
#endif

// ----------------------------------------------------------------------------
// Common helpers
// ----------------------------------------------------------------------------

bool WifiProvisioning::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

bool WifiProvisioning::buttonSkipPressed() {
  // Wake/start button doubles as "skip Wi-Fi setup" during the portal.
  if (digitalRead(PIN_WAKE_BUTTON) != LOW) return false;
  delay(30);   // boot-only debounce wait
  return digitalRead(PIN_WAKE_BUTTON) == LOW;
}

bool WifiProvisioning::waitForConnection(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(50);   // boot-only wait, never used during the game
  }
  return WiFi.status() == WL_CONNECTED;
}

void WifiProvisioning::goOffline(const char *reason) {
  // OFFLINE is a TERMINAL, deterministic state (until reboot). The critical
  // part is killing every source of background STA scanning: an idle STA
  // that keeps looking for its AP hops channels every few seconds and drags
  // the ESP-NOW TX channel with it (HW test: Display locked/lost signal in
  // an endless loop while the router was off).
  WiFi.setAutoReconnect(false);   // no stack-driven retries
  WiFi.disconnect(false);         // stop STA association, keep radio ON
  WiFi.mode(WIFI_STA);            // make sure the setup AP is gone
  delay(50);                      // boot/offline transition only — let it settle
  // Park the radio on the fixed channel so the Display's scan is deterministic.
  esp_wifi_set_channel(ESPNOW_FIXED_CHANNEL, WIFI_SECOND_CHAN_NONE);
  _status = WIFI_ST_OFFLINE;
  DBG("[wifi] %s\n", reason);
  DBG("[wifi] Wi-Fi unavailable, continuing offline (ESP-NOW fixed on channel %d)\n",
      ESPNOW_FIXED_CHANNEL);
  DBG("[wifi] to retry Wi-Fi: reboot; to change network: hold the button during power-on\n");
}

// ----------------------------------------------------------------------------
// begin() — boot-time connection / portal (blocking allowed here only)
// ----------------------------------------------------------------------------

void WifiProvisioning::begin() {
  _status = WIFI_ST_CONNECTING;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);          // keep latency low for ESP-NOW coexistence
  WiFi.setAutoReconnect(false);  // WE decide when to retry — stack-driven
                                 // retries scan channels and break ESP-NOW

  // Holding the wake button during power-up forces the setup portal — the
  // ONLY way to reach it once credentials exist (see below for why).
  const bool forcePortal = (digitalRead(PIN_WAKE_BUTTON) == LOW);
  const bool haveSaved   = hasSavedCredentials();
  if (forcePortal) DBG("[wifi] wake button held at boot -> forcing setup portal\n");

  if (!forcePortal && haveSaved) {
    DBG("[wifi] trying saved credentials (timeout %d ms)...\n", WIFI_CONNECT_TIMEOUT_MS);
    if (tryConnectSaved()) {
      _status = WIFI_ST_CONNECTED;
      DBG("[wifi] connected to '%s', IP=%s, channel=%d\n",
          WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.channel());
      return;
    }
    // Saved network exists but is unreachable right now -> ask the user
    // (instructions appear on the Display): short press = play OFFLINE,
    // long press = open the setup portal. Timeout -> offline.
    if (runDecisionPhase() == DEC_OFFLINE) {
      goOffline("offline chosen (short press or decision timeout)");
      return;
    }
    // DEC_PORTAL falls through to the portal below.
  }

  DBG("[wifi] starting setup portal '%s'\n", WIFI_AP_NAME);
  DBG("[wifi] connect with a phone/laptop and open http://192.168.4.1\n");
  DBG("[wifi] portal exits on: success, %d s timeout, or wake-button skip\n",
      WIFI_PORTAL_TIMEOUT_SEC);

#if ENABLE_WIFI_MANAGER
  runWiFiManagerPortal();
#else
  runFallbackPortal();
#endif

  if (isConnected()) {
    _status = WIFI_ST_CONNECTED;
    DBG("[wifi] connected to '%s', IP=%s, channel=%d\n",
        WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.channel());
    return;
  }

  // Portal closed without success (timeout or button skip). Last chance:
  // try whatever credentials exist now (maybe an older network came back),
  // then settle into deterministic offline mode.
  DBG("[wifi] portal closed without connection\n");
  if (hasSavedCredentials() && tryConnectSaved()) {
    _status = WIFI_ST_CONNECTED;
    DBG("[wifi] connected to '%s' after portal close, IP=%s, channel=%d\n",
        WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.channel());
    return;
  }
  goOffline("portal closed and saved Wi-Fi unreachable");
}

// Saved Wi-Fi unreachable: wait for the user's choice on the wake button.
// Short press  -> play offline.
// Long press   -> open the setup portal (scan for new networks).
// No input for WIFI_DECISION_TIMEOUT_SEC -> offline (demo never hangs).
WifiProvisioning::Decision WifiProvisioning::runDecisionPhase() {
  _status = WIFI_ST_DECISION;
  _decisionHeldMs = 0;
  DBG("[wifi] saved Wi-Fi unreachable — choose on the wake button:\n");
  DBG("[wifi]   SHORT press           -> continue OFFLINE\n");
  DBG("[wifi]   LONG press (>=%d ms)  -> open setup portal\n", WIFI_DECISION_LONGPRESS_MS);
  DBG("[wifi]   no input for %d s     -> OFFLINE\n", WIFI_DECISION_TIMEOUT_SEC);

  const uint32_t t0 = millis();
  uint32_t pressStartMs = 0;

  while (millis() - t0 < (uint32_t)WIFI_DECISION_TIMEOUT_SEC * 1000UL) {
    const uint32_t now = millis();
    const bool pressed = (digitalRead(PIN_WAKE_BUTTON) == LOW);

    if (pressed) {
      if (pressStartMs == 0) pressStartMs = now;
      _decisionHeldMs = now - pressStartMs;          // Display draws this
      if (_decisionHeldMs >= WIFI_DECISION_LONGPRESS_MS) {
        _decisionHeldMs = 0;
        DBG("[wifi] long press -> setup portal\n");
        return DEC_PORTAL;
      }
    } else if (pressStartMs != 0) {
      const uint32_t held = now - pressStartMs;
      pressStartMs = 0;
      _decisionHeldMs = 0;
      if (held >= 30) {                              // debounce glitches
        DBG("[wifi] short press -> OFFLINE\n");
        return DEC_OFFLINE;
      }
    }

    if (_portalTick) _portalTick();   // keep the Display informed (40 Hz)
    delay(10);                        // boot-only yield
  }

  DBG("[wifi] decision timeout\n");
  return DEC_OFFLINE;
}

// True if a Wi-Fi network was provisioned earlier (survives reboots in NVS).
bool WifiProvisioning::hasSavedCredentials() {
#if ENABLE_WIFI_MANAGER
  // WiFiManager persists into the esp-wifi NVS blob.
  wifi_config_t conf;
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) return false;
  return conf.sta.ssid[0] != 0;
#else
  Preferences prefs;
  prefs.begin("wifiprov", true);
  String ssid = prefs.getString("ssid", "");
  prefs.end();
  return !ssid.isEmpty();
#endif
}

// ----------------------------------------------------------------------------
// Saved-credentials attempt
// ----------------------------------------------------------------------------

bool WifiProvisioning::tryConnectSaved() {
#if ENABLE_WIFI_MANAGER
  // WiFiManager persists credentials in the esp32 Wi-Fi NVS blob; a no-arg
  // begin() reuses them.
  WiFi.begin();
#else
  Preferences prefs;
  prefs.begin("wifiprov", true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();
  if (ssid.isEmpty()) {
    DBG("[wifi] no saved credentials in NVS\n");
    return false;
  }
  DBG("[wifi] saved SSID: '%s'\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), pass.c_str());
#endif
  return waitForConnection(WIFI_CONNECT_TIMEOUT_MS);
}

// ----------------------------------------------------------------------------
// Portal implementation A: tzapu/WiFiManager (non-blocking process loop so we
// can honor the skip button and broadcast portal status to the Display)
// ----------------------------------------------------------------------------

#if ENABLE_WIFI_MANAGER

void WifiProvisioning::runWiFiManagerPortal() {
  WiFiManager wm;
  wm.setDebugOutput(ENABLE_DEBUG_SERIAL);
  wm.setConfigPortalBlocking(false);                 // we drive the loop
  wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_SEC);
  wm.setBreakAfterConfig(true);

  _status = WIFI_ST_SETUP_PORTAL;
  wm.startConfigPortal(WIFI_AP_NAME);

  const uint32_t t0 = millis();
  while (true) {
    // process() returns true once the user successfully configured Wi-Fi.
    if (wm.process() || WiFi.status() == WL_CONNECTED) {
      DBG("[wifi] portal: credentials configured\n");
      break;
    }
    if (millis() - t0 >= (uint32_t)WIFI_PORTAL_TIMEOUT_SEC * 1000UL) {
      wm.stopConfigPortal();
      DBG("[wifi] portal timeout — nobody configured Wi-Fi\n");
      return;   // begin() retries saved creds once, then goes offline
    }
    if (buttonSkipPressed()) {
      wm.stopConfigPortal();
      DBG("[wifi] portal skipped by wake button\n");
      return;   // begin() retries saved creds once, then goes offline
    }
    if (_portalTick) _portalTick();   // broadcast WIFI_ST_SETUP_PORTAL packets
    delay(10);                        // boot-only yield
  }

  // Give the STA connection a moment to finish after the portal closes.
  if (!waitForConnection(WIFI_CONNECT_TIMEOUT_MS)) {
    goOffline("portal closed but connection failed");
  }
}

// ----------------------------------------------------------------------------
// Portal implementation B: minimal fallback (no external library)
// AP "SpiroBird-Setup" + DNS catch-all + one page with a network scan list.
// Credentials are saved to Preferences("wifiprov") and connection retried.
// ----------------------------------------------------------------------------

#else // !ENABLE_WIFI_MANAGER

static String htmlEscape(const String &s) {
  String o;
  o.reserve(s.length());
  for (char c : s) {
    switch (c) {
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '&': o += "&amp;"; break;
      case '"': o += "&quot;"; break;
      default:  o += c;
    }
  }
  return o;
}

void WifiProvisioning::runFallbackPortal() {
  _status = WIFI_ST_SETUP_PORTAL;

  // Scan BEFORE raising the AP — simpler and more reliable than AP+scan.
  DBG("[wifi] scanning networks for the portal page...\n");
  int n = WiFi.scanNetworks();
  String options;
  for (int i = 0; i < n && i < 20; i++) {
    String ssid = htmlEscape(WiFi.SSID(i));
    options += "<option value=\"" + ssid + "\">" + ssid +
               " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
  }
  WiFi.scanDelete();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(WIFI_AP_NAME);
  IPAddress apIp = WiFi.softAPIP();   // 192.168.4.1
  DBG("[wifi] fallback portal AP up at http://%s\n", apIp.toString().c_str());

  DNSServer dns;
  dns.start(53, "*", apIp);           // captive: every hostname -> portal

  WebServer server(80);
  bool credsReceived = false;
  String newSsid, newPass;

  String page =
      "<!DOCTYPE html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
      "<title>SpiroBird Setup</title><style>body{font-family:sans-serif;margin:2em;background:#0b1d33;color:#eee}"
      "input,select,button{width:100%;padding:.6em;margin:.4em 0;font-size:1em}"
      "button{background:#2e8b57;color:#fff;border:0;border-radius:4px}</style></head>"
      "<body><h2>SpiroBird Wi-Fi Setup</h2>"
      "<form method='POST' action='/save'>"
      "<label>Network</label><select name='ssid'>" + options + "</select>"
      "<label>or type SSID</label><input name='ssid_manual' placeholder='SSID'>"
      "<label>Password</label><input name='pass' type='password' placeholder='password'>"
      "<button type='submit'>Save &amp; Connect</button></form>"
      "<p>Skip: press the wake button on the device.</p></body></html>";

  server.on("/", [&]() { server.send(200, "text/html", page); });
  server.on("/save", HTTP_POST, [&]() {
    newSsid = server.arg("ssid_manual").length() ? server.arg("ssid_manual")
                                                 : server.arg("ssid");
    newPass = server.arg("pass");
    credsReceived = true;
    server.send(200, "text/html",
                "<html><body style='font-family:sans-serif'>"
                "<h3>Saved. SpiroBird is connecting...</h3>"
                "<p>You can close this page.</p></body></html>");
  });
  server.onNotFound([&]() {   // captive-portal redirect
    server.sendHeader("Location", "http://" + apIp.toString(), true);
    server.send(302, "text/plain", "");
  });
  server.begin();

  const uint32_t t0 = millis();
  while (true) {
    dns.processNextRequest();
    server.handleClient();

    if (credsReceived) {
      DBG("[wifi] portal: trying SSID '%s'...\n", newSsid.c_str());
      WiFi.begin(newSsid.c_str(), newPass.c_str());
      if (waitForConnection(10000)) {
        Preferences prefs;
        prefs.begin("wifiprov", false);
        prefs.putString("ssid", newSsid);
        prefs.putString("pass", newPass);
        prefs.end();
        DBG("[wifi] portal: credentials saved to NVS\n");
        server.stop();
        dns.stop();
        WiFi.mode(WIFI_STA);   // drop the setup AP
        return;
      }
      DBG("[wifi] portal: connection failed, try again\n");
      credsReceived = false;
    }

    if (millis() - t0 >= (uint32_t)WIFI_PORTAL_TIMEOUT_SEC * 1000UL) {
      server.stop();
      dns.stop();
      WiFi.mode(WIFI_STA);
      DBG("[wifi] portal timeout — nobody configured Wi-Fi\n");
      return;   // begin() retries saved creds once, then goes offline
    }
    if (buttonSkipPressed()) {
      server.stop();
      dns.stop();
      WiFi.mode(WIFI_STA);
      DBG("[wifi] portal skipped by wake button\n");
      return;   // begin() retries saved creds once, then goes offline
    }
    if (_portalTick) _portalTick();
    delay(10);   // boot-only yield
  }
}

#endif // ENABLE_WIFI_MANAGER

// ----------------------------------------------------------------------------
// update() — non-blocking reconnect upkeep (called every loop)
// ----------------------------------------------------------------------------

void WifiProvisioning::update(uint32_t nowMs, bool allowReconnect) {
  // OFFLINE is terminal until reboot — deterministic for the live demo (no
  // surprise portals, no background scans disturbing ESP-NOW).
  if (_status == WIFI_ST_OFFLINE || _status == WIFI_ST_SETUP_PORTAL) return;

  if (WiFi.status() == WL_CONNECTED) {
    if (_status != WIFI_ST_CONNECTED) {
      _status = WIFI_ST_CONNECTED;
      _retriesLeft = WIFI_RECONNECT_MAX_ATTEMPTS;   // re-arm for the next loss
      DBG("[wifi] (re)connected, IP=%s, channel=%d\n",
          WiFi.localIP().toString().c_str(), WiFi.channel());
    }
    return;
  }

  if (_status == WIFI_ST_CONNECTED) {
    _status = WIFI_ST_CONNECTING;
    _lastReconnectMs = nowMs;   // grace period before the first retry
    DBG("[wifi] connection lost — will retry %d time(s), then lock into offline mode\n",
        _retriesLeft);
  }

  // Bounded, explicit retries: each one is a full STA scan that drags the
  // ESP-NOW channel around, so after the budget is spent we go (and stay)
  // offline instead of looping forever.
  if (allowReconnect && nowMs - _lastReconnectMs >= WIFI_RECONNECT_INTERVAL_MS) {
    _lastReconnectMs = nowMs;
    if (_retriesLeft == 0) {
      goOffline("reconnect attempts exhausted");
      return;
    }
    _retriesLeft--;
    DBG("[wifi] reconnect attempt (%d left after this)...\n", _retriesLeft);
    WiFi.reconnect();   // async — result observed on later update() calls
  }
}

#endif // ENABLE_WIFI
