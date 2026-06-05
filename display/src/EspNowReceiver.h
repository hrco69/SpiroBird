// ============================================================================
// EspNowReceiver — receives SpiroPackets without knowing the channel upfront
//
// The Display NEVER connects to Wi-Fi. It cycles channels 1..13 until valid
// SpiroBird packets (magic + version + checksum) arrive, locks after
// LOCK_VALID_PACKETS, and falls back to scanning after SIGNAL_LOST_RESCAN_MS
// of silence. Invalid packets are counted and ignored — never rendered.
//
// The ESP-NOW receive callback runs in the Wi-Fi task: it only validates,
// copies the packet under a spinlock and bumps counters. All decisions happen
// in update() on the main loop.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <esp_now.h>
#include "config.h"
#include "protocol.h"

class EspNowReceiver {
public:
  enum ScanState : uint8_t { SCANNING = 0, LOCKED = 1 };

  bool begin();
  void update(uint32_t nowMs);

  ScanState scanState()      const { return _state; }
  uint8_t   currentChannel() const { return _channel; }
  bool      everReceived()   const { return _everValid; }
  // Fresh = usable for live rendering. Stale data is never animated.
  bool      isFresh(uint32_t nowMs) const;

  SpiroPacket lastPacket() const;   // consistent copy (spinlock)
  uint32_t  lastPacketMs() const;
  uint32_t  validCount()   const;
  uint32_t  invalidCount() const;
  uint32_t  rxPerSecond()  const { return _rxRate; }

private:
  void setChannel(uint8_t ch);
  void onReceive(const uint8_t *data, int len);   // Wi-Fi task context!

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  static void onReceiveStatic(const esp_now_recv_info_t *info,
                              const uint8_t *data, int len);
#else
  static void onReceiveStatic(const uint8_t *mac, const uint8_t *data, int len);
#endif

  ScanState _state   = SCANNING;
  uint8_t   _channel = CHANNEL_SCAN_MIN;
  uint32_t  _lastHopMs = 0;
  bool      _everValid = false;

  // Shared with the receive callback (guarded by _mux):
  SpiroPacket       _last = {};
  volatile uint32_t _lastPacketMs   = 0;
  volatile uint32_t _validTotal     = 0;
  volatile uint32_t _invalidTotal   = 0;
  volatile uint32_t _validOnChannel = 0;

  // RX rate bookkeeping (main loop only):
  uint32_t _rxRate = 0, _rxRateLastCount = 0, _rxRateLastMs = 0;
};
