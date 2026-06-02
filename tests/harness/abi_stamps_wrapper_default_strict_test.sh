#!/usr/bin/env bash
# tests/harness/abi_stamps_wrapper_default_strict_test.sh
#
# Issue #470 (wrapper-flip slice): canary that pins
# `build/scripts/validate_abi_stamps.sh` to strict mode by default.
#
# Sibling of:
#   - tests/harness/abi_stamps_strict_no_skip_test.sh (PR #479) — proves
#     the validator's --strict-no-skip flag and the STRICT_STAMPS=1
#     wrapper path.
#   - tests/harness/abi_stamps_strict_no_placeholder_test.sh (PR #510) —
#     proves --strict-no-placeholder.
#
# This canary asserts the new contract: the wrapper now defaults to
# strict (no env var required) and STRICT_STAMPS=0 reverts to the
# legacy SKIP-tolerant mode.
#
# Contract:
#   On success: TEST:PASS:abi_stamps_wrapper_default_strict_canary, exit 0.
#   On failure: TEST:FAIL:abi_stamps_wrapper_default_strict_canary:<reason>, exit 1.

set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
# shellcheck disable=SC2064
trap "rm -rf '$TMP_DIR'" EXIT

printf 'TEST:START:abi_stamps_wrapper_default_strict_canary\n'

SANDBOX="$TMP_DIR/repo"
mkdir -p "$SANDBOX/docs/abi"
(
  cd "$SANDBOX"
  git init -q -b main
  git config user.email canary@local
  git config user.name canary
) || { printf 'TEST:FAIL:abi_stamps_wrapper_default_strict_canary:sandbox_git_init_failed\n'; exit 1; }

cat > "$SANDBOX/docs/abi/syscalls.md" <<'EOF'
# Sandbox: syscalls ABI surface

No stamp line in this file; strict wrapper default must FAIL on it.
EOF
(
  cd "$SANDBOX"
  git add docs/abi/syscalls.md
  git commit -q -m "sandbox: one unstamped abi doc"
) || { printf 'TEST:FAIL:abi_stamps_wrapper_default_strict_canary:sandbox_commit_failed\n'; exit 1; }

# --- Arm 1: wrapper default (no env var) must FAIL on the unstamped doc.
DEFAULT_LOG="$TMP_DIR/default.log"
set +e
unset STRICT_STAMPS
bash "$ROOT_DIR/build/scripts/validate_abi_stamps.sh" \
  --abi-dir "$SANDBOX/docs/abi" \
  > "$DEFAULT_LOG" 2>&1
DEFAULT_RC=$?
set -e

if [ "$DEFAULT_RC" -ne 1 ]; then
  printf 'TEST:FAIL:abi_stamps_wrapper_default_strict_canary:wrapper_default_expected_rc_1_got:%d\n' "$DEFAULT_RC"
  cat "$DEFAULT_LOG"
  exit 1
fi
if ! grep -qE 'ABI_STAMP:FAIL:[^ ]*docs/abi/syscalls.md:no_stamp_line' "$DEFAULT_LOG"; then
  printf 'TEST:FAIL:abi_stamps_wrapper_default_strict_canary:wrapper_default_missing_fail_marker\n'
  cat "$DEFAULT_LOG"
  exit 1
fi

# --- Arm 2: STRICT_STAMPS=0 reverts to legacy SKIP-tolerant mode.
LEGACY_LOG="$TMP_DIR/legacy.log"
set +e
STRICT_STAMPS=0 bash "$ROOT_DIR/build/scripts/validate_abi_stamps.sh" \
  --abi-dir "$SANDBOX/docs/abi" \
  > "$LEGACY_LOG" 2>&1
LEGACY_RC=$?
set -e

if [ "$LEGACY_RC" -ne 0 ]; then
  printf 'TEST:FAIL:abi_stamps_wrapper_default_strict_canary:legacy_mode_expected_rc_0_got:%d\n' "$LEGACY_RC"
  cat "$LEGACY_LOG"
  exit 1
fi
if ! grep -qE 'ABI_STAMP:SKIP:[^ ]*docs/abi/syscalls.md:no_stamp_line' "$LEGACY_LOG"; then
  printf 'TEST:FAIL:abi_stamps_wrapper_default_strict_canary:legacy_mode_missing_skip_marker\n'
  cat "$LEGACY_LOG"
  exit 1
fi

# --- Arm 3: STRICT_STAMPS=1 still works (explicit-strict path remains).
EXPLICIT_LOG="$TMP_DIR/explicit.log"
set +e
STRICT_STAMPS=1 bash "$ROOT_DIR/build/scripts/validate_abi_stamps.sh" \
  --abi-dir "$SANDBOX/docs/abi" \
  > "$EXPLICIT_LOG" 2>&1
EXPLICIT_RC=$?
set -e

if [ "$EXPLICIT_RC" -ne 1 ]; then
  printf 'TEST:FAIL:abi_stamps_wrapper_default_strict_canary:explicit_strict_expected_rc_1_got:%d\n' "$EXPLICIT_RC"
  cat "$EXPLICIT_LOG"
  exit 1
fi
if ! grep -qE 'ABI_STAMP:FAIL:[^ ]*docs/abi/syscalls.md:no_stamp_line' "$EXPLICIT_LOG"; then
  printf 'TEST:FAIL:abi_stamps_wrapper_default_strict_canary:explicit_strict_missing_fail_marker\n'
  cat "$EXPLICIT_LOG"
  exit 1
fi

printf 'TEST:PASS:abi_stamps_wrapper_default_strict_canary\n'
exit 0
