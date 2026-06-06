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

// NOTE: bare piezo discs are loudest near their resonance (~2-4 kHz) — tones
// below ~1 kHz are barely audible. All cues live in the 1.5-3.2 kHz band.

void Haptics::bootChirp() {
  // Three rising chirps right after begin(): instant hardware self-test.
  static const ToneStep chirp[] = {
    {2000, 120}, {0, 60}, {2500, 120}, {0, 60}, {3000, 180},
  };
  playSequence(chirp, sizeof(chirp) / sizeof(chirp[0]));
}

void Haptics::startBeep() {
  beep(2200, 150);
}

void Haptics::zoneBeep() {
  beep(2700, 100);
}

void Haptics::successMelody() {
  // Rising arpeggio with a held top note, all in the loud piezo band.
  static const ToneStep mel[] = {
    {1568, 140}, {0, 40}, {2093, 140}, {0, 40}, {2637, 140}, {0, 40}, {3136, 400},
  };
  playSequence(mel, sizeof(mel) / sizeof(mel[0]));
}

void Haptics::errorBeep() {
  // Descending two-tone "wrong" sound.
  static const ToneStep err[] = {
    {2000, 200}, {0, 60}, {1500, 400},
  };
  playSequence(err, sizeof(err) / sizeof(err[0]));
}

// -------------------------------- motor -------------------------------------

void Haptics::motorPulse(uint16_t durationMs) {
  MotorStep s[1] = {{durationMs, 0}};
  motorPattern(s, 1);
}

void Haptics::motorFail() {
  // Several long pulses in a row — unmistakable on the heavy vibro motor.
  MotorStep seq[MOTOR_MAX_STEPS];
  size_t n = min((size_t)MOTOR_FAIL_PULSE_COUNT, MOTOR_MAX_STEPS);
  for (size_t i = 0; i < n; i++) {
    seq[i] = {MOTOR_FAIL_PULSE_ON_MS, MOTOR_FAIL_PULSE_OFF_MS};
  }
  motorPattern(seq, n);
}

void Haptics::motorPattern(const MotorStep *steps, size_t count) {
#if ENABLE_MOTOR
  uint32_t now = millis();
  if (count == 0 || count > MOTOR_MAX_STEPS) return;
  if (_motorActive) return;                                   // don't interrupt
  if (now - _motorLastOffMs < MOTOR_COOLDOWN_MS) return;      // cooldown guard

  memcpy(_mSeq, steps, count * sizeof(MotorStep));
  _mLen = count;
  _mIdx = 0;
  _motorActive = true;
  _mPhaseOn = true;
  _mPhaseStartMs = now;
  digitalWrite(PIN_MOTOR, HIGH);
  DBG("[haptics] motor pattern: %u step(s), first ON %u ms\n",
      (unsigned)count, _mSeq[0].onMs);
#else
  (void)steps; (void)count;
#endif
}

void Haptics::motorFinish(uint32_t nowMs) {
  digitalWrite(PIN_MOTOR, LOW);
  _motorActive = false;
  _mPhaseOn = false;
  _motorLastOffMs = nowMs;
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
  if (_motorActive) {
    const uint32_t elapsed = nowMs - _mPhaseStartMs;
    if (_mPhaseOn) {
      // Hard watchdog: an ON segment can NEVER exceed MOTOR_MAX_PULSE_MS,
      // regardless of what the pattern requested.
      uint16_t onMs = _mSeq[_mIdx].onMs;
      if (onMs > MOTOR_MAX_PULSE_MS) onMs = MOTOR_MAX_PULSE_MS;
      if (elapsed >= onMs) {
        digitalWrite(PIN_MOTOR, LOW);
        if (_mSeq[_mIdx].offMs == 0 && _mIdx + 1 >= _mLen) {
          motorFinish(nowMs);             // pattern done
        } else {
          _mPhaseOn = false;              // gap between pulses
          _mPhaseStartMs = nowMs;
        }
      }
    } else {
      if (elapsed >= _mSeq[_mIdx].offMs) {
        _mIdx++;
        if (_mIdx >= _mLen) {
          motorFinish(nowMs);
        } else {
          _mPhaseOn = true;
          _mPhaseStartMs = nowMs;
          digitalWrite(PIN_MOTOR, HIGH);
        }
      }
    }
  }
#endif
}

void Haptics::allOff() {
  _playing = false;
  toneOff();
  motorFinish(millis());
}
