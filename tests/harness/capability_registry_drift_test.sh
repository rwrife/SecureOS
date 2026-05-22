#!/usr/bin/env bash
# tests/integration/_canary_must_fail/capability_registry_drift.sh
#
# Negative canary for the capability-registry validator (issue #234).
#
# Why this exists:
#   The validator in build/scripts/validate_capability_registry.sh claims
#   that adding a brand-new CAP_* to kernel/cap/capability.h without
#   adding a matching row to docs/abi/capability-registry.json is a hard
#   failure. This canary proves that claim is real — not a silent no-op
#   from a stale dispatcher arm or a typo in the regex. It mirrors the
#   discipline introduced in #213 (validator harness self-test) and #177
#   (canary_must_fail).
#
# Mechanics:
#   1. Stage a sandbox copy of capability.h with a fake CAP_REGISTRY_DRIFT_PROBE
#      entry appended to the capability_id_t enum.
#   2. Run the validator against that sandbox header (keeping the real
#      registry + test.sh as-is, so the only delta is the extra enum entry).
#   3. Assert the validator exits non-zero AND emitted the
#      REGISTRY_VALIDATE:FAIL:enum_not_in_registry:CAP_REGISTRY_DRIFT_PROBE
#      marker. Anything else (including a clean exit 0) is itself a
#      regression — the validator went blind.
#
# Contract:
#   On success this script emits TEST:PASS:capability_registry_drift_canary
#   and exits 0. On failure it emits TEST:FAIL:capability_registry_drift_canary:<reason>
#   and exits 1.

set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
# shellcheck disable=SC2064
trap "rm -rf '$TMP_DIR'" EXIT

printf 'TEST:START:capability_registry_drift_canary\n'

REAL_HEADER="$ROOT_DIR/kernel/cap/capability.h"
SANDBOX_HEADER="$TMP_DIR/capability.h"

if [ ! -f "$REAL_HEADER" ]; then
    printf 'TEST:FAIL:capability_registry_drift_canary:real_header_missing\n'
    exit 1
fi

# Append a fake CAP_* into the capability_id_t enum by injecting it right
# before the closing brace of that specific enum. We do not mutate any
# other enum (CAP_AUDIT_*, CAP_ACCESS_*) — the validator only looks at
# capability_id_t, so the injection has to land inside that block.
if ! REAL_HEADER="$REAL_HEADER" python3 -c '
import os, re, sys, pathlib
src = pathlib.Path(os.environ["REAL_HEADER"]).read_text(encoding="utf-8")
def inject(match):
    body = match.group("body").rstrip()
    if not body.endswith(","):
        body += ","
    body += "\n  CAP_REGISTRY_DRIFT_PROBE = 999,\n"
    return "typedef enum {" + body + "} capability_id_t;"
out = re.sub(
    r"typedef\s+enum\s*\{(?P<body>.*?)\}\s*capability_id_t\s*;",
    inject, src, count=1, flags=re.DOTALL,
)
if out == src:
    sys.stderr.write("could not locate capability_id_t enum in sandbox\n")
    sys.exit(2)
sys.stdout.write(out)
' > "$SANDBOX_HEADER"; then
    printf 'TEST:FAIL:capability_registry_drift_canary:sandbox_injection_failed\n'
    exit 1
fi

# Run the Python validator directly with --header override so we don't
# have to clone the rest of the tree.
LOG_PATH="$TMP_DIR/validator.log"
set +e
python3 "$ROOT_DIR/tools/validate_capability_registry.py" \
    --root "$ROOT_DIR" \
    --header "$SANDBOX_HEADER" \
    > "$LOG_PATH" 2>&1
RC=$?
set -e

if [ "$RC" -eq 0 ]; then
    printf 'TEST:FAIL:capability_registry_drift_canary:validator_returned_zero_on_drift\n'
    cat "$LOG_PATH"
    exit 1
fi

if ! grep -q "REGISTRY_VALIDATE:FAIL:enum_not_in_registry:CAP_REGISTRY_DRIFT_PROBE" "$LOG_PATH"; then
    printf 'TEST:FAIL:capability_registry_drift_canary:expected_marker_missing\n'
    cat "$LOG_PATH"
    exit 1
fi

printf 'TEST:PASS:capability_registry_drift_canary\n'
exit 0
