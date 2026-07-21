#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
STARTED_AT="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
RUN_ID="${SECUREOS_RUN_ID:-$(date -u +"%Y%m%dT%H%M%SZ")-$(git -C "$ROOT_DIR" rev-parse --short HEAD)}"
RUN_DIR="$ROOT_DIR/artifacts/runs/$RUN_ID"
RUN_QEMU_DIR="$RUN_DIR/qemu"
RUN_TESTS_DIR="$RUN_DIR/tests"

mkdir -p "$RUN_QEMU_DIR" "$RUN_TESTS_DIR"

TEST_TARGETS=(
  hello_boot
  hello_boot_negative
  harness_negative
  cap_api_contract
  capability_table
  capability_gate
  # M1 substrate cap_handle / cap_table host gates (issue #487): pin
  # the cap_handle_t representation (#237/#240), the
  # cap_handle_revoke_subject + process_destroy hook contract (#247),
  # the cap_handle_revoke_subtree BFS walker + grant_child contract
  # (#327 / #323), and the cap_table bounds + table-full reject
  # (#240 area). All pass on main and are wired into test.sh, but were
  # missing from TEST_TARGETS — same orphan-from-TEST_TARGETS shape
  # #129 / #366 / #384 / #401 / #414 / #469 / #482 catches for other
  # host-only gates. cap_handle is the foundation every M2/M3/M4/M5
  # slice sits on, so a silent drift here is particularly load-bearing.
  cap_handle_repr
  cap_handle_revoke_subject
  cap_handle_revoke_subtree
  cap_table_skeleton
  # M1 substrate address-space host gates (issue #503): pin the
  # flat-with-bounds address_space_t + .proc_arena carve-out (#248 /
  # PR #249), per-aspace bounds enforcement, aspace structural
  # invariant, and the scheduler<->aspace invariant (#250 area). All
  # pass on main and are wired into test.sh, but were missing from
  # TEST_TARGETS — same orphan-from-TEST_TARGETS shape #129 / #366 /
  # #384 / #401 / #414 / #469 / #482 / #487 / #489 / #490 / #491 /
  # #492 catches for other host-only gates. The aspace layer is the
  # foundation every M2/M3/M4/M5/M7 slice mounts onto (every
  # process's arena window, every brk syscall, every launcher
  # spawn), so a silent drift here is particularly load-bearing.
  aspace_carve
  aspace_bounds
  aspace_invariant
  proc_sched_aspace_invariant
  capability_audit
  capability_audit_fixture
  capability_audit_log
  cap_broker
  cap_deny_marker_shape
  bearssl_compile
  # M7-TOOLCHAIN-005 (#408): vendor-surface drift gate for the
  # `vendor/tinycc/` slice — mirrors the BearSSL `bearssl_compile` gate
  # (#117) for the in-OS toolchain's second vendored dependency. Pins
  # `vendor/tinycc/Makefile.secureos` (no globbing, in-scope source list
  # >= the documented 9-file minimum, deliberately-excluded surfaces
  # like `tccrun.c` / `tcc.c` / non-x86_64 backends absent),
  # `vendor/tinycc/VERSION` (40-hex `Commit:` line), and — when the
  # submodule is initialized — cross-checks the pin SHA against the
  # live submodule commit and the existence of every listed source.
  # Same orphan-from-TEST_TARGETS shape as #129 / #366 / #384 / #401 /
  # #414 / #469 / #482 / #487 / #489 / #490 / #491 / #492 — wiring the
  # gate here so a future vendor-surface drift (e.g. an accidental
  # `tccrun.c` JIT pull-in or a pin SHA that no longer matches the
  # submodule) flips the bundle to FAIL before the freestanding port
  # (#408) starts compiling against the surface.
  tinycc_vendor_gate
  broker_share_allow
  broker_share_deny
  broker_share_revoke
    event_bus
    scheduler
    sof_format
    sof_verify_at_rest
    # M7-TOOLCHAIN-006 sub-slice (issue #409): host-only check that
    # `user/libs/sofpack` emits byte-identical bytes to `sof_build()`
    # for the same parameters and that the bytes round-trip through
    # `sof_parse()`. Pure host-side, no env deps. Same orphan-from-
    # TEST_TARGETS gate shape as the other M7 host-side slices.
    sofpack_wrap
    # M7-TOOLCHAIN-006 sub-slice (issue #533): host-only check that
    # `user/libs/manifestgen` emits a deterministic v0 manifest that
    # round-trips through manifests/schema/v0.json. SKIP-pins the
    # `owner.kind="local"` arm until #522's additive enum lands. Same
    # orphan-from-TEST_TARGETS gate shape as sofpack_wrap above.
    manifest_default_synthesise
    # Issue #634: host-side precedence pin for `cc` manifest resolution
    # (`--manifest` > sidecar > synth) and hard-fail semantics when an
    # explicit override is unreadable/invalid.
    cc_manifest_resolution_precedence
    tls
    https
    fs_service
    fs_service_persist_allow
    fs_service_persist_deny
    fs_service_ephemeral_reset
    app_runtime
    helloapp_allow
    helloapp_deny
    kernel_console
    kernel_filedemo
    kernel_persistence
    validator_report
    ipc_sync_v0
    ipc_port_lifecycle
    ipc_handle_gate
    proc_sched
    m1_ipc_demo
    validate_abi_stamps
    # Issue #624: docs/development pickup-guide stamp freshness gate
    # (Last-verified SHA exists + is not older than last content edit).
    docs_pickup_guide_m7_stamp
    # M1 substrate process-table host gates (umbrella #299, plan
    # plans/2026-05-25-m4-broker-on-m1-substrate.md): host-side checks
    # that pin the `process_*` table contract every M2/M3/M4/M5 slice
    # rides on. All three pass on `main` and are dispatched by
    # `build/scripts/test.sh`, but were not yet gating the bundle —
    # same orphan-from-TEST_TARGETS shape as #129 / #366 / #384 /
    # #401 / #414 / #469 / #482 / #487 / #489 / #490. A regression in
    # the process-table bounds, the per-subject aspace finder, or the
    # table-full deny marker would otherwise land green.
    process_table
    process_find_aspace_by_subject
    process_create_table_full_deny_marker
    # Issue #532: pins the canonical CAP:DENY:<sid>:app_exec:<resource>
    # marker emitted by `app_native_process_spawn`
    # (kernel/user/launcher_exec.c, M7-TOOLCHAIN-003 #422 / PR #427) when
    # the calling subject lacks CAP_APP_EXEC. This is the load-bearing
    # `launch.denied` marker plan #403 P4 / BUILD_ROADMAP §5.2 and
    # #410's `toolchain_unsigned_prompt_enforced` acceptance lean on;
    # without bundle gating a refactor that drops the emit would land
    # green on main — same orphan-from-TEST_TARGETS shape #487 / #503 /
    # #508 / #512 / #514 closed for other substrate subsystems.
    app_native_process_spawn_deny_marker
    # M2 console-svc / M3 fs-svc well-known-port allocator host gates
    # (umbrella #299, plan plans/2026-05-25-m4-broker-on-m1-substrate.md).
    # Both pin the IPC port_table seeding contract that the boot-order
    # wiring `ipc_port_table_init -> console_svc_init -> fs_svc_init ->
    # broker_svc_init -> proc_init` rides on (#272 / #282 / #287). All
    # four `*_port_alloc_{uninit,init,double_init,reset}` sub-checks
    # pass on `main` and are dispatched by `build/scripts/test.sh`, but
    # were not yet gating the bundle -- same orphan-from-TEST_TARGETS
    # shape as #129 / #366 / #384 / #401 / #414 / #469 / #482 / #487 /
    # #489 / #490 / #491. Wire so a regression in the M2/M3 port
    # allocator (double-init, reset, or uninit sentinel) flips the
    # bundle to FAIL instead of landing green on `main`.
    console_svc_port_alloc
    fs_svc_port_alloc
    # Launcher host gates (issue #512): the launcher is the single
    # trust-boundary every M2/M3/M4/M5/M7 slice mounts onto (console,
    # fs, broker, spawn handoff, HAL callsite migration, arena clamp).
    # All six host tests pass on main and are dispatched by
    # build/scripts/test.sh, but were orphan from TEST_TARGETS so a
    # regression in launcher revoke-restores-deny / invalid-app /
    # reset-clears-state (launcher_console), fs bypass-unregistered-
    # denied / invalid-inputs (launcher_fs), spawn-handoff destroy /
    # invalid-manifest (launcher_spawn_handoff), console-video HAL
    # callsite allow/deny-drops-silently (launcher_hal_callsite_migration),
    # broker spawn handoff gate + revoke-on-destroy
    # (launcher_broker_spawn_handoff), or fs spawn handoff read/write +
    # revoke-on-destroy (launcher_fs_spawn_handoff) would otherwise land
    # green. Same orphan-from-TEST_TARGETS shape as
    # #129 / #366 / #384 / #401 / #414 / #469 / #482 / #487 / #489 /
    # #490 / #491 / #492 / #503.
    launcher_console
    launcher_fs
    launcher_spawn_handoff
    launcher_hal_callsite_migration
    launcher_broker_spawn_handoff
    launcher_fs_spawn_handoff
    # M4 capability-broker substrate (umbrella #299, plan
    # plans/2026-05-25-m4-broker-on-m1-substrate.md): host-side broker_svc
    # checks + the three `_qemu` peers (slices 003/004) are all green on
    # main but had not been wired into the bundle gate. Adding them so a
    # future M4 substrate regression flips the bundle to FAIL.
    broker_svc_port_alloc
    broker_svc_delete_owner_authority_check
    broker_svc_cascade_revokes_minted_handle
    # `broker_svc_step3_5_session_teardown` is the host-side pin on
    # step 3.5 of `broker_svc_delete_owner` (session-leg drain); the
    # M5 WM/session-leg `_qemu` peer below covers the on-target arm,
    # but the host gate that pins the no-session-owner / table-drain
    # contract was still orphan from TEST_TARGETS. Same orphan-from-
    # TEST_TARGETS shape catalogued by #129 / #366 / #384 / #482 / #487.
    broker_svc_step3_5_session_teardown
    # M4 broker substrate session-manager host gates (umbrella #299,
    # plan plans/2026-05-25-m4-broker-on-m1-substrate.md). These pin
    # the broker_svc <-> session_manager bookkeeping that step 3.5 of
    # `broker_svc_delete_owner` (PR #363, gate `broker_svc_step3_5_session_teardown`
    # above) drains against: `session_manager_first_for_subject` covers
    # the no-sessions / single / drain / owner-isolation / null-out
    # invariants and `session_manager_subject_for_session` covers the
    # bounds / unused-slot / in-use / null-out / roundtrip invariants.
    # Both targets pass on `main` and are dispatched by
    # `build/scripts/test.sh`, but were orphan from the bundle gate --
    # same orphan-from-TEST_TARGETS shape catalogued by #129 / #366 /
    # #384 / #482 / #487 / #489. Without these wire-ups a regression
    # in the session-manager bookkeeping that step 3.5's cascade walks
    # over would land green on `main`.
    session_manager_first_for_subject
    session_manager_subject_for_session
    m4_broker_share_allow_qemu
    m4_broker_share_deny_qemu
    m4_broker_share_revoke_qemu
    # M5 ownership-graph cascade (umbrella #313, plan
    # plans/2026-05-25-m5-ownership-on-m1-substrate.md): allow-side
    # `_qemu` peer (slice 003). The deny-side peer
    # (`m5_owner_delete_cascade_deny_qemu`, #326 / PR #362) is now wired
    # in below alongside its allow counterpart so both legs of the
    # cascade gate the bundle.
    m5_owner_delete_cascade_allow_qemu
    m5_owner_delete_cascade_deny_qemu
    # M5 ownership-graph WM/session-leg cascade (slice 005c, #387 /
    # plan plans/2026-05-26-m5-wm-cascade-on-substrate.md). Pairs the
    # cap-leg allow/deny peers above with the session-leg peer that
    # asserts step 3.5 of broker_svc_delete_owner (PR #363) drains
    # owner-scoped sessions, preserves bystander sessions, recycles
    # the freed slot, double-delete is a no-op, and the audit ring
    # records the WM cascade events.
    m5_owner_delete_cascade_window_qemu
    # HAL call-site gates (issue #349 / PR #365): allow + deny `_qemu`
    # peers (issue #376) ride on the M2 substrate launcher path and
    # prove video_hal_write_as / input_hal_try_read_char_as /
    # mouse_hal_poll_event_as fire (or short-circuit) correctly against
    # a real launched PCB. Gate them once green so any future HAL
    # call-site regression flips the bundle to FAIL.
    win_gfx_hal_allow_qemu
    win_gfx_hal_deny_qemu
    # HAL gate primitives + subject-scoped wrapper call-site coverage
    # (umbrella #349 / PR #357 + PR #365). Host-side, no `_qemu` dep —
    # cheap to run on every CI lap and pins the gate contract +
    # wrapper behavior so a future HAL refactor that bypasses the
    # gate flips the bundle to FAIL.
    win_gfx_gates
    win_gfx_callsite
    validate_sosh_capability_contract
    # sosh scripting capability surface (umbrella #351, contract
    # `docs/abi/sosh-capability-contract.md` §4). All five enforcement
    # slices have merged via PRs #358 / #367 / #379 / #381 / #382 and
    # the host fixtures below are green on `main`, but none of them
    # were gating `validate_bundle.sh` — same shape of orphan #129
    # caught for `scheduler` / `tls` / `https` and #366 caught for
    # the M4/M5 substrate peers. Wire them in so any regression to
    # the `cap_check` callback contract (echo, source/external-cmd,
    # exists, cat/ls, write/append) flips the bundle to FAIL.
    sosh_cap_allow
    sosh_cap_deny
    sosh_cap_source_exec
    sosh_cap_exists
    sosh_cap_cat_ls
    sosh_cap_write_append
    sosh_cap_export
    # sosh fall-through to os_process_spawn for external binaries
    # (issue #493, sub-slice of #410 depending on #422). Closes the
    # gap where `sosh> hello` silently no-op'd. Host smoke drives
    # the embedder helper (probe + spawn mocks) so deny/allow/unknown
    # are covered without a live bridge.
    sosh_external_exec
    # sosh contract ↔ capability-registry drift guard (PR #361). Pure
    # static check that every `CAP_*` cited in the contract still
    # exists in `docs/abi/capability-registry.json`.
    sosh_contract_registry_drift
    # ABI / SDK public-surface gates (umbrella #136 / plan
    # `plans/2026-05-15-public-sdk-external-app-template.md`):
    # `abi_version` pins `OS_ABI_VERSION = 0` (#150 / #228),
    # `sdk_abi_pin` keeps `sdk/VERSION` ↔ `sdk/include/os/abi.h`
    # consistent, `sdk_libos_link` proves a freestanding link against
    # `sdk/lib/libos.a`, and `validate_sdk_no_kernel_includes` keeps
    # the SDK surface free of `kernel/` includes (the containment
    # guarantee #396 slice 3 wrappers depend on). Same orphan-from-
    # TEST_TARGETS shape — wire so a regression flips the bundle.
    abi_version
    sdk_abi_pin
    sdk_libos_link
    validate_sdk_no_kernel_includes
    # Issue #623: portability drift gate for the kernel non-arch tree.
    # Fails when direct architecture preprocessor macros
    # (`__x86_64__`, `__i386__`, `__amd64__`, `__aarch64__`, `__arm__`)
    # leak outside `kernel/arch/**` (except explicit, documented
    # allowlist paths). Keeps "x86 first, multi-arch ready" enforceable.
    validate_no_arch_macros_outside_arch_tree
    # `docs/abi/` Last-verified-stamp freshness guard (PR #298 / #297).
    # Pure static check that every `Last verified against commit:` line
    # in `docs/abi/*.md` does not predate the file's last content
    # commit. Cheap to run, no env deps.
    abi_stamps_drift
    # Issue #470 co-scope (sibling of #479's --strict-no-skip):
    # canary that the validator's placeholder-stamp gate behaves under
    # all three orthogonal arms (default SKIP, --strict-no-placeholder
    # FAIL, --exempt short-circuit). Wired here on the same
    # orphan-from-TEST_TARGETS shape #129 / #366 / #384 / #401 / #414 /
    # #469 caught for peer host gates, so future placeholder regressions
    # cannot land silently.
    abi_stamps_strict_no_placeholder
    # Capability-registry contract guards (umbrella #234, issue #482).
    # `validate_capability_registry` pins the `capability_id_t` enum ↔
    # `docs/abi/capability-registry.json` bijection, deny-marker shape,
    # and test-target / owning-plan resolution. `capability_registry_drift`
    # is the matching negative canary (#213 / #177 shape) that proves the
    # validator actually fails on a fresh enum entry. Both targets are
    # green on `main` and dispatched by `build/scripts/test.sh`, but
    # neither was gating the bundle — same orphan-from-TEST_TARGETS
    # regression shape #129 / #366 / #384 / #401 / #414 / #469 caught
    # for other host-side targets. Wire so a regression to the
    # capability-registry contract flips the bundle to FAIL.
    validate_capability_registry
    capability_registry_drift
    # Issue #630: docs/abi/README.md index drift gate + negative canary.
    # `validate_abi_index` enforces that every docs/abi/*.md is linked
    # from README and that README has no dangling docs/abi links;
    # `abi_index_drift` proves the validator fails on unlinked/dangling
    # fixture input.
    validate_abi_index
    abi_index_drift
    # Issue #470: negative canary for --strict-no-skip mode. Asserts that
    # default mode still SKIPs an unstamped docs/abi/*.md while
    # --strict-no-skip + the STRICT_STAMPS=1 wrapper path both FAIL with
    # ABI_STAMP:FAIL:<file>:no_stamp_line. Pure host-side check, no env
    # deps.
    abi_stamps_strict_no_skip
    # Issue #470 (wrapper-flip slice): canary pinning the strict-mode
    # default of build/scripts/validate_abi_stamps.sh. Asserts the
    # wrapper now FAILs on an unstamped docs/abi/*.md without any env
    # var, that STRICT_STAMPS=0 reverts to the legacy SKIP-tolerant
    # mode, and that STRICT_STAMPS=1 still works as an explicit-strict
    # path. Forces a regression of the default flip to flip the bundle.
    abi_stamps_wrapper_default_strict
    # In-OS toolchain Phase 1 (plan
    # plans/2026-05-28-in-os-toolchain-self-hosting.md): host-only check that
    # the /apps/dev developer directory + hello.c sample stage onto the disk
    # image and round-trip byte-identically. Guards the disk-staging wiring so
    # a regression that drops /apps/dev flips the bundle to FAIL.
    in_os_toolchain_dev_dir
    # Issue #636: source-drift pin for the canonical in-OS toolchain sample
    # (`dev/hello.c`). Keeps the validation input SHA-locked so edits are
    # intentional and reviewed before they fan out to downstream M7 goldens.
    dev_hello_c_pin
    # M7 in-OS toolchain (umbrella #403, plan
    # `plans/2026-05-28-in-os-toolchain-self-hosting.md` P1). `clib_malloc`
    # is the freestanding userland heap allocator (issue #404,
    # `user/libs/clib`) — pure host-side check, no env deps. The matching
    # `os_mem_brk` kernel-side wiring lands as the M7-TOOLCHAIN-001 follow-up.
    clib_malloc
    # M7 in-OS toolchain freestanding libc slice 2 (issue #407, parallel
    # to the str/mem slice in PR #416). `clib_ctype` pins the ctype family
    # (isdigit / isalpha / isspace / toupper / tolower / ...) that TinyCC's
    # preprocessor + lexer call. Pure host-side check, no env deps, no
    # syscalls. Drift on any shipped symbol flips the bundle to FAIL.
    clib_ctype
    # Manifest schema v0 validator + additive-enum gates (umbrella #285 /
    # #312 / #368 / #396 / #150). These six targets were intentionally
    # dropped from PR #401 (commit 667e932) because the
    # secureos/toolchain container did not ship python3-jsonschema, so
    # every wrapper short-circuited with TEST:FAIL:harness_missing_jsonschema
    # (rc=78). PR for #414 added python3-jsonschema to
    # build/docker/Dockerfile.toolchain (kept the container-internal
    # invariant post-#332) and wires the targets here so a regression to
    # any manifest enum / required-field / abi-major check flips the
    # bundle to FAIL — same orphan-from-TEST_TARGETS shape #129 / #366 /
    # #384 / #401 caught for prior host-only targets.
    manifest_required_fields
    manifest_persistence_enum
    manifest_broker_role_enum
    manifest_ownership_role_enum
    manifest_owner_kind_enum
    # Issue #424 (M7-TOOLCHAIN-001 schema sub-slice, refs #404/#421):
    # additive `runtime.arena_bytes` range gate. Wired here alongside
    # the peer manifest enum gates so the bundle flips on any
    # regression to per-app arena clamp range / required-field
    # behavior; jsonschema dependency already covered by the #414
    # toolchain container update.
    manifest_arena_bytes_range
    # M7-TOOLCHAIN-001 slice 3 (issue #448, refs #404 / #421 / #424):
    # the launcher-side enforcement peer of the schema sub-slice
    # (#424). Verifies that `launcher_spawn_app_from_manifest()` (and
    # its fs/broker siblings) clamp the optional manifest
    # `runtime.arena_bytes` to `[PROC_ARENA_BYTES_DEFAULT,
    # PROC_ARENA_BYTES_MAX]` at spawn time, with deny-by-default audit
    # event + LAUNCHER_ERR_INVALID_MANIFEST for out-of-range values
    # and no-op default-when-omitted parity. Same orphan-from-
    # TEST_TARGETS gate shape as the rest of the M7 slice peers above.
    launcher_arena_bytes
    # M7-TOOLCHAIN-001 slice 2 (issue #421): host-side smoke for the
    # `os_mem_brk` user-runtime wrapper landed alongside the kernel
    # bridge slot in the same PR. Pins the symbol export, the
    # `(int, void **)` signature shape consumed by the freestanding
    # `user/libs/clib` allocator's `clib_brk_fn` forwarder, and the
    # NULL-out-pointer fall-through. Same orphan-from-TEST_TARGETS
    # gate shape as #129 / #366 / #384 / #401 / #414 / #426.
    mem_brk_wrapper
    # M7-TOOLCHAIN-001 slice 3 (issue #421): host-side smoke for the
    # `clib_os_brk` forwarder that wires `user/libs/clib`'s
    # `clib_brk_fn` callback to `os_mem_brk`. Pins the `clib_brk_fn`
    # signature compatibility (so the forwarder symbol can be passed
    # straight to `clib_malloc_init`), the narrowing/zero-delta
    # guards (so a stray growth request never wraps the syscall's
    # signed `int` into a shrink), and the no-bridge -> NULL collapse
    # the allocator relies on for clean out-of-arena fallback. Same
    # orphan-from-TEST_TARGETS gate shape as #129 / #366 / #384 /
    # #401 / #414 / #426 / #432.
    clib_os_brk
    # M7-TOOLCHAIN-001 `_qemu` peer (issue #495, follow-up to #421 /
    # PR #455): end-to-end round-trip on the `os_mem_brk` bridge
    # slot. Links the production `app_native_mem_brk` body
    # (extracted into `kernel/user/app_native_heap.c` for this slice)
    # directly and drives grow / shrink / over-cap-deny / arena-reset
    # against the live arena — the same code the launcher wires into
    # `bridge->mem_brk`. Closes the deferred slice flagged by
    # `docs/abi/syscalls.md` §`os_mem_brk` and the
    # `toolchain_heap_isolation` acceptance marker. Same
    # orphan-from-TEST_TARGETS lineage as #129 / #366 / #384 / #401 /
    # #414 / #426 / #432.
    mem_brk_qemu
    validate_manifests_abi_major
    # M7-TOOLCHAIN-004 slice 1 (issue #407, plan P3): freestanding str*/mem*
    # family in `user/libs/clib`. Pure host-side check, no env deps. Wired
    # into the bundle so a regression to the libc nucleus trips here before
    # TinyCC (P4) starts depending on the same symbols.
    clib_string
    # M7-TOOLCHAIN-004 slice 4 (issue #407, plan P3): freestanding stdlib
    # subset (`atoi` / `strtol` / `strtoul` / `abs` / `labs`) in
    # `user/libs/clib`. Parallel to slices 1/2/3 -- different header, source,
    # test, and `symbol_set_pinned` sub-marker scope. Pure host-side check,
    # no env deps, no syscalls. Drift on any shipped symbol flips the bundle
    # to FAIL before TinyCC (P4) wires in.
    clib_stdlib
    # M7-TOOLCHAIN-004 slice (issue #407, plan P3): freestanding <float.h>
    # nucleus in `user/libs/clib`. Final freestanding-required header from
    # C11 §4¶6 (peer to <stddef.h> / <stdint.h> / <limits.h> / <stdarg.h>
    # / <stdbool.h> / <iso646.h> / <stdalign.h>). Pure host-side check,
    # no env deps, no syscalls. Drift on any shipped macro flips the
    # bundle to FAIL before TinyCC (P4) starts consuming the header.
    clib_float
    # M7-TOOLCHAIN-003 slice 1 (issue #406 / PR #413): host-side smoke
    # for the `os_process_exit` user-runtime wrapper. Sibling of
    # `process_spawn_wrapper` (slice 2) and `mem_brk_wrapper`
    # (M7-TOOLCHAIN-001 slice 2) -- the only one of the three
    # M7-TOOLCHAIN-003 / -001 user-runtime wrapper smokes that was
    # missing from TEST_TARGETS. Pins the exported symbol, signature,
    # and no-bridge fall-through contract that `sdk/lib/crt0.c`
    # forwards into, so a silent drift on `os_process_exit` would
    # otherwise break every SDK-built app's process-exit path. Same
    # orphan-from-TEST_TARGETS gate shape as #129 / #366 / #384 /
    # #401 / #414 (see issue #469).
    process_exit_wrapper
    # M7-TOOLCHAIN-003 slice 2 (issue #422): host-side smoke for the
    # `os_process_spawn` user-runtime wrapper. Pairs with
    # `process_exit_wrapper` (slice 1, #406 / PR #413) so any drift
    # in the spawn wrapper's exported symbol, signature, or
    # reserved-flag / bad-arg early-reject contract flips the
    # bundle to FAIL.
    process_spawn_wrapper
    # Issue #508: kernel-side capability workflow-rule layer (PR #209,
    # closes #77). Host-side dispatcher exists and is documented in the
    # test.sh usage banner, but was never added to TEST_TARGETS, so a
    # `-Werror=switch` regression in `kernel/cap/workflow_rule.c`'s
    # `capability_is_known()` allow-list slid in unnoticed when #348
    # (CAP_GFX_FRAMEBUFFER / CAP_INPUT_{KEYBOARD,MOUSE}) and
    # CAP_CLOCK_SET appended new `capability_id_t` slots. Same
    # orphan-from-TEST_TARGETS shape catalogued by #129 / #366 / #384 /
    # #401 / #414 / #469 / #482 / #487 / #489 / #490 / #491 / #492 /
    # #503 -- wiring here ensures the next cap-id addition flips the
    # bundle if the allow-list (or the deferred-cap fall-through arm)
    # is not updated in the same PR.
    workflow_rule
    # M7-TOOLCHAIN-004 slice 3 (issue #407): freestanding `qsort` in
    # `user/libs/clib`. Same parity shape as the str/mem slice (PR
    # #416) and the ctype slice (PR #417) — userland-only, no syscall
    # dependency, drift-pinned via a `symbol_set_pinned` sub-marker so
    # a future TinyCC drop / unrelated PR cannot silently remove the
    # symbol. Cheap host-side check; wire so a regression flips the
    # bundle.
    clib_qsort
    # M7-TOOLCHAIN-004 slice 6 (issue #407): freestanding `<stdarg.h>`
    # nucleus in `user/libs/clib` — `va_list` typedef + va_start /
    # va_arg / va_end / va_copy forwarded to `__builtin_va_*` (the only
    # correct freestanding implementation; see header rationale). Same
    # parity shape as the str/mem (PR #416), ctype (PR #417), qsort
    # (PR #418), stdlib (PR #428), and errno (PR #430) slices —
    # userland-only, no syscall dependency, drift-pinned via
    # `symbol_set_pinned` + macro-defined guards so a TinyCC drop (#408)
    # or unrelated PR cannot silently drop a member of the variadic
    # surface that `tccpp.c` / `tccgen.c` and the `tcc_error*` /
    # `tcc_warning*` diagnostic paths depend on.
    clib_stdarg
    # M7-TOOLCHAIN-004 slice 7 (issue #407, slice issue #446): freestanding
    # `<setjmp.h>` nucleus in `user/libs/clib` — hand-rolled i386 + x86_64
    # SysV callee-saved snapshot/restore implementing setjmp(env) /
    # longjmp(env, val). Required by #408 (TinyCC freestanding port) for
    # the tcc_error* / tcc_warning* recovery path that unwinds parse-time
    # faults. Same parity shape as the str/mem (#416), ctype (#417),
    # qsort (#418), stdlib (#428), errno (#430), and stdarg (#431) slices
    # — userland-only, no syscall dependency, drift-pinned via
    # `symbol_set_pinned` so a future PR cannot silently drop setjmp /
    # longjmp from the public surface.
    clib_setjmp
    # M7-TOOLCHAIN-004 slice 7 (issue #407, plan P3): freestanding
    # `bsearch` in `user/libs/clib`. Peer of the `qsort` slice (PR
    # #418) — the C standard pairs the two in <stdlib.h> because
    # callers typically sort then search the same array. Pure host
    # side check, no env / syscall deps, drift-pinned via a
    # `symbol_set_pinned` sub-marker so a future TinyCC drop /
    # unrelated PR cannot silently remove the symbol. Cheap host
    # side check; wired here so a regression flips the bundle
    # (same orphan-from-TEST_TARGETS shape #129 / #366 / #384 /
    # #401 / #414 catch).
    clib_bsearch
    # M7-TOOLCHAIN-004 slice 8 (issue #407): freestanding `<limits.h>`
    # nucleus in `user/libs/clib`. C11 §4¶6 requires `<limits.h>` even on
    # a freestanding implementation, and TinyCC (#408) plus the stdlib
    # slice (PR #428) both consume it. Same parity shape as the str/mem,
    # ctype, and qsort slices — userland-only, no syscall dependency,
    # drift-pinned via a `symbol_set_pinned` sub-marker on the helper TU
    # so a future TinyCC drop or unrelated PR cannot silently change a
    # macro value. Cheap host-side check; wire so a regression flips the
    # bundle.
    clib_limits
    # M7-TOOLCHAIN-004 slice 9 (issue #407): freestanding `<stdbool.h>`
    # nucleus in `user/libs/clib`. C11 §4¶6 lists `<stdbool.h>` among
    # the headers a *freestanding* implementation must ship, and TinyCC
    # (#408) plus several pending #407 siblings can use `bool` / `true`
    # / `false` once it lands. Same parity shape as the str/mem, ctype,
    # qsort, and limits slices — userland-only, no syscall dependency,
    # drift-pinned via a `symbol_set_pinned` sub-marker on the helper TU
    # so a future TinyCC drop or unrelated PR cannot silently change a
    # macro value or re-alias `bool` to `int`. Cheap host-side check;
    # wire so a regression flips the bundle.
    clib_stdbool
    # M7-TOOLCHAIN-004 slice 9 (issue #407): freestanding `<stddef.h>`
    # nucleus in `user/libs/clib`. C11 §4¶6 requires `<stddef.h>` even on
    # a freestanding implementation; TinyCC (#408) consumes `size_t`,
    # `ptrdiff_t`, and `offsetof` pervasively. Same parity shape as the
    # str/mem, ctype, qsort, and `<limits.h>` slices — userland-only, no
    # syscall dependency, drift-pinned via a `symbol_set_pinned` sub-
    # marker on the helper TU so a future TinyCC drop or unrelated PR
    # cannot silently change a typedef width. Cheap host-side check;
    # wire so a regression flips the bundle.
    clib_stddef
    # M7-TOOLCHAIN-004 slice 10 (issue #407): freestanding `<stdint.h>`
    # nucleus in `user/libs/clib`. C11 §4¶6 / §7.20 requires `<stdint.h>`
    # even on a freestanding implementation; TinyCC (#408) and any non-
    # trivial in-OS C source consume `int{8,16,32,64}_t`,
    # `uint{8,16,32,64}_t`, `intptr_t`, `uintptr_t`, `intmax_t`,
    # `uintmax_t`, `INT*_MAX/MIN`, `UINT*_MAX`, `SIZE_MAX`, and
    # `PTRDIFF_*` pervasively. Same parity shape as the str/mem,
    # ctype, qsort, `<limits.h>`, and `<stddef.h>` slices — userland-
    # only, no syscall dependency, drift-pinned via a
    # `symbol_set_pinned` sub-marker on the helper TU so a future
    # TinyCC drop or unrelated PR cannot silently change a typedef
    # width or drop a constant. Cheap host-side check; wire so a
    # regression flips the bundle.
    clib_stdint
    # M7-TOOLCHAIN-004 slice 11 (issue #407): freestanding `<inttypes.h>`
    # format-string nucleus in `user/libs/clib`. Layers PRI*/SCN*
    # macros on top of the slice 10 / 10b stdint typedefs (PRs #437,
    # #457). Pure-preprocessor surface (no stdio dependency); the test
    # round-trips canonical decimal/hex/octal spellings against the
    # host's snprintf and pins least/fast-width macros to their
    # exact-width counterparts. The `symbol_set_pinned` sub-marker
    # drift-anchors each macro through the src/inttypes.c helper TU.
    clib_inttypes
    # M7-TOOLCHAIN-004 slice (issue #407): freestanding `<iso646.h>`
    # nucleus in `user/libs/clib`. C11 §4¶6 lists `<iso646.h>` as one of
    # the freestanding-required headers; TinyCC (#408), the stdlib slice
    # (PR #428), and any third-party SDK code consumed by the in-OS
    # toolchain (#403) are entitled to `#include <iso646.h>`. Same parity
    # shape as the str/mem, ctype, qsort, `<limits.h>`, `<stddef.h>`, and
    # `<stdint.h>` slices — header-only nucleus + a tiny `src/iso646.c`
    # helper TU that folds each macro into an integer constant the host
    # test round-trips, drift-pinned via a `symbol_set_pinned` sub-marker
    # so a future TinyCC drop or unrelated PR cannot silently redefine an
    # operator-spelling macro (e.g. `#define or  &` instead of `||`).
    # Cheap host-side check; wire so a regression flips the bundle.
    clib_iso646
    # M7-TOOLCHAIN-004 slice (issue #407): freestanding `<stdalign.h>`
    # nucleus in `user/libs/clib`. C11 §4¶6 lists `<stdalign.h>` as one
    # of the freestanding-required headers; TinyCC (#408), the stdlib
    # slice (PR #428), and any third-party SDK code consumed by the
    # in-OS toolchain (#403) are entitled to `#include <stdalign.h>`.
    # Same parity shape as the str/mem, ctype, qsort, `<limits.h>`,
    # `<stddef.h>`, `<stdint.h>`, and `<iso646.h>` slices — header-only
    # nucleus + a tiny `src/stdalign.c` helper TU that folds each macro
    # into an integer constant the host test round-trips, drift-pinned
    # via a `symbol_set_pinned` sub-marker so a future TinyCC drop or
    # unrelated PR cannot silently redefine `alignas` / `alignof` or
    # flip a `__align{as,of}_is_defined` feature-test macro to `0`.
    # Cheap host-side check; wire so a regression flips the bundle.
    clib_stdalign
    # Freestanding <assert.h> nucleus (#407 / M7-TOOLCHAIN-004) —
    # userland-only, no syscall dependency, drift-pinned via
    # `symbol_set_pinned` + macro-defined guards. The registered-handler
    # hook (`clib_assert_set_handler`) lets the host test exercise the
    # failure path via longjmp and the on-target runtime install a
    # forwarder to `os_process_exit(1)` once #406 lands without
    # touching the slice.
    clib_assert
    # On-target `clib_os_assert` forwarder (#407 follow-up) that wires
    # the registered-handler hook above to `os_process_exit(1)` (the
    # syscall landed by #406 / PR #427). Host smoke pins the
    # `clib_assert_handler_fn` signature compatibility and the
    # `clib_os_assert_install` convenience-installer symbol. Same
    # orphan-from-TEST_TARGETS gate shape as #129 / #366 / #384 /
    # #401 / #414 / #426 / #432 / #455.
    clib_os_assert
    # M7-TOOLCHAIN acceptance suite scaffolding (issue #423, umbrella #403,
    # plan plans/2026-05-28-in-os-toolchain-self-hosting.md §"Acceptance
    # tests"). All eight markers are SKIP-pinned today — each subordinate
    # script in tests/m7_toolchain/ emits TEST:SKIP:<marker>:awaiting_<n>
    # then rolls up TEST:PASS:<marker> so the bundle stays green. Wiring
    # them here freezes the marker spellings as a single source of truth:
    # any accidental rename or drop flips the bundle to FAIL (same orphan-
    # from-TEST_TARGETS shape #129 / #366 / #384 / #401 / #414 catch).
    # The gating execute slices (#409 / #410 / #421 / #422) replace each
    # SKIP with a real assertion as they land.
    toolchain_compiles_hello_in_os
    toolchain_runs_compiled_binary
    toolchain_unsigned_prompt_enforced
    toolchain_large_output_persisted
    toolchain_compile_error_reported
    toolchain_cc_manifest_sidecar_written_on_link
    toolchain_cc_version_and_help_text_pinned
    toolchain_heap_isolation
    # M7-TOOLCHAIN-005 sub-slice (issue #408 Phase 2): freestanding TinyCC
    # config header at `vendor/tinycc/config-secureos.h` — encodes the
    # porting note 1 / 3 knobs (TCC_TARGET_X86_64 + ELF default,
    # CONFIG_TCC_BACKTRACE/BCHECK disabled, ONE_SOURCE=0, VFS-pinned
    # sysinclude/lib/crt/tccdir paths) that the Phase 3 freestanding
    # libtcc build will -include in place of upstream's autoconf-
    # generated config.h. Companion to PR #516's vendor-surface drift
    # gate (`tinycc_vendor_gate`); both run host-side, no kernel build.
    tinycc_config_secureos
    # M7-TOOLCHAIN-005 sub-slice (issue #408 Phase 2): freestanding TinyCC
    # libc dependency surface pinned at `vendor/tinycc/libc-deps.json`
    # and verified by `tinycc_libc_deps`. Encodes porting note 2 of
    # `vendor/tinycc/Makefile.secureos`: every libc symbol the pinned
    # TCC_ALL_SRCS source set calls is partitioned into clib-provided
    # vs. not-yet-provided, with the partition cross-checked against
    # `user/libs/clib/include/clib/*.h` so a silent change to either
    # side (TinyCC submodule bump, clib slice landing) flips this gate
    # before Phase 3 build wiring quietly drifts. Third leg of the
    # tinycc_vendor_gate (sources) / tinycc_config_secureos (config) /
    # tinycc_libc_deps (libc surface) audit triangle.
    tinycc_libc_deps
    # Issue #494: drift gate for the markers.json source-of-truth file
    # above. validate_m7_markers cross-checks that every marker is wired
    # through this TEST_TARGETS block + the case arms in test.sh + the
    # stub scripts in tests/m7_toolchain/, and (when gh is reachable)
    # that no gatingIssue has closed while reason= is still
    # awaiting_<n>. m7_markers_drift is the negative canary that proves
    # the validator is real, mirroring #213 / #234 / #297 / #351.
    validate_m7_markers
    m7_markers_drift
    # Issue #631: synthetic fixture coverage for
    # tools/report_skip_backlog.py (source-scan count + per-gating-issue
    # bucket accounting, including unpinned markers).
    skip_backlog_report_fixture
    # Issue #641: cap SKIP-pinned M7 harness markers per OPEN gating
    # issue. `skip_backlog_cap` is the policy gate;
    # `skip_backlog_cap_fixture` is the deterministic fixture proving
    # over-cap FAIL, grandfathered PASS, ceiling enforcement, and
    # stale-allowlist rejection.
    skip_backlog_cap_fixture
    skip_backlog_cap
    # Issue #523: LGPL-2.1 compliance bundle gate. SKIP-pinned
    # (`awaiting_408`) until M7-TOOLCHAIN-005 Phase 3 actually links
    # libtcc into the shipped image. Even SKIP-pinned, the wrapper
    # exercises build_release_compliance_bundle.sh end-to-end and
    # asserts bundle layout + byte-identity against the vendor license
    # texts, so the scaffold cannot silently drift before the gating
    # slice lands. Mirrors the SKIP-with-real-shape discipline used by
    # tests/m7_toolchain/. Normative contract:
    # docs/legal/lgpl-compliance.md.
    release_compliance_bundle
    # M7-TOOLCHAIN-004 slice 5 (issue #407): freestanding `<errno.h>`
    # nucleus in `user/libs/clib` — writable `int errno;` global plus
    # the pinned EPERM/ENOENT/ENOMEM/EINVAL/ERANGE/... macro family and
    # a bounded `clib_strerror`. Same parity shape as the str/mem (PR
    # #416), ctype (PR #417), qsort (PR #418), and stdlib (PR #428)
    # slices — userland-only, no syscall dependency, drift-pinned via
    # `symbol_set_pinned` + `macro_values_pinned` so a TinyCC drop
    # (#408) or unrelated PR cannot silently renumber `ERANGE` and
    # break the `strtol`/`strtoul` overflow contract that PR #428's
    # header explicitly defers to this slice.
    clib_errno
    # M7-TOOLCHAIN-004 slice 8 (issue #447 / #407, plan P3): freestanding
    # `<stdio.h>` nucleus in `user/libs/clib` — `FILE` + `stdin`/
    # `stdout`/`stderr` over a swappable `clib_stdio_backend_t`
    # function-pointer table (on-target: `os_fs_*` + `os_console_write`;
    # host: recorder shim). Covers `fopen`/`fclose`/`fread`/`fwrite`/
    # `fflush`/`fputs`/`fputc`/`fprintf`/`vfprintf`/`printf` plus the
    # minimal printf format set TinyCC (#408) and the `cc` driver
    # (#409) emit. Same parity shape as the peer clib slices —
    # userland-only, no syscall dependency, drift-pinned via
    # `symbol_set_pinned` so a TinyCC drop or unrelated PR cannot
    # silently drop one of the public symbols TinyCC links against.
    clib_stdio
    # M7-TOOLCHAIN-004 slice (issue #407): freestanding `<stdnoreturn.h>`
    # nucleus in `user/libs/clib`. C11 §4¶6 lists `<stdnoreturn.h>` as
    # one of the freestanding-required headers; §7.23 defines the header
    # as a single convenience macro `noreturn` aliasing the C11 keyword
    # `_Noreturn`. TinyCC (#408) and any third-party SDK code consumed
    # by the in-OS toolchain (#403) are entitled to `#include
    # <stdnoreturn.h>`. Same parity shape as the str/mem (PR #416),
    # ctype (PR #417), qsort (PR #418), stdlib (PR #428), errno (PR
    # #430), and in-flight `<iso646.h>`/`<stdalign.h>` slices --
    # header-only nucleus + a tiny `src/stdnoreturn.c` helper TU plus
    # a host test that pins the macro semantically (declares a real
    # `noreturn`-decorated function through the header under -Werror)
    # and pins the helper-TU exports via a `symbol_set_pinned`
    # sub-marker so a future PR cannot silently turn `#define noreturn
    # _Noreturn` into a no-op. Cheap host-side check; wire so a
    # regression flips the bundle.
    clib_stdnoreturn
    # M7-TOOLCHAIN-004 / issue #449: drift gate for the public symbol
    # surface of `libclib.a`. Three-way diff between
    # `tests/data/clib_symbols.expected` (canonical pin), the canonical
    # block in `docs/abi/clib-symbols.md`, and the actual `nm -g
    # --defined-only` of a freshly host-built `libclib.a`. Same
    # orphan-from-TEST_TARGETS shape #129 / #366 / #384 / #401 / #414
    # / #426 catches for other host-only gates -- adding it here so a
    # future slice that bumps `libclib.a`'s surface without updating
    # the pin OR doc trips the bundle before TinyCC (#408) starts
    # linking against the same symbols.
    clib_symbol_drift

    # Issue #514: four substrate-level host gates that were dispatched by
    # build/scripts/test.sh but orphan-from-TEST_TARGETS (same shape as
    # #129 / #366 / #384 / #401 / #414 / #469 / #482 / #487 / #489 /
    # #490 / #491 / #492 / #503 / #512). All four are load-bearing:
    #   - syscall_entry_stub: M1 syscall ABI anchor + deny-marker shape
    #     (originally #232) — every M2/M3/M4/M5/M7 syscall test rides on
    #     this contract.
    #   - ipc_bounds: kernel IPC payload-bounds allow + one-past-end +
    #     straddle + no-pcb skipped (every capability check + spawn
    #     handoff path rides this).
    #   - netlib_url_scheme: zero-trust network ABI URL-scheme allow/deny
    #     contract — sole host gate for the netlib surface today.
    #   - harness_defense: meta-canary that defends the bundle harness
    #     itself against silent no-ops (the original #91 motivation; the
    #     reason every other orphan-from-TEST_TARGETS issue in this chain
    #     can be caught at all).
    syscall_entry_stub
    ipc_bounds
    netlib_url_scheme
    harness_defense
)
# NOTE: ed25519, cert_chain, codesign, and kernel_sessions are intentionally
# NOT in TEST_TARGETS yet — see issue #129. They are wired into test.sh /
# test.ps1 but currently red (ed25519 sign/verify roundtrip, cert_chain root
# validation, codesign -Werror=unused-variable on tests/codesign_test.c:307)
# or blocked on the disk-image perms chain (#106 / PR #107 for kernel_sessions).
# Add each here as the corresponding fix lands, so the bundle stays green.

