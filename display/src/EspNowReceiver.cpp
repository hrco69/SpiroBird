#include "EspNowReceiver.h"

#include <WiFi.h>
#include <esp_wifi.h>

static EspNowReceiver *s_instance = nullptr;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

// ----------------------------------------------------------------------------
// Receive callback — Wi-Fi task context, keep it minimal
// ----------------------------------------------------------------------------

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
void EspNowReceiver::onReceiveStatic(const esp_now_recv_info_t *info,
                                     const uint8_t *data, int len) {
  (void)info;
  if (s_instance) s_instance->onReceive(data, len);
}
#else
void EspNowReceiver::onReceiveStatic(const uint8_t *mac,
                                     const uint8_t *data, int len) {
  (void)mac;
  if (s_instance) s_instance->onReceive(data, len);
}
#endif

void EspNowReceiver::onReceive(const uint8_t *data, int len) {
  if (!spiroPacketValid(data, (size_t)len)) {
    _invalidTotal = _invalidTotal + 1;   // wrong size/magic/version/checksum
    return;
  }
  portENTER_CRITICAL(&s_mux);
  memcpy(&_last, data, sizeof(SpiroPacket));
  _lastPacketMs   = millis();
  _validTotal     = _validTotal + 1;
  _validOnChannel = _validOnChannel + 1;
  portEXIT_CRITICAL(&s_mux);
}

// ----------------------------------------------------------------------------
// Setup
// ----------------------------------------------------------------------------

bool EspNowReceiver::begin() {
  s_instance = this;

  // STA mode but NEVER connected — the Display does not use Wi-Fi networks,
  // does not run WiFiManager and stores no credentials.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    DBG("[rx] ERROR: esp_now_init failed\n");
    return false;
  }
  esp_now_register_recv_cb(onReceiveStatic);

  setChannel(CHANNEL_SCAN_MIN);
  _lastHopMs = millis();
  DBG("[rx] ESP-NOW ready — NO SIGNAL / SCANNING CH %u\n", _channel);
  return true;
}

void EspNowReceiver::setChannel(uint8_t ch) {
  _channel = ch;
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  portENTER_CRITICAL(&s_mux);
  _validOnChannel = 0;
  portEXIT_CRITICAL(&s_mux);
}

// ----------------------------------------------------------------------------
// Scan / lock / re-scan state machine (main loop)
// ----------------------------------------------------------------------------

void EspNowReceiver::update(uint32_t nowMs) {
  // RX rate once per second (for the HUD).
  if (nowMs - _rxRateLastMs >= 1000) {
    uint32_t total = validCount();
    _rxRate = total - _rxRateLastCount;
    _rxRateLastCount = total;
    _rxRateLastMs = nowMs;
  }

#if ENABLE_ESPNOW_CHANNEL_SCAN
  if (_state == SCANNING) {
    uint32_t onChannel;
    portENTER_CRITICAL(&s_mux);
    onChannel = _validOnChannel;
    portEXIT_CRITICAL(&s_mux);

    if (onChannel >= LOCK_VALID_PACKETS) {
      _state = LOCKED;
      _everValid = true;
      DBG("[rx] LOCKED CH %u — RECEIVING SPIROBIRD DATA\n", _channel);
      return;
    }
    if (nowMs - _lastHopMs >= CHANNEL_SCAN_DWELL_MS) {
      _lastHopMs = nowMs;
      uint8_t next = (_channel >= CHANNEL_SCAN_MAX) ? CHANNEL_SCAN_MIN
                                                    : (uint8_t)(_channel + 1);
      setChannel(next);
      DBG("[rx] NO SIGNAL / SCANNING CH %u\n", _channel);
    }
  } else { // LOCKED
    if (nowMs - lastPacketMs() >= SIGNAL_LOST_RESCAN_MS) {
      DBG("[rx] signal lost for %d ms — back to channel scanning\n",
          SIGNAL_LOST_RESCAN_MS);
      _state = SCANNING;
      _lastHopMs = nowMs;
      setChannel(_channel);   // start where we last heard the Controller
    }
  }
#else
  // Scan disabled: stay on the fixed start channel, lock on first packets.
  if (_state == SCANNING && validCount() >= LOCK_VALID_PACKETS) {
    _state = LOCKED;
    _everValid = true;
  }
#endif
}

// ----------------------------------------------------------------------------
// Accessors
// ----------------------------------------------------------------------------

bool EspNowReceiver::isFresh(uint32_t nowMs) const {
  return _everValid && (nowMs - lastPacketMs() <= SIGNAL_STALE_MS);
}

SpiroPacket EspNowReceiver::lastPacket() const {
  SpiroPacket copy;
  portENTER_CRITICAL(&s_mux);
  copy = _last;
  portEXIT_CRITICAL(&s_mux);
  return copy;
}

uint32_t EspNowReceiver::lastPacketMs() const {
  portENTER_CRITICAL(&s_mux);
  uint32_t t = _lastPacketMs;
  portEXIT_CRITICAL(&s_mux);
  return t;
}

uint32_t EspNowReceiver::validCount() const {
  portENTER_CRITICAL(&s_mux);
  uint32_t v = _validTotal;
  portEXIT_CRITICAL(&s_mux);
  return v;
}

uint32_t EspNowReceiver::invalidCount() const {
  return _invalidTotal;
}
