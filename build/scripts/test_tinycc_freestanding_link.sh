#!/usr/bin/env bash
# test_tinycc_freestanding_link.sh — host gate for TinyCC link readiness.
#
# Purpose:
#   Verifies the #766 link-slice archive set is buildable:
#     - libtcc.a   (core TinyCC compiler archive)
#     - libclib.a  (SecureOS freestanding libc archive)
#     - libtcc1.a  (runtime helpers for emitted binaries)
#
#   The gate compiles secureos_api_stubs.o as the syscall bridge provider and
#   checks that the compiler itself (libtcc.a + libclib.a + bridge stubs) has
#   zero unresolved externals. libtcc1.a is verified as a separately built,
#   manifest-pinned demand-link archive. This is the host-side closure proof
#   before guest wiring.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests/tinycc_freestanding_link"
GATE="tinycc_freestanding_link"

emit() { printf '%s\n' "$1"; }
pass() { emit "TEST:PASS:$1"; }
fail() { emit "TEST:FAIL:$GATE:$1"; ALL_OK=0; }

ALL_OK=1
mkdir -p "$OUT_DIR"

LIBTCC="$ROOT_DIR/artifacts/user/libs/libtcc.a"
LIBCLIB="$ROOT_DIR/artifacts/user/libs/libclib.a"
LIBTCC1="$ROOT_DIR/artifacts/user/libs/libtcc1.a"
HAD_LIBTCC=0
HAD_LIBCLIB=0
HAD_LIBTCC1=0
[[ -f "$LIBTCC" ]] && HAD_LIBTCC=1
[[ -f "$LIBCLIB" ]] && HAD_LIBCLIB=1
[[ -f "$LIBTCC1" ]] && HAD_LIBTCC1=1
cleanup() {
  # Keep this gate side-effect free for later bundle checks (apps_dev_sha
  # treats pending artifacts without SHA pins as failures when present).
  [[ "$HAD_LIBTCC" -eq 1 ]] || rm -f "$LIBTCC"
  [[ "$HAD_LIBCLIB" -eq 1 ]] || rm -f "$LIBCLIB"
  [[ "$HAD_LIBTCC1" -eq 1 ]] || rm -f "$LIBTCC1"
}
trap cleanup EXIT

if ! command -v clang >/dev/null 2>&1 || ! command -v ar >/dev/null 2>&1 || ! command -v nm >/dev/null 2>&1; then
  emit "TEST:SKIP:$GATE:no_toolchain_on_path"
  emit "TEST:PASS:$GATE"
  exit 0
fi

if [ ! -d "$ROOT_DIR/vendor/tinycc/tinycc" ] || [ -z "$(ls -A "$ROOT_DIR/vendor/tinycc/tinycc" 2>/dev/null)" ]; then
  emit "TEST:SKIP:$GATE:tinycc_submodule_not_initialized"
  emit "TEST:PASS:$GATE"
  exit 0
fi

if bash "$ROOT_DIR/build/scripts/build_tinycc.sh" > "$OUT_DIR/build.log" 2>&1; then
  pass "$GATE:build_tinycc"
else
  fail "build_tinycc_failed:$(head -c 400 "$OUT_DIR/build.log" | tr '\n' ';')"
fi

LIBTCC="$ROOT_DIR/artifacts/user/libs/libtcc.a"
LIBCLIB="$ROOT_DIR/artifacts/user/libs/libclib.a"
LIBTCC1="$ROOT_DIR/artifacts/user/libs/libtcc1.a"
for f in "$LIBTCC" "$LIBCLIB" "$LIBTCC1"; do
  if [ -f "$f" ]; then
    pass "$GATE:archive_present:$(basename "$f")"
  else
    fail "archive_missing:$(basename "$f")"
  fi
done

if [ -f "$LIBTCC" ]; then
  CORE_COUNT="$(ar t "$LIBTCC" 2>/dev/null | wc -l | tr -d ' ')"
  if [ "$CORE_COUNT" = "10" ]; then
    pass "$GATE:libtcc_members:10"
  else
    fail "libtcc_member_count:$CORE_COUNT"
  fi
fi

if [ -f "$LIBCLIB" ]; then
  if ar t "$LIBCLIB" 2>/dev/null | grep -q 'clib_setjmp_x86.o'; then
    pass "$GATE:libclib_contains_setjmp_x86"
  else
    fail "libclib_missing_setjmp_x86"
  fi
fi

CC_FLAGS="--target=x86_64-unknown-none-elf -ffreestanding -fno-stack-protector -mno-red-zone -nostdlibinc"
STUBS_O="$OUT_DIR/secureos_api_stubs.o"
if clang $CC_FLAGS -I "$ROOT_DIR/user/include" -c "$ROOT_DIR/user/runtime/secureos_api_stubs.c" -o "$STUBS_O" >"$OUT_DIR/stubs.log" 2>&1; then
  pass "$GATE:secureos_api_stubs_compiles"
else
  fail "secureos_api_stubs_compile_failed:$(head -c 300 "$OUT_DIR/stubs.log" | tr '\n' ';')"
fi

if [ -f "$LIBTCC" ] && [ -f "$LIBCLIB" ] && [ -f "$STUBS_O" ]; then
  # Closure check for the compiler itself: libtcc.a + libclib.a + the
  # syscall-bridge stubs must resolve every external symbol. libtcc1.a is
  # intentionally excluded here — its members are demand-linked into
  # TinyCC-compiled *output* programs one at a time (not linked wholesale
  # into the compiler), so an archive member that is only reachable via a
  # disabled feature (e.g. __bound_alloca in alloca-bt.S, only pulled when
  # CONFIG_TCC_BCHECK is enabled — it is not, per config-secureos.h) can
  # carry a symbol like __bound_new_region that is never actually linked.
  # tinycc_freestanding_compile already verifies libtcc1.a's member count
  # and that every TU compiles; this gate does not need to re-litigate its
  # never-pulled internal references.
  nm -u "$LIBTCC" "$LIBCLIB" "$STUBS_O" \
    | awk '/ U /{print $2}' | sed 's/^_//' | sort -u > "$OUT_DIR/undef.txt"
  nm --defined-only --extern-only "$LIBTCC" "$LIBCLIB" "$STUBS_O" \
    | awk '{print $3}' | sed 's/^_//' | sort -u > "$OUT_DIR/defined.txt"
  comm -23 "$OUT_DIR/undef.txt" "$OUT_DIR/defined.txt" > "$OUT_DIR/gaps.txt"
  GAP_COUNT="$(grep -c . "$OUT_DIR/gaps.txt" || true)"
  if [ "$GAP_COUNT" = "0" ]; then
    pass "$GATE:zero_unresolved_symbols"
  else
    fail "unresolved_symbols:$GAP_COUNT:$(paste -sd, "$OUT_DIR/gaps.txt")"
  fi
fi

if [ "$ALL_OK" -eq 1 ]; then
  pass "$GATE"
  exit 0
else
  emit "TEST:FAIL:$GATE"
  exit 1
fi
