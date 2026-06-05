// ============================================================================
// SpiroBird shared ESP-NOW protocol
//
// !!! THIS FILE MUST BE BYTE-IDENTICAL IN BOTH FIRMWARE PROJECTS !!!
//     controller/include/protocol.h  ==  display/include/protocol.h
// If you change anything here, copy the file to the other project and bump
// SPIROBIRD_PROTOCOL_VERSION so stale receivers reject the packets.
// ============================================================================
#pragma once
#include <Arduino.h>

#define SPIROBIRD_MAGIC 0x5342            // "SB"
#define SPIROBIRD_PROTOCOL_VERSION 1

// Broadcast MAC — no hardcoded peer addresses in v1
#define SPIROBIRD_BROADCAST_MAC { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }

// ----------------------------------------------------------------------------
// Enums (1 byte each — sent inside SpiroPacket)
// ----------------------------------------------------------------------------

enum ExerciseState : uint8_t {
  STATE_IDLE = 0,
  STATE_CALIBRATING = 1,
  STATE_READY = 2,
  STATE_ACTIVE = 3,
  STATE_SUCCESS = 4,
  STATE_FAIL = 5,
  STATE_RESULT = 6,
  STATE_SLEEP = 7
};

enum FailReason : uint8_t {
  FAIL_NONE = 0,
  FAIL_OVER_1200 = 1,
  FAIL_UNSTABLE = 2,
  FAIL_COLLISION = 3,
  FAIL_TIMEOUT = 4
};

// Wi-Fi status of the Controller. The Display only RENDERS this — it never
// connects to Wi-Fi itself.
enum WifiStatus : uint8_t {
  WIFI_ST_DISABLED = 0,      // ENABLE_WIFI 0
  WIFI_ST_CONNECTING = 1,    // trying saved credentials
  WIFI_ST_SETUP_PORTAL = 2,  // AP "SpiroBird-Setup" active -> show portal hint
  WIFI_ST_CONNECTED = 3,
  WIFI_ST_OFFLINE = 4        // portal timed out / skipped -> "Playing Offline"
};

enum ServerStatus : uint8_t {
  SERVER_ST_UNKNOWN = 0,
  SERVER_ST_ONLINE = 1,      // last POST/health check succeeded
  SERVER_ST_OFFLINE = 2,     // last POST failed
  SERVER_ST_DISABLED = 3     // ENABLE_SERVER_POST 0 or Wi-Fi offline
};

// ----------------------------------------------------------------------------
// SpiroPacket — sent Controller -> Display via ESP-NOW broadcast at 30-50 Hz.
// Packed: layout must match on both sides, no padding allowed.
// Total size: 48 bytes (well under the 250-byte ESP-NOW payload limit).
// ----------------------------------------------------------------------------

struct __attribute__((packed)) SpiroPacket {
  uint16_t magic;            // SPIROBIRD_MAGIC
  uint8_t  version;          // SPIROBIRD_PROTOCOL_VERSION
  uint32_t seq;              // increments every packet; detects loss/restart
  uint32_t timestampMs;      // controller millis()

  uint16_t rawAdc;           // raw ADC reading 0-4095
  int16_t  deviationAdc;     // rawAdc - calibrated offset

  float flowMlS;             // mapped flow before filtering
  float filteredFlowMlS;     // EMA-filtered flow (bird position source)
  float volumeMl;            // integrated volume of current attempt
  float maxFlowMlS;          // max flow of current attempt
  float avgFlowMlS;          // average flow of current attempt

  uint16_t stableTimeMs;     // continuous stable time, 0-5000
  uint8_t  state;            // ExerciseState
  uint8_t  failReason;       // FailReason

  bool targetZone;           // 900 <= filtered <= 1200 ml/s
  bool dangerZone;           // filtered > 1200 ml/s
  bool success;              // attempt finished successfully
  bool fail;                 // attempt failed
  bool deepSleepPending;     // controller is about to enter real deep sleep

  uint8_t wifiStatus;        // WifiStatus
  uint8_t serverStatus;      // ServerStatus
  uint8_t espNowChannel;     // channel the controller transmits on

  uint8_t checksum;          // XOR of all preceding bytes — MUST stay last
};

static_assert(sizeof(SpiroPacket) == 48, "SpiroPacket layout changed — update protocol.md and bump version");
static_assert(offsetof(SpiroPacket, checksum) == sizeof(SpiroPacket) - 1, "checksum must be the last byte");

// ----------------------------------------------------------------------------
// Checksum helpers
// ----------------------------------------------------------------------------

// XOR of every byte except the checksum byte itself.
inline uint8_t spiroPacketChecksum(const SpiroPacket &p) {
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&p);
  uint8_t x = 0;
  for (size_t i = 0; i < sizeof(SpiroPacket) - 1; i++) x ^= bytes[i];
  return x;
}

// Controller side: stamp magic/version/checksum right before sending.
inline void spiroPacketFinalize(SpiroPacket &p) {
  p.magic = SPIROBIRD_MAGIC;
  p.version = SPIROBIRD_PROTOCOL_VERSION;
  p.checksum = spiroPacketChecksum(p);
}

// Display side: validate a received buffer before using it.
// Rejects wrong size, wrong magic, wrong version and checksum mismatch.
inline bool spiroPacketValid(const uint8_t *data, size_t len) {
  if (data == nullptr || len != sizeof(SpiroPacket)) return false;
  const SpiroPacket *p = reinterpret_cast<const SpiroPacket *>(data);
  if (p->magic != SPIROBIRD_MAGIC) return false;
  if (p->version != SPIROBIRD_PROTOCOL_VERSION) return false;
  return spiroPacketChecksum(*p) == p->checksum;
}