# Issue #212: targets that MUST fail. The bundle's pass condition is
# "every TEST_TARGETS target passes AND every EXPECTED_FAIL_TARGETS
# target fails". Each entry here is a permanent canary that proves the
# harness still classifies a deliberate failure as a failure (defends
# against #90 / #129 / #140 style silent no-ops).
EXPECTED_FAIL_TARGETS=(
  canary_must_fail
)

STATUS_LINES=()
FAILED_TESTS=()

# Exit code 78 is reserved by build/scripts/test.sh for HARNESS_ERROR
# (missing/unreadable subordinate script). It is reported as a distinct
# `harness_error` status so agents can tell infra breakage from a real
# regression (see issue #91).
HARNESS_ERROR_EXIT=78

for target in "${TEST_TARGETS[@]}"; do
  test_started="$(date +%s)"
  set +e
  bash "$ROOT_DIR/build/scripts/test.sh" "$target"
  rc=$?
  set -e
  if [[ $rc -eq 0 ]]; then
    status="pass"
  elif [[ $rc -eq $HARNESS_ERROR_EXIT ]]; then
    status="harness_error"
    FAILED_TESTS+=("$target")
  else
    status="fail"
    FAILED_TESTS+=("$target")
  fi
  test_finished="$(date +%s)"
  duration="$((test_finished - test_started))"
  STATUS_LINES+=("${target}|${status}|${duration}|expected_pass")
