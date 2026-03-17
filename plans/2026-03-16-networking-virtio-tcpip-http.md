# Plan: Virtualized Networking, TCP/IP Stack, and HTTP Command

**Date:** 2026-03-16
**Status:** Accepted
**Priority:** High

## Problem

SecureOS has no networking capability. Adding networking is required to:
- Enable outbound HTTP requests from the kernel console
- Support future application networking use cases
- Provide a foundation for capability-gated network access

## Decisions

| Property | Choice | Reason |
|---|---|---|
| Primary NIC | virtio-net-pci (legacy) | QEMU native, simplest polling path |
| NIC fallback | HAL returns not-ready | Clean error path, no silent fallback |
| Stack depth | Ethernet + ARP + IPv4 + UDP + DNS + TCP client | All required for HTTP to named hosts |
| DNS | UDP/A-record, fixed resolver (QEMU user-net 10.0.2.3) | No DHCP needed in QEMU user mode |
| HTTP scope | GET, POST, custom headers, bounded body | MVP for `http` command use cases |
| HTTPS/TLS | Deferred to v2 | Requires crypto infrastructure changes |
| Capability gate | Single `CAP_NETWORK` | Simple v1 model, split in v2 if needed |
| Event audit | Three topics: TX_REQUEST, RX_PACKET, DECISION | Observable at every consent boundary |

## Architecture

### Component Boundary Map

```
Console / process.c
    └── app_sys_http()     [CAP_NETWORK check here]
            |
            v
    kernel/net/http.c      [HTTP request builder + response parser]
            |
            v
    kernel/net/tcp.c       [TCP client: connect / send / recv / close]
            |
            v
    kernel/net/dns.c       [DNS A-record resolver via UDP]
    kernel/net/udp.c       [UDP datagram TX/RX]
    kernel/net/ipv4.c      [IP packet TX/RX, routing table]
    kernel/net/arp.c       [ARP request/reply for next-hop MAC]
    kernel/net/eth.c       [Ethernet frame TX/RX]
            |
            v
    kernel/hal/network_hal.c   [HAL abstraction: register_primary, tx, rx]
            |
            v
    kernel/drivers/network/virtio_net.c   [virtio-net-pci legacy polling driver]
            |
            v
    QEMU -netdev user,id=net0 -device virtio-net-pci,netdev=net0
```

### Capability Check Flow

All network operations gated by `CAP_NETWORK = 12`:
1. `app_sys_http()` calls `app_require_capability(subject, CAP_NETWORK)`.
2. On deny → emit `EVENT_TOPIC_NETWORK_DECISION` with DENY payload and return `PROCESS_ERR_CAPABILITY`.
3. On allow → emit `EVENT_TOPIC_NETWORK_TX_REQUEST` before sending, `EVENT_TOPIC_NETWORK_RX_PACKET` on response.

### QEMU Network Configuration

QEMU user-mode networking (no tap, no bridge):
```
-netdev user,id=net0
-device virtio-net-pci,netdev=net0
```

Guest IP address (DHCP via user-net): 10.0.2.15
QEMU DNS resolver: 10.0.2.3
Default gateway: 10.0.2.2

Since DHCP is out of scope, guest IP and gateway are hardcoded constants in virtio_net.h.

## File Map

### New Files

| File | Purpose |
|---|---|
| `kernel/hal/network_hal.h` | Network HAL public interface |
| `kernel/hal/network_hal.c` | Network HAL registration and dispatch |
| `kernel/drivers/network/virtio_net.h` | Virtio-net-pci driver header |
| `kernel/drivers/network/virtio_net.c` | Virtio-net-pci polling driver |
| `kernel/net/eth.h` | Ethernet frame types and layout |
| `kernel/net/eth.c` | Ethernet TX/RX framing |
| `kernel/net/arp.h` | ARP protocol types |
| `kernel/net/arp.c` | ARP cache and request/reply |
| `kernel/net/ipv4.h` | IPv4 packet types |
| `kernel/net/ipv4.c` | IPv4 TX/RX, checksum |
| `kernel/net/udp.h` | UDP datagram types |
| `kernel/net/udp.c` | UDP TX/RX |
| `kernel/net/dns.h` | DNS resolver types |
| `kernel/net/dns.c` | DNS A-record resolver over UDP |
| `kernel/net/tcp.h` | TCP client types |
| `kernel/net/tcp.c` | TCP client state machine (polling) |
| `kernel/net/http.h` | HTTP client types |
| `kernel/net/http.c` | HTTP/1.1 GET/POST client |
| `apps/os/http/resources/http.txt` | CLI help resource for `http` |
| `tests/network_hal_test.c` | Network HAL unit tests |

