#include "ServerClient.h"

#if ENABLE_SERVER_POST

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static const char *failReasonName(FailReason r) {
  switch (r) {
    case FAIL_OVER_1200: return "FAIL_OVER_1200";
    case FAIL_UNSTABLE:  return "FAIL_UNSTABLE";
    case FAIL_COLLISION: return "FAIL_COLLISION";
    case FAIL_TIMEOUT:   return "FAIL_TIMEOUT";
    case FAIL_NONE:
    default:             return "";
  }
}

void ServerClient::begin() {
  if (WiFi.status() != WL_CONNECTED) {
    _status = SERVER_ST_UNKNOWN;
    DBG("[server] Wi-Fi not connected, health check skipped\n");
    return;
  }

  // Quick boot-time health probe (bounded by HTTP_TIMEOUT_MS).
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);

  String url = String(SERVER_BASE_URL) + SERVER_HEALTH_PATH;
  bool ok = false;

  if (url.startsWith("https")) {
    WiFiClientSecure client;
    client.setInsecure();   // demo project: skip cert validation (Render TLS)
    if (http.begin(client, url)) ok = (http.GET() == 200);
  } else {
    if (http.begin(url)) ok = (http.GET() == 200);
  }
  http.end();

  _status = ok ? SERVER_ST_ONLINE : SERVER_ST_OFFLINE;
  DBG("[server] health check %s: %s\n", url.c_str(), ok ? "ONLINE" : "OFFLINE");
}

void ServerClient::queueResult(const AttemptResult &r) {
  if (!r.valid) return;
  _queued       = r;
  _pending      = true;
  _attemptsLeft = SERVER_POST_RETRIES;
  _lastTryMs    = 0;   // try on the next eligible update()
  DBG("[server] result queued for POST (%s, %.0f ml)\n",
      r.success ? "SUCCESS" : "FAIL", r.volumeMl);
}

void ServerClient::update(uint32_t nowMs, bool allowBlocking) {
  if (!_pending || !allowBlocking) return;
  if (nowMs - _lastTryMs < SERVER_RETRY_INTERVAL_MS && _lastTryMs != 0) return;

  if (WiFi.status() != WL_CONNECTED) {
    // Offline mode: drop immediately, the local game/high score already works.
    DBG("[server] Wi-Fi offline -> result not sent (local mode)\n");
    _pending = false;
    return;
  }

  _lastTryMs = nowMs;
  if (postQueued()) {
    _pending = false;
  } else if (--_attemptsLeft == 0) {
    DBG("[server] giving up after %d attempts — continuing without server\n",
        SERVER_POST_RETRIES);
    _pending = false;
  }
}

bool ServerClient::postQueued() {
  // Body matches docs/protocol.md / server README:
  // { deviceId, success, volumeMl, maxFlowMlS, avgFlowMlS,
  //   stableTimeMs, failReason, timestampMs }
  JsonDocument doc;
  doc["deviceId"]     = DEVICE_ID;
  doc["success"]      = _queued.success;
  doc["volumeMl"]     = (int)roundf(_queued.volumeMl);
  doc["maxFlowMlS"]   = (int)roundf(_queued.maxFlowMlS);
  doc["avgFlowMlS"]   = (int)roundf(_queued.avgFlowMlS);
  doc["stableTimeMs"] = _queued.stableTimeMs;
  if (_queued.success) doc["failReason"] = nullptr;
  else                 doc["failReason"] = failReasonName(_queued.failReason);
  doc["timestampMs"]  = _queued.timestampMs;

  String body;
  serializeJson(doc, body);

  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);   // hard caps: server can never
  http.setTimeout(HTTP_TIMEOUT_MS);          // freeze the controller

  String url = String(SERVER_BASE_URL) + SERVER_RESULTS_PATH;
  bool began;
  WiFiClientSecure secureClient;
  if (url.startsWith("https")) {
    secureClient.setInsecure();   // demo: no cert validation
    began = http.begin(secureClient, url);
  } else {
    began = http.begin(url);
  }
  if (!began) {
    DBG("[server] ERROR: http.begin failed for %s\n", url.c_str());
    _status = SERVER_ST_OFFLINE;
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  http.end();

  DBG("[server] POST %s -> %d\n", url.c_str(), code);
  if (code >= 200 && code < 300) {
    _status = SERVER_ST_ONLINE;
    return true;
  }
  _status = SERVER_ST_OFFLINE;
  return false;
}

#endif // ENABLE_SERVER_POST
