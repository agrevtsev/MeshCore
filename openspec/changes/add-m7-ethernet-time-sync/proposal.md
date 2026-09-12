## Why

The ThinkNode M7 has Ethernet but no battery-backed RTC in the target installation. A repeater must obtain accurate time after power-on before creating its own advertisements, while continuing to relay other nodes' traffic.

## What Changes

- Enable CH390 Ethernet for the M7 repeater with persistent CLI configuration for DHCP or static IPv4, gateway, subnet, and DNS.
- Use Ethernet for DNS and SNTP synchronization without exposing an Ethernet administration or companion server.
- Persist an NTP server setting: `0.0.0.0` selects DHCP-provided NTP first with public fallback; any explicit IPv4 address or hostname selects that server exclusively.
- Suppress boot, periodic, and manual self-advertisements until successful SNTP synchronization in the current boot. Keep forwarding active throughout.
- Continue advertising using the running clock during subsequent network outages, retry synchronization, and require fresh synchronization after a power cycle.
- Expose network and synchronization status through the existing CLI and avoid presenting unsynchronized time as a valid clock to mesh peers.

## Capabilities

### New Capabilities

- `m7-ethernet-time-sync`: Persistent Ethernet configuration, DNS, NTP selection, synchronization lifecycle, and diagnostics for the M7 repeater.
- `repeater-synchronized-adverts`: Gate self-advertisements on first synchronization while preserving relaying and handling unsynchronized clock responses.

### Modified Capabilities

None. No existing OpenSpec capability specifications are present.

## Impact

- M7 PlatformIO repeater configuration and CH390 network initialization.
- Repeater startup, main loop, advert creation, clock-bearing replies, and CLI configuration/status paths.
- Persistent settings and handling of bookkeeping affected by clock corrections.
- Reuse the pinned PlatformIO Espressif32 6.11.0 platform (Arduino-ESP32 2.0.17, ESP-IDF 4.4.7) and existing pinned ESP32-CH390 dependency; verify on hardware.
- Other boards and companion/room-server behavior remain outside this change's scope.
