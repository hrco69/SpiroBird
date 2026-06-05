#include "EspNowSender.h"

#if ENABLE_ESPNOW

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

static const uint8_t BROADCAST_MAC[6] = SPIROBIRD_BROADCAST_MAC;

// Delivery-failure counter updated from the ESP-NOW TX callback. For broadcast
// frames "success" only means the frame left the radio, so this mostly catches
// init/driver problems, not a missing Display.
static volatile uint32_t s_txFailCount = 0;

// ESP-NOW send callback signature changed in Arduino core 3.x.
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
static void onEspNowSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  (void)info;
  if (status != ESP_NOW_SEND_SUCCESS) s_txFailCount++;
}
#else
static void onEspNowSent(const uint8_t *mac, esp_now_send_status_t status) {
  (void)mac;
  if (status != ESP_NOW_SEND_SUCCESS) s_txFailCount++;
}
#endif

bool EspNowSender::begin() {
  // ESP-NOW needs the Wi-Fi driver up in STA mode (works fine unconnected,
  // and keeps working when WifiProvisioning later switches to AP+STA portal).
  if (WiFi.getMode() == WIFI_MODE_NULL) WiFi.mode(WIFI_STA);

#if !ENABLE_WIFI
  // No Wi-Fi in this build: park the radio on the fixed channel so the
  // Display's scan can find us deterministically.
  esp_wifi_set_channel(ESPNOW_FIXED_CHANNEL, WIFI_SECOND_CHAN_NONE);
#endif

  if (esp_now_init() != ESP_OK) {
    DBG("[espnow] ERROR: esp_now_init failed\n");
    _ready = false;
    return false;
  }

  esp_now_register_send_cb(onEspNowSent);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BROADCAST_MAC, 6);
  peer.channel = 0;            // 0 = "use the current Wi-Fi channel"
  peer.encrypt = false;        // broadcast cannot be encrypted
  peer.ifidx   = WIFI_IF_STA;

  esp_err_t err = esp_now_add_peer(&peer);
  if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
    DBG("[espnow] ERROR: add_peer failed (%d)\n", (int)err);
    _ready = false;
    return false;
  }

  _ready = true;
  syncChannel();
  DBG("[espnow] ready: broadcast to FF:FF:FF:FF:FF:FF, channel %u\n", _channel);
  return true;
}

void EspNowSender::syncChannel() {
  uint8_t ch = 0;
  wifi_second_chan_t sc;
  if (esp_wifi_get_channel(&ch, &sc) == ESP_OK && ch > 0) {
    if (ch != _channel) DBG("[espnow] channel sync: %u -> %u\n", _channel, ch);
    _channel = ch;
  }
}

bool EspNowSender::send(SpiroPacket &p) {
  if (!_ready) return false;

  // Channel can drift when Wi-Fi connects/roams — keep the packet honest.
  syncChannel();
  p.espNowChannel = _channel;
  spiroPacketFinalize(p);

  esp_err_t err = esp_now_send(BROADCAST_MAC,
                               reinterpret_cast<const uint8_t *>(&p), sizeof(p));
  if (err == ESP_OK) {
    _sentCount++;
    return true;
  }
  return false;
}

uint32_t EspNowSender::failCount() const {
  return s_txFailCount;
}

#endif // ENABLE_ESPNOW