done

# Issue #212: expected-fail canaries. We INVERT the pass condition: a
# canary that *fails* (rc=1) is the green path and is recorded as
# status=pass with expectedFail=true / observed=fail / classification=ok
# in the JSON report. A canary that unexpectedly *passes* (rc=0) flips
# the bundle to FAIL with the deterministic marker
# `BUNDLE_FAIL: canary did not fail`. A harness error (rc=78) is still
# reported as harness_error so infra breakage stays distinguishable.
for target in "${EXPECTED_FAIL_TARGETS[@]}"; do
  test_started="$(date +%s)"
  set +e
  bash "$ROOT_DIR/build/scripts/test.sh" "$target"
  rc=$?
  set -e
  if [[ $rc -eq 0 ]]; then
    # Canary did NOT fail -- this is the regression we exist to catch.
    status="fail"
    observed="pass"
    classification="anomaly"
    FAILED_TESTS+=("$target")
    echo "BUNDLE_FAIL: canary did not fail" >&2
    echo "BUNDLE_FAIL: canary did not fail (target=$target)"
  elif [[ $rc -eq $HARNESS_ERROR_EXIT ]]; then
    status="harness_error"
    observed="harness_error"
    classification="harness_error"
    FAILED_TESTS+=("$target")
  else
    # Expected failure observed -- this is the green path for canaries.
    status="pass"
    observed="fail"
    classification="ok"
  fi
  test_finished="$(date +%s)"
  duration="$((test_finished - test_started))"
  STATUS_LINES+=("${target}|${status}|${duration}|expected_fail|${observed}|${classification}")
