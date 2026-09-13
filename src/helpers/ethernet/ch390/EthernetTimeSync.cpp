#ifdef M7_ETHERNET_TIME_SYNC
#include "EthernetTimeSync.h"
#include "CH390Network.h"
#include <helpers/ethernet/NetworkPrefsStore.h>
#include <WiFi.h>
#include <atomic>
#include <esp_netif.h>
#include <esp_sntp.h>
#include <lwip/apps/sntp.h>
#include <lwip/tcpip.h>
#include <sys/time.h>

// The pinned driver defines fallback macros over IDF enum identifiers, with
// incorrect values for STOP/CONNECTED/DISCONNECTED. Use native events and the
// real IDF enums here, independently of the driver's Arduino event forwarding.
#undef ETHERNET_EVENT_START
#undef ETHERNET_EVENT_STOP
#undef ETHERNET_EVENT_CONNECTED
#undef ETHERNET_EVENT_DISCONNECTED

EthernetTimeSync ethernetTimeSync;

namespace {
const char* const PUBLIC_SERVERS[] = {"time.cloudflare.com", "time.nist.gov"};
NetworkPrefs active;
TimeSyncPolicy policy; // Only accessed on the TCP/IP thread.
ip_addr_t dhcpServers[SNTP_MAX_SERVERS];
uint8_t dhcpCount = 0;
bool dhcpChanged = false, wasReady = false;
uint32_t leaseGeneration = 0;
std::atomic<bool> initialized{false}, linkUp{false}, addressReady{false}, polling{false}, configured{false};
std::atomic<uint32_t> disconnects{0};
uint32_t seenDisconnects = 0;
portMUX_TYPE stateLock = portMUX_INITIALIZER_UNLOCKED;
struct State {
  bool synced = false, holdover = false;
  uint32_t sequence = 0, lastSync = 0;
  TimeSyncPolicy::Source source = TimeSyncPolicy::NONE;
  const char* error = "starting";
} state;

void publish(const char* error = nullptr) {
  portENTER_CRITICAL(&stateLock);
  state.synced = policy.synced;
  state.holdover = policy.holdover;
  state.lastSync = policy.lastSync;
  state.source = policy.source;
  if (error) state.error = error;
  portEXIT_CRITICAL(&stateLock);
}
State snapshot() {
  portENTER_CRITICAL(&stateLock);
  State copy = state;
  portEXIT_CRITICAL(&stateLock);
  return copy;
}
void error(const char* message) {
  portENTER_CRITICAL(&stateLock);
  state.error = message;
  portEXIT_CRITICAL(&stateLock);
}

void startAttempt(TimeSyncPolicy::Source source) {
  sntp_stop();
  ip_addr_t empty{};
  for (unsigned i = 0; i < SNTP_MAX_SERVERS; ++i) {
    sntp_setservername(i, nullptr);
    sntp_setserver(i, &empty);
  }
  if (source == TimeSyncPolicy::OVERRIDE) {
    ip_addr_t address;
    if (ipaddr_aton(active.server, &address)) sntp_setserver(0, &address);
    else sntp_setservername(0, active.server);
  } else if (source == TimeSyncPolicy::DHCP) {
    for (unsigned i = 0; i < dhcpCount; ++i) sntp_setserver(i, &dhcpServers[i]);
  } else {
    for (unsigned i = 0; i < 2 && i < SNTP_MAX_SERVERS; ++i) sntp_setservername(i, PUBLIC_SERVERS[i]);
  }
  sntp_init();
  publish();
}

void configureSntp(void*) {
  sntp_stop();
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_servermode_dhcp(active.automatic() && active.dhcp());
  sntp_set_sync_interval(TimeSyncPolicy::SYNC_MS);
  configured.store(true);
}

void tick(void*) {
  uint32_t now = millis();
  uint32_t generation = disconnects.load();
  if (generation != seenDisconnects) {
    seenDisconnects = generation;
    // DHCP notifications carry their own lease snapshot; the link event invalidates
    // a previous lease immediately through the generation checked by the wrapper.
    if (leaseGeneration != generation) { dhcpCount = 0; dhcpChanged = false; }
    wasReady = false;
    sntp_stop();
    policy.disconnect();
  }
  bool ready = initialized.load() && linkUp.load() && addressReady.load();
  if (!ready) {
    if (wasReady) sntp_stop();
    policy.disconnect();
    wasReady = false;
    publish(linkUp.load() ? "waiting for address" : "link down");
  } else if (!wasReady || dhcpChanged) {
    dhcpChanged = false;
    wasReady = true;
    startAttempt(policy.connect(now, active.automatic(), dhcpCount != 0));
  } else {
    bool attempted = policy.attempting;
    TimeSyncPolicy::Source next = policy.tick(now);
    if (next != TimeSyncPolicy::NONE) {
      if (attempted) error("NTP/DNS timeout; fallback");
      startAttempt(next);
    } else if (attempted && !policy.attempting) {
      sntp_stop();
      publish("NTP/DNS timeout; retrying");
    } else if (!policy.attempting && sntp_enabled()) {
      sntp_stop(); // One completed attempt; policy owns the next hourly retry.
    }
    publish();
  }
  polling.store(false);
}

void networkTask(void*) {
  if (esp_netif_init() != ESP_OK) {
    error("network init failed");
    vTaskDelete(nullptr);
    return;
  }
  esp_err_t loopResult = esp_event_loop_create_default();
  if (loopResult != ESP_OK && loopResult != ESP_ERR_INVALID_STATE) {
    error("event loop init failed");
    vTaskDelete(nullptr);
    return;
  }
  auto eventHandler = [](void*, esp_event_base_t base, int32_t event, void*) {
    if (base == IP_EVENT) {
      if (event == IP_EVENT_ETH_GOT_IP) addressReady.store(true);
      if (event == IP_EVENT_ETH_LOST_IP) {
        addressReady.store(false);
        disconnects.fetch_add(1);
      }
      return;
    }
    if (event == ETHERNET_EVENT_CONNECTED) linkUp.store(true);
    if (event == ETHERNET_EVENT_DISCONNECTED || event == ETHERNET_EVENT_STOP) {
      addressReady.store(false);
      linkUp.store(false);
      disconnects.fetch_add(1);
    }
  };
  if (esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, eventHandler, nullptr) != ESP_OK ||
      esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, eventHandler, nullptr) != ESP_OK) {
    error("event registration failed");
    vTaskDelete(nullptr);
    return;
  }
  if (tcpip_callback(configureSntp, nullptr) != ERR_OK) {
    error("SNTP init queue failed");
    vTaskDelete(nullptr);
    return;
  }
  while (!configured.load()) vTaskDelay(pdMS_TO_TICKS(10));
  if (!beginCH390Network()) {
    // The pinned driver's failure path does not fully release all SPI resources;
    // repeated begin() calls could leak. Report a hardware error until reboot.
    error("CH390 init failed; reboot");
    vTaskDelete(nullptr);
    return;
  }
  if (!active.dhcp()) {
    IPAddress ip, gateway, subnet, dns;
    ip.fromString(active.ip); gateway.fromString(active.gateway);
    subnet.fromString(active.subnet); dns.fromString(active.dns);
    if (!CH390.config(ip, gateway, subnet, dns)) {
      error("static IP setup failed");
      vTaskDelete(nullptr);
      return;
    }
  }
  initialized.store(true);
  vTaskDelete(nullptr);
}
} // namespace

