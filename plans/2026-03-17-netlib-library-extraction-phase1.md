# 2026-03-17 Netlib Library Extraction Phase 1

## Goal
Create a standalone `netlib.lib` artifact and establish a user-space networking contract that commands/apps can share, while keeping kernel networking behavior stable.

## Scope (Phase 1)
- Add `user/libs/netlib/main.c` as a standalone loadable library binary target.
- Add `user/include/lib/netlib.h` with wrappers for `ifconfig`, `http get`, and `ping` operations.
- Ensure disk image build pipeline packages all `user/libs/*` into `/lib/*.lib`.
- Remove kernel boot-time hardcoded seeding of `/lib/envlib.lib`, `/lib/fslib.lib`, and `/lib/soflib.lib`.

## Why This Phase Exists
A complete migration of `kernel/net` logic into a user-space library would require a broader architecture shift. Phase 1 introduces the reusable ABI/library boundary first, then subsequent phases can move implementation details behind that boundary.

## Follow-Up (Phase 2+)
- Introduce explicit net service syscall entrypoints owned by a narrow HAL-aware kernel network service facade.
- Migrate command/app networking usage to `netlib` interfaces everywhere.
- Split protocol-specific logic into reusable user-space modules where feasible.
- Add integration tests verifying that `loadlib netlib` works and networking commands still pass in QEMU harness.
