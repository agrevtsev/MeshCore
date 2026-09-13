# Validation record

## Automated checks

- Full PlatformIO `native` suite: **51 tests passed** across mesh tables, configuration serialization, utilities, UTF-8 helpers, network/time policy, and routing policy. The existing companion preference suite contains no enabled tests.
- New network/time suite: **11 tests passed**, including repeated persistence, failed writes/renames, interrupted commit recovery, configuration validation, DHCP-first/public selection, exclusive override failure, first-sync/holdover/reboot behavior, backward corrections, and monotonic timer wrap. Re-run successfully after rejecting ambiguous leading-zero IPv4 strings.
- `openspec validate add-m7-ethernet-time-sync --strict`: passed.
- `git diff --check`: passed.

Native tests exercise configuration/storage and policy decisions. They do not emulate the actual CH390, DHCP packets, SNTP network exchange, or radio transmissions. The existing routing tests and code-path review support the forwarding behavior; on-device end-to-end verification is still required.

## Firmware builds

| Target | Result |
| --- | --- |
| ThinkNode_M7_repeater | Passed |
| ThinkNode_M7_companion_radio_ethernet | Passed |
| ThinkNode_M7_companion_radio_ble | Passed |
| ThinkNode_M7_room_server | Passed |
| ThinkNode_M6_repeater | Passed |

The M7 uses PlatformIO Espressif32 6.11.0, framework package `3.20017.241212+sha.dcc1105b` (Arduino-ESP32 2.0.17), and CH390 commit `47b401f1de546118b03c18b5689dedec45871f2d`. Its qio_opi SDK configuration enables DHCP NTP and three SNTP server slots. Symbol inspection confirms the M7 repeater links the DHCP notification wrapper and custom SNTP time-validation hook.

Two supporting fixes were needed: an explicit standard-library include in ConfigSerializer for native builds, and the missing NullDisplayDriver source entry in the M7 Ethernet companion target. Concurrent PlatformIO runs interfered with build-directory cleanup; final firmware builds were run sequentially and passed.

## Pending device acceptance

No M7 USB serial device was visible during implementation, and no hardware was flashed. OpenSpec tasks 6.1–6.3 remain pending. Follow [the hardware acceptance checklist](../../../docs/m7_ethernet_time_sync.md#hardware-acceptance-checklist) and record USB logs, DHCP option 42/UDP 123 captures, and mesh packet timestamps before archiving this change.

In particular, verify same-IP DHCP renewal, static DNS, server fallback timing, relaying with no network, self-advert suppression/release, holdover, clock-bearing management responses, and identity preservation across rollback. An initial CH390 hardware initialization failure is reported and requires reboot; retries after partial driver initialization are avoided because the pinned driver does not completely clean up that failure path.

Final driver review identified incorrect Arduino Ethernet event forwarding in the pinned CH390 library. The M7 time service uses native ESP-IDF events with the correct enum values; physical cable/reconnect validation remains part of device acceptance.
