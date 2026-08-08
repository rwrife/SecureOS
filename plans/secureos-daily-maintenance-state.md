# SecureOS Daily Maintenance State

## Run timestamp (UTC)
- 2026-08-08T21:04:14Z

## Open PR snapshot
- Snapshot moment: after syncing `main`, PR sweep, and implementation PR update push.
- Open PR count at snapshot: **9**

- #735 — `test(ipc): add malformed-envelope host gate (refs #586)`  
  https://github.com/rwrife/SecureOS/pull/735
  - Draft: `true`
  - Head: `fix/ipc-wire-malformed-harness-586` → Base: `main`
  - Mergeable: `MERGEABLE`
  - Merge state: `CLEAN`
  - Checks: 3 green, 0 pending, 0 failing
  - Sweep result: blocked: draft PR

- #736 — `test(process): add process_exit_qemu starter bridge gate (refs #551)`  
  https://github.com/rwrife/SecureOS/pull/736
  - Draft: `true`
  - Head: `feature/process-exit-qemu-551` → Base: `main`
  - Mergeable: `MERGEABLE`
  - Merge state: `CLEAN`
  - Checks: 3 green, 0 pending, 0 failing
  - Sweep result: blocked: draft PR

- #743 — `test(audit): pin launch owner_kind marker contract (refs #554)`  
  https://github.com/rwrife/SecureOS/pull/743
  - Draft: `true`
  - Head: `fix/launch-owner-kind-audit-554` → Base: `main`
  - Mergeable: `MERGEABLE`
  - Merge state: `CLEAN`
  - Checks: 3 green, 0 pending, 0 failing
  - Sweep result: blocked: draft PR

- #744 — `test(m7): scaffold cc determinism qemu gate (refs #572)`  
  https://github.com/rwrife/SecureOS/pull/744
  - Draft: `true`
  - Head: `feature/cc-determinism-qemu-572` → Base: `main`
  - Mergeable: `MERGEABLE`
  - Merge state: `CLEAN`
  - Checks: 3 green, 0 pending, 0 failing
  - Sweep result: blocked: draft PR

- #745 — `feat: scaffold cc app entrypoint + manifest (refs #540)`  
  https://github.com/rwrife/SecureOS/pull/745
  - Draft: `true`
  - Head: `feature/cc-scaffold-540` → Base: `main`
  - Mergeable: `MERGEABLE`
  - Merge state: `CLEAN`
  - Checks: 3 green, 0 pending, 0 failing
  - Sweep result: blocked: draft PR

- #746 — `feat(m5): enforce ownership_role broker edges at runtime (refs #585)`  
  https://github.com/rwrife/SecureOS/pull/746
  - Draft: `true`
  - Head: `feature/m5-ownership-role-scaffold-585` → Base: `main`
  - Mergeable: `MERGEABLE`
  - Merge state: `UNSTABLE`
  - Checks: 2 green, 0 pending, 1 failing
    - Failing: build-and-validate(FAILURE)
  - Sweep result: blocked: draft PR; failing checks: build-and-validate(FAILURE)

- #747 — `test(process): pin argv join-collision evidence for #724`  
  https://github.com/rwrife/SecureOS/pull/747
  - Draft: `true`
  - Head: `feature/process-spawn-argv-eval-724` → Base: `main`
  - Mergeable: `MERGEABLE`
  - Merge state: `CLEAN`
  - Checks: 3 green, 0 pending, 0 failing
  - Sweep result: blocked: draft PR

- #748 — `feat(m6): add hello-from-sdk host gate starter (refs #584)`  
  https://github.com/rwrife/SecureOS/pull/748
  - Draft: `true`
  - Head: `feature/m6-sample-sdk-build-gate-584` → Base: `main`
  - Mergeable: `MERGEABLE`
  - Merge state: `CLEAN`
  - Checks: no reported checks
  - Sweep result: blocked: draft PR

- #749 — `docs(abi): align /apps/dev/include manifest header path with 8.3 staging (refs #613)`  
  https://github.com/rwrife/SecureOS/pull/749
  - Draft: `true`
  - Head: `docs/apps-dev-layout-613-alias` → Base: `main`
  - Mergeable: `MERGEABLE`
  - Merge state: `UNSTABLE`
  - Checks: 0 green, 3 pending, 0 failing
    - Pending: build-iso-vm-smoke, lint, build-and-validate
  - Sweep result: blocked: draft PR; pending checks: build-iso-vm-smoke, lint, build-and-validate

## Open issue snapshot
- Open issue count at snapshot: **18**

- #396 — M6-SDK-003: os-cc / os-pack / os-run tool wrappers + manifest schema additions (execute slice 3 of plan #136)  
  https://github.com/rwrife/SecureOS/issues/396
- #403 — M7-TOOLCHAIN: in-OS toolchain — compile apps inside SecureOS (umbrella, plan in #402)  
  https://github.com/rwrife/SecureOS/issues/403
