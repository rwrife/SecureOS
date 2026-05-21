#!/usr/bin/env bash
#
# check_shell_parity.sh — enforce AGENTS.md "Keep ps1 and sh scripts in sync"
# (root AGENTS.md, "Guidlines" section).
#
# Walks build/scripts/ and reports any *.sh without a sibling *.ps1 (and vice
# versa). Intentional asymmetries live in
# build/scripts/.shell_parity_allowlist (one bare name per line, comments with
# '#'). Exits non-zero on unexplained drift so CI can wire this in.
#
# Usage:
#   build/scripts/check_shell_parity.sh                 # check build/scripts/
#   build/scripts/check_shell_parity.sh path/to/dir...  # check given dirs
#
# Tracked by SecureOS issue #156.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ALLOWLIST_FILE="${ROOT_DIR}/build/scripts/.shell_parity_allowlist"

declare -a SCAN_DIRS
if [[ $# -gt 0 ]]; then
  SCAN_DIRS=("$@")
else
  SCAN_DIRS=("${ROOT_DIR}/build/scripts")
fi

# Load allowlist into an associative array of bare script names.
declare -A ALLOW=()
if [[ -f "$ALLOWLIST_FILE" ]]; then
  while IFS= read -r line; do
    # Strip comments and whitespace.
    line="${line%%#*}"
    line="${line//[$'\t\r\n ']}"
    [[ -z "$line" ]] && continue
    ALLOW["$line"]=1
  done < "$ALLOWLIST_FILE"
fi

missing_ps1=()
missing_sh=()

for dir in "${SCAN_DIRS[@]}"; do
  if [[ ! -d "$dir" ]]; then
    echo "check_shell_parity: not a directory: $dir" >&2
    exit 2
  fi

  while IFS= read -r -d '' sh; do
    base="$(basename "$sh" .sh)"
    [[ -n "${ALLOW[$base]:-}" ]] && continue
    if [[ ! -f "${dir}/${base}.ps1" ]]; then
      missing_ps1+=("${dir}/${base}.sh")
    fi
  done < <(find "$dir" -maxdepth 1 -type f -name '*.sh' -print0)

  while IFS= read -r -d '' ps1; do
    base="$(basename "$ps1" .ps1)"
    [[ -n "${ALLOW[$base]:-}" ]] && continue
    if [[ ! -f "${dir}/${base}.sh" ]]; then
      missing_sh+=("${dir}/${base}.ps1")
    fi
  done < <(find "$dir" -maxdepth 1 -type f -name '*.ps1' -print0)
done

status=0

if [[ ${#missing_ps1[@]} -gt 0 ]]; then
  status=1
  echo "PARITY:MISSING_PS1: ${#missing_ps1[@]} .sh script(s) have no .ps1 peer" >&2
  for f in "${missing_ps1[@]}"; do
    echo "  - $f" >&2
  done
fi

if [[ ${#missing_sh[@]} -gt 0 ]]; then
  status=1
  echo "PARITY:MISSING_SH: ${#missing_sh[@]} .ps1 script(s) have no .sh peer" >&2
  for f in "${missing_sh[@]}"; do
    echo "  - $f" >&2
  done
fi

if [[ $status -eq 0 ]]; then
  echo "PARITY:OK: build/scripts/ .sh ↔ .ps1 in sync (allowlist: ${#ALLOW[@]} entries)"
else
  cat <<EOF >&2

AGENTS.md requires .sh and .ps1 build scripts to stay in sync.
To resolve, either:
  - port the missing script to the other shell, OR
  - add its bare name (no extension) to build/scripts/.shell_parity_allowlist
    with a comment explaining why the asymmetry is intentional.

Tracked by SecureOS issue #156.
EOF
fi

exit "$status"
