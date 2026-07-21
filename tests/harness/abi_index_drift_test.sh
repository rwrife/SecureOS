#!/usr/bin/env bash
# tests/harness/abi_index_drift_test.sh
#
# Negative canary for tools/validate_abi_index.py (issue #630).
# Demonstrates failure when an ABI doc exists on disk but is not linked
# from docs/abi/README.md, and when README links a dangling file.

set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
# shellcheck disable=SC2064
trap "rm -rf '$TMP_DIR'" EXIT

printf 'TEST:START:abi_index_drift_canary\n'

SANDBOX="$TMP_DIR/repo"
mkdir -p "$SANDBOX/docs/abi"

cat > "$SANDBOX/docs/abi/README.md" <<'EOF'
# ABI index sandbox

- [syscalls.md](syscalls.md)
- [dangling.md](dangling.md)
EOF

cat > "$SANDBOX/docs/abi/syscalls.md" <<'EOF'
# syscalls
EOF

cat > "$SANDBOX/docs/abi/manifest.md" <<'EOF'
# manifest (present but unlinked)
EOF

OUT="$TMP_DIR/stdout.log"
ERR="$TMP_DIR/stderr.log"

set +e
python3 "$ROOT_DIR/tools/validate_abi_index.py" --root "$SANDBOX" > "$OUT" 2> "$ERR"
RC=$?
set -e

if [[ "$RC" -ne 1 ]]; then
  printf 'TEST:FAIL:abi_index_drift_canary:unexpected_exit:%d\n' "$RC"
  printf '----- stdout -----\n' >&2; cat "$OUT" >&2
  printf '----- stderr -----\n' >&2; cat "$ERR" >&2
  exit 1
fi

if ! grep -Fq 'ABI_INDEX:FAIL:missing_link:manifest.md' "$ERR"; then
  printf 'TEST:FAIL:abi_index_drift_canary:missing_link_marker_absent\n'
  cat "$ERR" >&2
  exit 1
fi

if ! grep -Fq 'ABI_INDEX:FAIL:dangling_link:dangling.md' "$ERR"; then
  printf 'TEST:FAIL:abi_index_drift_canary:dangling_link_marker_absent\n'
  cat "$ERR" >&2
  exit 1
fi

if ! grep -Fq 'ABI_INDEX:DIFF:--- expected_docs_abi_md' "$ERR"; then
  printf 'TEST:FAIL:abi_index_drift_canary:diff_header_absent\n'
  cat "$ERR" >&2
  exit 1
fi

printf 'TEST:PASS:abi_index_drift_canary\n'
exit 0