// Linker-wrapped only for the M7 repeater. DHCP invokes this on the TCP/IP
// thread even on same-IP lease renewals. Preserve option 42 independently of
// the active SNTP slots, whose contents change when falling back to DNS names.
extern "C" void __wrap_dhcp_set_ntp_servers(u8_t count, const ip4_addr_t* servers) {
  if (!active.automatic() || !active.dhcp()) return;
  leaseGeneration = disconnects.load();
  dhcpCount = 0;
  for (unsigned i = 0; i < count && dhcpCount < SNTP_MAX_SERVERS; ++i) {
    uint32_t host = lwip_ntohl(servers[i].addr);
    if (!NetworkPrefs::unicast(host)) continue;
    ip_addr_copy_from_ip4(dhcpServers[dhcpCount], servers[i]);
    ++dhcpCount;
  }
  dhcpChanged = true;
}

// ESP-IDF exposes this as a weak customization hook. Validate before changing
// time, not in the notification callback after an invalid date was applied.
extern "C" void sntp_sync_time(struct timeval* tv) {
  if (!wasReady || !linkUp.load() || tv->tv_sec < 0 ||
      uint64_t(tv->tv_sec) > UINT32_MAX || !TimeSyncPolicy::plausible(uint32_t(tv->tv_sec))) {
    error("invalid NTP time");
    return;
  }
  if (!policy.attempting) return;
  if (settimeofday(tv, nullptr) != 0) { error("clock update failed"); return; }
  policy.success(millis(), uint32_t(tv->tv_sec));
  sntp_set_sync_status(SNTP_SYNC_STATUS_COMPLETED);
  portENTER_CRITICAL(&stateLock);
  ++state.sequence;
  portEXIT_CRITICAL(&stateLock);
  publish("none");
}

