# SecureOS Daily Maintenance State

## Run timestamp (UTC)
- 2026-09-05T21:13:48Z

## Open PR snapshot
- Open PR count at snapshot: **7**

- #755 — test(audit): add launcher owner-kind marker host gate (refs #554)  
  https://github.com/rwrife/SecureOS/pull/755
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
  - Draft: `false`
  - Mergeable: `CONFLICTING`
  - Merge state: `DIRTY`
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
- https://github.com/rwrife/SecureOS/issues/554

## Issues newly created this run
- _none_

## Branch / PR created for active work
- Branch: `feature/launcher-owner-kind-audit-554`
- Worktree used: `/home/rwrife/repos/secureos/.worktrees/feature/launcher-owner-kind-audit-554`
- Implementation PR created this run: https://github.com/rwrife/SecureOS/pull/755

## Blockers / notes
- No open PR was safe to merge in this run.
  - #755 blocked (for merge now) because it is draft and checks are still pending.
  - #750 blocked by merge conflict (`mergeable=CONFLICTING`, `mergeState=DIRTY`).
  - #749 blocked by merge conflict (`mergeable=CONFLICTING`, `mergeState=DIRTY`).
  - #748 blocked because it remains draft and has no reported checks yet.
  - #747 blocked by merge conflict (`mergeable=CONFLICTING`, `mergeState=DIRTY`).
  - #746 blocked by merge conflict and failing check (`build-and-validate:FAILURE`).
  - #736 blocked by merge conflict (`mergeable=CONFLICTING`, `mergeState=DIRTY`).
- Headless auth pitfall observed: `git push` initially failed with HTTP 403 when environment token override was active. Recovery was to unset `GH_TOKEN` / `GITHUB_TOKEN`, run `gh auth setup-git`, and retry push successfully.
- Selected issue #554 because it closes a direct zero-trust forensics gap (owner-kind audit discrimination) without introducing syscall/capability/schema surface changes, and it had no open PR duplicating this slice.
