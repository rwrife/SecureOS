#!/usr/bin/env bash
# Validate every in-tree *.manifest.json against the canonical schema.
# Cross-platform peer: build/scripts/validate_manifests.ps1 (AGENTS.md / #156).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCHEMA="${MANIFEST_SCHEMA:-$ROOT_DIR/manifests/schema/v0.json}"
VALIDATOR="$ROOT_DIR/tools/manifest_validate/validate.py"

if [[ ! -f "$SCHEMA" ]]; then
  echo "validate_manifests.sh: schema not found: $SCHEMA" >&2
  exit 2
fi

if [[ ! -f "$VALIDATOR" ]]; then
  echo "validate_manifests.sh: validator not found: $VALIDATOR" >&2
  exit 2
fi

# Discover every manifest under the repo. Excludes build/ artifacts/.
mapfile -t MANIFESTS < <(
  find "$ROOT_DIR" \
    \( -path "$ROOT_DIR/.git" -o -path "$ROOT_DIR/artifacts" -o -path "$ROOT_DIR/build/docker" \) -prune \
    -o -type f -name "*.manifest.json" -print | sort
)

if [[ ${#MANIFESTS[@]} -eq 0 ]]; then
  echo "validate_manifests.sh: no *.manifest.json found under $ROOT_DIR" >&2
  exit 2
fi

echo "validate_manifests.sh: schema=$SCHEMA, ${#MANIFESTS[@]} manifest(s)"
python3 "$VALIDATOR" --schema "$SCHEMA" "${MANIFESTS[@]}"