void EthernetTimeSync::begin(fs::FS& fs) {
  _fs = &fs;
  if (!loadNetworkPrefs(fs, _saved) && (fs.exists("/network.json") || fs.exists("/network.bak")))
    error("invalid saved config; defaults");
  active = _saved;
  if (xTaskCreate(networkTask, "eth_time", 6144, nullptr, 1, nullptr) != pdPASS)
    error("network task failed");
}

void EthernetTimeSync::loop() {
  if (!initialized.load() || millis() - _lastPoll < 100) return;
  _lastPoll = millis();
  if (polling.exchange(true)) return;
  if (tcpip_try_callback(tick, nullptr) != ERR_OK) polling.store(false);
}
bool EthernetTimeSync::isSynced() const { return snapshot().synced; }
uint32_t EthernetTimeSync::syncSequence() const { return snapshot().sequence; }

bool EthernetTimeSync::handleCommand(const char* command, char* reply, size_t size) {
  if (!strcmp(command, "eth.status")) {
    snprintf(reply, size, "ETH: %s ip=%s dns=%s mode=%s%s", linkUp.load() ? "link up" : "link down",
      (initialized.load() ? CH390.localIP() : IPAddress()).toString().c_str(),
      (initialized.load() ? CH390.dnsIP() : IPAddress()).toString().c_str(), active.mode,
      _pendingReboot ? " (saved changes; reboot required)" : "");
    return true;
  }
  if (!strcmp(command, "ntp.status")) {
    State s = snapshot();
    const char* names[] = {"none", "override", "dhcp", "public"};
    snprintf(reply, size, "NTP: %s source=%s last=%lu error=%s%s",
      !s.synced ? "never_synced" : (s.holdover ? "holdover" : "synced"), names[s.source],
      (unsigned long)s.lastSync, s.error, _pendingReboot ? "; reboot required" : "");
    return true;
  }
  if (!strncmp(command, "get ", 4)) {
    const char* value = _saved.get(command + 4);
    if (!value) return false;
    snprintf(reply, size, "> %s", value);
    return true;
  }
  if (strncmp(command, "set ", 4)) return false;
  const char* separator = strchr(command + 4, ' ');
  if (!separator) return false;
  size_t length = separator - (command + 4);
  if (length >= 24) return false;
  char key[24];
  memcpy(key, command + 4, length); key[length] = 0;
  if (!_saved.get(key)) return false;
  NetworkPrefs candidate = _saved;
  if (!candidate.set(key, separator + 1)) {
    snprintf(reply, size, "ERR: invalid/incomplete network setting");
    return true;
  }
  if (!saveNetworkPrefs(*_fs, candidate)) {
    snprintf(reply, size, "ERR: saving network settings failed");
    return true;
  }
  _saved = candidate;
  _pendingReboot = true;
  snprintf(reply, size, "OK - saved; reboot required");
  return true;
}
#endif
