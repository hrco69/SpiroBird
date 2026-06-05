// ============================================================================
// ServerClient — POSTs finished attempt results to the Node.js backend
//
// Rules (master spec):
//  - results are POSTed ONLY after SUCCESS/FAIL, never during ACTIVE sampling
//  - hard HTTP timeout (HTTP_TIMEOUT_MS = 1500 ms) — a dead server can stall
//    one loop iteration briefly but can never freeze the system
//  - failure -> log + retry a few times -> drop; the game always continues
//
// queueResult() only stores the result; the actual blocking POST happens in
// update() and main.cpp gates it with allowBlocking = (state != STATE_ACTIVE).
// ============================================================================
#pragma once
#include <Arduino.h>
#include "config.h"
#include "types.h"

#if ENABLE_SERVER_POST

class ServerClient {
public:
  void begin();   // optional GET /health probe when Wi-Fi is already up

  void queueResult(const AttemptResult &r);
  void update(uint32_t nowMs, bool allowBlocking);

  ServerStatus status() const { return _status; }

private:
  bool postQueued();   // true = delivered (or permanently dropped)

  ServerStatus  _status = SERVER_ST_UNKNOWN;
  AttemptResult _queued;
  bool          _pending = false;
  uint8_t       _attemptsLeft = 0;
  uint32_t      _lastTryMs = 0;
};

#endif // ENABLE_SERVER_POST
