## Context

The M7 repeater inherits PlatformIO Espressif32 6.11.0 and Arduino-ESP32 2.0.17 (ESP-IDF 4.4.7). Its companion builds already integrate the pinned CH390 driver, but repeater Ethernet currently includes an nRF52-specific CLI implementation. CH390 initialization currently starts a companion TCP server. The M7 clock falls back to ESP32 system time when no external RTC is detected.

Arduino 2.0.17's [SimpleTime example](https://github.com/espressif/arduino-esp32/blob/2.0.17/libraries/ESP32/examples/Time/SimpleTime/SimpleTime.ino) demonstrates SNTP callbacks and DHCP option 42. DHCP NTP acceptance must be enabled before DHCP address acquisition, and a later configTime call can overwrite acquired servers. Actual CH390 integration still requires build and device validation.

## Goals / Non-Goals

**Goals:** Persistent Ethernet and NTP CLI settings; DNS resolution; explicit source selection and retries; correct self-advert timestamps after power-on; uninterrupted ordinary forwarding; useful diagnostics.

**Non-Goals:** Ethernet administration, companion TCP service, Wi-Fi provisioning, new RTC hardware, room-server/companion feature changes, authenticated NTP, or framework migration unless evidence requires it.

## Decisions

### Reuse the network stack and separate network startup from TCP services

Extract or share the minimum CH390 initialization needed by a repeater-owned network/time service, preserving existing companion behavior. Do not enable the existing nRF52 Ethernet CLI on ESP32. Reuse lwIP DNS/SNTP rather than implementing packet protocols. Keep networking asynchronous so absent cables, DHCP, DNS, and NTP cannot block the mesh loop.

### Persistent settings through existing CLI

Use the existing `ConfigSerializer` and filesystem persistence pattern for a separate versioned network settings record, avoiding a new serialization dependency or changes to unrelated boards' mesh preferences. Bound hostname lengths to the CLI and serializer token limits, and reject overlength input instead of truncating it. Default to DHCP and NTP `0.0.0.0`. Proposed commands follow existing conventions: `get/set eth.mode`, `eth.ip`, `eth.gateway`, `eth.subnet`, `eth.dns`, `ntp.server`, and `eth.status` / `ntp.status`. Validate before saving. Save settings immediately and report that a reboot is required to apply changes; live network reconfiguration is deferred. Static mode requires usable IP/subnet/gateway/DNS settings, and switching to it must reject incomplete configuration. CLI access uses existing transport and authorization rules.

### Existing-code audit and CLI compatibility

- `src/helpers/nrf52/EthernetCLI.h` and `docs/cli_commands.md` already define the standalone `eth.status` command for RAK4631 repeater/room-server Ethernet. Preserve that spelling on M7; use standalone `ntp.status` for matching diagnostics. M7 status reports link/address information without implying a listening TCP port. Existing RAK output remains unchanged.
- `src/helpers/CommonCLI.cpp` establishes `get <key>` / `set <key> <value>` for configuration. New keys are `eth.mode` (`dhcp` or `static`), `eth.ip`, `eth.gateway`, `eth.subnet`, `eth.dns`, and `ntp.server`. These are new keys, not previously supported settings. Do not introduce `get eth.status` as the canonical command.
- RAK13800 companion Ethernet already supports `ETHERNET_STATIC_IP`, `ETHERNET_STATIC_GATEWAY`, `ETHERNET_STATIC_SUBNET`, and `ETHERNET_STATIC_DNS` build flags, otherwise DHCP. Its SPI/W5100S implementation cannot directly drive the M7 CH390. Reuse its configuration vocabulary and DHCP-maintenance expectations, not its board-specific hardware code.
- The existing M7 CH390 companion driver already supports DHCP and static IP/gateway/subnet build flags, but its static configuration call does not pass explicit DNS. Extend the reusable network initialization to accept runtime DNS as well as runtime addressing.
- Companion Wi-Fi uses build-time `WIFI_SSID` / `WIFI_PWD` in `examples/companion_radio/main.cpp`; `SerialWifiInterface::begin()` already separates TCP startup from Wi-Fi setup. There is no persistent Wi-Fi/IP configuration CLI in the inspected paths to adopt.
- Ethernet `isConnected()` currently means a connected companion TCP client. The time service must observe link/address events directly; it must not wait for a companion connection to start SNTP.
- Existing network command interception on the RAK is in local serial/TCP startup code. New M7 command integration must preserve existing authorization for any mesh-accessible configuration and must not inadvertently expose unauthenticated management.

### Source selection

An explicit IPv4 address or DNS hostname is exclusive: disable DHCP NTP acceptance and never switch to public or DHCP servers on failure. `0.0.0.0` enables automatic selection. In automatic DHCP mode, enable option 42 before starting DHCP, preserve acquired addresses, and try them first. If absent, use public hostname fallback immediately after address acquisition; if supplied but unreachable, advance after a bounded 30-second source-attempt window. In static mode, automatic selection uses public fallback directly. Use `time.cloudflare.com` and `time.nist.gov` as public fallback candidates. Cloudflare documents its public NTP endpoint at https://developers.cloudflare.com/time-services/ntp/usage/. Avoid embedding the generic NTP Pool hostname in distributed firmware: the pool asks vendors to obtain a vendor zone (https://www.ntppool.org/en/vendors.html).

