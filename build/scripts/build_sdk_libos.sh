#!/usr/bin/env bash
# @file build_sdk_libos.sh
# @brief M6-SDK-002 (#388) — assemble the userland `libos.a` for SDK apps.
#
# Purpose:
#   Slice 2 of the M6 SDK scaffold (plan #136 / BUILD_ROADMAP §5.6).
#   Produces `artifacts/sdk/libos.a`, the userland archive that
#   external apps will link against alongside the SDK headers shipped
#   in slice 1. Composition (kept intentionally narrow for slice 2):
#
#     * sdk/lib/crt0.o          — `_start` entry shim (this slice)
#     * sdk/lib/libos/version.o — SDK-owned ABI re-export anchor
#     * user/runtime/secureos_api_stubs.o
#                               — strict re-export of the existing
#                                 syscall/IPC wrappers documented in
#                                 `docs/abi/syscalls.md` and
#                                 `docs/abi/ipc-wire.md`. NO new ABI
#                                 surface is minted here.
#
#   Tooling-first / deterministic-build principles (see
#   `BUILD_ROADMAP.md` §§1-3): compilation uses the same freestanding
#   `clang --target=x86_64-unknown-none-elf` invocation that the
#   in-tree user-app build (`build_user_app.sh`) already uses, so the
#   archive that external apps link against is byte-for-byte the same
#   shape an in-tree app would link.
#
#   This script is designed to run INSIDE the Docker toolchain
#   container (the same one `build/scripts/build.sh` brings up). It
#   is wired into the top-level orchestrator via the new `build_sdk`
#   step and is safe to invoke standalone for incremental work.
#
# Interactions:
#   - sdk/lib/crt0.c, sdk/lib/libos/version.c — slice-2 sources.
#   - user/runtime/secureos_api_stubs.c — existing syscall surface.
#   - sdk/include/os/abi.h, user/include/secureos_abi.h, secureos_api.h
#       — public + in-tree headers.
#   - build/scripts/test_sdk_libos_link.sh — host link-test peer.
#
# Launched by:
#   build/scripts/build.sh (target `sdk`), and standalone for slice
#   development.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/sdk"

mkdir -p "$OUT_DIR"

# Mirror build_user_app.sh / build_user_lib.sh exactly so external SDK
# apps inherit the same freestanding ABI. NOTE: `-I user/include` is
# load-bearing — the SDK header `os/abi.h` re-includes
# `secureos_abi.h`, and `crt0.c` includes `secureos_api.h`; both live
# under `user/include/`.
SDK_CFLAGS="--target=x86_64-unknown-none-elf -ffreestanding -fno-stack-protector -mno-red-zone"
SDK_CFLAGS="$SDK_CFLAGS -I $ROOT_DIR/sdk/include -I $ROOT_DIR/user/include"

if ! command -v clang >/dev/null 2>&1; then
  echo "BUILD_SDK_LIBOS:FAIL:clang_not_found"
  echo "build_sdk_libos.sh expects to run inside the SecureOS toolchain"
  echo "container (see build/docker/Dockerfile.toolchain)."
  exit 1
fi
if ! command -v ar >/dev/null 2>&1; then
  echo "BUILD_SDK_LIBOS:FAIL:ar_not_found"
  exit 1
fi

cd "$ROOT_DIR"

CRT0_O="$OUT_DIR/crt0.o"
VERSION_O="$OUT_DIR/version.o"
STUBS_O="$OUT_DIR/secureos_api_stubs.o"
LIBOS_A="$OUT_DIR/libos.a"

# Each compile is its own line so the failure marker pinpoints the
# offender; do not collapse into a loop.
clang $SDK_CFLAGS -c sdk/lib/crt0.c          -o "$CRT0_O"
clang $SDK_CFLAGS -c sdk/lib/libos/version.c -o "$VERSION_O"
clang $SDK_CFLAGS -c user/runtime/secureos_api_stubs.c -o "$STUBS_O"

# `D` = deterministic (no timestamps / uids), matching the
# repo-wide deterministic-artifact rule. `rcs` = create, replace,
# write index.
rm -f "$LIBOS_A"
ar Drcs "$LIBOS_A" "$CRT0_O" "$VERSION_O" "$STUBS_O"

echo "BUILD_SDK_LIBOS:PASS:$LIBOS_A"