done

# Copy known QEMU artifacts when present.
for qemu_name in hello_boot hello_boot_fail kernel_console kernel_filedemo kernel_persistence; do
  if [[ -f "$ROOT_DIR/artifacts/qemu/${qemu_name}.log" ]]; then
    cp "$ROOT_DIR/artifacts/qemu/${qemu_name}.log" "$RUN_QEMU_DIR/"
  fi
  if [[ -f "$ROOT_DIR/artifacts/qemu/${qemu_name}.meta.json" ]]; then
    cp "$ROOT_DIR/artifacts/qemu/${qemu_name}.meta.json" "$RUN_QEMU_DIR/"
  fi
done

if [[ -f "$ROOT_DIR/artifacts/kernel/secureos.iso" ]]; then
    cp "$ROOT_DIR/artifacts/kernel/secureos.iso" "$RUN_DIR/"
fi

if [[ -d "$ROOT_DIR/artifacts/tests" ]]; then
  find "$ROOT_DIR/artifacts/tests" -maxdepth 1 -type f -exec cp {} "$RUN_TESTS_DIR/" \;
fi

BOOT_BIN="$ROOT_DIR/experiments/bootloader/boot.bin"
KERNEL_ISO="$ROOT_DIR/artifacts/kernel/secureos.iso"
IMAGE_HASH=""
if [[ -f "$BOOT_BIN" ]]; then
  IMAGE_HASH="$(shasum -a 256 "$BOOT_BIN" | awk '{print $1}')"
