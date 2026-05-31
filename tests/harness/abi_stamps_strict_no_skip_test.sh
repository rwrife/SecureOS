#!/usr/bin/env bash
# tests/harness/abi_stamps_strict_no_skip_test.sh
#
# Issue #470: negative canary for the ABI-stamp-freshness validator's
# strict-no-skip mode.
#
# Default behavior of tools/validate_abi_stamps.py treats a docs/abi/*.md
# file with no `Last verified against commit:` line as
# ABI_STAMP:SKIP:<file>:no_stamp_line (exit 0). That bootstrap concession
# has become a steady source of silent gaps (#463, #467, #468 plus the two
# *-contract.md files). Strict-no-skip mode (#470) promotes that SKIP to
# FAIL/exit-1 so a future ABI doc cannot land without a stamp.
#
# This canary proves both arms of the contract in one repo sandbox:
#
#   - default mode: the unstamped doc emits ABI_STAMP:SKIP and exit 0.
#   - --strict-no-skip: the same unstamped doc emits ABI_STAMP:FAIL and
#     exit 1, AND a peer doc listed via --exempt is dropped from
#     iteration (no PASS, no FAIL, no SKIP marker).
#
# Same shape as tests/harness/abi_stamps_drift_test.sh (#297) and
# tests/harness/capability_registry_drift_test.sh (#234).
#
# Contract:
#   On success: TEST:PASS:abi_stamps_strict_no_skip_canary, exit 0.
#   On failure: TEST:FAIL:abi_stamps_strict_no_skip_canary:<reason>, exit 1.

set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
# shellcheck disable=SC2064
trap "rm -rf '$TMP_DIR'" EXIT

printf 'TEST:START:abi_stamps_strict_no_skip_canary\n'

SANDBOX="$TMP_DIR/repo"
mkdir -p "$SANDBOX/docs/abi"
(
  cd "$SANDBOX"
  git init -q -b main
  git config user.email canary@local
  git config user.name canary
) || { printf 'TEST:FAIL:abi_stamps_strict_no_skip_canary:sandbox_git_init_failed\n'; exit 1; }

# Two in-scope docs, neither carrying a `Last verified against commit:`
# line. One will be the strict-mode FAIL target; the other will be
# --exempt'ed to prove the escape hatch still drops it from iteration.
cat > "$SANDBOX/docs/abi/syscalls.md" <<'EOF'
# Sandbox: syscalls ABI surface

No stamp line in this file; default mode SKIPs, strict mode FAILs.
EOF
cat > "$SANDBOX/docs/abi/index-only.md" <<'EOF'
# Sandbox: pure-index page, deliberately exempt

Stand-in for a genuinely non-freshness doc (e.g. an index page).
EOF
(
  cd "$SANDBOX"
  git add docs/abi/syscalls.md docs/abi/index-only.md
  git commit -q -m "sandbox: two unstamped abi docs"
) || { printf 'TEST:FAIL:abi_stamps_strict_no_skip_canary:sandbox_commit_failed\n'; exit 1; }

# --- Arm 1: default mode must still SKIP and exit 0. -------------------
DEFAULT_LOG="$TMP_DIR/default.log"
set +e
python3 "$ROOT_DIR/tools/validate_abi_stamps.py" --root "$SANDBOX" \
  > "$DEFAULT_LOG" 2>&1
DEFAULT_RC=$?
set -e

if [ "$DEFAULT_RC" -ne 0 ]; then
  printf 'TEST:FAIL:abi_stamps_strict_no_skip_canary:default_mode_nonzero_rc:%d\n' "$DEFAULT_RC"
  cat "$DEFAULT_LOG"
  exit 1
fi
if ! grep -qF "ABI_STAMP:SKIP:docs/abi/syscalls.md:no_stamp_line" "$DEFAULT_LOG"; then
  printf 'TEST:FAIL:abi_stamps_strict_no_skip_canary:default_mode_missing_skip_marker\n'
  cat "$DEFAULT_LOG"
  exit 1
fi

# --- Arm 2: --strict-no-skip must FAIL on the unstamped, non-exempt doc.
STRICT_LOG="$TMP_DIR/strict.log"
set +e
python3 "$ROOT_DIR/tools/validate_abi_stamps.py" --root "$SANDBOX" \
  --strict-no-skip \
  --exempt index-only.md \
  > "$STRICT_LOG" 2>&1
STRICT_RC=$?
set -e

if [ "$STRICT_RC" -ne 1 ]; then
  printf 'TEST:FAIL:abi_stamps_strict_no_skip_canary:strict_mode_expected_rc_1_got:%d\n' "$STRICT_RC"
  cat "$STRICT_LOG"
  exit 1
fi
if ! grep -qF "ABI_STAMP:FAIL:docs/abi/syscalls.md:no_stamp_line" "$STRICT_LOG"; then
  printf 'TEST:FAIL:abi_stamps_strict_no_skip_canary:strict_mode_missing_fail_marker\n'
  cat "$STRICT_LOG"
  exit 1
fi
# Exempt file must be dropped from iteration entirely under strict mode.
if grep -q "docs/abi/index-only.md" "$STRICT_LOG"; then
  printf 'TEST:FAIL:abi_stamps_strict_no_skip_canary:exempt_file_leaked_into_output\n'
  cat "$STRICT_LOG"
  exit 1
fi

# --- Arm 3: STRICT_STAMPS=1 env (wrapper path) must produce the same FAIL.
WRAP_LOG="$TMP_DIR/wrap.log"
set +e
STRICT_STAMPS=1 bash "$ROOT_DIR/build/scripts/validate_abi_stamps.sh" \
  --abi-dir "$SANDBOX/docs/abi" \
  --exempt index-only.md \
  > "$WRAP_LOG" 2>&1
WRAP_RC=$?
set -e

# Note: the wrapper drives the validator against $ROOT_DIR but with
# --abi-dir pointing at our sandbox, so git history queries run against
# the real repo. The unstamped sandbox file does not exist in the real
# repo's git index, so last_content_commit() returns None for it and the
# strict-mode no_stamp_line check still fires before we get to the git
# history lookup. We only assert the strict marker + rc here.
if [ "$WRAP_RC" -ne 1 ]; then
  printf 'TEST:FAIL:abi_stamps_strict_no_skip_canary:wrapper_strict_expected_rc_1_got:%d\n' "$WRAP_RC"
  cat "$WRAP_LOG"
  exit 1
fi
if ! grep -qF "ABI_STAMP:FAIL:" "$WRAP_LOG"; then
  printf 'TEST:FAIL:abi_stamps_strict_no_skip_canary:wrapper_strict_missing_fail_marker\n'
  cat "$WRAP_LOG"
  exit 1
fi

printf 'TEST:PASS:abi_stamps_strict_no_skip_canary\n'
exit 0
