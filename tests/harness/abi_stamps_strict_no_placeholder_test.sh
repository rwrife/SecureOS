#!/usr/bin/env bash
# tests/harness/abi_stamps_strict_no_placeholder_test.sh
#
# Negative canary for the ABI-stamp placeholder gate (issue #470,
# co-scope of #479's --strict-no-skip).
#
# Why this exists:
#   tools/validate_abi_stamps.py historically required `Last verified
#   against commit:` to be a 7-40 char hex SHA. A literal placeholder
#   like `HEAD` (the #463 manifest.md case) silently fell through the
#   strict-SHA regex to the `no_stamp_line` SKIP arm, exit 0 — exactly
#   the silent-bypass shape #470 was filed to close.
#
#   This canary proves three contract arms in a single sandbox:
#     1. Default mode still emits `ABI_STAMP:SKIP:<file>:placeholder:HEAD`
#        (exit 0) for a placeholder-stamp doc — backward-compatible.
#     2. `--strict-no-placeholder` emits
#        `ABI_STAMP:FAIL:<file>:placeholder:HEAD` (exit 1) for the same
#        doc.
#     3. A genuinely-missing line (`no_stamp_line`) is still the
#        no_stamp_line SKIP under default — i.e. --strict-no-placeholder
#        does not over-reach into the no_stamp_line arm (that arm is
#        #479's --strict-no-skip's job).
#
# Mirrors the canary discipline from #213 / #234 / #297 / #479.
#
# Contract:
#   On success: TEST:PASS:abi_stamps_strict_no_placeholder_canary, exit 0.
#   On failure: TEST:FAIL:abi_stamps_strict_no_placeholder_canary:<reason>, exit 1.

set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
# shellcheck disable=SC2064
trap "rm -rf '$TMP_DIR'" EXIT

printf 'TEST:START:abi_stamps_strict_no_placeholder_canary\n'

SANDBOX="$TMP_DIR/repo"
mkdir -p "$SANDBOX/docs/abi"
(
  cd "$SANDBOX"
  git init -q -b main
  git config user.email canary@local
  git config user.name canary
) || { printf 'TEST:FAIL:abi_stamps_strict_no_placeholder_canary:sandbox_git_init_failed\n'; exit 1; }

PLACEHOLDER_DOC="$SANDBOX/docs/abi/syscalls.md"
NO_STAMP_DOC="$SANDBOX/docs/abi/ipc-wire.md"

# Placeholder stamp: `HEAD` is the canonical #463 shape that used to
# silently SKIP. Using `HEAD` here keeps the regression linked to the
# real-world bug.
cat > "$PLACEHOLDER_DOC" <<'EOF'
# Sandbox: ABI surface with placeholder stamp

Initial body text.

Last verified against commit: HEAD
EOF

# Peer file with no stamp line at all — proves --strict-no-placeholder
# does NOT promote the no_stamp_line arm (that's --strict-no-skip's job
# in #479).
cat > "$NO_STAMP_DOC" <<'EOF'
# Sandbox: ABI surface with no stamp line

Initial body text. No `Last verified against commit:` line at all.
EOF

(
  cd "$SANDBOX"
  git add docs/abi/syscalls.md docs/abi/ipc-wire.md
  git commit -q -m "sandbox: placeholder + no-stamp docs"
) || { printf 'TEST:FAIL:abi_stamps_strict_no_placeholder_canary:sandbox_commit_failed\n'; exit 1; }

# Arm 1: default mode — placeholder line should drop to SKIP, exit 0.
LOG1="$TMP_DIR/default.log"
set +e
python3 "$ROOT_DIR/tools/validate_abi_stamps.py" --root "$SANDBOX" \
  > "$LOG1" 2>&1
RC1=$?
set -e
if [ "$RC1" -ne 0 ]; then
  printf 'TEST:FAIL:abi_stamps_strict_no_placeholder_canary:default_mode_nonzero_exit:%s\n' "$RC1"
  cat "$LOG1"
  exit 1
fi
if ! grep -qE 'ABI_STAMP:SKIP:docs/abi/syscalls\.md:placeholder:HEAD' "$LOG1"; then
  printf 'TEST:FAIL:abi_stamps_strict_no_placeholder_canary:default_mode_missing_placeholder_skip\n'
  cat "$LOG1"
  exit 1
fi
if ! grep -qE 'ABI_STAMP:SKIP:docs/abi/ipc-wire\.md:no_stamp_line' "$LOG1"; then
  printf 'TEST:FAIL:abi_stamps_strict_no_placeholder_canary:default_mode_missing_no_stamp_skip\n'
  cat "$LOG1"
  exit 1
fi

# Arm 2: --strict-no-placeholder — placeholder line must FAIL, exit 1.
LOG2="$TMP_DIR/strict.log"
set +e
python3 "$ROOT_DIR/tools/validate_abi_stamps.py" --root "$SANDBOX" \
  --strict-no-placeholder \
  > "$LOG2" 2>&1
RC2=$?
set -e
if [ "$RC2" -ne 1 ]; then
  printf 'TEST:FAIL:abi_stamps_strict_no_placeholder_canary:strict_mode_wrong_exit:%s\n' "$RC2"
  cat "$LOG2"
  exit 1
fi
if ! grep -qE 'ABI_STAMP:FAIL:docs/abi/syscalls\.md:placeholder:HEAD' "$LOG2"; then
  printf 'TEST:FAIL:abi_stamps_strict_no_placeholder_canary:strict_mode_missing_placeholder_fail\n'
  cat "$LOG2"
  exit 1
fi

# Arm 3: --strict-no-placeholder must NOT promote the no_stamp_line arm.
# The ipc-wire.md doc has no stamp line at all; under
# --strict-no-placeholder alone it should still SKIP, not FAIL. (Promoting
# that arm is #479's --strict-no-skip's job; the two flags are
# orthogonal.)
if ! grep -qE 'ABI_STAMP:SKIP:docs/abi/ipc-wire\.md:no_stamp_line' "$LOG2"; then
  printf 'TEST:FAIL:abi_stamps_strict_no_placeholder_canary:strict_mode_overreached_into_no_stamp_arm\n'
  cat "$LOG2"
  exit 1
fi

# Arm 4: --exempt continues to drop the placeholder file from iteration
# entirely (same semantics as the existing capability-registry.md
# exemption). With both docs exempt, the validator should fail with
# `no_files_in_scope`, proving the exempt list is honored before the
# placeholder gate fires.
LOG3="$TMP_DIR/exempt.log"
set +e
python3 "$ROOT_DIR/tools/validate_abi_stamps.py" --root "$SANDBOX" \
  --strict-no-placeholder \
  --exempt syscalls.md --exempt ipc-wire.md \
  > "$LOG3" 2>&1
RC3=$?
set -e
if [ "$RC3" -ne 2 ]; then
  printf 'TEST:FAIL:abi_stamps_strict_no_placeholder_canary:exempt_path_wrong_exit:%s\n' "$RC3"
  cat "$LOG3"
  exit 1
fi
if ! grep -qE 'ABI_STAMP:FAIL:no_files_in_scope' "$LOG3"; then
  printf 'TEST:FAIL:abi_stamps_strict_no_placeholder_canary:exempt_path_missing_no_files_in_scope\n'
  cat "$LOG3"
  exit 1
fi

printf 'TEST:PASS:abi_stamps_strict_no_placeholder_canary\n'
exit 0
