#include "Storage.h"

static const char *NVS_NAMESPACE = "spirobird";

void Storage::begin() {
  if (!_prefs.begin(NVS_NAMESPACE, /*readOnly=*/false)) {
    DBG("[storage] ERROR: NVS namespace '%s' failed to open\n", NVS_NAMESPACE);
    return;
  }
  // isKey() guards avoid scary (but harmless) NOT_FOUND error logs on the
  // very first boot, before any value was ever stored.
  _bestVolumeMl = _prefs.isKey("bestVol")    ? _prefs.getFloat("bestVol", 0.0f) : 0.0f;
  _bestStableMs = _prefs.isKey("bestStable") ? _prefs.getUShort("bestStable", 0) : 0;
  _attemptCount = _prefs.isKey("attempts")   ? _prefs.getULong("attempts", 0)   : 0;
  _successCount = _prefs.isKey("successes")  ? _prefs.getULong("successes", 0)  : 0;
  printStats();
}

bool Storage::recordAttempt(const AttemptResult &r) {
  if (!r.valid) return false;

  _attemptCount++;
  _prefs.putULong("attempts", _attemptCount);

  bool newBestVolume = false;

  if (r.success) {
    _successCount++;
    _prefs.putULong("successes", _successCount);

    if (r.volumeMl > _bestVolumeMl) {
      _bestVolumeMl = r.volumeMl;
      _prefs.putFloat("bestVol", _bestVolumeMl);
      newBestVolume = true;
      DBG("[storage] NEW HIGH SCORE: %.0f ml\n", _bestVolumeMl);
    }
  }

  if (r.stableTimeMs > _bestStableMs) {
    _bestStableMs = r.stableTimeMs;
    _prefs.putUShort("bestStable", _bestStableMs);
  }

  return newBestVolume;
}

void Storage::printStats() const {
  DBG("[storage] high score: bestVol=%.0f ml, bestStable=%u ms, attempts=%lu, successes=%lu\n",
      _bestVolumeMl, _bestStableMs,
      (unsigned long)_attemptCount, (unsigned long)_successCount);
}