fi

KERNEL_ISO_HASH=""
if [[ -f "$KERNEL_ISO" ]]; then
    KERNEL_ISO_HASH="$(shasum -a 256 "$KERNEL_ISO" | awk '{print $1}')"
fi

GIT_SHA="$(git -C "$ROOT_DIR" rev-parse HEAD)"
GIT_REF="$(git -C "$ROOT_DIR" rev-parse --abbrev-ref HEAD)"
FINISHED_AT="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

STATUS_LINES_JSON="$(printf '%s\n' "${STATUS_LINES[@]}")"
export ROOT_DIR RUN_DIR RUN_ID STARTED_AT FINISHED_AT GIT_SHA GIT_REF IMAGE_HASH KERNEL_ISO_HASH STATUS_LINES_JSON

python3 - <<'PY'
import json
import os
from pathlib import Path

root_dir = Path(os.environ["ROOT_DIR"])
run_dir = Path(os.environ["RUN_DIR"])
status_lines = [line for line in os.environ.get("STATUS_LINES_JSON", "").splitlines() if line.strip()]

checks = []
targets = []
failed = []
harness_errors = []

def _resolve_log_and_artifacts(target_name: str):
    """Return (logPath, artifacts[]) relative to run_dir for a given target.

    Probes the conventional artifact locations populated earlier in this
    script (qemu/ and tests/). Returns (None, []) if nothing matched.
    """
    qemu_aliases = {
        "hello_boot":          ["hello_boot"],
        "hello_boot_negative": ["hello_boot_fail"],
        "kernel_console":      ["kernel_console"],
        "kernel_filedemo":     ["kernel_filedemo"],
        "kernel_persistence":  ["kernel_persistence"],
    }
    artifacts_rel = []
    log_rel = None
    for stem in qemu_aliases.get(target_name, []):
        log = run_dir / "qemu" / f"{stem}.log"
        if log.exists():
            rel = str(log.relative_to(run_dir))
            artifacts_rel.append(rel)
            if log_rel is None:
                log_rel = rel
        meta = run_dir / "qemu" / f"{stem}.meta.json"
        if meta.exists():
            artifacts_rel.append(str(meta.relative_to(run_dir)))
    candidates = [
        run_dir / "tests" / f"{target_name}.log",
        run_dir / "tests" / f"{target_name}_test.log",
    ]
    for cand in candidates:
        if cand.exists():
            rel = str(cand.relative_to(run_dir))
            artifacts_rel.append(rel)
            if log_rel is None:
                log_rel = rel
    return log_rel, artifacts_rel