Retry failed selection cycles with backoff from 30 seconds to a 15-minute cap; do not busy-loop or retry every mesh iteration. Link recovery and changed DHCP server information trigger a new selection cycle, respecting the exclusive override. Maintain automatic source preference on subsequent cycles. Re-synchronize approximately hourly after success. Implement timers using monotonic uptime. Exact lwIP server-slot replacement and lease-renewal interaction must be verified against the pinned implementation; do not assume the example alone supplies failover policy.

### Synchronization lifecycle

Track `never_synced`, `synced`, and `holdover` (previously synced but resynchronization/network currently unavailable). Start each firmware boot at `never_synced`, even if the system clock happens to retain a plausible date. Only a successful SNTP response with a plausible timestamp unlocks adverts; manual time commands do not unlock the gate. Use UTC epoch time. A callback records an event safely; mesh-owned state changes happen in the main loop. The current M7 fallback reads system time directly, so avoid maintaining a second independent software clock.

On first sync, release one deferred startup zero-hop advert if boot adverts are enabled, and restart periodic scheduling without replaying suppressed requests. Holdover keeps adverts enabled and retries synchronization. No outage duration re-closes the gate. A reboot or power cycle starts a fresh gate.

### Gate locally originated adverts and clock reporting

Guard createSelfAdvert centrally for the enabled M7 feature, covering startup, periodic, and manual callers. Return an honest CLI suppression result, rather than an unconditional success. Gate before packet creation to avoid queued packets retaining invalid timestamps. Preserve ordinary forwarding, routing, and packet payload timestamps.

Before synchronization, suppress locally generated clock-bearing responses where the existing wire format has no validity marker, including anonymous clock/owner/region and login replies carrying local time; local CLI status explains the unsynchronized state. USB CLI remains available. This means some mesh management interactions wait for synchronization, while transit relaying continues.

Audit neighbor age records, rate limiters, and unique timestamp tracking around first sync and later backward corrections. Use monotonic elapsed time for local interval accounting where practical, or reset affected transient bookkeeping on a correction; never turn existing peer replay protections off. Ensure self-advert timestamps do not regress within a boot: after a backward correction, defer new self-adverts until actual time exceeds the last emitted timestamp instead of inventing a future timestamp. Cross-power-cycle recovery from a previously incorrect future timestamp is outside this change.

## Risks / Trade-offs

- CH390 link/DHCP behavior differs from the Wi-Fi example → compile with pinned dependencies and verify option 42 through a controlled DHCP server and packet capture.
- Public DNS/NTP may be unavailable → bounded retries, visible status, and indefinite pre-sync advert suppression.
- Standard SNTP trusts the selected server → exclusive override supports deliberate server selection; authenticated time is outside scope.
- Time jumps disturb local bookkeeping → targeted correction tests and monotonic interval accounting; retain replay checks.
- Blocking helpers or callback races disrupt forwarding → keep network waits out of the main loop and marshal notifications safely.
- Network changes require reboot → simpler, predictable persistence and activation; CLI explicitly reports this behavior.

## Migration Plan

Enable the feature for the M7 repeater build only and preserve unrelated builds. Missing settings use DHCP/automatic NTP defaults. Document the new startup gate before flashing. Validate on the user's M7 with USB logs and a controlled network. Roll back by flashing the previous firmware; isolated network settings must not change identity or existing mesh preferences.

## Open Questions

No user requirement blocks proposal completion. Implementation must verify CH390 option 42 handling, server-slot behavior during renewal, and the existing test harness best suited to clock corrections. These are validation tasks, not reasons to assume a framework rebuild is necessary.

## Implementation evidence

The resolved framework package is `3.20017.241212+sha.dcc1105b`. Its M7 qio_opi sdkconfig enables DHCP NTP and three SNTP server slots. The compiled lwIP archive exports `dhcp_set_ntp_servers` and a weak `sntp_sync_time`. Use an M7-only linker wrapper for DHCP's NTP notification to retain the current lease's addresses independently of active SNTP slots (including same-IP renewals). Keep selection and all raw lwIP SNTP operations on the TCP/IP thread. A weak SNTP time-application override validates time before changing the system clock; main-loop notifications handle mesh bookkeeping. This avoids modifying/rebuilding the bundled framework.

The pinned CH390 `linkUp()` actually tests for an address; use Ethernet connect/disconnect events instead. Driver startup runs in a background task because hardware initialization may wait. Existing peer CLI replies also carry local time; suppress these before first sync in addition to anonymous replies. Existing native GoogleTest/ConfigSerializer mocks support pure configuration and policy tests.

Settings persistence uses temporary and backup records because SPIFFS rename does not replace an existing destination. Read back the temporary record before moving the current record; recover the backup if boot finds an interrupted commit. Native tests cover repeated saves and failed writes/renames.

The pinned CH390 header also incorrectly overlays native Ethernet event enum names with fallback macros (CONNECTED=1 instead of 2, DISCONNECTED=2 instead of 3, STOP=3 instead of 1). The time service registers directly for ESP-IDF ETH_EVENT/IP_EVENT notifications and undefines those macros in its own translation unit, preserving the native enum values without changing the dependency or companion behavior.
