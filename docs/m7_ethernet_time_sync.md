# ThinkNode M7 repeater Ethernet time synchronization

The `ThinkNode_M7_repeater` build uses the M7's CH390 Ethernet controller for DNS and SNTP. It starts relaying immediately, but creates no self-advertisements until SNTP succeeds in the current boot. There is no Ethernet CLI or companion TCP listener.

Configure it through the existing USB serial CLI (115200 baud). Existing authenticated mesh CLI access is available after synchronization; before synchronization, clock-bearing mesh replies, including login and text CLI replies, are suppressed. Transit relaying continues under the normal routing rules.

## Configuration

All settings are saved immediately and take effect after `reboot`. `get` returns the saved value; status describes the running configuration and indicates pending changes. Invalid input and incomplete static configurations are rejected without changing saved settings.

| Command | Meaning |
| --- | --- |
| `eth.status` | Ethernet link, active IPv4/DNS/mode, pending reboot |
| `ntp.status` | Synchronization state, source category, last successful UTC epoch, latest error |
| `get eth.mode` | Saved `dhcp` or `static` |
| `set eth.mode dhcp` | Acquire IPv4 and DNS through DHCP (default) |
| `set eth.ip 192.168.1.50` | Stage a static IPv4 address |
| `set eth.gateway 192.168.1.1` | Stage the static gateway |
| `set eth.subnet 255.255.255.0` | Stage the static subnet mask |
| `set eth.dns 192.168.1.1` | Stage the static DNS server |
| `set eth.mode static` | Select complete static settings |
| `get ntp.server` | Read the saved server selection |
| `set ntp.server 0.0.0.0` | Automatic NTP selection (default) |
| `set ntp.server 192.168.1.1` | Use only this NTP server |
| `set ntp.server time.example.org` | Use only this NTP hostname |

Every setting key supports both `get <key>` and `set <key> <value>`. Hostnames are limited to 95 characters. Stage static address, gateway, subnet, and DNS before selecting static mode. To replace an entire static subnet, first select DHCP in the saved configuration, stage the new fields, then select static mode and reboot. The running interface remains unchanged until reboot.

## Server selection and retries

- `0.0.0.0` with DHCP: use DHCP option 42 servers first. If none are supplied, use public fallback. If supplied servers do not synchronize within a 30-second attempt window, use public fallback.
- `0.0.0.0` with static addressing: use public fallback through the configured DNS server.
- Explicit IPv4/hostname: use that server exclusively. DHCP NTP options are ignored and public fallback is disabled.
- Public fallback endpoints are `time.cloudflare.com` and `time.nist.gov`.
- Failed cycles back off from 30 seconds to a maximum of 15 minutes. Successful synchronization schedules another attempt approximately hourly. Link recovery and received DHCP NTP updates restart selection according to the saved-at-boot policy.

The implementation uses the bundled lwIP DNS and SNTP clients. A combined `NTP/DNS timeout` does not distinguish DNS failure from an unreachable NTP service. Network initialization failure is reported through `ntp.status` and requires a reboot; ordinary missing cable/DHCP/NTP conditions are handled without blocking relaying.

[Cloudflare documents its public NTP endpoint](https://developers.cloudflare.com/time-services/ntp/usage/). The generic NTP Pool hostname is intentionally not embedded: [the Pool's vendor guidance](https://www.ntppool.org/en/vendors.html) asks distributed products to obtain a vendor zone.

## Advertisement behavior

Before the first sync, boot, periodic, and manual self-adverts are suppressed indefinitely. A manually set or retained clock does not unlock this gate. The first successful sync releases one deferred zero-hop boot advert if boot adverts are enabled, then restarts normal periodic scheduling without a backlog.

After successful synchronization, loss of Ethernet/NTP changes status to `holdover`; the running clock and self-adverts continue. Every reboot or power cycle requires a new sync. After a backward time correction, new self-adverts wait until actual time exceeds the last self-advert timestamp created in that boot. Other nodes' forwarded timestamps remain unchanged. Neighbor ages and local rate-limit windows use uptime on this build.

Ethernet time synchronization inhibits power-saving sleep so forwarding and network recovery continue. Existing identity and mesh preferences are separate from `/network.json`; `/network.bak` provides recovery for interrupted network-setting saves.

## Hardware acceptance checklist

These checks require an M7 and a second mesh node or packet capture. They are not replaced by successful builds or native policy tests.

1. Record the existing node identity and mesh settings. Flash the M7 repeater application without erasing its filesystem. Connect USB serial and record `eth.status`, `ntp.status`, and `clock`.
2. Cold-boot with no cable. Wait beyond the configured local advert interval. Verify no self-advert, a manual `advert` suppression response, responsive USB CLI, and forwarding of known-good traffic between test nodes.
3. Connect Ethernet to DHCP with option 42 pointing to a controlled working NTP server. Capture DHCP/UDP 123 traffic; verify DHCP source selection, successful sync, and the first self-advert's correct UTC epoch.
4. Remove option 42 and cold-boot. Verify DNS and public fallback. Repeat with option 42 pointing to a nonresponsive server and verify fallback after its attempt window.
5. Save an explicit nonresponsive NTP IPv4 address, reboot, and verify no public/DHCP NTP traffic and indefinite self-advert suppression. Repeat with an unresolved explicit hostname. Restore `0.0.0.0` afterward.
6. Stage static IPv4/gateway/subnet/DNS, select static mode, reboot, and verify the active address and hostname-based synchronization. Confirm invalid masks, multicast addresses, and overlength hostnames are rejected.
7. After synchronization, unplug Ethernet. Verify continued self-adverts and relaying with holdover status. Reconnect, renew the lease with changed option 42 (including same-IP renewal), and confirm server selection updates.
8. Use a controlled NTP server to make a small backward correction. Verify no regressing newly created self-advert timestamps, continuing relaying, sane neighbor ages, and retained replay protection.
9. Power-cycle again and confirm fresh suppression until synchronization. Verify repeated setting saves survive reboot and no Ethernet TCP management listener is present.
10. Restore the previous firmware without erasing the filesystem and verify identity/mesh preferences remain intact. Record firmware revision, serial logs, packet timestamps, DHCP capture, and outcomes.

Hardware validation is pending; no device has been flashed by this change.
