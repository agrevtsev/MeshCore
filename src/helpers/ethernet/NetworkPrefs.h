#pragma once

#include <helpers/ConfigSerializer.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Kept independent of the ESP32 network stack for validation and native tests.
class NetworkPrefs : public ConfigSerializer {
protected:
  void structure() override {
    def("version", version);
    def("mode", mode, sizeof(mode));
    def("ip", ip, sizeof(ip));
    def("gateway", gateway, sizeof(gateway));
    def("subnet", subnet, sizeof(subnet));
    def("dns", dns, sizeof(dns));
    def("server", server, sizeof(server));
  }

public:
  uint8_t version = 1;
  char mode[8] = "dhcp";
  char ip[16] = "0.0.0.0";
  char gateway[16] = "0.0.0.0";
  char subnet[16] = "0.0.0.0";
  char dns[16] = "0.0.0.0";
  char server[96] = "0.0.0.0";

  bool dhcp() const { return strcmp(mode, "dhcp") == 0; }
  bool automatic() const { return strcmp(server, "0.0.0.0") == 0; }

  static bool parseIPv4(const char* s, uint32_t& result) {
    result = 0;
    for (int i = 0; i < 4; ++i) {
      const char* start = s;
      unsigned n = 0, digits = 0;
      while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s++ - '0');
        if (++digits > 3 || n > 255) return false;
      }
      // Arduino/lwIP address parsers can interpret leading zeroes as octal.
      // Accept only canonical decimal octets to keep validation and use identical.
      if (!digits || (digits > 1 && *start == '0')) return false;
      result = (result << 8) | n;
      if (i < 3) { if (*s++ != '.') return false; }
      else if (*s != 0) return false;
    }
    return true;
  }

  static bool unicast(uint32_t ip) {
    return ip != 0 && (ip >> 24) != 0 && (ip >> 24) != 127 && (ip >> 24) < 224;
  }

  static bool validServer(const char* value) {
    if (!*value || strlen(value) >= sizeof(server)) return false;
    uint32_t addr;
    if (parseIPv4(value, addr)) return addr == 0 || unicast(addr);
    // Reject malformed numeric addresses rather than treating them as hostnames.
    if (strspn(value, "0123456789.") == strlen(value)) return false;
    unsigned label = 0;
    char previous = 0;
    for (const char* p = value; *p; ++p) {
      char c = *p;
      if (c == '.') {
        if (!label || previous == '-') return false;
        label = 0;
      } else {
        bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!alnum && c != '-') return false;
        if ((!label && c == '-') || ++label > 63) return false;
      }
      previous = c;
    }
    return label > 0 && previous != '-';
  }

  bool valid() const {
    if (version != 1 || (!dhcp() && strcmp(mode, "static") != 0) || !validServer(server)) return false;
    uint32_t a, g, m, d;
    if (!parseIPv4(ip, a) || !parseIPv4(gateway, g) || !parseIPv4(subnet, m) || !parseIPv4(dns, d)) return false;
    uint32_t hostmask = ~m;
    if (m && (hostmask & (hostmask + 1))) return false;
    if ((a && !unicast(a)) || (g && !unicast(g)) || (d && !unicast(d))) return false;
    if (dhcp()) return true; // Permit staging static settings before selecting static mode.
    return a && g && d && m && hostmask > 1 && (a & m) == (g & m)
      && (a & hostmask) && (a & hostmask) != hostmask
      && (g & hostmask) && (g & hostmask) != hostmask && a != g;
  }

  const char* get(const char* key) const {
    if (!strcmp(key, "eth.mode")) return mode;
    if (!strcmp(key, "eth.ip")) return ip;
    if (!strcmp(key, "eth.gateway")) return gateway;
    if (!strcmp(key, "eth.subnet")) return subnet;
    if (!strcmp(key, "eth.dns")) return dns;
    if (!strcmp(key, "ntp.server")) return server;
    return nullptr;
  }

  bool set(const char* key, const char* value) {
    NetworkPrefs candidate = *this;
    char* dest = nullptr;
    size_t size = 0;
    if (!strcmp(key, "eth.mode")) { dest = candidate.mode; size = sizeof(mode); }
    else if (!strcmp(key, "eth.ip")) { dest = candidate.ip; size = sizeof(ip); }
    else if (!strcmp(key, "eth.gateway")) { dest = candidate.gateway; size = sizeof(gateway); }
    else if (!strcmp(key, "eth.subnet")) { dest = candidate.subnet; size = sizeof(subnet); }
    else if (!strcmp(key, "eth.dns")) { dest = candidate.dns; size = sizeof(dns); }
    else if (!strcmp(key, "ntp.server")) { dest = candidate.server; size = sizeof(server); }
    if (!dest || strlen(value) >= size) return false;
    strcpy(dest, value);
    if (!candidate.valid()) return false;
    *this = candidate;
    return true;
  }
};
