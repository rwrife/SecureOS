#!/usr/bin/env bash
# generate_keys.sh - Generate code signing keys and certificate
#
# This script runs INSIDE the Docker toolchain container. It produces the
# signing materials needed by sof_wrap to sign all OS and user binaries.
# Called by: build/scripts/build.sh (before kernel and app builds)
#
# Output:
#   artifacts/keys/root.pub            — root Ed25519 public key (32 bytes)
#   artifacts/keys/intermediate.seed   — intermediate signing seed (32 bytes)
#   artifacts/keys/intermediate.cert   — SCRT certificate (132 bytes)
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KEYS_DIR="$ROOT_DIR/artifacts/keys"

mkdir -p "$KEYS_DIR"

# Build keygen if not already built
if [ ! -x "$ROOT_DIR/tools/keygen/keygen" ]; then
  make -C "$ROOT_DIR/tools/keygen"
fi

# Generate signing materials
"$ROOT_DIR/tools/keygen/keygen" --out-dir "$KEYS_DIR"

echo "PASS: signing keys generated in $KEYS_DIR"
