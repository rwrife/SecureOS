#!/usr/bin/env bash
# tests/harness/sosh_contract_registry_drift_test.sh
#
# Negative canary for the sosh-contract drift validator
# (issue #351, drift-validator slice).
#
# Why this exists:
#   build/scripts/validate_sosh_capability_contract.sh claims that a
#   `CAP_*` cited in §4 of docs/abi/sosh-capability-contract.md but
#   missing from docs/abi/capability-registry.json is a hard failure.
#   This canary proves that claim is real — not a silent no-op from a
#   stale dispatcher arm, a typo in the regex, or a doc-parse path that
#   walks off the end of §4 without consuming any rows. Mirrors the
#   discipline in #213 (validator harness self-test) and the
#   #234 / capability_registry_drift + #297 / abi_stamps_drift canaries.
#
# Mechanics:
#   1. Build a throwaway repo skeleton containing:
#        - docs/abi/capability-registry.json with ONE registered cap
#          (CAP_CONSOLE_WRITE) using the canonical deny-marker shape.
#        - docs/abi/sosh-capability-contract.md with a §4 table that
#          cites BOTH that cap AND a fake `CAP_TOTALLY_FAKE` that is
#          deliberately absent from the registry — the drift the
#          validator must catch.
#   2. Run validate_sosh_capability_contract.py against that sandbox
#      and assert:
#        - exit code 1
#        - stderr contains
#          SOSH_CONTRACT:FAIL:cap_missing_from_registry:CAP_TOTALLY_FAKE
#        - stdout still contains a PASS line for CAP_CONSOLE_WRITE
#          (proving the validator did walk the table and was not
#          fail-fast on the first row).
#   3. Bonus check: also exercise the marker-name drift path by
#      rewriting the row for CAP_CONSOLE_WRITE to cite a marker name
#      ("console_yell") that does not match the registry entry, and
#      assert SOSH_CONTRACT:FAIL:marker_name_mismatch:CAP_CONSOLE_WRITE
#      fires. This proves the second check arm is wired, mirroring the
#      multi-check coverage of capability_registry_drift_test.sh.
#
# Contract:
#   On success: TEST:PASS:sosh_contract_registry_drift_canary, exit 0.
#   On failure: TEST:FAIL:sosh_contract_registry_drift_canary:<reason>, exit 1.

set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
# shellcheck disable=SC2064
trap "rm -rf '$TMP_DIR'" EXIT

printf 'TEST:START:sosh_contract_registry_drift_canary\n'

SANDBOX="$TMP_DIR/repo"
mkdir -p "$SANDBOX/docs/abi" "$SANDBOX/tools"

cp "$ROOT_DIR/tools/validate_sosh_capability_contract.py" \
   "$SANDBOX/tools/validate_sosh_capability_contract.py" \
  || { printf 'TEST:FAIL:sosh_contract_registry_drift_canary:tool_copy_failed\n'; exit 1; }

# Sandbox registry — only ONE cap registered; CAP_TOTALLY_FAKE absent.
cat > "$SANDBOX/docs/abi/capability-registry.json" <<'EOF'
{
  "$schema_version": 0,
  "description": "sandbox registry for sosh_contract_registry_drift canary",
  "frozen_marker_grammar": "^CAP:DENY:[0-9]+:[a-z][a-z0-9_]*:[\\x20-\\x39\\x3B-\\x7E]+$",
  "capabilities": [
    {
      "cap_id": "CAP_CONSOLE_WRITE",
      "numeric_id": 1,
      "subject_kinds": ["app"],
      "deny_marker": "CAP:DENY:<actor_subject_id>:console_write:-",
      "allow_test_target": "helloapp_allow",
      "deny_test_target": "helloapp_deny",
      "owning_plan": null,
      "frozen_since_abi": "0.0"
    }
  ]
}
EOF

# Sandbox §4 table that drifts in two ways at once: it cites a
# CAP_TOTALLY_FAKE the registry has never heard of, AND it spells the
# CAP_CONSOLE_WRITE deny-marker name wrong ("console_yell"). The
# validator should catch BOTH and still emit one PASS line for the
# fake-`(if defined)` row to prove it did not abort mid-walk.
CONTRACT="$SANDBOX/docs/abi/sosh-capability-contract.md"
cat > "$CONTRACT" <<'EOF'
# sosh capability contract (sandbox)

## 4. Side-effecting builtins + required capabilities

| sosh surface | syscall | Required cap | Deny marker |
| ------------ | ------- | ------------ | ----------- |
| `echo`       | `os_console_write` | `CAP_CONSOLE_WRITE` | `CAP:DENY:<sid>:console_yell:-` |
| `cat`        | `os_fs_read_file`  | `CAP_TOTALLY_FAKE`  | `CAP:DENY:<sid>:totally_fake:-` |
| `export`     | env set            | `CAP_ENV_WRITE` (if defined) | `CAP:DENY:<sid>:env_write:<var>` |
EOF

PY="${PYTHON:-python3}"
OUT="$TMP_DIR/stdout"
ERR="$TMP_DIR/stderr"

set +e
"$PY" "$SANDBOX/tools/validate_sosh_capability_contract.py" --root "$SANDBOX" \
  > "$OUT" 2> "$ERR"
RC=$?
set -e

if [[ "$RC" -ne 1 ]]; then
  printf 'TEST:FAIL:sosh_contract_registry_drift_canary:unexpected_exit_code:%d\n' "$RC"
  printf '----- stdout -----\n' >&2; cat "$OUT" >&2
  printf '----- stderr -----\n' >&2; cat "$ERR" >&2
  exit 1
fi

if ! grep -Fq 'SOSH_CONTRACT:FAIL:cap_missing_from_registry:CAP_TOTALLY_FAKE' "$ERR"; then
  printf 'TEST:FAIL:sosh_contract_registry_drift_canary:missing_cap_marker_absent\n'
  cat "$ERR" >&2
  exit 1
fi

if ! grep -Fq 'SOSH_CONTRACT:FAIL:marker_name_mismatch:CAP_CONSOLE_WRITE:doc=console_yell:registry=console_write' "$ERR"; then
  printf 'TEST:FAIL:sosh_contract_registry_drift_canary:marker_mismatch_marker_absent\n'
  cat "$ERR" >&2
  exit 1
fi

if ! grep -Fq 'SOSH_CONTRACT:PASS:CAP_ENV_WRITE:if_defined_absent_ok' "$OUT"; then
  printf 'TEST:FAIL:sosh_contract_registry_drift_canary:if_defined_pass_absent\n'
  cat "$OUT" >&2
  exit 1
fi

printf 'TEST:PASS:sosh_contract_registry_drift_canary\n'
exit 0
