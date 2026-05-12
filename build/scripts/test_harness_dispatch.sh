#!/usr/bin/env bash
# Fixture for issue #91: validates that build/scripts/test.sh fails
# deterministically when a subordinate validator script is missing or
# unreadable, instead of leaking a `Permission denied` line.
#
# This test reuses the existing dispatch logic via the SECUREOS_SCRIPTS_DIR
# override so it can construct a temporary scripts directory without mutating
# the real tree. It does not require any toolchain or QEMU.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TEST_HARNESS="$ROOT_DIR/build/scripts/test.sh"
HARNESS_ERROR_EXIT=78

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

fail=0

assert_eq() {
  local label="$1" expected="$2" actual="$3"
  if [[ "$expected" == "$actual" ]]; then
    echo "TEST:PASS:test_harness_dispatch:$label"
  else
    echo "TEST:FAIL:test_harness_dispatch:$label expected=$expected actual=$actual" >&2
    fail=1
  fi
}

assert_contains() {
  local label="$1" needle="$2" haystack="$3"
  if [[ "$haystack" == *"$needle"* ]]; then
    echo "TEST:PASS:test_harness_dispatch:$label"
  else
    echo "TEST:FAIL:test_harness_dispatch:$label needle=$needle" >&2
    echo "--- haystack ---" >&2
    echo "$haystack" >&2
    echo "--- end ---" >&2
    fail=1
  fi
}

# --- Case 1: missing subordinate script -------------------------------------
SCRIPTS_DIR_MISSING="$TMP_DIR/missing"
mkdir -p "$SCRIPTS_DIR_MISSING"
# Intentionally do NOT create test_event_bus.sh.

set +e
out="$(SECUREOS_SCRIPTS_DIR="$SCRIPTS_DIR_MISSING" "$TEST_HARNESS" event_bus 2>&1)"
rc=$?
set -e
assert_eq "missing_script_exit_code" "$HARNESS_ERROR_EXIT" "$rc"
assert_contains "missing_script_marker" "TEST:FAIL:harness_missing_script:" "$out"
# Must NOT leak the legacy "Permission denied" surface.
if [[ "$out" == *"Permission denied"* ]]; then
  echo "TEST:FAIL:test_harness_dispatch:missing_script_no_perm_denied_leak" >&2
  fail=1
else
  echo "TEST:PASS:test_harness_dispatch:missing_script_no_perm_denied_leak"
fi

# --- Case 2: unreadable subordinate script (chmod 000) ----------------------
SCRIPTS_DIR_UNREAD="$TMP_DIR/unreadable"
mkdir -p "$SCRIPTS_DIR_UNREAD"
echo '#!/usr/bin/env bash' >"$SCRIPTS_DIR_UNREAD/test_event_bus.sh"
chmod 000 "$SCRIPTS_DIR_UNREAD/test_event_bus.sh"
# Restore permissions on cleanup so trap rm works.
trap 'chmod -R u+rw "$TMP_DIR" 2>/dev/null || true; rm -rf "$TMP_DIR"' EXIT

if [[ "${EUID:-$(id -u)}" -eq 0 ]]; then
  # Root bypasses the 000 read check; skip this case but record a deterministic
  # marker so CI logs reflect why.
  echo "TEST:SKIP:test_harness_dispatch:unreadable_script_marker reason=running_as_root"
else
  set +e
  out="$(SECUREOS_SCRIPTS_DIR="$SCRIPTS_DIR_UNREAD" "$TEST_HARNESS" event_bus 2>&1)"
  rc=$?
  set -e
  assert_eq "unreadable_script_exit_code" "$HARNESS_ERROR_EXIT" "$rc"
  assert_contains "unreadable_script_marker" "TEST:FAIL:harness_unreadable_script:" "$out"
fi

# --- Case 3: present-but-non-+x script still runs via `bash <path>` ---------
SCRIPTS_DIR_OK="$TMP_DIR/no_exec_bit"
mkdir -p "$SCRIPTS_DIR_OK"
cat >"$SCRIPTS_DIR_OK/test_event_bus.sh" <<'STUB'
#!/usr/bin/env bash
echo "TEST:PASS:stub_event_bus"
STUB
chmod 644 "$SCRIPTS_DIR_OK/test_event_bus.sh"  # explicitly no +x

set +e
out="$(SECUREOS_SCRIPTS_DIR="$SCRIPTS_DIR_OK" "$TEST_HARNESS" event_bus 2>&1)"
rc=$?
set -e
assert_eq "no_exec_bit_exit_code" "0" "$rc"
assert_contains "no_exec_bit_runs_via_bash" "TEST:PASS:stub_event_bus" "$out"

if [[ $fail -ne 0 ]]; then
  echo "TEST:FAIL:test_harness_dispatch" >&2
  exit 1
fi

echo "TEST:PASS:test_harness_dispatch"
