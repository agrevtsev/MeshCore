## ADDED Requirements

### Requirement: Persistent Ethernet configuration
The M7 repeater SHALL support DHCP and static IPv4 with gateway, subnet, and DNS configuration through existing CLI access. Valid settings SHALL persist across reboot; invalid settings SHALL leave saved settings unchanged. Configuration changes SHALL report that reboot is required for activation. Defaults SHALL be DHCP and automatic NTP. Configuration SHALL use `get <key>` and `set <key> <value>` with keys `eth.mode`, `eth.ip`, `eth.gateway`, `eth.subnet`, `eth.dns`, and `ntp.server`; `eth.mode` SHALL accept `dhcp` or `static`.

#### Scenario: Static configuration survives reboot
- **WHEN** a user saves complete valid static settings and reboots
- **THEN** the repeater uses those IP, subnet, gateway, and DNS settings

#### Scenario: Invalid configuration
- **WHEN** a user supplies an invalid address or selects static mode without complete settings
- **THEN** the CLI reports an error and preserves the previous saved configuration

#### Scenario: DHCP configuration
- **WHEN** DHCP mode boots on a network offering an address and DNS servers
- **THEN** the repeater acquires its address and uses the supplied DNS for hostname resolution

### Requirement: Automatic NTP selection
The persisted NTP setting `0.0.0.0` SHALL select automatic mode. DHCP-provided NTP SHALL take precedence over public hostname fallback. DHCP NTP acceptance SHALL be configured before address acquisition. Absent DHCP NTP SHALL select public fallback; unreachable supplied servers SHALL cause fallback after a bounded attempt window. Automatic mode with static IP SHALL use public fallback directly.

#### Scenario: DHCP provides working NTP
- **WHEN** automatic mode receives a DHCP NTP server and it responds within the attempt window
- **THEN** synchronization uses that server before attempting public fallback

#### Scenario: DHCP NTP is absent or unreachable
- **WHEN** DHCP supplies no NTP server or supplied servers fail within the attempt window
- **THEN** the repeater resolves and attempts public NTP fallback without blocking forwarding

#### Scenario: Static address with automatic NTP
- **WHEN** static mode boots with `ntp.server` set to `0.0.0.0`
- **THEN** the repeater uses configured DNS to attempt public NTP without waiting for DHCP

### Requirement: Exclusive NTP override
An explicit non-sentinel IPv4 address or hostname SHALL select that server exclusively. DHCP NTP acceptance SHALL be disabled in this mode. Unreachable explicit servers SHALL be retried without DHCP or public fallback.

#### Scenario: Override fails
- **WHEN** an explicit server is configured and cannot be resolved or reached
- **THEN** the repeater retries it with backoff and does not contact another NTP server

### Requirement: Synchronization lifecycle and diagnostics
The repeater SHALL periodically resynchronize and retry failures using bounded backoff and monotonic scheduling. Status commands SHALL be standalone `eth.status` and `ntp.status`, preserving the existing Ethernet command spelling. Status SHALL distinguish never synchronized, synchronized, and holdover, and expose address/link state, selected source, last successful synchronization when available, and latest failure. Lease renewal and link recovery SHALL preserve source-selection policy. Ethernet SHALL NOT expose a CLI or companion TCP listener for this feature.

#### Scenario: Outage after synchronization
- **WHEN** Ethernet or NTP becomes unavailable after successful synchronization
- **THEN** status indicates holdover and the clock continues running while synchronization is retried

#### Scenario: Link recovers
- **WHEN** connectivity recovers or DHCP supplies changed NTP information
- **THEN** synchronization retries honor automatic selection or the exclusive override as configured

#### Scenario: Synchronization-only networking
- **WHEN** Ethernet is initialized
- **THEN** DNS and SNTP can operate without opening an Ethernet management or companion server

#### Scenario: Established status command spelling
- **WHEN** a user enters `eth.status` on the M7 repeater
- **THEN** the CLI reports Ethernet state without requiring a `get` prefix or implying an Ethernet management listener
