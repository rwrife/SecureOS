#!/usr/bin/env bash
# build/scripts/validate_docs_pickup_guide_m7_stamp.sh
#
# Issue #624: freshness gate for docs/development/pickup-guide-m7.md.
# Reuses the ABI-stamp validator logic to enforce:
#   1) the doc has a `Last verified against commit: <sha>` line,
#   2) the SHA resolves in git, and
#   3) the recorded SHA is at least as new as the file's most-recent
#      content-changing commit.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TARGET_REL="docs/development/pickup-guide-m7.md"
TARGET_PATH="$ROOT_DIR/$TARGET_REL"

if [[ ! -f "$TARGET_PATH" ]]; then
  echo "DOC_STAMP:FAIL:${TARGET_REL}:missing" >&2
  exit 1
fi

PY="${PYTHON:-python3}"
if ! command -v "$PY" >/dev/null 2>&1; then
  echo "DOC_STAMP:FAIL:${TARGET_REL}:python3_not_found" >&2
  exit 2
fi

ARGS=(
  --root "$ROOT_DIR"
  --abi-dir "$ROOT_DIR/docs/development"
  --strict-no-skip
  --strict-no-placeholder
)

# Keep only pickup-guide-m7.md in scope; exempt every other docs/development
# markdown file so this target remains stable as new docs are added.
while IFS= read -r name; do
  [[ "$name" == "pickup-guide-m7.md" ]] && continue
  ARGS+=(--exempt "$name")
done < <(find "$ROOT_DIR/docs/development" -maxdepth 1 -type f -name '*.md' -printf '%f\n' | sort)

exec "$PY" "$ROOT_DIR/tools/validate_abi_stamps.py" "${ARGS[@]}"
