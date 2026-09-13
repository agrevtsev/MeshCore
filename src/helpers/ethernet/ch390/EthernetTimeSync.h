#pragma once
#ifdef M7_ETHERNET_TIME_SYNC
#include <FS.h>
#include <helpers/ethernet/NetworkPrefs.h>
#include <helpers/ethernet/TimeSyncPolicy.h>

class EthernetTimeSync {
public:
  void begin(fs::FS& fs);
  void loop();
  bool handleCommand(const char* command, char* reply, size_t size);
  bool isSynced() const;
  // Sequence changes after every successful sync; mesh code handles corrections.
  uint32_t syncSequence() const;
private:
  fs::FS* _fs = nullptr;
  NetworkPrefs _saved;
  bool _pendingReboot = false;
  uint32_t _lastPoll = 0;
};
extern EthernetTimeSync ethernetTimeSync;
#endif