- #408 — M7-TOOLCHAIN-005: TinyCC freestanding port (libtcc) (in-OS toolchain, refs #403)  
  https://github.com/rwrife/SecureOS/issues/408
- #409 — M7-TOOLCHAIN-006: sofpack lib + cc driver app (in-OS toolchain, refs #403)  
  https://github.com/rwrife/SecureOS/issues/409
- #410 — M7-TOOLCHAIN-007: unsigned-run wiring + m7_toolchain acceptance suite (in-OS toolchain, refs #403)  
  https://github.com/rwrife/SecureOS/issues/410
- #531 — disk-image: stage secureos_api.h under /apps/dev/include (TinyCC sysinclude default, refs #408 #409)  
  https://github.com/rwrife/SecureOS/issues/531
- #538 — M7-TOOLCHAIN-005 sub-slice: clib POSIX-fd nucleus (open/close/read/lseek/unlink over os_fs_*, refs #408)  
  https://github.com/rwrife/SecureOS/issues/538
- #539 — M7-TOOLCHAIN-005 sub-slice: close remaining 11 libc-deps gaps (plain-name aliases + deterministic stubs + dl* JIT stubs, refs #408 #538)  
  https://github.com/rwrife/SecureOS/issues/539
- #540 — M7-TOOLCHAIN-006 sub-slice: user/apps/cc driver-app skeleton + disk-staging to /apps/dev/cc (refs #409 #521 #533)  
  https://github.com/rwrife/SecureOS/issues/540
- #551 — test(qemu): end-to-end os_process_exit status round-trip — sibling of mem_brk_qemu (#495), pre-#410 unblock (refs #406 #422 #546)  
  https://github.com/rwrife/SecureOS/issues/551
- #554 — audit: pin owner_kind=<internal|external|local> on launch.granted/launch.denied audit records (M7/M6 zero-trust forensics, refs #522 #396 #410 #542)  
  https://github.com/rwrife/SecureOS/issues/554
- #558 — test(cap): pin os_mem_brk arena-cap CAP:DENY marker — refuse-to-grow-past-runtime.arena_bytes contract (refs #421 #424 #404)  
  https://github.com/rwrife/SecureOS/issues/558
- #572 — test(qemu): cc determinism gate — byte-identical SOF across repeated/cross-boot compiles (refs #409 #410 #555)  
  https://github.com/rwrife/SecureOS/issues/572
- #584 — M6-SDK-004: third-party sample app samples/hello-from-sdk/ (execute slice 4 of plan #136, BUILD_ROADMAP §5.6)  
  https://github.com/rwrife/SecureOS/issues/584
- #585 — M5-SUBSTRATE: launcher + broker_svc runtime enforcement of manifest capabilities.ownership_role (follow-up to #368, BUILD_ROADMAP §5.5)  
  https://github.com/rwrife/SecureOS/issues/585
- #586 — test(ipc): malformed IPC frame boundary harness — pin docs/abi/ipc-wire.md error model on bad header/length/opcode (BUILD_ROADMAP §6.2, §7)  
  https://github.com/rwrife/SecureOS/issues/586
- #613 — disk-image: stage sofpack.h + manifestgen.h public headers under /apps/dev/include/ (in-OS cc link prereq, sibling of #531, refs #521 #533 #409 #540)  
  https://github.com/rwrife/SecureOS/issues/613
- #724 — follow-up: evaluate length-prefixed argv wire format for os_process_spawn  
  https://github.com/rwrife/SecureOS/issues/724

## PRs merged this run
- _none_

## Issue selected for implementation
- #613 — https://github.com/rwrife/SecureOS/issues/613

## Issues newly created this run
- _none_

## Branch / PR created for active work
- Branch: `docs/apps-dev-layout-613-alias` (existing draft branch updated this run)
- Worktree used: `/home/rwrife/repos/secureos/.worktrees/docs/apps-dev-layout-613-alias`
- Implementation PR (draft, updated): https://github.com/rwrife/SecureOS/pull/749

## Blockers / notes
- All open PRs are still drafts, so merge/auto-merge remained blocked by draft policy in unattended sweep.
- PR #746 currently has failing `build-and-validate` and pending `build-iso-vm-smoke` checks.
- PR #749 failed `validate_abi_stamps` in prior run; this run updated `docs/abi/apps-dev-layout.md` Last-verified stamp and validated locally via `./build/scripts/test.sh validate_abi_stamps` (PASS).
- Focused capability/gap review covered `BUILD_ROADMAP.md` + `plans/2026-05-28-in-os-toolchain-self-hosting.md` + `tests/disk_image/apps_dev_include_set.json`; selected #613 because `/apps/dev/include` layout is a prerequisite for in-OS `cc` driver viability in the M7 toolchain path.
