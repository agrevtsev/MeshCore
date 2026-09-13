#pragma once
#include "NetworkPrefs.h"

// SPIFFS rename does not replace an existing destination. Keep a previous
// complete record so interruption between renames does not discard settings.
template <typename FS>
bool loadNetworkPrefs(FS& fs, NetworkPrefs& prefs) {
  const char* paths[] = {"/network.json", "/network.bak"};
  for (const char* path : paths) {
    if (!fs.exists(path)) continue;
    auto file = fs.open(path, "r");
    NetworkPrefs candidate;
    if (file && candidate.loadSerial(file) && candidate.valid()) {
      prefs = candidate;
      return true;
    }
  }
  return false;
}

template <typename FS>
bool saveNetworkPrefs(FS& fs, NetworkPrefs& prefs) {
  if (!prefs.valid()) return false;
  auto file = fs.open("/network.tmp", "w");
  bool ok = file && prefs.saveSerial(file);
  if (file) { file.flush(); file.close(); }
  if (!ok) return false;
  // Read back before moving the last known good record.
  file = fs.open("/network.tmp", "r");
  NetworkPrefs verified;
  ok = file && verified.loadSerial(file) && verified.valid();
  if (file) file.close();
  if (!ok) return false;
  if (fs.exists("/network.json")) {
    if (fs.exists("/network.bak") && !fs.remove("/network.bak")) return false;
    if (!fs.rename("/network.json", "/network.bak")) return false;
  }
  if (!fs.rename("/network.tmp", "/network.json")) {
    if (fs.exists("/network.bak")) fs.rename("/network.bak", "/network.json");
    return false;
  }
  return true;
}
