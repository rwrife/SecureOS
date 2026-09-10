#!/usr/bin/env bash
# build/scripts/test_launcher_owner_kind_audit_marker.sh
#
# Issue #554 host gate that pins the owner_kind launch-audit contract against
# manifest fixture variants (internal/external/local + default-when-omitted).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

python3 tools/check_launcher_owner_kind_audit_marker.py --root "$ROOT_DIR"
