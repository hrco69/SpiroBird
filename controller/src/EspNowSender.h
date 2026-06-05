// ============================================================================
// EspNowSender — broadcasts SpiroPackets to the Display (no hardcoded MACs)
//
// Strategy (docs/protocol.md):
//  - peer = broadcast MAC FF:FF:FF:FF:FF:FF, peer channel 0 ("current channel")
//    so ESP-NOW automatically follows whatever channel Wi-Fi is on
//  - with Wi-Fi disabled/offline the radio is parked on ESPNOW_FIXED_CHANNEL
//  - the actual TX channel is stamped into every packet (espNowChannel) so the
//    Display can verify its scan lock
// ============================================================================
#pragma once
#include <Arduino.h>
#include "config.h"
#include "protocol.h"

#if ENABLE_ESPNOW

class EspNowSender {
public:
  bool begin();          // call BEFORE WifiProvisioning so portal status can be sent
  void syncChannel();    // re-read the active Wi-Fi channel (call after Wi-Fi connects)
  bool send(SpiroPacket &p);   // finalizes (magic/version/checksum) and broadcasts

  bool     ready()     const { return _ready; }
  uint8_t  channel()   const { return _channel; }
  uint32_t sentCount() const { return _sentCount; }
  uint32_t failCount() const;   // delivery callbacks reporting failure

private:
  bool    _ready    = false;
  uint8_t _channel  = ESPNOW_FIXED_CHANNEL;
  uint32_t _sentCount = 0;
};

#endif // ENABLE_ESPNOW
