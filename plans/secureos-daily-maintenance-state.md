# SecureOS Daily Maintenance State

## Run timestamp (UTC)
- 2026-07-27T21:04:52Z

## Open PR snapshot
- Snapshot moment: after syncing latest `main`, before merge actions for this run.
- Open PR count at snapshot: **2**

- #736 — `test(process): add process_exit_qemu starter bridge gate (refs #551)`  
  https://github.com/rwrife/SecureOS/pull/736
  - Draft: `true`
  - Head: `feature/process-exit-qemu-551` → Base: `main`
  - Mergeable: `CONFLICTING`
  - Merge state: `DIRTY`
  - Checks:
    - `build-iso-vm-smoke`: **FAILURE** (https://github.com/rwrife/SecureOS/actions/runs/30175447916/job/89723381169)
    - `lint`: **FAILURE** (https://github.com/rwrife/SecureOS/actions/runs/30175447932/job/89723381230)
    - `build-and-validate`: **FAILURE** (https://github.com/rwrife/SecureOS/actions/runs/30175447925/job/89723381126)

- #735 — `test(ipc): add malformed-envelope host gate (refs #586)`  
  https://github.com/rwrife/SecureOS/pull/735
  - Draft: `true`
  - Head: `fix/ipc-wire-malformed-harness-586` → Base: `main`
  - Mergeable: `CONFLICTING`
  - Merge state: `DIRTY`
  - Checks:
    - `build-iso-vm-smoke`: **FAILURE** (https://github.com/rwrife/SecureOS/actions/runs/30126776621/job/89592026365)
    - `build-and-validate`: **FAILURE** (https://github.com/rwrife/SecureOS/actions/runs/30126776595/job/89592026226)
    - `lint`: **SUCCESS** (https://github.com/rwrife/SecureOS/actions/runs/30126776531/job/89592026175)

## Open issue snapshot
- Open issue count at snapshot: **20**

- #739 — ci(drift): scheduled-drift-gate failure on main — auto-updated  
  https://github.com/rwrife/SecureOS/issues/739
- #724 — follow-up: evaluate length-prefixed argv wire format for os_process_spawn  
  https://github.com/rwrife/SecureOS/issues/724
- #613 — disk-image: stage sofpack.h + manifestgen.h public headers under /apps/dev/include/ (in-OS cc link prereq, sibling of #531, refs #521 #533 #409 #540)  
  https://github.com/rwrife/SecureOS/issues/613
- #586 — test(ipc): malformed IPC frame boundary harness — pin docs/abi/ipc-wire.md error model on bad header/length/opcode (BUILD_ROADMAP §6.2, §7)  
  https://github.com/rwrife/SecureOS/issues/586
- #585 — M5-SUBSTRATE: launcher + broker_svc runtime enforcement of manifest capabilities.ownership_role (follow-up to #368, BUILD_ROADMAP §5.5)  
  https://github.com/rwrife/SecureOS/issues/585
- #584 — M6-SDK-004: third-party sample app samples/hello-from-sdk/ (execute slice 4 of plan #136, BUILD_ROADMAP §5.6)  
  https://github.com/rwrife/SecureOS/issues/584
- #577 — test(manifestgen): byte-identical golden-corpus gate for libmanifestgen output (sibling of #555 #572, refs #533 PR#535)  
  https://github.com/rwrife/SecureOS/issues/577
- #572 — test(qemu): cc determinism gate — byte-identical SOF across repeated/cross-boot compiles (refs #409 #410 #555)  
  https://github.com/rwrife/SecureOS/issues/572
- #558 — test(cap): pin os_mem_brk arena-cap CAP:DENY marker — refuse-to-grow-past-runtime.arena_bytes contract (refs #421 #424 #404)  
  https://github.com/rwrife/SecureOS/issues/558
- #554 — audit: pin owner_kind=<internal|external|local> on launch.granted/launch.denied audit records (M7/M6 zero-trust forensics, refs #522 #396 #410 #542)  
  https://github.com/rwrife/SecureOS/issues/554
- #551 — test(qemu): end-to-end os_process_exit status round-trip — sibling of mem_brk_qemu (#495), pre-#410 unblock (refs #406 #422 #546)  
  https://github.com/rwrife/SecureOS/issues/551
- #540 — M7-TOOLCHAIN-006 sub-slice: user/apps/cc driver-app skeleton + disk-staging to /apps/dev/cc (refs #409 #521 #533)  
  https://github.com/rwrife/SecureOS/issues/540
- #539 — M7-TOOLCHAIN-005 sub-slice: close remaining 11 libc-deps gaps (plain-name aliases + deterministic stubs + dl* JIT stubs, refs #408 #538)  
  https://github.com/rwrife/SecureOS/issues/539
- #538 — M7-TOOLCHAIN-005 sub-slice: clib POSIX-fd nucleus (open/close/read/lseek/unlink over os_fs_*, refs #408)  
  https://github.com/rwrife/SecureOS/issues/538
- #531 — disk-image: stage secureos_api.h under /apps/dev/include (TinyCC sysinclude default, refs #408 #409)  
  https://github.com/rwrife/SecureOS/issues/531
- #410 — M7-TOOLCHAIN-007: unsigned-run wiring + m7_toolchain acceptance suite (in-OS toolchain, refs #403)  
  https://github.com/rwrife/SecureOS/issues/410
- #409 — M7-TOOLCHAIN-006: sofpack lib + cc driver app (in-OS toolchain, refs #403)  
  https://github.com/rwrife/SecureOS/issues/409
- #408 — M7-TOOLCHAIN-005: TinyCC freestanding port (libtcc) (in-OS toolchain, refs #403)  
  https://github.com/rwrife/SecureOS/issues/408
- #403 — M7-TOOLCHAIN: in-OS toolchain — compile apps inside SecureOS (umbrella, plan in #402)  
  https://github.com/rwrife/SecureOS/issues/403
- #396 — M6-SDK-003: os-cc / os-pack / os-run tool wrappers + manifest schema additions (execute slice 3 of plan #136)  
  https://github.com/rwrife/SecureOS/issues/396

## PRs merged this run
- _none_

## Issue selected for implementation
- https://github.com/rwrife/SecureOS/issues/739

## Issues newly created this run
- _none_

## Branch / PR created for active work
- Branch: `fix/abi-manifest-stamp-739`
- Worktree used: `/home/rwrife/repos/secureos/.worktrees/fix-abi-manifest-stamp-739`
- Implementation PR: https://github.com/rwrife/SecureOS/pull/740

## Blockers / notes
- PR #736 was not merged: merge conflict (`mergeable=CONFLICTING`, `mergeStateStatus=DIRTY`) and all required checks failing (`build-iso-vm-smoke`, `lint`, `build-and-validate`).
- PR #735 was not merged: merge conflict (`mergeable=CONFLICTING`, `mergeStateStatus=DIRTY`) and required checks failing (`build-iso-vm-smoke`, `build-and-validate`).
- Initial env-token auth (`GH_TOKEN`/`GITHUB_TOKEN`) failed write preflight with `403 Resource not accessible by personal access token`; run continued using stored `gh` auth after unsetting env overrides.
- Selected issue #739 because scheduled drift gates on `main` were red due `ABI_STAMP:FAIL:docs/abi/manifest.md`; implementation PR #740 refreshes the stale ABI stamp and verifies `validate_abi_stamps` locally.