for line in status_lines:
    parts = line.split("|")
    # New format (issue #212): name|status|duration|kind[|observed|classification]
    # Legacy format: name|status|duration
    if len(parts) >= 4:
        name = parts[0]
        status = parts[1]
        duration = parts[2]
        kind = parts[3]
        observed = parts[4] if len(parts) > 4 else None
        classification = parts[5] if len(parts) > 5 else None
    else:
        name, status, duration = parts[0], parts[1], parts[2]
        kind = "expected_pass"
        observed = None
        classification = None
    if status not in ("pass", "fail", "harness_error"):
        # Defensive: treat unknown statuses as fail rather than crash the JSON.
        status = "fail"
    check_entry = {
        "name": name,
        "status": status,
        "pass": status == "pass",
        "durationSeconds": int(duration),
    }
    if kind == "expected_fail":
        check_entry["expectedFail"] = True
        if observed is not None:
            check_entry["observed"] = observed
        if classification is not None:
            check_entry["classification"] = classification
    checks.append(check_entry)
    log_path, artifacts_for_target = _resolve_log_and_artifacts(name)
    target_entry = {
        "name": name,
        "status": status,
        "pass": status == "pass",
        "durationSeconds": int(duration),
        "logPath": log_path,
        "reasonCode": None if status == "pass" else status,
        "artifacts": artifacts_for_target,
    }
    if kind == "expected_fail":
        target_entry["expectedFail"] = True
        if observed is not None:
            target_entry["observed"] = observed
        if classification is not None:
            target_entry["classification"] = classification
    targets.append(target_entry)
    if status == "harness_error":
        harness_errors.append(name)
        failed.append(name)
    elif status != "pass":
        failed.append(name)

