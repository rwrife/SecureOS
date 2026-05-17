#!/usr/bin/env bash
# _os_wrapper_common.sh — shared helpers for the os-* deterministic agent
# tool wrappers (BUILD_ROADMAP §4.3 / issue #162).
#
# Sourced by os-build, os-package, os-run-qemu, os-validate, os-snapshot.
#
# Contract (per wrapper, on stdout AND mirrored into the per-run bundle):
#   {
#     "tool":        "os-build" | "os-package" | "os-run-qemu" | "os-validate" | "os-snapshot",
#     "version":     "1",
#     "ok":          true | false,
#     "exit_code":   <int>,
#     "artifacts":   [ "<path>", ... ],
#     "started_at":  "YYYY-MM-DDTHH:MM:SSZ",
#     "finished_at": "YYYY-MM-DDTHH:MM:SSZ",
#     "reason":      "<short failure description, only when ok=false>"
#   }
#
# Behavior notes:
#   - Wrappers are intentionally thin: they do not re-implement the underlying
#     build/test logic, only stabilize the agent-facing surface (#162 scope).
#   - JSON is always emitted on stdout, regardless of --json (the flag exists
#     for forward-compat with future human-readable modes).
#   - The mirrored file lives at
#         artifacts/runs/<SECUREOS_RUN_ID>/<tool>.json
#     coordinating with the per-run bundle layout from #161 / validate_bundle.sh.

# Resolve repo root (one level up from build/scripts/).
OS_WRAPPER_ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

OS_WRAPPER_API_VERSION="1"

os_wrapper_now() { date -u +"%Y-%m-%dT%H:%M:%SZ"; }

# Stable run id: prefer caller-provided SECUREOS_RUN_ID, else derive
# timestamp-shortsha (matches validate_bundle.sh / run_qemu.sh #161 contract).
os_wrapper_run_id() {
  if [[ -n "${SECUREOS_RUN_ID:-}" ]]; then
    printf '%s' "$SECUREOS_RUN_ID"
    return 0
  fi
  local ts sha
  ts="$(date -u +"%Y%m%dT%H%M%SZ")"
  if sha="$(git -C "$OS_WRAPPER_ROOT_DIR" rev-parse --short HEAD 2>/dev/null)"; then
    printf '%s-%s' "$ts" "$sha"
  else
    printf '%s' "$ts"
  fi
}

# Minimal JSON string escaper: backslash, double-quote, and control chars.
os_wrapper_json_escape() {
  local s="$1"
  s="${s//\\/\\\\}"
  s="${s//\"/\\\"}"
  s="${s//$'\n'/\\n}"
  s="${s//$'\r'/\\r}"
  s="${s//$'\t'/\\t}"
  printf '%s' "$s"
}

# os_wrapper_emit <tool> <ok 0|1> <exit_code> <started_at> <finished_at> <reason> <artifact...>
# Prints the JSON to stdout AND writes it into the run bundle.
os_wrapper_emit() {
  local tool="$1"; shift
  local ok_bool="$1"; shift     # "true" / "false"
  local exit_code="$1"; shift
  local started_at="$1"; shift
  local finished_at="$1"; shift
  local reason="$1"; shift
  local artifacts=("$@")

  local run_id run_dir out_file
  run_id="$(os_wrapper_run_id)"
  run_dir="$OS_WRAPPER_ROOT_DIR/artifacts/runs/$run_id"
  mkdir -p "$run_dir" 2>/dev/null || true
  out_file="$run_dir/$tool.json"

  local arts_json="" first=1 a esc
  for a in "${artifacts[@]}"; do
    esc="$(os_wrapper_json_escape "$a")"
    if [[ $first -eq 1 ]]; then
      arts_json="\"$esc\""
      first=0
    else
      arts_json+=",\"$esc\""
    fi
  done

  local reason_field=""
  if [[ "$ok_bool" == "false" && -n "$reason" ]]; then
    reason_field=",\"reason\":\"$(os_wrapper_json_escape "$reason")\""
  fi

  local json
  json="{\"tool\":\"$tool\",\"version\":\"$OS_WRAPPER_API_VERSION\",\"ok\":$ok_bool,\"exit_code\":$exit_code,\"artifacts\":[${arts_json}],\"started_at\":\"$started_at\",\"finished_at\":\"$finished_at\",\"run_id\":\"$(os_wrapper_json_escape "$run_id")\"$reason_field}"

  printf '%s\n' "$json"
  printf '%s\n' "$json" > "$out_file" 2>/dev/null || true
}

# Helper: run a child script, tee its output, then emit JSON.
# Usage: os_wrapper_run_and_emit <tool> <reason-on-fail> <artifact...> -- <cmd> [args...]
os_wrapper_run_and_emit() {
  local tool="$1"; shift
  local fail_reason="$1"; shift
  local artifacts=()
  while [[ $# -gt 0 && "$1" != "--" ]]; do
    artifacts+=("$1"); shift
  done
  [[ "${1:-}" == "--" ]] && shift

  local started finished rc=0
  started="$(os_wrapper_now)"
  set +e
  "$@"
  rc=$?
  set -e
  finished="$(os_wrapper_now)"

  local ok="true" reason=""
  if [[ $rc -ne 0 ]]; then
    ok="false"
    reason="$fail_reason (exit $rc)"
  fi
  os_wrapper_emit "$tool" "$ok" "$rc" "$started" "$finished" "$reason" "${artifacts[@]}"
  return $rc
}
