#include "Haptics.h"

// ----------------------------------------------------------------------------
// LEDC compatibility: Arduino-ESP32 core 3.x changed the LEDC API.
//   core 2.x: ledcSetup(ch,...) + ledcAttachPin(pin,ch) + ledcWriteTone(ch,f)
//   core 3.x: ledcAttach(pin,...)                       + ledcWriteTone(pin,f)
// ----------------------------------------------------------------------------
#if ENABLE_BUZZER
  #if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
    #define BUZZER_INIT()        ledcAttach(PIN_BUZZER, 2000, 10)
    #define BUZZER_TONE(f)       ledcWriteTone(PIN_BUZZER, (f))
  #else
    #define BUZZER_INIT()        do { ledcSetup(BUZZER_LEDC_CHANNEL, 2000, 10); \
                                      ledcAttachPin(PIN_BUZZER, BUZZER_LEDC_CHANNEL); } while (0)
    #define BUZZER_TONE(f)       ledcWriteTone(BUZZER_LEDC_CHANNEL, (f))
  #endif
#endif

void Haptics::begin() {
  // SAFETY FIRST: the motor pin is already forced LOW as the very first
  // statement of setup() in main.cpp — repeat here defensively.
  pinMode(PIN_MOTOR, OUTPUT);
  digitalWrite(PIN_MOTOR, LOW);

#if ENABLE_BUZZER
  BUZZER_INIT();
  BUZZER_TONE(0);
  DBG("[haptics] buzzer ready on GPIO%d (LEDC)\n", PIN_BUZZER);
#else
  DBG("[haptics] buzzer DISABLED\n");
#endif

#if ENABLE_MOTOR
  DBG("[haptics] motor ENABLED on GPIO%d (max pulse %d ms, cooldown %d ms)\n",
      PIN_MOTOR, MOTOR_MAX_PULSE_MS, MOTOR_COOLDOWN_MS);
#else
  DBG("[haptics] motor DISABLED (enable only after transistor wiring is verified)\n");
#endif
}

// ------------------------------- buzzer -------------------------------------

void Haptics::toneOn(uint16_t freqHz) {
#if ENABLE_BUZZER
  BUZZER_TONE(freqHz);
#else
  (void)freqHz;
#endif
}

void Haptics::toneOff() {
#if ENABLE_BUZZER
  BUZZER_TONE(0);
#endif
}

void Haptics::playSequence(const ToneStep *steps, size_t count) {
#if ENABLE_BUZZER
  if (count == 0 || count > MAX_STEPS) return;
  memcpy(_seq, steps, count * sizeof(ToneStep));
  _seqLen = count;
  _seqIdx = 0;
  _playing = true;
  _stepStartMs = millis();
  applyStep(_stepStartMs);
#else
  (void)steps; (void)count;
#endif
}

void Haptics::applyStep(uint32_t nowMs) {
  _stepStartMs = nowMs;
  if (_seq[_seqIdx].freqHz > 0) toneOn(_seq[_seqIdx].freqHz);
  else                          toneOff();
}

void Haptics::beep(uint16_t freqHz, uint16_t durationMs) {
  ToneStep s[1] = {{freqHz, durationMs}};
  playSequence(s, 1);
}

void Haptics::startBeep() {
  beep(1047 /* C6 */, 80);
}

void Haptics::zoneBeep() {
  beep(1568 /* G6 */, 60);
}

void Haptics::successMelody() {
  // Rising C-major arpeggio with a held top note.
  static const ToneStep mel[] = {
    {1047, 120}, {0, 40}, {1319, 120}, {0, 40}, {1568, 120}, {0, 40}, {2093, 320},
  };
  playSequence(mel, sizeof(mel) / sizeof(mel[0]));
}

void Haptics::errorBeep() {
  static const ToneStep err[] = {
    {330, 180}, {0, 60}, {220, 320},
  };
  playSequence(err, sizeof(err) / sizeof(err[0]));
}

// -------------------------------- motor -------------------------------------

void Haptics::motorPulse(uint16_t durationMs) {
#if ENABLE_MOTOR
  uint32_t now = millis();
  if (_motorOn) return;                                       // already pulsing
  if (now - _motorLastOffMs < MOTOR_COOLDOWN_MS) return;      // cooldown guard
  if (durationMs > MOTOR_MAX_PULSE_MS) durationMs = MOTOR_MAX_PULSE_MS;
  if (durationMs == 0) return;

  _motorOn = true;
  _motorOnMs = now;
  _motorDurMs = durationMs;
  digitalWrite(PIN_MOTOR, HIGH);
  DBG("[haptics] motor pulse %u ms\n", durationMs);
#else
  (void)durationMs;
#endif
}

// ------------------------------- update -------------------------------------

void Haptics::update(uint32_t nowMs) {
  // Advance melody steps.
  if (_playing && nowMs - _stepStartMs >= _seq[_seqIdx].durationMs) {
    _seqIdx++;
    if (_seqIdx >= _seqLen) {
      _playing = false;
      toneOff();
    } else {
      applyStep(nowMs);
    }
  }

#if ENABLE_MOTOR
  if (_motorOn) {
    uint32_t elapsed = nowMs - _motorOnMs;
    // Normal end of pulse, plus a hard watchdog: the motor can NEVER stay on
    // longer than MOTOR_MAX_PULSE_MS regardless of what was requested.
    if (elapsed >= _motorDurMs || elapsed >= MOTOR_MAX_PULSE_MS) {
      digitalWrite(PIN_MOTOR, LOW);
      _motorOn = false;
      _motorLastOffMs = nowMs;
    }
  }
#endif
}

void Haptics::allOff() {
  _playing = false;
  toneOff();
  digitalWrite(PIN_MOTOR, LOW);
  _motorOn = false;
  _motorLastOffMs = millis();
}
