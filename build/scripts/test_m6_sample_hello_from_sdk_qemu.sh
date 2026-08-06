#!/usr/bin/env bash
# @file test_m6_sample_hello_from_sdk_qemu.sh
# @brief QEMU peer placeholder for M6-SDK-004 while wrapper flow is pending #396.
#
# This marker follows issue #584's required SKIP discipline so validators can
# distinguish an explicit deferred assertion from an unexecuted test.
set -euo pipefail

echo "TEST:START:m6_sample_hello_from_sdk_qemu"
echo "TEST:SKIP:m6_sample_hello_from_sdk_qemu:os_cc_build:wrappers_unwired_pending_issue_396"