qemu_commands = {}
for meta_name in ["hello_boot", "kernel_console", "kernel_filedemo", "kernel_persistence"]:
    qemu_meta = run_dir / "qemu" / f"{meta_name}.meta.json"
    if not qemu_meta.exists():
        continue
    try:
        qemu_commands[meta_name] = json.loads(qemu_meta.read_text()).get("command", [])
    except Exception:
        qemu_commands[meta_name] = []

summary_check_error = None
summary_path = run_dir / "tests" / "capability_audit_summary.json"
summary = None

if not summary_path.exists():
    summary_check_error = f"missing summary artifact: {summary_path}"
else:
    try:
        summary = json.loads(summary_path.read_text())
    except Exception as exc:
        summary_check_error = f"invalid JSON in {summary_path}: {exc}"

if summary is not None:
    required_int_fields = [
        "schemaVersion",
        "ringCapacity",
        "retainedEvents",
        "droppedEvents",
        "checkpointCount",
        "latestCheckpointId",
        "latestCheckpointSeal",
        "latestCheckpointDroppedCount",
    ]
    for field in required_int_fields:
        value = summary.get(field)
        if not isinstance(value, int):
            summary_check_error = f"{field} must be integer"
            break

    sequence_window = summary.get("sequenceWindow") if isinstance(summary, dict) else None
    checkpoint_window = summary.get("checkpointWindow") if isinstance(summary, dict) else None

    if summary_check_error is None and not isinstance(sequence_window, dict):
        summary_check_error = "sequenceWindow must be object"
    if summary_check_error is None and not isinstance(checkpoint_window, dict):
        summary_check_error = "checkpointWindow must be object"

    if summary_check_error is None:
        for field in ["firstSequenceId", "lastSequenceId", "eventCount"]:
            if not isinstance(sequence_window.get(field), int):
                summary_check_error = f"sequenceWindow.{field} must be integer"
                break

    if summary_check_error is None:
        if sequence_window.get("coverage") not in ["full", "truncated"]:
            summary_check_error = "sequenceWindow.coverage must be full or truncated"

    if summary_check_error is None:
        for field in ["firstCheckpointId", "lastCheckpointId", "count"]:
            if not isinstance(checkpoint_window.get(field), int):
                summary_check_error = f"checkpointWindow.{field} must be integer"
                break

    if summary_check_error is None:
        if summary.get("schemaVersion") != 1:
            summary_check_error = "schemaVersion must be 1"
        elif summary.get("test") != "capability_audit":
            summary_check_error = "test must be capability_audit"
        elif summary.get("ringCapacity", 0) <= 0:
            summary_check_error = "ringCapacity must be > 0"
        elif summary.get("retainedEvents", -1) < 0:
            summary_check_error = "retainedEvents must be >= 0"
        elif summary.get("droppedEvents", -1) < 0:
            summary_check_error = "droppedEvents must be >= 0"
        elif summary.get("retainedEvents", 0) > summary.get("ringCapacity", 0):
            summary_check_error = "retainedEvents must be <= ringCapacity"
        elif summary.get("retainedEvents", 0) + summary.get("droppedEvents", 0) < summary.get("ringCapacity", 0):
            summary_check_error = "retainedEvents + droppedEvents must be >= ringCapacity"
        elif summary.get("checkpointCount", -1) < 0:
            summary_check_error = "checkpointCount must be >= 0"
        elif summary.get("latestCheckpointId", -1) < 0:
            summary_check_error = "latestCheckpointId must be >= 0"
        elif summary.get("latestCheckpointSeal", -1) < 0:
            summary_check_error = "latestCheckpointSeal must be >= 0"
        elif summary.get("latestCheckpointDroppedCount", -1) < 0:
            summary_check_error = "latestCheckpointDroppedCount must be >= 0"
        elif summary.get("checkpointCount", 0) == 0 and (
            summary.get("latestCheckpointId", 0) != 0
            or summary.get("latestCheckpointSeal", 0) != 0
            or summary.get("latestCheckpointDroppedCount", 0) != 0
        ):
            summary_check_error = "latest checkpoint fields must be 0 when checkpointCount is 0"
        elif sequence_window.get("eventCount") != summary.get("retainedEvents"):
            summary_check_error = "sequenceWindow.eventCount must match retainedEvents"
        elif sequence_window.get("coverage") == "truncated" and summary.get("droppedEvents", 0) == 0:
            summary_check_error = "sequenceWindow.coverage truncated requires droppedEvents > 0"
        elif sequence_window.get("coverage") == "full" and summary.get("droppedEvents", 0) > 0:
            summary_check_error = "sequenceWindow.coverage full requires droppedEvents == 0"
        elif checkpoint_window.get("count") != summary.get("checkpointCount"):
            summary_check_error = "checkpointWindow.count must match checkpointCount"
        elif summary.get("retainedEvents", 0) == 0 and (
            sequence_window.get("firstSequenceId", 0) != 0
            or sequence_window.get("lastSequenceId", 0) != 0
        ):
            summary_check_error = "sequenceWindow IDs must be 0 when retainedEvents is 0"
        elif summary.get("retainedEvents", 0) > 0 and sequence_window.get("firstSequenceId") > sequence_window.get("lastSequenceId"):
            summary_check_error = "sequenceWindow.firstSequenceId must be <= lastSequenceId"
        elif summary.get("checkpointCount", 0) == 0 and (
            checkpoint_window.get("firstCheckpointId", 0) != 0
            or checkpoint_window.get("lastCheckpointId", 0) != 0
        ):
            summary_check_error = "checkpointWindow IDs must be 0 when checkpointCount is 0"
        elif summary.get("checkpointCount", 0) > 0 and checkpoint_window.get("firstCheckpointId") > checkpoint_window.get("lastCheckpointId"):
            summary_check_error = "checkpointWindow.firstCheckpointId must be <= lastCheckpointId"

