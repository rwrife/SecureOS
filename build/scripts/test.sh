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
Usage: $(basename "$0") [hello_boot|hello_boot_negative|harness_negative|cap_api_contract|capability_table|cap_table_skeleton|cap_handle_repr|cap_handle_revoke_subject|cap_handle_revoke_subtree|process_table|process_create_table_full_deny_marker|process_find_aspace_by_subject|proc_sched|proc_sched_aspace_invariant|aspace_carve|clib_malloc|clib_ctype|clib_string|clib_qsort|aspace_bounds|aspace_invariant|capability_gate|console_svc_port_alloc|fs_svc_port_alloc|broker_svc_port_alloc|broker_svc_delete_owner_authority_check|broker_svc_cascade_revokes_minted_handle|broker_svc_step3_5_session_teardown|session_manager_first_for_subject|session_manager_subject_for_session|capability_audit|capability_audit_fixture|capability_audit_log|cap_broker|cap_deny_marker_shape|broker_share_allow|broker_share_deny|broker_share_revoke|workflow_rule|launcher_console|launcher_spawn_handoff|launcher_fs_spawn_handoff|launcher_broker_spawn_handoff|event_bus|scheduler|sof_format|sof_verify_at_rest|ed25519|cert_chain|codesign|tls|https|netlib_url_scheme|bearssl_compile|fs_service|launcher_fs|fs_service_persist_allow|fs_service_persist_deny|fs_service_ephemeral_reset|m3_fs_persist_allow_qemu|m3_fs_persist_deny_qemu|m3_fs_ephemeral_reset_qemu|m4_broker_share_allow_qemu|m4_broker_share_deny_qemu|m4_broker_share_revoke_qemu|m5_owner_delete_cascade_allow_qemu|m5_owner_delete_cascade_deny_qemu|m5_owner_delete_cascade_window_qemu|app_runtime|helloapp_allow|helloapp_deny|m2_helloapp_allow_qemu|m2_helloapp_deny_qemu|m2_launcher_console_qemu|kernel_console|kernel_filedemo|kernel_persistence|kernel_sessions|validator_report|abi_version|process_exit_wrapper|mem_brk_wrapper|sdk_abi_pin|sdk_libos_link|validate_sdk_no_kernel_includes|validate_manifests_abi_major|manifest_required_fields|manifest_persistence_enum|manifest_broker_role_enum|manifest_ownership_role_enum|manifest_owner_kind_enum|manifest_arena_bytes_range|ipc_sync_v0|ipc_port_lifecycle|ipc_handle_gate|ipc_bounds|m1_ipc_demo|syscall_entry_stub|validate_capability_registry|capability_registry_drift|validate_abi_stamps|abi_stamps_drift|parity|harness_defense|canary_must_fail|sosh_cap_allow|sosh_cap_deny|sosh_cap_source_exec|sosh_cap_exists|sosh_cap_cat_ls|sosh_cap_write_append|sosh_cap_export|win_gfx_gates|win_gfx_callsite|win_gfx_hal_allow_qemu|win_gfx_hal_deny_qemu|launcher_hal_callsite_migration|validate_sosh_capability_contract|sosh_contract_registry_drift|in_os_toolchain_dev_dir|toolchain_compiles_hello_in_os|toolchain_runs_compiled_binary|toolchain_unsigned_prompt_enforced|toolchain_large_output_persisted|toolchain_compile_error_reported|toolchain_heap_isolation]

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
  toolchain_compiles_hello_in_os|toolchain_runs_compiled_binary|toolchain_unsigned_prompt_enforced|toolchain_large_output_persisted|toolchain_compile_error_reported|toolchain_heap_isolation)
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
  mem_brk_wrapper)
    # Issue #421 (M7-TOOLCHAIN-001 slice 2): host-side smoke for the
    # `os_mem_brk` user-runtime wrapper. Pins the symbol export and
    # `(int, void **)` signature, plus the NULL-out-pointer guard
    # the freestanding `user/libs/clib` allocator relies on for the
    # host / no-bridge fall-through path.
    run_script "$ROOT_DIR/build/scripts/test_mem_brk_wrapper.sh"
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
  capability_registry_drift)
    # Issue #234: negative-canary self-test — adds a fake CAP_* to a
    # sandboxed capability.h copy and asserts the registry validator
    # emits the REGISTRY_VALIDATE:FAIL:enum_not_in_registry marker.
    # Mirrors the canary discipline from #213 / #177.
    run_script "$ROOT_DIR/tests/harness/capability_registry_drift_test.sh"
    ;;
  validate_abi_stamps)
    # Issue #297: fails if any docs/abi/*.md `Last verified against
    # commit:` stamp predates the file's most recent content-changing
    # commit. Follow-up to #257 (one-shot stamp bump) so the drift
    # cannot silently recur.
    run_script "$ROOT_DIR/build/scripts/validate_abi_stamps.sh"
    ;;
  abi_stamps_drift)
    # Issue #297: negative-canary self-test — builds a sandbox repo
    # whose docs/abi/syscalls.md stamp is older than its last content
    # commit and asserts the ABI_STAMP:FAIL:<file>:stamp=...:last_content=...
    # marker fires. Mirrors the canary discipline from #213 / #234.
    run_script "$ROOT_DIR/tests/harness/abi_stamps_drift_test.sh"
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
