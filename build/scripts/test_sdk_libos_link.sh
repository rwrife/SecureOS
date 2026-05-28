#!/usr/bin/env bash
# @file test_sdk_libos_link.sh
# @brief M6-SDK-002 (#388) — host-side link smoke for `libos.a`.
#
# Purpose:
#   Slice 2 of the M6 SDK scaffold (plan #136 / BUILD_ROADMAP §5.6).
#   Wraps `tests/sdk_libos_link_test.c` so the bundle/agent harness
#   gets a deterministic `TEST:PASS:sdk_libos_link` marker.
#
#   What it does (in order):
#     1. Compile the slice-2 sources (`sdk/lib/crt0.c`,
#        `sdk/lib/libos/version.c`) plus the existing
#        `user/runtime/secureos_api_stubs.c` with host `cc` into
#        `.o` files. We deliberately drop `-ffreestanding` here so
#        the host `cc` can produce ordinary host objects we can
#        archive and link with hosted tools — the production
#        freestanding build is exercised by
#        `build/scripts/build_sdk_libos.sh` inside the container.
#     2. Bundle them with `ar Drcs` into a host-side `libos.a`
#        sibling. This is what the test program is linked against,
#        and what `nm` is run over.
#     3. Compile + partially-link the fixture
#        `tests/sdk_libos_link_test_app.c` against `libos.a` with
#        `ld -r --whole-archive` so dead-strip cannot discard
#        `_start` / `os_get_abi_version` before symbol inspection.
#     4. Dump the resulting object's symbol table with `nm` and
#        feed the dump to the driver
#        `tests/sdk_libos_link_test.c`, which asserts both
#        `_start` and `os_get_abi_version` are defined (not `U`).
#
#   Conventions mirror `build/scripts/test_sdk_abi_pin.sh`:
#     - host `cc -std=c11 -Wall -Wextra -Werror` for compile units;
#     - artifacts land under `artifacts/tests/`;
#     - emits `SDK_LIBOS_LINK:PASS:libos_built` then defers final
#       `TEST:PASS:sdk_libos_link` emission to the driver.
#
# Interactions:
#   - sdk/lib/crt0.c, sdk/lib/libos/version.c,
#     user/runtime/secureos_api_stubs.c — slice-2 + reused payload.
#   - sdk/include/, user/include/ — header search paths.
#   - tests/sdk_libos_link_test.c, tests/sdk_libos_link_test_app.c.
#
# Launched by:
#   build/scripts/test.sh sdk_libos_link
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests/sdk_libos_link"
mkdir -p "$OUT_DIR"

cd "$ROOT_DIR"

CC="${CC:-cc}"
AR="${AR:-ar}"
LD="${LD:-ld}"
NM="${NM:-nm}"

# Host-side compile flags: same warning floor as the slice-1 ABI pin
# test, plus the SDK + in-tree include layout. We do NOT pass
# `-ffreestanding` here — see header for rationale.
HOST_CFLAGS="-std=c11 -Wall -Wextra -Werror -Wno-builtin-declaration-mismatch"
HOST_CFLAGS="$HOST_CFLAGS -I$ROOT_DIR/sdk/include -I$ROOT_DIR/user/include"

CRT0_O="$OUT_DIR/crt0.o"
VERSION_O="$OUT_DIR/version.o"
STUBS_O="$OUT_DIR/secureos_api_stubs.o"
APP_O="$OUT_DIR/sdk_libos_link_test_app.o"
LIBOS_A="$OUT_DIR/libos.a"
LINKED_O="$OUT_DIR/sdk_libos_link_test_app.linked.o"
NM_DUMP="$OUT_DIR/sdk_libos_link_test_app.nm.txt"
DRIVER="$OUT_DIR/sdk_libos_link_test"

# crt0.c contains x86-specific inline asm (`hlt`); the host `cc` on
# x86_64 accepts it, but on other arches we cannot compile it as-is.
# Detect and skip cleanly so the test does not falsely fail on ARM
# CI runners. The freestanding container build is unaffected.
HOST_ARCH="$(uname -m 2>/dev/null || echo unknown)"
case "$HOST_ARCH" in
  x86_64|amd64) ;;
  *)
    echo "TEST:SKIP:sdk_libos_link:host_arch_not_x86_64:$HOST_ARCH"
    exit 0
    ;;
esac

$CC $HOST_CFLAGS -c "$ROOT_DIR/sdk/lib/crt0.c"                       -o "$CRT0_O"
$CC $HOST_CFLAGS -c "$ROOT_DIR/sdk/lib/libos/version.c"              -o "$VERSION_O"
$CC $HOST_CFLAGS -c "$ROOT_DIR/user/runtime/secureos_api_stubs.c"    -o "$STUBS_O"
$CC $HOST_CFLAGS -c "$ROOT_DIR/tests/sdk_libos_link_test_app.c"      -o "$APP_O"

rm -f "$LIBOS_A"
$AR Drcs "$LIBOS_A" "$CRT0_O" "$VERSION_O" "$STUBS_O"

echo "SDK_LIBOS_LINK:PASS:libos_built:$LIBOS_A"

# Partial-link the app + whole archive into one relocatable object.
# `--whole-archive` is the GNU ld spelling; on platforms where ld is
# lld-flavoured the same flag is honoured.
$LD -r --whole-archive "$APP_O" "$LIBOS_A" --no-whole-archive -o "$LINKED_O"

$NM "$LINKED_O" > "$NM_DUMP"

# Build the driver and run it against the nm dump.
$CC -std=c11 -Wall -Wextra -Werror "$ROOT_DIR/tests/sdk_libos_link_test.c" -o "$DRIVER"
"$DRIVER" "$NM_DUMP"
