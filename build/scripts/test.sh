#!/usr/bin/env bash
# build/scripts/test.sh
#
# SecureOS test dispatcher. Routes a named test target to the corresponding
# subordinate validator script.
#
# Defensive harness contract (issue #91):
#   * Each subordinate script is invoked through `run_script`, which:
#       - checks the script exists and is readable BEFORE attempting to run;
#       - invokes it with `bash <path>` so the executable bit (+x) is NOT
#         required (prevents the "Permission denied" regression seen in #90);
#       - on a missing/unreadable script, emits the deterministic marker
#         `TEST:FAIL:harness_missing_script:<path>` on stderr and exits with
#         status 78 (HARNESS_ERROR_EXIT). 78 is distinct from `1` so callers
#         (e.g. validate_bundle.sh) can classify harness/infrastructure
#         failures separately from real test failures.
#
# Exit codes:
#   0  - all dispatched scripts succeeded
#   1  - a subordinate test script reported failure (real test_fail)
#   2  - usage error (unknown target)
#  78  - harness error: a script referenced by a target was missing or
#        unreadable; no test was actually executed.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TEST_NAME="${1:-hello_boot}"

# Stable exit code for harness/infra errors (see header).
HARNESS_ERROR_EXIT=78

# run_script <path> [args...]
#
# Defensive launcher. Verifies the script is present and readable, then
# invokes it via `bash` so it does not depend on the executable bit. On a
# missing/unreadable script, emits a deterministic harness marker and exits
# with HARNESS_ERROR_EXIT (78) so downstream callers can classify the
# failure as `harness_error` rather than `test_fail`.
run_script() {
  local path="$1"
  shift
  if [[ ! -e "$path" ]]; then
    printf 'TEST:FAIL:harness_missing_script:%s\n' "$path" >&2
    exit "$HARNESS_ERROR_EXIT"
  fi
  if [[ ! -r "$path" ]]; then
    printf 'TEST:FAIL:harness_unreadable_script:%s\n' "$path" >&2
    exit "$HARNESS_ERROR_EXIT"
  fi
  bash "$path" "$@"
}

usage() {
  cat <<EOF
Usage: $(basename "$0") [hello_boot|hello_boot_negative|harness_negative|cap_api_contract|capability_table|cap_table_skeleton|cap_handle_repr|cap_handle_revoke_subject|cap_handle_revoke_subtree|process_table|process_create_table_full_deny_marker|app_native_process_spawn_deny_marker|process_find_aspace_by_subject|proc_sched|proc_sched_aspace_invariant|aspace_carve|clib_malloc|clib_ctype|clib_string|clib_qsort|clib_iso646|clib_bsearch|clib_stdlib|clib_errno|clib_stdarg|clib_stdio|clib_setjmp|clib_assert|clib_os_assert|clib_limits|clib_stdbool|clib_stddef|clib_stdint|clib_inttypes|clib_stdalign|clib_float|clib_stdnoreturn|clib_symbol_drift|aspace_bounds|aspace_invariant|capability_gate|console_svc_port_alloc|fs_svc_port_alloc|broker_svc_port_alloc|broker_svc_delete_owner_authority_check|broker_svc_cascade_revokes_minted_handle|broker_svc_step3_5_session_teardown|session_manager_first_for_subject|session_manager_subject_for_session|capability_audit|capability_audit_fixture|capability_audit_log|cap_broker|cap_deny_marker_shape|broker_share_allow|broker_share_deny|broker_share_revoke|workflow_rule|launcher_console|launcher_spawn_handoff|launcher_arena_bytes|launcher_fs_spawn_handoff|launcher_broker_spawn_handoff|event_bus|scheduler|sof_format|sof_format_constants|sof_verify_at_rest|ed25519|cert_chain|codesign|tls|https|netlib_url_scheme|bearssl_compile|tinycc_vendor_gate|fs_service|launcher_fs|fs_service_persist_allow|fs_service_persist_deny|fs_service_ephemeral_reset|m3_fs_persist_allow_qemu|m3_fs_persist_deny_qemu|m3_fs_ephemeral_reset_qemu|m4_broker_share_allow_qemu|m4_broker_share_deny_qemu|m4_broker_share_revoke_qemu|m5_owner_delete_cascade_allow_qemu|m5_owner_delete_cascade_deny_qemu|m5_owner_delete_cascade_window_qemu|app_runtime|helloapp_allow|helloapp_deny|m2_helloapp_allow_qemu|m2_helloapp_deny_qemu|m2_launcher_console_qemu|kernel_console|kernel_filedemo|kernel_persistence|kernel_sessions|validator_report|abi_version|process_exit_wrapper|process_spawn_wrapper|process_spawn_argv_roundtrip|mem_brk_wrapper|clib_os_brk|mem_brk_qemu|sdk_abi_pin|sdk_libos_link|validate_sdk_no_kernel_includes|validate_manifests_abi_major|manifest_required_fields|manifest_persistence_enum|manifest_broker_role_enum|manifest_ownership_role_enum|manifest_owner_kind_enum|manifest_arena_bytes_range|ipc_sync_v0|ipc_port_lifecycle|ipc_handle_gate|ipc_bounds|m1_ipc_demo|syscall_entry_stub|validate_capability_registry|capability_registry_drift|validate_abi_stamps|abi_stamps_drift|abi_stamps_strict_no_skip|abi_stamps_strict_no_placeholder|abi_stamps_wrapper_default_strict|parity|harness_defense|canary_must_fail|sosh_cap_allow|sosh_cap_deny|sosh_cap_source_exec|sosh_cap_exists|sosh_cap_cat_ls|sosh_cap_write_append|sosh_cap_export|sosh_external_exec|win_gfx_gates|win_gfx_callsite|win_gfx_hal_allow_qemu|win_gfx_hal_deny_qemu|launcher_hal_callsite_migration|validate_sosh_capability_contract|sosh_contract_registry_drift|release_compliance_bundle|in_os_toolchain_dev_dir|toolchain_compiles_hello_in_os|toolchain_runs_compiled_binary|toolchain_unsigned_prompt_enforced|toolchain_large_output_persisted|toolchain_compile_error_reported|toolchain_heap_isolation|validate_m7_markers|m7_markers_drift|tinycc_config_secureos|tinycc_libc_deps|sofpack_wrap|manifest_default_synthesise]


Runs SecureOS test targets. Subordinate scripts are dispatched via bash so
the executable bit is not required. Harness errors (missing/unreadable
scripts) exit with status 78 and emit TEST:FAIL:harness_missing_script:<path>.
EOF
}

if [[ "$TEST_NAME" == "-h" || "$TEST_NAME" == "--help" ]]; then
  usage
  exit 0
fi

