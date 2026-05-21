#!/usr/bin/env bash
# tests/integration/_canary_must_fail/canary_must_fail.sh
#
# Intentionally failing canary test target. See README.md in this
# directory for the full rationale; this file is deliberately tiny so
# it cannot fail for the wrong reason.
#
# Contract (do not change without updating the validator harness in
# build/scripts/validate_bundle.sh and the validator-report schema):
#
#   * Emits exactly one TEST:START line and one TEST:FAIL line.
#   * Exits with status 1 (a real test failure -- NOT 78, which is
#     reserved for harness errors).
#
# Issue: #212.

set -u

printf 'TEST:START:_canary_must_fail\n'
printf 'TEST:FAIL:_canary_must_fail:intentional\n'
exit 1