### Modified Files

| File | Change |
|---|---|
| `kernel/cap/capability.h` | Add `CAP_NETWORK = 12` |
| `kernel/event/event_bus.h` | Add 3 network event topics, increment count |
| `kernel/core/kmain.c` | Add network HAL + driver init after storage; add `CAP_NETWORK` bootstrap grant |
| `kernel/user/process.c` | Add `app_sys_http()` handler + command-table entry |
| `kernel/fs/fs_service.c` | Register `os/http.bin` SOF artifact + update help listing |
| `build/qemu/x86_64-headless.args` | Add `-netdev user` and `-device virtio-net-pci` |
| `build/qemu/x86_64-graphical.args` | Same |
| `build/scripts/build_kernel_entry.sh` | Add compile + link for all new kernel/net and kernel/drivers/network objects |
| `docs/architecture/CAPABILITIES.md` | Document `CAP_NETWORK` contract |

## Protocol Design Constraints

### v1 Limits

- Packet buffers: 2 KB per TX/RX slot (covers standard 1500-byte Ethernet MTU)
- RX ring depth: 16 descriptors
- TX ring depth: 16 descriptors
- TCP: single blocking connection per call (no concurrent sockets)
- HTTP response body buffer: 4 KB maximum (printed in 512-byte chunks to console)
- DNS: single A-record lookup per call, timeout 3 poll-loop iterations (~1s equivalent)
- TCP timeout: 1000000 poll-loop iterations (generous; real timeout via future SysTick)
- Headers: up to 8 custom headers, each up to 128 bytes

### Out of Scope for v1

- TLS/HTTPS
- IPv6
- DHCP (hardcoded guest IP)
- TCP server sockets (inbound)
- Advanced TCP features (window scaling, SACK, congestion control)
- Interrupt-driven I/O (polling only)
- Multiple concurrent connections
- HTTP/2 or HTTP/3

## Testing Plan

| Test | Type | Pass Condition |
|---|---|---|
| `network_hal_init` | Unit | `network_hal_ready()` returns 1 after virtio probe |
| `network_cap_allow` | Unit | Bootstrap subject can call http command |
| `network_cap_deny` | Unit | Subject without `CAP_NETWORK` returns `PROCESS_ERR_CAPABILITY` |
| `virtio_probe` | Integration | Boot log shows `network backend=virtio-net` |
| `dns_resolve` | Integration | `network_dns_resolve("10.0.2.3", "example.com")` returns an IP |
| `http_get` | Integration | HTTP GET to `10.0.2.2` returns response with status line |
| `http_post` | Integration | HTTP POST with body sends correct Content-Length header |
| `http_headers` | Integration | Custom `X-Header` appears in wire trace |
| `http_bad_url` | Unit | Malformed URL returns `PROCESS_ERR_INVALID_ARG` |
| `net_events` | Unit | TX_REQUEST and DECISION events appear in audit ring |

## V2 Considerations

1. Split `CAP_NETWORK` into `CAP_NETWORK_TX`, `CAP_NETWORK_RX`, `CAP_NETWORK_DNS` for finer granularity.
2. Add `network` command analogous to `storage` command showing NIC backend, IP, gateway.
3. Add DHCP client so guest IP is dynamic.
4. Add TLS via a separate `kernel/crypto/tls.c` once a wolfSSL or small TLS port is evaluated.
5. Move DNS resolver configuration to a runtime environment variable.
