#!/usr/bin/env bash
# tests/harness/abi_stamps_drift_test.sh
#
# Negative canary for the ABI-stamp-freshness validator (issue #297).
#
# Why this exists:
#   build/scripts/validate_abi_stamps.sh claims that a docs/abi/*.md
#   file whose `Last verified against commit:` stamp predates the
#   file's most recent content-changing commit is a hard failure.
#   This canary proves that claim is real -- not a silent no-op from a
#   stale dispatcher arm, a typo in the regex, or a git-log invocation
#   that returns the wrong order. Mirrors the discipline in #213
#   (validator harness self-test) and #234 / capability_registry_drift.
#
# Mechanics:
#   1. Build a throwaway git repo with one in-scope docs/abi/syscalls.md
#      (initial commit + a stamp-only bump so the stamp validly points
#      at the first commit's SHA).
#   2. Add a third commit that edits prose but leaves the stamp alone.
#      This is the drift the validator must catch.
#   3. Run validate_abi_stamps.py against that sandbox repo and assert:
#      - exit code 1
#      - stderr contains ABI_STAMP:FAIL:docs/abi/syscalls.md:stamp=<OLD>:last_content=<NEW>
#
# Contract:
#   On success: TEST:PASS:abi_stamps_drift_canary, exit 0.
#   On failure: TEST:FAIL:abi_stamps_drift_canary:<reason>, exit 1.

set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
# shellcheck disable=SC2064
trap "rm -rf '$TMP_DIR'" EXIT

printf 'TEST:START:abi_stamps_drift_canary\n'

SANDBOX="$TMP_DIR/repo"
mkdir -p "$SANDBOX/docs/abi"
(
  cd "$SANDBOX"
  git init -q -b main
  git config user.email canary@local
  git config user.name canary
) || { printf 'TEST:FAIL:abi_stamps_drift_canary:sandbox_git_init_failed\n'; exit 1; }

DOC="$SANDBOX/docs/abi/syscalls.md"

# Commit 1: initial content with a placeholder stamp.
cat > "$DOC" <<'EOF'
# Sandbox: ABI surface

Initial body text.

Last verified against commit: PLACEHOLDER_STAMP
EOF
(
  cd "$SANDBOX"
  git add docs/abi/syscalls.md
  git commit -q -m "sandbox: initial content"
) || { printf 'TEST:FAIL:abi_stamps_drift_canary:sandbox_commit1_failed\n'; exit 1; }
C1_SHA="$(git -C "$SANDBOX" rev-parse HEAD)"

# Commit 2: stamp-only bump to point at C1 (treated as non-content by
# the validator, so last_content stays at C1).
sed -i "s|PLACEHOLDER_STAMP|${C1_SHA}|" "$DOC"
(
  cd "$SANDBOX"
  git add docs/abi/syscalls.md
  git commit -q -m "sandbox: stamp-only bump to commit 1"
) || { printf 'TEST:FAIL:abi_stamps_drift_canary:sandbox_stamp_bump_failed\n'; exit 1; }
OLD_SHA="$C1_SHA"

# Commit 3: edit prose, leave stamp alone -- the drift the validator must catch.
python3 - <<PY
import pathlib
p = pathlib.Path("$DOC")
text = p.read_text()
text = text.replace("Initial body text.", "Edited body text (drift introduced).")
p.write_text(text)
PY
(
  cd "$SANDBOX"
  git add docs/abi/syscalls.md
  git commit -q -m "sandbox: drift-introducing content edit"
) || { printf 'TEST:FAIL:abi_stamps_drift_canary:sandbox_commit3_failed\n'; exit 1; }
NEW_SHA="$(git -C "$SANDBOX" rev-parse HEAD)"

if ! grep -q "Last verified against commit: ${OLD_SHA}" "$DOC"; then
  printf 'TEST:FAIL:abi_stamps_drift_canary:stamp_not_old_after_edit\n'
  exit 1
fi

LOG_PATH="$TMP_DIR/validator.log"
set +e
python3 "$ROOT_DIR/tools/validate_abi_stamps.py" --root "$SANDBOX" \
  > "$LOG_PATH" 2>&1
RC=$?
set -e

if [ "$RC" -eq 0 ]; then
  printf 'TEST:FAIL:abi_stamps_drift_canary:validator_returned_zero_on_drift\n'
  cat "$LOG_PATH"
  exit 1
fi

OLD_PREFIX="${OLD_SHA:0:10}"
NEW_PREFIX="${NEW_SHA:0:10}"
EXPECTED="ABI_STAMP:FAIL:docs/abi/syscalls.md:stamp=${OLD_PREFIX}:last_content=${NEW_PREFIX}"

if ! grep -qF "$EXPECTED" "$LOG_PATH"; then
  printf 'TEST:FAIL:abi_stamps_drift_canary:expected_marker_missing:%s\n' "$EXPECTED"
  cat "$LOG_PATH"
  exit 1
fi

printf 'TEST:PASS:abi_stamps_drift_canary\n'
exit 0
