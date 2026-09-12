## ADDED Requirements

### Requirement: First synchronization gates all self-adverts
The enabled M7 repeater SHALL suppress boot, periodic, and manually requested self-advertisements until successful SNTP synchronization during the current boot. A plausible preexisting date or manual clock setting SHALL NOT satisfy the gate. Suppression SHALL occur before advert packet creation and SHALL persist indefinitely if synchronization fails.

#### Scenario: Boot without NTP
- **WHEN** the repeater boots and no NTP server responds
- **THEN** it creates no self-advertisements regardless of elapsed time or periodic deadlines

#### Scenario: Manual advert before synchronization
- **WHEN** a user requests an advert before successful SNTP synchronization
- **THEN** the CLI reports suppression and no advert is created or queued

#### Scenario: First successful synchronization
- **WHEN** the first successful SNTP synchronization occurs
- **THEN** the repeater permits newly created self-adverts with synchronized timestamps, emits one deferred startup zero-hop advert if boot adverts are enabled, and resumes periodic scheduling without a backlog

### Requirement: Forwarding remains active
The repeater SHALL continue ordinary transit forwarding before synchronization and during network failures. Network operations SHALL NOT block the mesh loop. Relayed payload timestamps SHALL remain those of the originating packets.

#### Scenario: Transit traffic while unsynchronized
- **WHEN** an otherwise forwardable packet arrives while DHCP, DNS, or NTP is unavailable
- **THEN** normal routing and forwarding rules apply without rewriting the origin timestamp

### Requirement: Holdover and fresh boot
Successful synchronization SHALL keep the advert gate open through later network outages. Every reboot or power cycle SHALL require fresh SNTP synchronization.

#### Scenario: Network disappears after sync
- **WHEN** a previously synchronized repeater loses connectivity
- **THEN** scheduled self-adverts continue using its running clock

#### Scenario: Power cycle
- **WHEN** a previously synchronized repeater powers on again
- **THEN** self-adverts remain suppressed until a new successful SNTP synchronization

### Requirement: Invalid clock reporting and clock corrections
Before synchronization, the repeater SHALL NOT originate mesh replies presenting its local clock as valid time. Where the wire format lacks an invalid-time indicator, it SHALL suppress those replies while keeping local CLI diagnostics available. Time corrections SHALL NOT corrupt interval bookkeeping or disable peer replay protections. Self-advert timestamps SHALL NOT regress within a boot; backward corrections SHALL defer adverts until actual time exceeds the last emitted advert timestamp.

#### Scenario: Clock-bearing request before sync
- **WHEN** a peer requests a response containing the repeater's local clock before synchronization
- **THEN** the repeater suppresses that response and local CLI status reports that synchronization is pending

#### Scenario: Backward time correction
- **WHEN** a later synchronization corrects time behind the last emitted self-advert timestamp
- **THEN** new self-adverts wait until corrected time exceeds that timestamp, forwarding continues, and peer replay checks remain enabled
