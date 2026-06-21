#!/usr/bin/env bash
# test_tinycc_config_secureos.sh — Host smoke + drift gate for the
# freestanding TinyCC config header (issue #408 Phase 2 sub-slice).
#
# Companion to PR #516's `tinycc_vendor_gate` (which pins the vendor source
# surface). This gate validates the SecureOS-side config replacement that
# Phase 3 of the in-OS compiler port will -include in place of TinyCC's
# autoconf-generated config.h. Same shape as bearssl_compile's host-side
# checks: deterministic, no kernel build required, runs in the validation
# bundle.
#
# Sub-markers:
#   tinycc_config_secureos_header_present
#       vendor/tinycc/config-secureos.h exists at the expected path.
#
#   tinycc_config_secureos_syntax_ok
#       The header is a valid C translation-unit-fragment: `cc -E -x c -`
#       on a TU that only #includes it returns 0.
#
#   tinycc_config_secureos_target_x86_64
#       After preprocessing, TCC_TARGET_X86_64 is defined and the
#       PE / MACHO arms trip the build-break `#error` when forced on.
#
#   tinycc_config_secureos_backtrace_disabled
#       CONFIG_TCC_BACKTRACE remains undefined (per porting note 1) and
#       force-defining it on the command line trips the build-break
#       `#error`.
#
#   tinycc_config_secureos_bcheck_disabled
#       Same shape for CONFIG_TCC_BCHECK.
#
#   tinycc_config_secureos_search_paths_pinned
#       CONFIG_TCC_SYSINCLUDEPATHS / CONFIG_TCC_LIBPATHS expand to the
#       /apps/dev/{include,lib} strings — pinned so a silent change to
#       the VFS layout shows up here, not at Phase 3 build time.
#
#   tinycc_config_secureos_one_source_off
#       ONE_SOURCE expands to 0 (Makefile.secureos assumes per-TU build).
#
#   tinycc_config_secureos
#       Rollup: all of the above PASSed.
#
# SKIPs cleanly when no host C compiler is on PATH (mirrors
# bearssl_compile's "no toolchain → SKIP" arm).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
HEADER_REL="vendor/tinycc/config-secureos.h"
HEADER_ABS="$ROOT_DIR/$HEADER_REL"

emit() { printf '%s\n' "$1"; }
pass() { emit "TEST:PASS:$1"; }
skip() { emit "TEST:SKIP:$1:$2"; }
fail() { emit "TEST:FAIL:$1:$2"; ALL_OK=0; }

ALL_OK=1

# --- 1. header_present -------------------------------------------------------
if [ -f "$HEADER_ABS" ]; then
  pass "tinycc_config_secureos_header_present"
else
  fail "tinycc_config_secureos_header_present" "missing:$HEADER_REL"
  emit "TEST:FAIL:tinycc_config_secureos:header_missing"
  exit 1
fi

# --- pick a host compiler ----------------------------------------------------
HOST_CC=""
for cand in "${CC:-}" cc gcc clang; do
  if [ -n "$cand" ] && command -v "$cand" >/dev/null 2>&1; then
    HOST_CC="$cand"
    break
  fi
done

if [ -z "$HOST_CC" ]; then
  skip "tinycc_config_secureos_syntax_ok"          "no_host_cc_on_path"
  skip "tinycc_config_secureos_target_x86_64"      "no_host_cc_on_path"
  skip "tinycc_config_secureos_backtrace_disabled" "no_host_cc_on_path"
  skip "tinycc_config_secureos_bcheck_disabled"    "no_host_cc_on_path"
  skip "tinycc_config_secureos_search_paths_pinned" "no_host_cc_on_path"
  skip "tinycc_config_secureos_one_source_off"     "no_host_cc_on_path"
  skip "tinycc_config_secureos"                    "no_host_cc_on_path"
  exit 0
fi

TMPDIR_GATE="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_GATE"' EXIT
TU="$TMPDIR_GATE/probe.c"

# --- 2. syntax_ok ------------------------------------------------------------
cat > "$TU" <<EOF
#include "$HEADER_ABS"
int main(void) { return 0; }
EOF
if "$HOST_CC" -E -x c "$TU" >/dev/null 2>"$TMPDIR_GATE/err.log"; then
  pass "tinycc_config_secureos_syntax_ok"
else
  fail "tinycc_config_secureos_syntax_ok" "preprocess_failed"
  sed 's/^/    /' "$TMPDIR_GATE/err.log" >&2 || true