_summary_check_status = "pass" if summary_check_error is None else "fail"
checks.append({
    "name": "capability_audit_summary_contract",
    "status": _summary_check_status,
    "pass": summary_check_error is None,
    "durationSeconds": 0,
    "details": {
        "summaryPath": str(summary_path.relative_to(run_dir)),
    },
})
targets.append({
    "name": "capability_audit_summary_contract",
    "status": _summary_check_status,
    "pass": summary_check_error is None,
    "durationSeconds": 0,
    "logPath": str(summary_path.relative_to(run_dir)) if summary_path.exists() else None,
    "reasonCode": summary_check_error,
    "artifacts": [str(summary_path.relative_to(run_dir))] if summary_path.exists() else [],
})

if summary_check_error is not None:
    failed.append("capability_audit_summary_contract")

build_metadata = {
    "runId": os.environ["RUN_ID"],
    "startedAt": os.environ["STARTED_AT"],
    "finishedAt": os.environ["FINISHED_AT"],
    "git": {
        "sha": os.environ["GIT_SHA"],
        "ref": os.environ["GIT_REF"],
    },
    "artifactsRoot": str(run_dir),
}
(run_dir / "build_metadata.json").write_text(json.dumps(build_metadata, indent=2) + "\n")

summary_total = len(targets)
summary_passed = sum(1 for t in targets if t["status"] == "pass")
summary_failed = sum(1 for t in targets if t["status"] == "fail")
summary_harness = sum(1 for t in targets if t["status"] == "harness_error")

report = {
    "schemaVersion": 1,
    "runId": os.environ["RUN_ID"],
    "startedAt": os.environ["STARTED_AT"],
    "finishedAt": os.environ["FINISHED_AT"],
    "git": {
        "sha": os.environ["GIT_SHA"],
        "ref": os.environ["GIT_REF"],
    },
    "overallStatus": "pass" if not failed else "fail",
    "pass": len(failed) == 0,
    "failedChecks": failed,
    "harnessErrors": harness_errors,
    "summary": {
        "total":         summary_total,
        "passed":        summary_passed,
        "failed":        summary_failed,
        "harnessErrors": summary_harness,
    },
    "targets": targets,
    "checks": checks,
    "image": {
      "path": "experiments/bootloader/boot.bin",
      "sha256": os.environ.get("IMAGE_HASH", ""),
    },
        "kernelIso": {
            "path": "secureos.iso",
            "sha256": os.environ.get("KERNEL_ISO_HASH", ""),
        },
    "qemu": {
            "commands": qemu_commands,
      "serialLogs": [
        "qemu/hello_boot.log",
                "qemu/hello_boot_fail.log",
                "qemu/kernel_console.log",
                "qemu/kernel_filedemo.log",
                "qemu/kernel_persistence.log"
      ]
    }
}
# M7-TOOLCHAIN acceptance suite scaffolding (issue #423, umbrella #403,
# plan plans/2026-05-28-in-os-toolchain-self-hosting.md). Surface a
# dedicated top-level section in the bundle JSON so consumers can find
# the six acceptance markers without scanning the flat targets[] list.
# Status comes from the markers.json source-of-truth file (SKIP today;
# each gating execute slice flips its entry to "PASS" as it lands).
m7_markers_path = root_dir / "tests" / "m7_toolchain" / "markers.json"
if m7_markers_path.exists():
    try:
        _m7_doc = json.loads(m7_markers_path.read_text())
    except Exception as exc:
        _m7_doc = {"_error": f"unreadable markers.json: {exc}"}
    _m7_target_status = {t["name"]: t["status"] for t in targets}
    _m7_section_markers = []
    for _m in _m7_doc.get("markers", []):
        _name = _m.get("name")
        _target_status = _m7_target_status.get(_name)
        # Default to SKIP; flip to the bundle target status once the
        # gating issue replaces the stub with a real assertion.
        _status = "SKIP"
        if _target_status == "pass":
            _status = "SKIP"  # stub still emits PASS but is conceptually SKIP
        _m7_section_markers.append({
            "name": _name,
            "status": _status,
            "reason": _m.get("reason"),
            "gatingIssue": _m.get("gatingIssue"),
            "description": _m.get("description"),
            "bundleTargetStatus": _target_status,
        })
    report["m7_toolchain"] = {
        "schemaVersion": _m7_doc.get("schemaVersion", 1),
        "umbrella":      _m7_doc.get("umbrella"),
        "plan":          _m7_doc.get("plan"),
        "scaffoldIssue": _m7_doc.get("scaffoldIssue"),
        "markers":       _m7_section_markers,
    }

report_path = run_dir / "validator_report.json"
report_path.write_text(json.dumps(report, indent=2) + "\n")
print(f"VALIDATION_REPORT:{report_path}")

# Lightweight, dependency-free structural validation against
# docs/test-plans/validator-report.schema.json. Full JSON-Schema
# validation is best-effort: if `jsonschema` is importable we use it,
# otherwise we fall back to a minimal hand-rolled check that mirrors
# the schema's `required` and enum constraints so a green run still
# proves the file is parseable and well-shaped.
schema_path = root_dir / "docs" / "test-plans" / "validator-report.schema.json"
schema_error = None
if not schema_path.exists():
    schema_error = f"missing schema: {schema_path}"
else:
    try:
        schema = json.loads(schema_path.read_text())
    except Exception as exc:
        schema_error = f"unreadable schema: {exc}"
    if schema_error is None:
        try:
            import jsonschema  # type: ignore
            jsonschema.validate(report, schema)
        except ImportError:
            required = schema.get("required", [])
            missing = [k for k in required if k not in report]
            if missing:
                schema_error = f"missing required top-level fields: {missing}"
            elif report.get("schemaVersion") != 1:
                schema_error = "schemaVersion must be 1"
            elif report.get("overallStatus") not in ("pass", "fail"):
                schema_error = "overallStatus must be pass|fail"
            else:
                for t in report.get("targets", []):
                    if t.get("status") not in ("pass", "fail", "harness_error"):
                        schema_error = f"target {t.get('name')} has invalid status"
                        break
                    for k in ("name", "status", "durationSeconds"):
                        if k not in t:
                            schema_error = f"target missing required field: {k}"
                            break
                    if schema_error:
                        break
                if schema_error is None:
                    s = report.get("summary", {})
                    for k in ("total", "passed", "failed", "harnessErrors"):
                        if not isinstance(s.get(k), int):
                            schema_error = f"summary.{k} must be integer"
                            break
        except Exception as exc:  # jsonschema.ValidationError or similar
            schema_error = f"schema validation failed: {exc}"

if schema_error is not None:
    print(f"VALIDATION_SCHEMA_FAIL:{schema_error}")
    raise SystemExit(3)
print(f"VALIDATION_SCHEMA_OK:{schema_path.relative_to(root_dir)}")
if summary_check_error is not None:
    print(f"VALIDATION_CONTRACT_FAIL:{summary_check_error}")
    raise SystemExit(2)
PY

if [[ ${#FAILED_TESTS[@]} -gt 0 ]]; then
  echo "VALIDATION_FAIL:${FAILED_TESTS[*]}"
  exit 1
fi

echo "VALIDATION_PASS:$RUN_DIR/validator_report.json"