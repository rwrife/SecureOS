#!/usr/bin/env bash
# test_matrix.sh — capability matrix harness (BUILD_ROADMAP §6.1 nightly / §6.2)
#
# Iterates the {cap_set, faux_policy, lifecycle_event} cells declared in
# tests/matrix/capability_matrix.json and re-runs the corresponding existing
# validator targets (see build/scripts/test.sh) once per cell. Per-cell logs
# and a pass/fail JSON are written under artifacts/runs/matrix-<run-id>/, plus
# a top-level matrix_report.json summary.
#
# Design notes (issue #151):
#   * This is intentionally a thin orchestrator — it does NOT introduce new
#     test binaries. Each cell composes onto today's deterministic targets so
#     adding/removing a cell costs one JSON edit.
#   * No yaml/jq dependency: matrix file is JSON, parsed with python3 (already
#     a CI-installed dep per .github/workflows/iso-vm-build.yml).
#   * Exit code: 0 if all cells pass, 1 otherwise. CI is expected to wire this
#     as a non-blocking nightly stage until the matrix stabilizes.
#
# Usage:
#   build/scripts/test_matrix.sh                  # run every cell
#   build/scripts/test_matrix.sh <cell-id>...     # run only the named cells
#   SECUREOS_MATRIX_FILE=path/to/matrix.json build/scripts/test_matrix.sh
#
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MATRIX_FILE="${SECUREOS_MATRIX_FILE:-$ROOT_DIR/tests/matrix/capability_matrix.json}"
RUN_ID="${SECUREOS_RUN_ID:-matrix-$(date -u +"%Y%m%dT%H%M%SZ")-$(git -C "$ROOT_DIR" rev-parse --short HEAD 2>/dev/null || echo nogit)}"
RUN_DIR="$ROOT_DIR/artifacts/runs/$RUN_ID"
REPORT_PATH="$RUN_DIR/matrix_report.json"

mkdir -p "$RUN_DIR"

if [[ ! -f "$MATRIX_FILE" ]]; then
  echo "test_matrix: matrix file not found: $MATRIX_FILE" >&2
  exit 2
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "test_matrix: python3 is required to parse the matrix JSON" >&2
  exit 2
fi

# Emit one whitespace-separated line per cell: "<id> <cap_set> <faux_policy> <lifecycle_event> <target1,target2,...>"
mapfile -t CELLS < <(python3 - "$MATRIX_FILE" <<'PY'
import json, sys
with open(sys.argv[1], "r", encoding="utf-8") as fh:
    doc = json.load(fh)
for cell in doc.get("cells", []):
    targets = ",".join(cell.get("targets", []))
    print(f"{cell['id']} {cell['cap_set']} {cell['faux_policy']} {cell['lifecycle_event']} {targets}")
PY
)

if [[ ${#CELLS[@]} -eq 0 ]]; then
  echo "test_matrix: no cells declared in $MATRIX_FILE" >&2
  exit 2
fi

# Optional cell-id allowlist from positional args.
SELECTED=()
if [[ $# -gt 0 ]]; then
  SELECTED=("$@")
fi

cell_selected() {
  local id="$1"
  if [[ ${#SELECTED[@]} -eq 0 ]]; then
    return 0
  fi
  local s
  for s in "${SELECTED[@]}"; do
    [[ "$s" == "$id" ]] && return 0
  done
  return 1
}

OVERALL_RC=0
CELL_JSON_FRAGMENTS=()

for cell in "${CELLS[@]}"; do
  # shellcheck disable=SC2206
  fields=($cell)
  CELL_ID="${fields[0]}"
  CAP_SET="${fields[1]}"
  FAUX_POLICY="${fields[2]}"
  LIFECYCLE="${fields[3]}"
  TARGETS_CSV="${fields[4]:-}"

  if ! cell_selected "$CELL_ID"; then
    continue
  fi

  CELL_DIR="$RUN_DIR/$CELL_ID"
  mkdir -p "$CELL_DIR"

  # Persist the cell config alongside its log for forensic replay.
  cat > "$CELL_DIR/cell.json" <<EOF
{
  "id": "$CELL_ID",
  "cap_set": "$CAP_SET",
  "faux_policy": "$FAUX_POLICY",
  "lifecycle_event": "$LIFECYCLE",
  "targets": [$(printf '"%s",' ${TARGETS_CSV//,/ } | sed 's/,$//')]
}
EOF

  CELL_LOG="$CELL_DIR/cell.log"
  : > "$CELL_LOG"
  CELL_RC=0
  TARGET_STATUSES=()

  IFS=',' read -r -a TARGETS <<< "$TARGETS_CSV"
  for target in "${TARGETS[@]}"; do
    [[ -z "$target" ]] && continue
    echo "::matrix:: cell=$CELL_ID target=$target" | tee -a "$CELL_LOG"
    if SECUREOS_MATRIX_CELL="$CELL_ID" \
       SECUREOS_MATRIX_CAP_SET="$CAP_SET" \
       SECUREOS_MATRIX_FAUX_POLICY="$FAUX_POLICY" \
       SECUREOS_MATRIX_LIFECYCLE="$LIFECYCLE" \
       "$ROOT_DIR/build/scripts/test.sh" "$target" >>"$CELL_LOG" 2>&1; then
      TARGET_STATUSES+=("\"$target\":\"pass\"")
    else
      CELL_RC=1
      OVERALL_RC=1
      TARGET_STATUSES+=("\"$target\":\"fail\"")
    fi
  done

  CELL_STATUS="pass"
  [[ $CELL_RC -ne 0 ]] && CELL_STATUS="fail"

  cat > "$CELL_DIR/result.json" <<EOF
{
  "cell": "$CELL_ID",
  "status": "$CELL_STATUS",
  "targets": { $(IFS=,; echo "${TARGET_STATUSES[*]}") },
  "log": "$CELL_DIR/cell.log"
}
EOF

  CELL_JSON_FRAGMENTS+=("{\"id\":\"$CELL_ID\",\"cap_set\":\"$CAP_SET\",\"faux_policy\":\"$FAUX_POLICY\",\"lifecycle_event\":\"$LIFECYCLE\",\"status\":\"$CELL_STATUS\",\"targets\":{$(IFS=,; echo "${TARGET_STATUSES[*]}")},\"artifacts\":\"$CELL_DIR\"}")
done

OVERALL_STATUS="pass"
[[ $OVERALL_RC -ne 0 ]] && OVERALL_STATUS="fail"

GENERATED_AT="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
GIT_SHA="$(git -C "$ROOT_DIR" rev-parse HEAD 2>/dev/null || echo unknown)"

{
  echo "{"
  echo "  \"schema\": \"secureos.matrix_report.v0\","
  echo "  \"run_id\": \"$RUN_ID\","
  echo "  \"generated_at\": \"$GENERATED_AT\","
  echo "  \"git_sha\": \"$GIT_SHA\","
  echo "  \"matrix_file\": \"$MATRIX_FILE\","
  echo "  \"status\": \"$OVERALL_STATUS\","
  echo "  \"cells\": ["
  IFS=,
  echo "    ${CELL_JSON_FRAGMENTS[*]}"
  unset IFS
  echo "  ]"
  echo "}"
} > "$REPORT_PATH"

echo "test_matrix: report written to $REPORT_PATH (status=$OVERALL_STATUS)"
exit "$OVERALL_RC"
