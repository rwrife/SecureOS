# SecureOS Daily Maintenance State

## Run timestamp (UTC)
- 2026-08-15T21:07:15Z

## Open PR snapshot
- Open PR count at snapshot: **11**

- #754 — feat(clib): add unlink truncate shim for POSIX fd bridge  
  https://github.com/rwrife/SecureOS/pull/754
  - Draft: `true`
  - Mergeable: `MERGEABLE`
  - Merge state: `UNSTABLE`
  - Checks overall: `pending`
  - Checks detail:
    - `build-iso-vm-smoke:IN_PROGRESS`
    - `lint:IN_PROGRESS`
    - `build-and-validate:IN_PROGRESS`

- #750 — test(mem): add mem_brk arena-cap deny marker host gate (refs #558)  
  https://github.com/rwrife/SecureOS/pull/750
  - Draft: `true`
  - Mergeable: `CONFLICTING`
  - Merge state: `DIRTY`
  - Checks overall: `passing`
  - Checks detail:
    - `build-iso-vm-smoke:SUCCESS`
    - `lint:SUCCESS`
    - `build-and-validate:SUCCESS`

- #749 — docs(abi): align /apps/dev/include manifest header path with 8.3 staging (refs #613)  
  https://github.com/rwrife/SecureOS/pull/749
  - Draft: `true`
  - Mergeable: `CONFLICTING`
  - Merge state: `DIRTY`
  - Checks overall: `passing`
  - Checks detail:
    - `build-iso-vm-smoke:SUCCESS`
    - `lint:SUCCESS`
    - `build-and-validate:SUCCESS`

- #748 — feat(m6): add hello-from-sdk host gate starter (refs #584)  
  https://github.com/rwrife/SecureOS/pull/748
  - Draft: `true`
  - Mergeable: `MERGEABLE`
  - Merge state: `CLEAN`
  - Checks overall: `none reported`
  - Checks detail: _none reported_

- #747 — test(process): pin argv join-collision evidence for #724  
  https://github.com/rwrife/SecureOS/pull/747
  - Draft: `true`
  - Mergeable: `MERGEABLE`
  - Merge state: `CLEAN`
  - Checks overall: `passing`
  - Checks detail:
    - `build-iso-vm-smoke:SUCCESS`
    - `lint:SUCCESS`
    - `build-and-validate:SUCCESS`

- #746 — feat(m5): enforce ownership_role broker edges at runtime (refs #585)  
  https://github.com/rwrife/SecureOS/pull/746
  - Draft: `true`
  - Mergeable: `CONFLICTING`
  - Merge state: `DIRTY`
  - Checks overall: `failing`
  - Checks detail:
    - `build-and-validate:FAILURE`
    - `build-iso-vm-smoke:SUCCESS`
    - `lint:SUCCESS`

- #745 — feat: scaffold cc app entrypoint + manifest (refs #540)  
  https://github.com/rwrife/SecureOS/pull/745
  - Draft: `true`
  - Mergeable: `MERGEABLE`
  - Merge state: `CLEAN`
  - Checks overall: `passing`
  - Checks detail:
    - `build-iso-vm-smoke:SUCCESS`
    - `lint:SUCCESS`
    - `build-and-validate:SUCCESS`

- #744 — test(m7): scaffold cc determinism qemu gate (refs #572)  
  https://github.com/rwrife/SecureOS/pull/744
  - Draft: `true`
  - Mergeable: `MERGEABLE`
  - Merge state: `CLEAN`
  - Checks overall: `passing`
  - Checks detail:
    - `build-iso-vm-smoke:SUCCESS`
    - `lint:SUCCESS`
    - `build-and-validate:SUCCESS`

- #743 — test(audit): pin launch owner_kind marker contract (refs #554)  
  https://github.com/rwrife/SecureOS/pull/743
  - Draft: `true`
  - Mergeable: `MERGEABLE`
  - Merge state: `CLEAN`
  - Checks overall: `passing`
  - Checks detail:
    - `build-iso-vm-smoke:SUCCESS`
    - `lint:SUCCESS`
    - `build-and-validate:SUCCESS`

- #736 — test(process): add process_exit_qemu starter bridge gate (refs #551)  
  https://github.com/rwrife/SecureOS/pull/736
  - Draft: `true`
  - Mergeable: `CONFLICTING`
  - Merge state: `DIRTY`
  - Checks overall: `passing`
  - Checks detail:
    - `build-iso-vm-smoke:SUCCESS`
    - `lint:SUCCESS`
    - `build-and-validate:SUCCESS`

- #735 — test(ipc): add malformed-envelope host gate (refs #586)  
  https://github.com/rwrife/SecureOS/pull/735
  - Draft: `true`
  - Mergeable: `MERGEABLE`
  - Merge state: `CLEAN`
  - Checks overall: `passing`
  - Checks detail:
    - `build-iso-vm-smoke:SUCCESS`
    - `lint:SUCCESS`
    - `build-and-validate:SUCCESS`

## Open issue snapshot
- Open issue count at snapshot: **17**

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
- https://github.com/rwrife/SecureOS/issues/538

## Issues newly created this run
- _none_

## Branch / PR created for active work
- Branch: `feature/m7-538-unlink-shim`
- Worktree used: `/home/rwrife/repos/secureos/.worktrees/feature-m7-538-unlink-shim`
- Implementation PR created this run: https://github.com/rwrife/SecureOS/pull/754

## Blockers / notes
- All open PRs remain blocked from merge because they are draft PRs. Additional blockers:
  - #754: mergeState=UNSTABLE; pending-checks=build-iso-vm-smoke:IN_PROGRESS,lint:IN_PROGRESS,build-and-validate:IN_PROGRESS
  - #750: mergeable=CONFLICTING; mergeState=DIRTY
  - #749: mergeable=CONFLICTING; mergeState=DIRTY
  - #746: mergeable=CONFLICTING; mergeState=DIRTY; failing-checks=build-and-validate:FAILURE
  - #736: mergeable=CONFLICTING; mergeState=DIRTY
- Headless auth pitfall observed: initial `git push` returned HTTP 403 under env-token override; recovered by unsetting `GH_TOKEN`/`GITHUB_TOKEN`, running `gh auth setup-git`, and retrying push successfully.
- Selected issue #538 because it directly advances M7 toolchain viability for QEMU/x86 by removing `unlink` ENOSYS from the freestanding clib bridge used by TinyCC paths.