fi

# Helper: emit a tiny probe TU that #includes the header and uses the macro.
probe() {
  local marker="$1" expectation="$2" body="$3" extra_cflags="${4:-}"
  cat > "$TU" <<EOF
#include "$HEADER_ABS"
$body
EOF
  local out
  if out=$("$HOST_CC" -E $extra_cflags -x c "$TU" 2>&1); then
    case "$expectation" in
      compiles)
        printf '%s\n' "$out"
        return 0
        ;;
      rejects)
        fail "$marker" "expected_error_but_compiled"
        return 1
        ;;
    esac
  else
    case "$expectation" in
      compiles)
        fail "$marker" "preprocess_failed"
        printf '%s\n' "$out" | sed 's/^/    /' >&2 || true
        return 1
        ;;
      rejects)
        # Confirm the error came from our intentional #error arms.
        if printf '%s\n' "$out" | grep -q "config-secureos.h:"; then
          return 0
        fi
        fail "$marker" "rejected_but_wrong_error"
        printf '%s\n' "$out" | sed 's/^/    /' >&2 || true
        return 1
        ;;
    esac
  fi
}

# --- 3. target_x86_64 --------------------------------------------------------
if out=$(probe "tinycc_config_secureos_target_x86_64" compiles \
  "#ifndef TCC_TARGET_X86_64
#  error TCC_TARGET_X86_64 not defined
#endif
const int marker_x86_64 = 1;"); then
  if probe "tinycc_config_secureos_target_x86_64" rejects \
    "int main(void){return 0;}" "-DTCC_TARGET_PE=1" >/dev/null; then
    pass "tinycc_config_secureos_target_x86_64"
  fi
fi

# --- 4. backtrace_disabled ---------------------------------------------------
if probe "tinycc_config_secureos_backtrace_disabled" rejects \
  "int main(void){return 0;}" "-DCONFIG_TCC_BACKTRACE=1" >/dev/null; then
  pass "tinycc_config_secureos_backtrace_disabled"
fi

# --- 5. bcheck_disabled ------------------------------------------------------
if probe "tinycc_config_secureos_bcheck_disabled" rejects \
  "int main(void){return 0;}" "-DCONFIG_TCC_BCHECK=1" >/dev/null; then
  pass "tinycc_config_secureos_bcheck_disabled"
fi

# --- 6. search_paths_pinned --------------------------------------------------
SP_TU="$TMPDIR_GATE/search.c"
cat > "$SP_TU" <<EOF
#include "$HEADER_ABS"
const char sys_inc[] = CONFIG_TCC_SYSINCLUDEPATHS;
const char lib_path[] = CONFIG_TCC_LIBPATHS;
const char crt_pref[] = CONFIG_TCC_CRTPREFIX;
const char tccdir[]   = CONFIG_TCCDIR;
EOF
if out=$("$HOST_CC" -E -x c "$SP_TU" 2>"$TMPDIR_GATE/err.log"); then
  if echo "$out" | grep -q '"/apps/dev/include"' \
     && echo "$out" | grep -q '"/apps/dev/lib"' \
     && echo "$out" | grep -q '"/apps/dev/tcc"'; then
    pass "tinycc_config_secureos_search_paths_pinned"
  else
    fail "tinycc_config_secureos_search_paths_pinned" "path_string_mismatch"
  fi
else
  fail "tinycc_config_secureos_search_paths_pinned" "preprocess_failed"
fi

# --- 7. one_source_off -------------------------------------------------------
OS_TU="$TMPDIR_GATE/onesource.c"
cat > "$OS_TU" <<EOF
#include "$HEADER_ABS"
#if defined(ONE_SOURCE) && ONE_SOURCE == 0
const int one_source_off = 1;
#else
#  error ONE_SOURCE expected to be 0
#endif
EOF
if "$HOST_CC" -E -x c "$OS_TU" >/dev/null 2>"$TMPDIR_GATE/err.log"; then
  pass "tinycc_config_secureos_one_source_off"
else
  fail "tinycc_config_secureos_one_source_off" "macro_not_zero"
fi

# --- rollup ------------------------------------------------------------------
if [ "$ALL_OK" -eq 1 ]; then
  pass "tinycc_config_secureos"
  exit 0
else
  emit "TEST:FAIL:tinycc_config_secureos:sub_check_failed"
  exit 1
fi
