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
  WiFi.mode(WIFI_STA);   // make sure the setup AP is gone
  _status = WIFI_ST_OFFLINE;
  DBG("[wifi] %s\n", reason);
  DBG("[wifi] Wi-Fi unavailable, continuing offline\n");
}

// ----------------------------------------------------------------------------
// begin() — boot-time connection / portal (blocking allowed here only)
// ----------------------------------------------------------------------------

void WifiProvisioning::begin() {
  _status = WIFI_ST_CONNECTING;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);   // keep latency low for ESP-NOW coexistence

  DBG("[wifi] trying saved credentials (timeout %d ms)...\n", WIFI_CONNECT_TIMEOUT_MS);
  if (tryConnectSaved()) {
    _status = WIFI_ST_CONNECTED;
    DBG("[wifi] connected to '%s', IP=%s, channel=%d\n",
        WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.channel());
    return;
  }

  DBG("[wifi] saved credentials failed/missing -> starting setup portal '%s'\n",
      WIFI_AP_NAME);
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
  }
  // else: goOffline() was already called inside the portal routine.
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
      goOffline("portal timeout — nobody configured Wi-Fi");
      return;
    }
    if (buttonSkipPressed()) {
      wm.stopConfigPortal();
      goOffline("portal skipped by wake button");
      return;
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
      goOffline("portal timeout — nobody configured Wi-Fi");
      return;
    }
    if (buttonSkipPressed()) {
      server.stop();
      dns.stop();
      goOffline("portal skipped by wake button");
      return;
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
  // Once we decided to play offline we stay offline until reboot — simple and
  // predictable for the live demo (no surprise portal mid-game).
  if (_status == WIFI_ST_OFFLINE || _status == WIFI_ST_SETUP_PORTAL) return;

  if (WiFi.status() == WL_CONNECTED) {
    if (_status != WIFI_ST_CONNECTED) {
      _status = WIFI_ST_CONNECTED;
      DBG("[wifi] (re)connected, IP=%s, channel=%d\n",
          WiFi.localIP().toString().c_str(), WiFi.channel());
    }
    return;
  }

  if (_status == WIFI_ST_CONNECTED) {
    _status = WIFI_ST_CONNECTING;
    DBG("[wifi] connection lost\n");
  }

  if (allowReconnect && nowMs - _lastReconnectMs >= WIFI_RECONNECT_INTERVAL_MS) {
    _lastReconnectMs = nowMs;
    DBG("[wifi] reconnect attempt...\n");
    WiFi.reconnect();   // async — result observed on later update() calls
  }
}

#endif // ENABLE_WIFI
