#!/usr/bin/env bash
# @file validate_sdk_no_kernel_includes.sh
# @brief M6-SDK-001 (#369) — refuse any source under `sdk/` that
#        `#include`s anything from `kernel/`.
#
# Purpose:
#   The plan (`plans/2026-05-15-public-sdk-external-app-template.md`)
#   states that `sdk/` is the ONLY surface external apps may depend on.
#   Concretely: nothing under `sdk/` may pull a header from `kernel/`,
#   because external consumers will not have `kernel/` on their include
#   path. Slice 1 ships only header + README, but we wire the check now
#   so a future slice cannot silently violate the rule.
#
# Behaviour:
#   - Scans every `.c` / `.h` under `sdk/`.
#   - Reports `SDK_VALIDATE:FAIL` and exits 1 on the first offender,
#     printing the file + offending line so failures are debuggable.
#   - Emits `SDK_VALIDATE:PASS:no_kernel_includes` on success so the
#     bundle / agent harness has a deterministic marker.
#
# Launched by:
#   build/scripts/test.sh validate_sdk_no_kernel_includes
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SDK_DIR="$ROOT_DIR/sdk"

if [ ! -d "$SDK_DIR" ]; then
  echo "SDK_VALIDATE:FAIL:no_sdk_dir"
  exit 1
fi

# Find candidate files. `-print0` + `read -d ''` survive paths with
# spaces; slice 1 has none, but future slices may.
offender=""
offender_line=""
while IFS= read -r -d '' f; do
  # Match `#include "kernel/..."` or `#include <kernel/...>`. We
  # intentionally allow occurrences inside comments to slip through —
  # the cost of a perfect parser outweighs the benefit here, and a
  # `#include` line is the only way the compiler honours it.
  line="$(grep -nE '^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"]kernel/' "$f" || true)"
  if [ -n "$line" ]; then
    offender="$f"
    offender_line="$line"
    break
  fi
done < <(find "$SDK_DIR" -type f \( -name '*.c' -o -name '*.h' \) -print0)

if [ -n "$offender" ]; then
  echo "SDK_VALIDATE:FAIL:kernel_include_in_sdk:$offender:$offender_line"
  exit 1
fi

echo "SDK_VALIDATE:PASS:no_kernel_includes"
