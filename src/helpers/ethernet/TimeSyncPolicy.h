#pragma once
#include <stdint.h>

// Pure policy; all timers use monotonic milliseconds, never wall-clock time.
class TimeSyncPolicy {
public:
  enum Source { NONE, OVERRIDE, DHCP, PUBLIC };
  static constexpr uint32_t ATTEMPT_MS = 30000;
  static constexpr uint32_t MAX_BACKOFF_MS = 15 * 60000;
  static constexpr uint32_t SYNC_MS = 60 * 60000;
  Source source = NONE;
  bool synced = false, holdover = false, attempting = false;
  uint32_t lastSync = 0;
  uint32_t lastAdvert = 0;

  static bool due(uint32_t now, uint32_t deadline) { return int32_t(now - deadline) >= 0; }
  static bool plausible(uint32_t epoch) { return epoch >= 1704067200UL && epoch < 4102444800UL; }

  Source connect(uint32_t now, bool automatic, bool hasDhcp) {
    _automatic = automatic;
    _hasDhcp = hasDhcp;
    _backoff = ATTEMPT_MS;
    return start(now, automatic ? (hasDhcp ? DHCP : PUBLIC) : OVERRIDE);
  }
  void disconnect() {
    source = NONE;
    attempting = false;
    holdover = synced;
  }
  bool success(uint32_t now, uint32_t epoch) {
    if (!attempting || !plausible(epoch)) return false;
    synced = true;
    holdover = false;
    lastSync = epoch;
    attempting = false;
    _deadline = now + SYNC_MS;
    _backoff = ATTEMPT_MS;
    return true;
  }
  // A non-NONE result requests a new attempt. A timeout with no result stops SNTP.
  Source tick(uint32_t now) {
    if (source == NONE || !due(now, _deadline)) return NONE;
    if (attempting) {
      holdover = synced;
      if (_automatic && source == DHCP) return start(now, PUBLIC);
      attempting = false;
      _deadline = now + _backoff;
      _backoff = _backoff >= MAX_BACKOFF_MS / 2 ? MAX_BACKOFF_MS : _backoff * 2;
      return NONE;
    }
    return start(now, _automatic ? (_hasDhcp ? DHCP : PUBLIC) : OVERRIDE);
  }
  bool canAdvert(uint32_t epoch) const { return synced && plausible(epoch) && epoch > lastAdvert; }
  void advertised(uint32_t epoch) { lastAdvert = epoch; }

private:
  uint32_t _deadline = 0, _backoff = ATTEMPT_MS;
  bool _automatic = true, _hasDhcp = false;
  Source start(uint32_t now, Source selected) {
    source = selected;
    attempting = true;
    _deadline = now + ATTEMPT_MS;
    return source;
  }
};