case "$TEST_NAME" in
  hello_boot)
    run_script "$ROOT_DIR/build/scripts/test_boot_sector.sh"
    run_script "$ROOT_DIR/build/scripts/run_qemu.sh" --test hello_boot
    ;;
  hello_boot_negative)
    run_script "$ROOT_DIR/build/scripts/test_boot_sector_fail.sh"
    run_script "$ROOT_DIR/build/scripts/run_qemu.sh" --test hello_boot_fail
    ;;
  harness_negative)
    "$ROOT_DIR/build/scripts/test_harness_negative.sh"
    ;;
  in_os_toolchain_dev_dir)
    # Phase 1 of plans/2026-05-28-in-os-toolchain-self-hosting.md: the
    # /apps/dev developer directory + sample are staged onto the disk image.
    run_script "$ROOT_DIR/build/scripts/test_in_os_toolchain_dev_dir.sh"
    ;;
  dev_hello_c_host_compiles)
    # Issue #574: host-side compile canary for the canonical in-OS
    # validation sample (`dev/hello.c`). Catches source/header drift with a
    # freestanding compile posture before M7 QEMU markers consume it.
    run_script "$ROOT_DIR/build/scripts/test_dev_hello_c_host_compiles.sh"
    ;;
  dev_hello_c_pin)
    # Issue #636: source-drift pin for the canonical in-OS compiler sample
    # (`dev/hello.c`). Recomputes SHA-256 and compares to
    # tests/host/pins/dev_hello_c.sha256 so input drift is explicit before it
    # cascades into M7 SOF/toolchain goldens.
    run_script "$ROOT_DIR/build/scripts/validate_dev_hello_c.sh"
    ;;
  hello_sof_golden)
    # Issue #619: host-side SOF golden pin for dev/hello.c. Rebuilds the
    # canonical sample through the build_user_app-compatible path and compares
    # source + output hashes against tests/m7_toolchain/hello_golden.json.
    run_script "$ROOT_DIR/build/scripts/test_hello_golden.sh"
    ;;
  toolchain_compiles_hello_in_os|toolchain_runs_compiled_binary|toolchain_unsigned_prompt_enforced|toolchain_large_output_persisted|toolchain_compile_error_reported|toolchain_cc_audit_markers|toolchain_cc_manifest_sidecar_written_on_link|toolchain_sofpack_plus_manifestgen_roundtrip|toolchain_launcher_manifest_ownership_role_enforced|toolchain_cc_manifest_override_precedence|toolchain_cc_version_and_help_text_pinned|toolchain_cc_exit_codes_match_v0_table|toolchain_cc_arena_exhaustion_audit_marker|toolchain_heap_isolation|toolchain_launch_audit_owner_kind_field_emitted|toolchain_launcher_owner_kind_cache_isolation|toolchain_launcher_sidecar_owner_kind_mismatch|toolchain_launcher_sidecar_caps_required_mismatch|toolchain_launcher_sidecar_size_and_malformed_json|toolchain_missing_manifest_sidecar|toolchain_libc_deps_phase3_complete)
    # M7-TOOLCHAIN acceptance suite scaffolding (issue #423, umbrella #403,
    # plan plans/2026-05-28-in-os-toolchain-self-hosting.md §"Acceptance
    # tests"). Each script is a SKIP-pinned stub today; the gating execute
    # slice flips it to a real TEST:PASS:<marker>. See tests/m7_toolchain/
    # README.md for the marker ↔ gating-issue mapping.
    run_script "$ROOT_DIR/tests/m7_toolchain/${TEST_NAME}.sh"
    ;;
  cap_api_contract)
    run_script "$ROOT_DIR/build/scripts/test_cap_api_contract.sh"
    ;;
  capability_table)
    run_script "$ROOT_DIR/build/scripts/test_capability_table.sh"
    ;;
  cap_table_skeleton)
    run_script "$ROOT_DIR/build/scripts/test_cap_table_skeleton.sh"
    ;;
  cap_handle_repr)
    run_script "$ROOT_DIR/build/scripts/test_cap_handle_repr.sh"
    ;;
  cap_handle_revoke_subject)
    run_script "$ROOT_DIR/build/scripts/test_cap_handle_revoke_subject.sh"
    ;;
  cap_handle_revoke_subtree)
    run_script "$ROOT_DIR/build/scripts/test_cap_handle_revoke_subtree.sh"
    ;;
  session_manager_first_for_subject)
    # Issue #350 / plan plans/2026-05-26-m5-wm-cascade-on-substrate.md
    # (M5-SUBSTRATE-005a). Host-side unit tests for the
    # session_manager_first_session_for_subject enumerator predicate
    # the cascade orchestrator will drive in step 3.5 of
    # broker_svc_delete_owner.
    run_script "$ROOT_DIR/build/scripts/test_session_manager_first_for_subject.sh"
    ;;
  session_manager_subject_for_session)
    # Issue #375 (HAL call-site migration follow-up to #349 / PR #365).
    # Host-side unit tests for the session_manager_subject_for_session
    # accessor used by the launcher/console HAL call-site migration to
    # thread the calling subject into the new _as wrappers in
    # kernel/hal/hal_cap_entry.h.
    run_script "$ROOT_DIR/build/scripts/test_session_manager_subject_for_session.sh"
    ;;
  process_table)
    run_script "$ROOT_DIR/build/scripts/test_process_table.sh"
    ;;
  process_create_table_full_deny_marker)
    # Issue #261: PROC_TABLE_FULL deny path emits the canonical
    # CAP:DENY:<subject>:app_exec:proc_table_full marker that
    # round-trips through cap_deny_marker_validate (the #221
    # conformance predicate). Reuses CAP_APP_EXEC + literal
    # resource string "proc_table_full" so the launcher's
    # `CAP:DENY:*:app_exec:*` greps pick up both policy and
    # exhaustion denies under one shape.
    run_script "$ROOT_DIR/build/scripts/test_process_create_table_full_deny_marker.sh"
    ;;
  app_native_process_spawn_deny_marker)
    # Issue #532: pins the canonical CAP:DENY:<sid>:app_exec:<resource>
    # marker emitted by `app_native_process_spawn` (M7-TOOLCHAIN-003
    # #422 / PR #427) when the calling subject lacks CAP_APP_EXEC.
    # Marker is the load-bearing audit-ring line `launch.denied` (plan
    # #403 P4 / BUILD_ROADMAP §5.2) and #410's
    # `toolchain_unsigned_prompt_enforced` acceptance leans on.
    # Same orphan-from-TEST_TARGETS shape #487 / #503 / #508 / #512 /
    # #514 fixed for other substrate subsystems. Exercises the body via
    # the `app_native_spawn_cap_check` seam (kernel/user/app_native_spawn.c),
    # same fixture-seam pattern PR #495 used for `app_native_mem_brk`.
    run_script "$ROOT_DIR/build/scripts/test_app_native_process_spawn_deny_marker.sh"
    ;;
  process_find_aspace_by_subject)
    run_script "$ROOT_DIR/build/scripts/test_process_find_aspace_by_subject.sh"
    ;;
  proc_sched)
    run_script "$ROOT_DIR/build/scripts/test_proc_sched.sh"
    ;;
  proc_sched_aspace_invariant)
    run_script "$ROOT_DIR/build/scripts/test_proc_sched_aspace_invariant.sh"
    ;;
  aspace_carve)
    run_script "$ROOT_DIR/build/scripts/test_aspace_carve.sh"
    ;;
  clib_malloc)
    run_script "$ROOT_DIR/build/scripts/test_clib_malloc.sh"
    ;;
  clib_ctype)
    run_script "$ROOT_DIR/build/scripts/test_clib_ctype.sh"
    ;;
  clib_string)
    run_script "$ROOT_DIR/build/scripts/test_clib_string.sh"
    ;;
  clib_qsort)
    run_script "$ROOT_DIR/build/scripts/test_clib_qsort.sh"
    ;;
  clib_iso646)
    # Issue #407 slice (M7-TOOLCHAIN-004): freestanding <iso646.h>
    # nucleus host unit test. Pins the 11 C11 §7.9¶1 macros
    # (and/and_eq/bitand/bitor/compl/not/not_eq/or/or_eq/xor/xor_eq)
    # and round-trips each through the helper TU so a future drift
    # cannot silently redefine an operator-spelling macro.
    run_script "$ROOT_DIR/build/scripts/test_clib_iso646.sh"
    ;;
  clib_stdlib)
    run_script "$ROOT_DIR/build/scripts/test_clib_stdlib.sh"
    ;;
  clib_errno)
    run_script "$ROOT_DIR/build/scripts/test_clib_errno.sh"
    ;;
  clib_stdarg)
    run_script "$ROOT_DIR/build/scripts/test_clib_stdarg.sh"
    ;;
  clib_setjmp)
    run_script "$ROOT_DIR/build/scripts/test_clib_setjmp.sh"
    ;;
  clib_assert)
    # Issue #407 slice (M7-TOOLCHAIN-004): freestanding <assert.h>
    # nucleus host unit test. Pins the C11 §7.2 macro semantics
    # (assert / static_assert / NDEBUG re-include / __assert_is_defined)
    # and the clib_assert_set_handler registered-handler hook used by
    # both host tests and the on-target crt0 forwarder.
    run_script "$ROOT_DIR/build/scripts/test_clib_assert.sh"
    ;;
  clib_os_assert)
    # Issue #407 follow-up: on-target `clib_os_assert` forwarder that
    # wires the freestanding <clib/assert.h> registered-handler hook to
    # `os_process_exit(1)` (the syscall landed by #406 / PR #427).
    # Pins the `clib_assert_handler_fn` signature compatibility and the
    # `clib_os_assert_install` convenience-installer symbol. End-to-end
    # exit round-trip is the deferred on-target peer (analogous to the
    # `clib_brk_growth_qemu` peer #421 calls out for `clib_os_brk`).
    run_script "$ROOT_DIR/build/scripts/test_clib_os_assert.sh"
    ;;
  clib_bsearch)
    run_script "$ROOT_DIR/build/scripts/test_clib_bsearch.sh"
    ;;
  clib_limits)
    run_script "$ROOT_DIR/build/scripts/test_clib_limits.sh"
    ;;
  clib_stdbool)
    run_script "$ROOT_DIR/build/scripts/test_clib_stdbool.sh"
    ;;
  clib_symbol_drift)
    run_script "$ROOT_DIR/build/scripts/test_clib_symbol_drift.sh"
    ;;
  clib_stdio)
    # M7-TOOLCHAIN-004 slice 8 (issue #447 / #407, plan P3): freestanding
    # <stdio.h> nucleus over a swappable backend (host: recorder shim;
    # on-target: os_fs_* + os_console_write). Covers FILE/stdin/stdout/
    # stderr/fopen/fclose/fread/fwrite/fflush/fputs/fputc/fprintf/
    # vfprintf/printf + the minimal printf spec set TinyCC (#408) and
    # the `cc` driver (#409) emit.
    run_script "$ROOT_DIR/build/scripts/test_clib_stdio.sh"
    ;;
  clib_stddef)
    # Issue #407 slice 9 (M7-TOOLCHAIN-004): freestanding <stddef.h>
    # nucleus host unit test. Pins NULL / size_t / ptrdiff_t /
    # wchar_t / max_align_t / offsetof at x86_64 SysV values and
    # drift-anchors them through the src/stddef.c helper TU.
    run_script "$ROOT_DIR/build/scripts/test_clib_stddef.sh"
    ;;
  clib_stdint)
    # Issue #407 slice 10 (M7-TOOLCHAIN-004): freestanding <stdint.h>
    # nucleus host unit test. Pins the exact-width / pointer-width /
    # max-width typedefs + INT*_MAX/MIN + UINT*_MAX + SIZE_MAX +
    # PTRDIFF_{MIN,MAX} + INTn_C/UINTn_C constant macros, and
    # drift-anchors the typedefs through the src/stdint.c helper TU.
    run_script "$ROOT_DIR/build/scripts/test_clib_stdint.sh"
    ;;
  clib_inttypes)
    # Issue #407 slice 11 (M7-TOOLCHAIN-004): freestanding
    # <inttypes.h> format-string nucleus host unit test. Pins the
    # PRI* / SCN* exact-width / least- / fast-width / intmax / intptr
    # macro families and drift-anchors them through the
    # src/inttypes.c helper TU so a future drift in the compiler
    # builtins or the fallback table cannot silently regress one side
    # without the other.
    run_script "$ROOT_DIR/build/scripts/test_clib_inttypes.sh"
    ;;
  clib_stdalign)
    # Issue #407 slice (M7-TOOLCHAIN-004): freestanding <stdalign.h>
    # nucleus host unit test. Pins the 4 C11 §7.15¶1-2 macros
    # (alignas / alignof / __alignas_is_defined / __alignof_is_defined)
    # and round-trips each through the helper TU so a future drift
    # cannot silently redefine an alignment macro or flip a
    # feature-test macro to 0.
    run_script "$ROOT_DIR/build/scripts/test_clib_stdalign.sh"
    ;;
  clib_float)
    # Issue #407 slice (M7-TOOLCHAIN-004): freestanding <float.h>
    # nucleus host unit test. Pins the C11 §5.2.4.2.2 + §7.7 macros
    # (FLT/DBL/LDBL_*) and drift-anchors them through src/float.c so
    # a future drift cannot silently redefine a floating-point
    # envelope macro or violate the C11 minima.
    run_script "$ROOT_DIR/build/scripts/test_clib_float.sh"
    ;;
  clib_stdnoreturn)
    # Issue #407 slice (M7-TOOLCHAIN-004): freestanding <stdnoreturn.h>
    # nucleus host unit test. Pins the single C11 §7.23¶1 macro
    # (`noreturn` -> `_Noreturn`) and round-trips it through the helper
    # TU so a future drift cannot silently turn the specifier into a
    # no-op.
    run_script "$ROOT_DIR/build/scripts/test_clib_stdnoreturn.sh"
    ;;
  aspace_bounds)
    run_script "$ROOT_DIR/build/scripts/test_aspace_bounds.sh"
    ;;
  console_svc_port_alloc)
    run_script "$ROOT_DIR/build/scripts/test_console_svc_port_alloc.sh"
    ;;
  fs_svc_port_alloc)
    run_script "$ROOT_DIR/build/scripts/test_fs_svc_port_alloc.sh"
    ;;
  broker_svc_port_alloc)
    run_script "$ROOT_DIR/build/scripts/test_broker_svc_port_alloc.sh"
    ;;
  broker_svc_delete_owner_authority_check)
    run_script "$ROOT_DIR/build/scripts/test_broker_svc_delete_owner_authority_check.sh"
    ;;
  broker_svc_cascade_revokes_minted_handle)
    run_script "$ROOT_DIR/build/scripts/test_broker_svc_cascade_revokes_minted_handle.sh"
    ;;
  broker_svc_step3_5_session_teardown)
    run_script "$ROOT_DIR/build/scripts/test_broker_svc_step3_5_session_teardown.sh"
    ;;
  aspace_invariant)
    run_script "$ROOT_DIR/build/scripts/test_aspace_invariant.sh"
    ;;
  capability_gate)
    run_script "$ROOT_DIR/build/scripts/test_capability_gate.sh"
    ;;
  capability_audit)
    run_script "$ROOT_DIR/build/scripts/test_capability_audit.sh"
    ;;
  capability_audit_fixture)
    run_script "$ROOT_DIR/build/scripts/test_capability_audit_fixture.sh"
    ;;
  capability_audit_log)
    run_script "$ROOT_DIR/build/scripts/test_capability_audit_log.sh"
    ;;
  cap_broker)
    run_script "$ROOT_DIR/build/scripts/test_cap_broker.sh"
    ;;
  cap_deny_marker_shape)
    run_script "$ROOT_DIR/build/scripts/test_cap_deny_marker_shape.sh"
    ;;
  broker_share_allow)
    run_script "$ROOT_DIR/build/scripts/test_broker_share_allow.sh"
    ;;
  broker_share_deny)
    run_script "$ROOT_DIR/build/scripts/test_broker_share_deny.sh"
    ;;
  broker_share_revoke)
    run_script "$ROOT_DIR/build/scripts/test_broker_share_revoke.sh"
    ;;
  workflow_rule)
    run_script "$ROOT_DIR/build/scripts/test_workflow_rule.sh"
    ;;
  launcher_console)
    run_script "$ROOT_DIR/build/scripts/test_launcher_console.sh"
    ;;
  launcher_spawn_handoff)
    run_script "$ROOT_DIR/build/scripts/test_launcher_spawn_handoff.sh"
    ;;
  launcher_arena_bytes)
    run_script "$ROOT_DIR/build/scripts/test_launcher_arena_bytes.sh"
    ;;
  launcher_fs_spawn_handoff)
    run_script "$ROOT_DIR/build/scripts/test_launcher_fs_spawn_handoff.sh"
    ;;
  launcher_broker_spawn_handoff)
    run_script "$ROOT_DIR/build/scripts/test_launcher_broker_spawn_handoff.sh"
    ;;
  event_bus)
    run_script "$ROOT_DIR/build/scripts/test_event_bus.sh"
    ;;
  scheduler)
    run_script "$ROOT_DIR/build/scripts/test_scheduler.sh"
    ;;
  sof_format)
    run_script "$ROOT_DIR/build/scripts/test_sof_format.sh"
    ;;
  sof_format_constants)
    # Issue #547: machine-readable JSON pin for SOF wire-format constants
    # declared in kernel/format/sof.h.
    run_script "$ROOT_DIR/build/scripts/test_sof_format_constants.sh"
    ;;
  sofpack_wrap)
    # M7-TOOLCHAIN-006 sub-slice (issue #409, plan
    # plans/2026-05-28-in-os-toolchain-self-hosting.md Phase 5):
    # `user/libs/sofpack` is the freestanding userland-callable factoring
    # of `sof_build()` (kernel/format/sof.c) the future in-OS `cc` driver
    # will call instead of pulling kernel + crypto headers into userland.
    # Host test pins byte-for-byte wire equivalence with sof_build and
    # round-trip parse acceptance via sof_parse.
    run_script "$ROOT_DIR/build/scripts/test_sofpack_wrap.sh"
    ;;
  manifest_default_synthesise)
    # M7-TOOLCHAIN-006 sub-slice (issue #533, plan
    # plans/2026-05-28-in-os-toolchain-self-hosting.md §"In-OS packaging"):
    # `user/libs/manifestgen` is the freestanding userland-callable v0
    # manifest synthesiser the in-OS `cc` driver (#409) calls after
    # `sofpack_wrap()` to emit a companion `<binary>.manifest.json`. Host
    # test pins signature, determinism, negatives, buffer-too-small, and
    # a schema round-trip against manifests/schema/v0.json. The owner.kind
    # = "local" arm SKIP-pins on `:awaiting_522` until #522's schema enum
    # lands.
    run_script "$ROOT_DIR/build/scripts/test_manifest_default_synthesise.sh"
    ;;
  manifestgen_default_arena)
    # Issue #595: pin the default `runtime.arena_bytes` emitted by
    # `libmanifestgen` when synthesis is selected (no explicit override
    # manifest source branch).
    run_script "$ROOT_DIR/build/scripts/test_manifestgen_default_arena.sh"
    ;;
  manifest_sidecar_suffix)
    # Issue #580: drift gate for the canonical `<binary>.manifest.json`
    # sidecar naming rule in docs/abi/manifest.md §5.8 and
    # MANIFEST_SIDECAR_SUFFIX in libmanifestgen's public header.
    run_script "$ROOT_DIR/build/scripts/test_manifest_sidecar_suffix.sh"
    ;;
  apps_dev_cc_manifest)
    # Issue #573: canonical /apps/dev/cc manifest pin (owner.kind +
    # minimal cap request set + runtime.arena_bytes) and host drift gate
    # against the staged app manifest from #540.
    run_script "$ROOT_DIR/build/scripts/test_apps_dev_cc_manifest.sh"
    ;;
  apps_dev_staging)
    # Issue #570: unifying /apps/dev disk-image staging presence gate over
    # the #531/#541/#545/#548/#550 sub-slices, with canonical SKIP marker
    # discipline while those staging issues remain open.
    run_script "$ROOT_DIR/build/scripts/test_apps_dev_staging.sh"
    ;;
  apps_dev_sha)
    # Issue #606: /apps/dev staged-source SHA drift gate. Runs after the
    # presence gate and enforces target->source mapping parity + source
    # content hashes for non-pending entries.
    run_script "$ROOT_DIR/build/scripts/test_disk_image_apps_dev_sha.sh"
    ;;
  apps_dev_include_set)
    # Issue #615: canonical /apps/dev/include header-set drift gate.
    # Enforces strict set parity once #531/#613 staging slices close,
    # and emits canonical SKIP marker while pending headers are still
    # gated by open issues.
    run_script "$ROOT_DIR/build/scripts/test_apps_dev_include_set.sh"
    ;;
  dev_building_txt_drift)
    # Issue #618: on-device guide drift gate for dev/building.txt.
    # Enforces parity between the guide's "Files here" claims and
    # build_disk_image /apps/dev staging, plus explicit #408/#409/#410
    # gate-labeling while m7_toolchain compile/run markers are still
    # SKIP-pinned.
    run_script "$ROOT_DIR/build/scripts/test_dev_building_txt_drift.sh"
    ;;
  cc_manifest_resolution_precedence)
    # Issue #634 (M7-TOOLCHAIN integration slice): host table-driven pin for
    # manifest source precedence in the `cc` driver wiring
    # (`--manifest` > sidecar > synth) plus hard-fail behavior for invalid
    # override manifests.
    run_script "$ROOT_DIR/build/scripts/test_cc_manifest_resolution_precedence.sh"
    ;;
  ed25519)
    run_script "$ROOT_DIR/build/scripts/test_ed25519.sh"
    ;;
  cert_chain)
    run_script "$ROOT_DIR/build/scripts/test_cert_chain.sh"
    ;;
  codesign)
    run_script "$ROOT_DIR/build/scripts/test_codesign.sh"
    ;;
  sof_verify_at_rest)
    run_script "$ROOT_DIR/build/scripts/test_sof_verify_at_rest.sh"
    ;;
  tls)
    run_script "$ROOT_DIR/build/scripts/test_tls.sh"
    ;;
  https)
    run_script "$ROOT_DIR/build/scripts/test_https.sh"
    ;;
  bearssl_compile)
    run_script "$ROOT_DIR/build/scripts/test_bearssl_compile.sh"
    ;;
  tinycc_config_secureos)
    run_script "$ROOT_DIR/build/scripts/test_tinycc_config_secureos.sh"
    ;;
  tinycc_vendor_gate)
    run_script "$ROOT_DIR/build/scripts/test_tinycc_vendor_gate.sh"
    ;;
  tinycc_libc_deps)
    run_script "$ROOT_DIR/build/scripts/test_tinycc_libc_deps.sh"
    ;;
  tinycc_libtcc1_srcs)
    run_script "$ROOT_DIR/build/scripts/test_tinycc_libtcc1_srcs.sh"
    ;;
  tinycc_arena_drift)
    run_script "$ROOT_DIR/build/scripts/test_tinycc_arena_drift.sh"
    ;;
  netlib_url_scheme)
    run_script "$ROOT_DIR/build/scripts/test_netlib_url_scheme.sh"
    ;;
  fs_service)
    run_script "$ROOT_DIR/build/scripts/test_fs_service.sh"
    ;;
  launcher_fs)
    run_script "$ROOT_DIR/build/scripts/test_launcher_fs.sh"
    ;;
  fs_service_persist_allow)
    run_script "$ROOT_DIR/build/scripts/test_fs_service_persist_allow.sh"
    ;;
  fs_service_persist_deny)
    run_script "$ROOT_DIR/build/scripts/test_fs_service_persist_deny.sh"
    ;;
  fs_service_ephemeral_reset)
    run_script "$ROOT_DIR/build/scripts/test_fs_service_ephemeral_reset.sh"
    ;;
  m3_fs_persist_allow_qemu)
    run_script "$ROOT_DIR/build/scripts/test_m3_fs_persist_allow_qemu.sh"
    ;;
  m3_fs_persist_deny_qemu)
    run_script "$ROOT_DIR/build/scripts/test_m3_fs_persist_deny_qemu.sh"
    ;;
  m3_fs_ephemeral_reset_qemu)
    run_script "$ROOT_DIR/build/scripts/test_m3_fs_ephemeral_reset_qemu.sh"
    ;;
  m4_broker_share_allow_qemu)
    run_script "$ROOT_DIR/build/scripts/test_m4_broker_share_allow_qemu.sh"
    ;;
  m4_broker_share_deny_qemu)
    run_script "$ROOT_DIR/build/scripts/test_m4_broker_share_deny_qemu.sh"
    ;;
  m4_broker_share_revoke_qemu)
    run_script "$ROOT_DIR/build/scripts/test_m4_broker_share_revoke_qemu.sh"
    ;;
  m5_owner_delete_cascade_allow_qemu)
    run_script "$ROOT_DIR/build/scripts/test_m5_owner_delete_cascade_allow_qemu.sh"
    ;;
  m5_owner_delete_cascade_deny_qemu)
    run_script "$ROOT_DIR/build/scripts/test_m5_owner_delete_cascade_deny_qemu.sh"
    ;;
  m5_owner_delete_cascade_window_qemu)
    run_script "$ROOT_DIR/build/scripts/test_m5_owner_delete_cascade_window_qemu.sh"
    ;;
  app_runtime)
    run_script "$ROOT_DIR/build/scripts/test_app_runtime.sh"
    ;;
  helloapp_allow)
    run_script "$ROOT_DIR/build/scripts/test_helloapp_allow.sh"
    ;;
  helloapp_deny)
    run_script "$ROOT_DIR/build/scripts/test_helloapp_deny.sh"
    ;;
  win_gfx_gates)
    run_script "$ROOT_DIR/build/scripts/test_win_gfx_gates.sh"
    ;;
  sosh_cap_allow)
    run_script "$ROOT_DIR/build/scripts/test_sosh_cap_allow.sh"
    ;;
  sosh_cap_deny)
    run_script "$ROOT_DIR/build/scripts/test_sosh_cap_deny.sh"
    ;;
  sosh_cap_source_exec)
    run_script "$ROOT_DIR/build/scripts/test_sosh_cap_source_exec.sh"
    ;;
  sosh_cap_exists)
    run_script "$ROOT_DIR/build/scripts/test_sosh_cap_exists.sh"
    ;;
  sosh_cap_cat_ls)
    run_script "$ROOT_DIR/build/scripts/test_sosh_cap_cat_ls.sh"
    ;;
  sosh_cap_write_append)
    run_script "$ROOT_DIR/build/scripts/test_sosh_cap_write_append.sh"
    ;;
  sosh_cap_export)
    run_script "$ROOT_DIR/build/scripts/test_sosh_cap_export.sh"
    ;;
  sosh_external_exec)
    run_script "$ROOT_DIR/build/scripts/test_sosh_external_exec.sh"
    ;;
  win_gfx_callsite)
    run_script "$ROOT_DIR/build/scripts/test_win_gfx_callsite.sh"
    ;;
  win_gfx_hal_allow_qemu)
    run_script "$ROOT_DIR/build/scripts/test_win_gfx_hal_allow_qemu.sh"
    ;;
  win_gfx_hal_deny_qemu)
    run_script "$ROOT_DIR/build/scripts/test_win_gfx_hal_deny_qemu.sh"
    ;;
  launcher_hal_callsite_migration)
    run_script "$ROOT_DIR/build/scripts/test_launcher_hal_callsite_migration.sh"
    ;;
  m2_helloapp_allow_qemu)
    run_script "$ROOT_DIR/build/scripts/test_m2_helloapp_allow_qemu.sh"
    ;;
  m2_helloapp_deny_qemu)
    run_script "$ROOT_DIR/build/scripts/test_m2_helloapp_deny_qemu.sh"
    ;;
  m2_launcher_console_qemu)
    run_script "$ROOT_DIR/build/scripts/test_m2_launcher_console_qemu.sh"
    ;;
  kernel_console)
    run_script "$ROOT_DIR/build/scripts/build_kernel_image.sh"
    run_script "$ROOT_DIR/build/scripts/build_disk_image.sh"
    run_script "$ROOT_DIR/build/scripts/run_qemu.sh" --test kernel_console
    ;;
  kernel_filedemo)
    run_script "$ROOT_DIR/build/scripts/build_kernel_image.sh"
    run_script "$ROOT_DIR/build/scripts/build_disk_image.sh"
    run_script "$ROOT_DIR/build/scripts/run_qemu.sh" --test kernel_filedemo
    ;;
  kernel_persistence)
    run_script "$ROOT_DIR/build/scripts/test_kernel_persistence.sh"
    ;;
  validator_report)
    run_script "$ROOT_DIR/build/scripts/test_validator_report.sh"
    ;;
  kernel_sessions)
    run_script "$ROOT_DIR/build/scripts/build_kernel_image.sh"
    run_script "$ROOT_DIR/build/scripts/build_disk_image.sh"
    run_script "$ROOT_DIR/build/scripts/run_qemu.sh" --test kernel_sessions
    ;;
  abi_version)
    run_script "$ROOT_DIR/build/scripts/test_abi_version.sh"
    ;;
  process_exit_wrapper)
    # Issue #406 (M7-TOOLCHAIN-003): host-side smoke for the
    # `os_process_exit` user-runtime wrapper. Locks in the
    # "no-op on host (no bridge attached) returns OS_STATUS_OK"
    # contract documented in docs/abi/syscalls.md.
    run_script "$ROOT_DIR/build/scripts/test_process_exit_wrapper.sh"
    ;;
  process_spawn_wrapper)
    # Issue #422 (M7-TOOLCHAIN-003 slice 2): host-side smoke for the
    # `os_process_spawn` user-runtime wrapper. Locks in the symbol
    # export + signature pin + reserved-flag refusal + bad-arg
    # early-reject contract documented in docs/abi/syscalls.md.
    run_script "$ROOT_DIR/build/scripts/test_process_spawn_wrapper.sh"
    ;;
  process_spawn_argv_roundtrip)
    # Issue #546: host-side dynamic bridge test for `os_process_spawn`
    # argv marshalling and `out_exit_status` propagation. Pins the
    # current v0 space-joined `raw_args` contract (including the
    # internal-space ambiguity caveat) before #409/#410 consume it.
    run_script "$ROOT_DIR/build/scripts/test_process_spawn_argv_roundtrip.sh"
    ;;
  mem_brk_wrapper)
    # Issue #421 (M7-TOOLCHAIN-001 slice 2): host-side smoke for the
    # `os_mem_brk` user-runtime wrapper. Pins the symbol export and
    # `(int, void **)` signature, plus the NULL-out-pointer guard
    # the freestanding `user/libs/clib` allocator relies on for the
    # host / no-bridge fall-through path.
    run_script "$ROOT_DIR/build/scripts/test_mem_brk_wrapper.sh"
    ;;
  clib_os_brk)
    # Issue #421 (M7-TOOLCHAIN-001 slice 3): host-side smoke for the
    # `clib_os_brk` forwarder that wires `user/libs/clib`'s
    # `clib_brk_fn` callback to `os_mem_brk`. Pins the `clib_brk_fn`
    # signature compatibility, the narrowing/zero-delta guards, and
    # the no-bridge -> NULL collapse the allocator relies on for
    # clean out-of-arena fallback. End-to-end QEMU growth coverage
    # is the deferred `clib_brk_growth_qemu` peer per the issue body.
    run_script "$ROOT_DIR/build/scripts/test_clib_os_brk.sh"
    ;;
  mem_brk_qemu)
    # Issue #495 (M7-TOOLCHAIN-001 `_qemu` peer): end-to-end
    # round-trip on the `os_mem_brk` bridge slot. Drives the
    # production `app_native_mem_brk` body (extracted into
    # `kernel/user/app_native_heap.c`) directly so the test exercises
    # the same code the launcher wires into `bridge->mem_brk`,
    # without the host-unreachable bridge mapping. Asserts grow /
    # shrink / over-cap-deny / arena-reset sub-markers + the
    # `toolchain_heap_isolation` half of the M7-TOOLCHAIN-001
    # acceptance contract called out in `docs/abi/syscalls.md`
    # §`os_mem_brk` ("end-to-end QEMU growth round-trip is a
    # deliberate follow-up slice").
    run_script "$ROOT_DIR/build/scripts/test_mem_brk_qemu.sh"
    ;;
  sdk_abi_pin)
    # Issue #369 (M6-SDK-001): SDK header `sdk/include/os/abi.h`
    # re-exports the in-tree `OS_ABI_VERSION_*` macros and `sdk/VERSION`
    # matches them. Guards against the SDK silently drifting from the
    # kernel ABI surface.
    run_script "$ROOT_DIR/build/scripts/test_sdk_abi_pin.sh"
    ;;
  sdk_libos_link)
    # Issue #388 (M6-SDK-002): the slice-2 sources (sdk/lib/crt0.c +
    # sdk/lib/libos/version.c) plus the existing
    # `user/runtime/secureos_api_stubs.c` compose into an `libos.a`
    # whose linkage exposes `_start` and `os_get_abi_version`. Guards
    # against the SDK losing its crt0 or its public ABI accessor.
    run_script "$ROOT_DIR/build/scripts/test_sdk_libos_link.sh"
    ;;
  validate_sdk_no_kernel_includes)
    # Issue #369 (M6-SDK-001): forbid `#include "kernel/..."` from any
    # source under `sdk/`. The SDK is the only surface external apps
    # may depend on; pulling kernel headers would silently leak
    # internal-only types into the public contract.
    run_script "$ROOT_DIR/build/scripts/validate_sdk_no_kernel_includes.sh"
    ;;
  validate_no_arch_macros_outside_arch_tree)
    # Issue #623: multi-architecture readiness drift gate. Forbid direct
    # `__x86_64__` / `__i386__` / `__amd64__` / `__aarch64__` /
    # `__arm__` preprocessor branches under kernel non-arch trees so
    # architecture-specific conditionals stay confined to
    # `kernel/arch/<arch>/` (or an explicit allowlist exception linked in
    # docs/architecture/multi-arch-readiness.md).
    run_script "$ROOT_DIR/build/scripts/validate_no_arch_macros_outside_arch_tree.sh"
    ;;
  validate_manifests_abi_major)
    # Issue #227: cross-check that --require-abi-major{,-from-header}
    # wiring rejects examples whose os_abi_version drifts from the
    # secureos_abi.h anchor.
    run_script "$ROOT_DIR/build/scripts/test_validate_manifests_abi_major.sh"
    ;;
  manifest_required_fields)
    # Issue #226: negative regression — confirm the validator
    # wrapper rejects a manifest missing a required field (e.g.
    # os_abi_version) with a deterministic MANIFEST_VALIDATE:*
    # marker so the disk-image build gate cannot silently pass.
    run_script "$ROOT_DIR/build/scripts/test_manifest_required_fields.sh"
    ;;
  manifest_persistence_enum)
    # Issue #285: positive + negative coverage for the optional
    # `capabilities.persistence` enum (ephemeral|persistent) added
    # to manifests/schema/v0.json. Asserts the checked-in persistent
    # example validates and that a bad enum value is rejected with a
    # deterministic MANIFEST_VALIDATE:* marker.
    run_script "$ROOT_DIR/build/scripts/test_manifest_persistence_enum.sh"
    ;;
  manifest_broker_role_enum)
    # Issue #312: positive + negative coverage for the optional
    # `capabilities.broker_role` enum (provider|consumer|none, default
    # none) added to manifests/schema/v0.json. Asserts the checked-in
    # provider/consumer examples validate, the pre-existing helloapp.json
    # (which omits the field) still validates, and that a bad enum value
    # is rejected with a deterministic MANIFEST_VALIDATE:* marker.
    run_script "$ROOT_DIR/build/scripts/test_manifest_broker_role_enum.sh"
    ;;
  manifest_ownership_role_enum)
    # Issue #368: positive + negative coverage for the optional
    # `capabilities.ownership_role` enum (owner|delegate|none, default
    # none) added to manifests/schema/v0.json. Asserts the checked-in
    # owner/delegate examples validate, the pre-existing helloapp.json
    # (which omits the field) still validates, and that a bad enum value
    # is rejected with a deterministic MANIFEST_VALIDATE:* marker.
    run_script "$ROOT_DIR/build/scripts/test_manifest_ownership_role_enum.sh"
    ;;
  manifest_owner_kind_enum)
    # Issue #396 (M6-SDK-003 schema sub-slice): positive + negative
    # coverage for the optional `owner.kind` enum (internal|external,
    # default internal) added to manifests/schema/v0.json per
    # plans/2026-05-15-public-sdk-external-app-template.md
    # §"Manifest Schema (additions)" and BUILD_ROADMAP §5.6. Asserts
    # the checked-in external/internal examples validate, the
    # pre-existing helloapp.json (which omits the `owner` object)
    # still validates, and a bad enum value is rejected with a
    # deterministic MANIFEST_VALIDATE:* marker.
    run_script "$ROOT_DIR/build/scripts/test_manifest_owner_kind_enum.sh"
    ;;
  manifest_arena_bytes_range)
    # Issue #424 (M7-TOOLCHAIN-001 schema sub-slice, refs #404 #421):
    # positive + negative coverage for the optional additive
    # `runtime.arena_bytes` integer field (range
    # [PROC_ARENA_BYTES_DEFAULT, PROC_ARENA_BYTES_MAX]) added to
    # manifests/schema/v0.json per plan
    # plans/2026-05-28-in-os-toolchain-self-hosting.md §P1. Asserts
    # the checked-in positive runtime_arena example validates, the
    # pre-existing helloapp.json (which omits the `runtime` object)
    # still validates, and over-max / below-min / negative /
    # wrong-type values are rejected with a deterministic
    # MANIFEST_VALIDATE:* marker.
    run_script "$ROOT_DIR/build/scripts/test_manifest_arena_bytes_range.sh"
    ;;
  ipc_sync_v0)
    run_script "$ROOT_DIR/build/scripts/test_ipc_sync_v0.sh"
    ;;
  ipc_port_lifecycle)
    # Issue #223: lock in the (generation << 16) | index handle
    # encoding by asserting create→destroy→create advances the
    # generation, stale handles are rejected, and the wrap-around
    # guard keeps IPC_PORT_INVALID unreachable.
    run_script "$ROOT_DIR/build/scripts/test_ipc_port_lifecycle.sh"
    ;;
  ipc_handle_gate)
    # Issue #246 (M1-CAPTBL-006): handle-gated ipc_send_h / ipc_recv_h
    # — allow + wrong-cap deny + stale deny + wrong-owner-on-recv deny
    # + cap_handle_owner contract. Locks in plan #197's final slice.
    run_script "$ROOT_DIR/build/scripts/test_ipc_handle_gate.sh"
    ;;
  ipc_bounds)
    # Issue #260: M1 process address-space bounds enforcement on the
    # IPC envelope buffer. allow + one-past-end deny + straddle deny
    # + backward-compat carve-out (no PCB ⇒ check skipped).
    run_script "$ROOT_DIR/build/scripts/test_ipc_bounds.sh"
    ;;
  m1_ipc_demo)
    # Issue #251 (plan #198 slice 4): M1 two-module IPC acceptance
    # demo — m1-sender → m1-receiver allow round-trip and m1-unauth
    # deny path. Maps directly to BUILD_ROADMAP §5.1's two validation
    # bullets via TEST:PASS:m1_ipc_allow / TEST:PASS:m1_ipc_deny.
    run_script "$ROOT_DIR/build/scripts/test_m1_ipc_demo.sh"
    ;;
  syscall_entry_stub)
    # Issue #232: M1 syscall entry stub — reserved ABI vector range,
    # all vectors return IPC_ERR_INVALID_MSG, deny-marker shape
    # cross-check, and OS_ABI_VERSION anchor cross-check.
    run_script "$ROOT_DIR/build/scripts/test_syscall_entry_stub.sh"
    ;;
  validate_capability_registry)
    # Issue #234: cross-check that docs/abi/capability-registry.json
    # stays consistent with the capability_id_t enum in
    # kernel/cap/capability.h, every referenced test.sh target exists,
    # every deny_marker conforms to the §4 grammar from
    # docs/abi/capability-deny-contract.md, and every owning_plan
    # resolves under plans/.
    run_script "$ROOT_DIR/build/scripts/validate_capability_registry.sh"
    ;;
  validate_abi_index)
    # Issue #630: docs/abi/README.md index drift gate. Every docs/abi/*.md
    # (except README.md itself) must be linked from the index, and the
    # index must not reference missing docs/abi/*.md files.
    python3 "$ROOT_DIR/tools/validate_abi_index.py" --root "$ROOT_DIR"
    ;;
  abi_index_drift)
    # Issue #630: negative-canary self-test proving validate_abi_index
    # fails when a docs/abi/*.md file exists but is unlinked and when a
    # README entry points at a missing file.
    run_script "$ROOT_DIR/tests/harness/abi_index_drift_test.sh"
    ;;
  capability_registry_drift)
    # Issue #234: negative-canary self-test — adds a fake CAP_* to a
    # sandboxed capability.h copy and asserts the registry validator
    # emits the REGISTRY_VALIDATE:FAIL:enum_not_in_registry marker.
    # Mirrors the canary discipline from #213 / #177.
    run_script "$ROOT_DIR/tests/harness/capability_registry_drift_test.sh"
    ;;
  audit_markers_drift)
    # Issue #591: drift gate for docs/abi audit marker registry parity.
    # Cross-checks docs/abi/audit-markers.md §3 marker-prefix rows against
    # docs/abi/audit-markers.json; optional gh issue-state checks can be
    # enabled via AUDIT_MARKERS_WITH_GH=1.
    run_script "$ROOT_DIR/build/scripts/test_audit_markers_drift.sh"
    ;;
  audit_markers_drift_canary)
    # Issue #591: negative-canary self-test proving markdown/json marker
    # divergence fails with deterministic missing_in_json diagnostics.
    run_script "$ROOT_DIR/tests/harness/audit_markers_drift_test.sh"
    ;;
  validate_abi_stamps)
    # Issue #297: fails if any docs/abi/*.md `Last verified against
    # commit:` stamp predates the file's most recent content-changing
    # commit. Follow-up to #257 (one-shot stamp bump) so the drift
    # cannot silently recur.
    run_script "$ROOT_DIR/build/scripts/validate_abi_stamps.sh"
    ;;
  docs_pickup_guide_m7_stamp)
    # Issue #624: enforce stamp freshness for
    # docs/development/pickup-guide-m7.md using the shared
    # validate_abi_stamps.py engine against docs/development scope.
    run_script "$ROOT_DIR/build/scripts/validate_docs_pickup_guide_m7_stamp.sh"
    ;;
  plan_sections_drift)
    # Issue #593: plan sub-section token -> issue linkage drift gate.
    # Scans plans/*.md for M*-style slice tokens and cross-checks each
    # token against GitHub issue search results. Unticketed slices must
    # be explicitly allowlisted in plans/.unticketed-allowlist.
    run_script "$ROOT_DIR/build/scripts/test_plan_sections_drift.sh"
    ;;
  abi_stamps_drift)
    # Issue #297: negative-canary self-test — builds a sandbox repo
    # whose docs/abi/syscalls.md stamp is older than its last content
    # commit and asserts the ABI_STAMP:FAIL:<file>:stamp=...:last_content=...
    # marker fires. Mirrors the canary discipline from #213 / #234.
    run_script "$ROOT_DIR/tests/harness/abi_stamps_drift_test.sh"
    ;;
  validate_m7_markers)
    # Issues #494 + #590: drift gate cross-checking that every marker in
    # tests/m7_toolchain/markers.json is wired through the case arms in
    # this file AND TEST_TARGETS in validate_bundle.sh AND has a stub
    # script under tests/m7_toolchain/ that emits the canonical
    # TEST:PASS:<marker> line. Gating issue state is resolved via online
    # gh lookup or the checked-in offline cache
    # tests/m7_toolchain/issue_state.cache.json.
    run_script "$ROOT_DIR/build/scripts/validate_m7_markers.sh"
    ;;
  validate_m7_markers_schema)
    # Issue #611: schema drift gate for tests/m7_toolchain/markers.json
    # rows (id/harnessPath/gatingIssue/skipReason/addedIn + legacy
    # compatibility fields). Runs alongside validate_m7_markers.
    run_script "$ROOT_DIR/build/scripts/validate_m7_markers_schema.sh"
    ;;
  m7_markers_schema_drift)
    # Issue #611: negative canary proving schema violations (missing/extra
    # keys, bad enum values, non-integer gatingIssue) fail deterministically.
    run_script "$ROOT_DIR/tests/harness/m7_markers_schema_drift_test.sh"
    ;;
  validate_m7_marker_harnesses)
    # Issue #604: every tests/m7_toolchain/markers.json row must have a
    # sibling harness on disk (`<marker>.sh` or `<marker>.c`) unless
    # explicitly allowlisted with justification.
    run_script "$ROOT_DIR/build/scripts/validate_m7_marker_harnesses.sh"
    ;;
  m7_marker_harnesses_drift)
    # Issue #604: negative-canary self-test proving the harness-presence
    # validator fails with deterministic missing-path diagnostics.
    run_script "$ROOT_DIR/tests/harness/m7_marker_harnesses_drift_test.sh"
    ;;
  m7_markers_drift)
    # Issue #494: negative-canary self-test — builds a sandbox repo
    # whose markers.json rename has drifted away from test.sh /
    # TEST_TARGETS and asserts the M7_MARKER:FAIL:<marker>:missing_*
    # and M7_MARKER:FAIL:<marker>:orphan_* markers fire. Mirrors the
    # canary discipline from #213 / #234 / #297 / #351.
    run_script "$ROOT_DIR/tests/harness/m7_markers_drift_test.sh"
    ;;
  m7_markers_issue_state_cache)
    # Issue #590: cache-mode canary proving CLOSED gating issues fail
    # while reason=awaiting_<closed_id>, and pass once reason is
    # retargeted away from the closed issue (including replacedBy hint).
    run_script "$ROOT_DIR/tests/harness/m7_markers_issue_state_cache_test.sh"
    ;;
  skip_backlog_report_fixture)
    # Issue #631: synthetic fixture for tools/report_skip_backlog.py.
    # Asserts TEST:SKIP source-scan totals + per-gating-issue counts
    # (including the unpinned bucket) are computed correctly.
    run_script "$ROOT_DIR/tests/harness/skip_backlog_report_fixture_test.sh"
    ;;
  skip_backlog_cap_fixture)
    # Issue #641: fixture coverage for tools/check_skip_backlog_cap.py.
    # Proves OPEN issue over-cap failures, grandfathered overage PASS,
    # grandfathered ceiling enforcement, and stale-allowlist rejection.
    run_script "$ROOT_DIR/tests/harness/skip_backlog_cap_fixture_test.sh"
    ;;
  skip_backlog_cap)
    # Issue #641: cap SKIP-pinned M7 harness markers per OPEN gating
    # issue (default cap=12) with a remove-only grandfather allowlist.
    run_script "$ROOT_DIR/build/scripts/check_skip_backlog_cap.sh"
    ;;
  abi_stamps_strict_no_skip)
    # Issue #470: negative-canary self-test for --strict-no-skip mode.
    # Builds a sandbox repo with an unstamped docs/abi/*.md and asserts
    # default mode still SKIPs (exit 0) while --strict-no-skip + the
    # STRICT_STAMPS=1 wrapper path both FAIL (exit 1) and the --exempt
    # escape hatch drops a peer file from iteration. Mirrors the canary
    # discipline from #213 / #234 / #297.
    run_script "$ROOT_DIR/tests/harness/abi_stamps_strict_no_skip_test.sh"
    ;;
  abi_stamps_strict_no_placeholder)
    # Issue #470 co-scope (sibling of #479's --strict-no-skip):
    # negative-canary self-test that proves
    # tools/validate_abi_stamps.py --strict-no-placeholder promotes a
    # placeholder-shape `Last verified against commit: HEAD` line
    # (#463 shape) from `ABI_STAMP:SKIP:<file>:placeholder:<token>` to
    # `ABI_STAMP:FAIL:<file>:placeholder:<token>` (exit 1), default
    # mode is unchanged, the flag does not over-reach into the
    # `no_stamp_line` arm (that's #479's job), and `--exempt` still
    # short-circuits before the placeholder gate fires.
    run_script "$ROOT_DIR/tests/harness/abi_stamps_strict_no_placeholder_test.sh"
    ;;
  abi_stamps_wrapper_default_strict)
    # Issue #470 (wrapper-flip slice): pins build/scripts/validate_abi_stamps.sh
    # to strict mode by default (no env var required). Asserts wrapper-default
    # FAILs on an unstamped doc, STRICT_STAMPS=0 reverts to SKIP-tolerant
    # legacy mode, and STRICT_STAMPS=1 remains a valid explicit-strict path.
    # Sibling of abi_stamps_strict_no_skip (#479) and the placeholder canary.
    run_script "$ROOT_DIR/tests/harness/abi_stamps_wrapper_default_strict_test.sh"
    ;;
  validate_sosh_capability_contract)
    # Issue #351 (drift validator slice): cross-check that every CAP_*
    # cited in §4 of docs/abi/sosh-capability-contract.md round-trips
    # against docs/abi/capability-registry.json — both that the cap id
    # exists (or is annotated `(if defined)`) and that the deny-marker
    # name segment matches the registry entry. Mirrors #234 / #297.
    run_script "$ROOT_DIR/build/scripts/validate_sosh_capability_contract.sh"
    ;;
  sosh_contract_registry_drift)
    # Issue #351 (drift validator slice): negative-canary self-test —
    # builds a sandbox repo whose sosh-capability-contract.md §4 cites a
    # CAP_* missing from the registry and asserts the
    # SOSH_CONTRACT:FAIL:cap_missing_from_registry:<cap_id> marker fires.
    # Mirrors the canary discipline from #213 / #234 / #297.
    run_script "$ROOT_DIR/tests/harness/sosh_contract_registry_drift_test.sh"
    ;;
  release_compliance_bundle_skip_pinned)
    # Issue #553: drift pin for the release_compliance_bundle SKIP marker.
    # Default mode requires the exact `awaiting_408` marker to stay pinned
    # in both script + docs; --strict-no-skip mode (for #408 Phase 3 flip)
    # requires the marker to be absent from both.
    run_script "$ROOT_DIR/build/scripts/test_release_compliance_bundle_skip_pinned.sh"
    ;;
  release_compliance_bundle)
    # Issue #523: LGPL-2.1 compliance bundle gate. SKIP-pinned
    # (`awaiting_408`) until M7-TOOLCHAIN-005 Phase 3 statically links
    # libtcc into the shipped image; layout + license byte-identity
    # checks run today so the scaffold cannot silently drift. See
    # docs/legal/lgpl-compliance.md.
    run_script "$ROOT_DIR/build/scripts/test_release_compliance_bundle.sh"
    ;;
  parity)
    run_script "$ROOT_DIR/build/scripts/test_shell_parity.sh"
    ;;
  harness_defense)
    # Self-test for the defensive dispatcher (see test_harness_defense.sh).
    run_script "$ROOT_DIR/build/scripts/test_harness_defense.sh"
    ;;
  canary_must_fail)
    # Intentionally failing canary (issue #212). The harness must
    # observe TEST:FAIL:_canary_must_fail and exit status 1; the bundle
    # treats this as the *expected* outcome via EXPECTED_FAIL_TARGETS.
    run_script "$ROOT_DIR/tests/integration/_canary_must_fail/canary_must_fail.sh"
    ;;
  *)
    echo "Unknown test: $TEST_NAME"
    usage
    exit 2
    ;;
esac
