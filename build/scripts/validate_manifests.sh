#!/usr/bin/env bash
# build/scripts/validate_manifests.sh — issue #195.
#
# Thin wrapper around tools/validate_manifests.py. Re-validates every
# in-tree app manifest against manifests/schema/v0.json so example +
# schema cannot silently drift (BUILD_ROADMAP §7 ABI-rot guard,
# follow-up to #187).
#
# PowerShell peer: build/scripts/validate_manifests.ps1 (kept in sync
# under the AGENTS.md cross-platform rule, #156).
#
# Exit codes mirror the Python entry point:
#   0 all manifests valid
#   1 at least one manifest failed validation
#   2 harness error (missing/unreadable schema)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

PY="${PYTHON:-python3}"
exec "$PY" "$REPO_ROOT/tools/validate_manifests.py" "$@"
