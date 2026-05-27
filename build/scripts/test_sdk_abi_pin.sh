#!/usr/bin/env bash
# @file test_sdk_abi_pin.sh
# @brief M6-SDK-001 (#369) — compile + run tests/sdk_abi_pin_test.c.
#
# Purpose:
#   Slice 1 of the M6 SDK scaffold. Asserts that
#   `sdk/include/os/abi.h` re-exports the in-tree `OS_ABI_VERSION_*`
#   macros from `user/include/secureos_abi.h` without drift, and that
#   `sdk/VERSION` matches the same MAJOR.MINOR.PATCH triple.
#
# Interactions:
#   - sdk/include/os/abi.h, user/include/secureos_abi.h, sdk/VERSION.
#   - Wired into build/scripts/test.sh under the `sdk_abi_pin` target.
#
# Launched by:
#   build/scripts/test.sh sdk_abi_pin
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

# Per issue #369 done-when: build the SDK header host-side with the
# documented include layout. We add `-Iuser/include` so the SDK header's
# `#include "secureos_abi.h"` resolves to the in-tree source of truth, and
# `-Isdk/include` so the test's `#include "os/abi.h"` resolves to the SDK
# header under test.
cc -std=c11 -Wall -Wextra -Werror \
  -I"$ROOT_DIR/sdk/include" \
  -I"$ROOT_DIR/user/include" \
  "$ROOT_DIR/tests/sdk_abi_pin_test.c" \
  -o "$OUT_DIR/sdk_abi_pin_test"

# The test resolves `sdk/VERSION` relative to cwd; run it from the repo
# root so the relative open() succeeds regardless of caller cwd.
cd "$ROOT_DIR"
"$OUT_DIR/sdk_abi_pin_test"
