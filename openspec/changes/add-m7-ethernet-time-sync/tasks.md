## 1. Verify pinned framework integration

- [ ] 1.1 Resolve the M7's pinned dependencies and confirm SNTP callback, DHCP option 42, server-slot, and DHCP renewal behavior in Arduino-ESP32 2.0.17 and the pinned CH390 driver.
- [ ] 1.2 Trace all repeater self-advert and clock-bearing response paths, correction-sensitive bookkeeping, and available test harnesses; record any required design adjustments.

## 2. Ethernet and persistent CLI settings

- [ ] 2.1 Separate reusable CH390 network initialization (including explicit DNS and link/address status independent of TCP-client state) from companion TCP service startup and wire the M7 repeater build without the nRF52 Ethernet CLI.
- [ ] 2.2 Reuse ConfigSerializer for versioned persistent network settings with DHCP/automatic defaults, validated static addressing and DNS, and exclusive IPv4/hostname NTP override.
- [ ] 2.3 Add CLI get/set commands for the specified keys, standalone `eth.status` and `ntp.status`, saved-versus-active reporting, reboot-required responses, and network/sync status under existing CLI authorization.
- [ ] 2.4 Implement asynchronous link management, DHCP/static configuration, DNS selection, and reconnect handling without blocking the mesh loop.

## 3. SNTP selection and lifecycle

- [ ] 3.1 Initialize SNTP policy before DHCP; implement `0.0.0.0` automatic mode and explicit exclusive mode.
- [ ] 3.2 Implement DHCP-first selection, bounded unreachable-server fallback, static-mode public selection, backoff, periodic resync, and policy-preserving lease renewal.
- [ ] 3.3 Add safe sync notification handling, plausible-time validation, UTC system-clock integration, and never-synced/synced/holdover status.

## 4. Advert and clock behavior

- [ ] 4.1 Gate all M7 self-advert creation on successful SNTP in the current boot and return accurate manual-advert suppression responses.
- [ ] 4.2 Release one deferred boot advert when enabled after first sync, restart periodic scheduling without backlog, and retain holdover behavior across outages.
- [ ] 4.3 Suppress invalid clock-bearing mesh replies before sync while preserving local CLI access and ordinary transit forwarding.
- [ ] 4.4 Handle first and subsequent clock corrections in neighbor ages, rate limits, and unique timestamps; defer adverts after backward corrections and preserve peer replay protection.

## 5. Automated and build validation

- [ ] 5.1 Test canonical CLI command spellings, configuration validation/persistence (including overlength hostnames), and source selection, including explicit-server failure without fallback and DHCP absence/unreachability.
- [ ] 5.2 Test first-sync gating across boot/manual/periodic paths, holdover, reboot, backward correction, and forwarding during network failures.
- [ ] 5.3 Build the M7 repeater and affected M7 companion targets; run relevant existing checks and a representative unaffected repeater build if shared code changes.

## 6. Hardware acceptance and documentation

- [ ] 6.1 On the user's M7, validate static/DHCP addressing, DNS, supplied DHCP NTP option 42, lease renewal, public fallback, and exclusive override with USB logs and controlled network evidence.
- [ ] 6.2 Verify cold-boot indefinite suppression with active relaying, first-sync advert timestamps, missing cable/NTP failure, reconnect, holdover adverts, and fresh gating after power cycling.
- [ ] 6.3 Verify no Ethernet management listener is started and existing identity/mesh preferences survive configuration and firmware rollback.
- [ ] 6.4 Document CLI commands, defaults, retry/source semantics, reboot activation, pre-sync management limitations, and public fallback service suitability; record hardware results and remaining limitations.
