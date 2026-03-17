# 2026-03-16 Netlib Full Network Stack Extraction

## Goal
Move all protocol-stack functionality currently living under `kernel/net` into `netlib`, leaving the kernel with only:
- network HAL abstractions
- network device drivers
- minimal syscall/ABI plumbing needed to expose raw frame/device access

## Current Blocker
SecureOS does not yet execute linked shared-library code. Today `loadlib` validates and registers `.lib` artifacts, but command execution still occurs in kernel-owned code paths. Because of that, deleting `kernel/net` immediately would remove the only runtime implementation of DNS/TCP/HTTP behavior.

## What Was Added In This Change
- Multi-source user library builds so `netlib` can absorb multiple implementation files.
- Raw network-device ABI placeholders in `secureos_api.h`:
  - device ready
  - backend name
  - MAC address
  - frame send
  - frame receive
- Expanded `netlib.h` contract to cover:
  - interface info
  - raw frame IO
  - existing host/request helper surface
- Kernel `net_service` facade to reduce process-layer coupling to individual protocol modules while the full migration is still in progress.

## Required Follow-Up To Complete The Extraction
1. Add real syscall wiring for the raw network-device ABI functions.
2. Port `eth/arp/ipv4/udp/dns/tcp/http` source into `user/libs/netlib/` with user-space include paths.
3. Add a user-space execution path for `netlib` code, either:
   - linked app execution against lib objects, or
   - runtime library symbol invocation support.
4. Migrate command/app networking paths to user-space netlib consumers.
5. Remove `kernel/net/*.c` from kernel build once user-space netlib is the active implementation.

## Exit Criteria
- `build_kernel_entry.{sh,ps1}` no longer compiles `kernel/net/*.c`.
- Network commands/apps still pass QEMU network harness tests.
- Kernel references only HAL/device-driver networking code plus syscall plumbing.
