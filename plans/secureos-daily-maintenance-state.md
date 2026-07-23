# SecureOS Daily Maintenance State

## Run timestamp (UTC)
- 2026-07-23T21:06:55Z

## Open PR snapshot
- Snapshot moment: pre-maintenance triage on latest `main`
- Open PR count at snapshot: **1**
- #731 — `test(manifestgen): add negative-input contract gate (refs #592)`  
  https://github.com/rwrife/SecureOS/pull/731
  - Draft at snapshot: `true`
  - Head: `feature/manifestgen-negative-contract-592` → Base: `main`
  - Mergeable: `MERGEABLE`
  - Merge state: `UNSTABLE`
  - Checks at snapshot:
    - `build-and-validate`: **failure** (`VALIDATION_FAIL:validate_abi_stamps`)
    - `build-iso-vm-smoke`: success
    - `lint`: success

## Open issue snapshot
- Open issue count at snapshot: **20**
- #724 — follow-up: evaluate length-prefixed argv wire format for os_process_spawn  
  https://github.com/rwrife/SecureOS/issues/724
- #613 — disk-image: stage sofpack.h + manifestgen.h public headers under /apps/dev/include/ (in-OS cc link prereq, sibling of #531, refs #521 #533 #409 #540)  
  https://github.com/rwrife/SecureOS/issues/613
- #592 — test(manifestgen): negative-input rejection contract — pin libmanifestgen error enum on bad owner.kind / arena_bytes / caps_required (sibling of #577 #588, refs #533 PR#535)  
  https://github.com/rwrife/SecureOS/issues/592
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
- https://github.com/rwrife/SecureOS/pull/731

## Issue selected for implementation
- https://github.com/rwrife/SecureOS/issues/592

## Issues newly created this run
- _none_

## Branch / PR created for active work
- Branch: `feature/manifestgen-negative-contract-592`
- Commit pushed this run: `e526af8` (`docs(abi): refresh manifest/audit marker verification stamps`)
- PR updated + merged: https://github.com/rwrife/SecureOS/pull/731
- Merge commit on `main`: `e884cadd29c186d86d62e51475a4fbeb82622224`

## Blockers / notes
- Initial blocker on open PR #731 was `VALIDATION_FAIL:validate_abi_stamps`.
- Root cause from CI logs: stale `Last verified against commit` lines in `docs/abi/manifest.md` and `docs/abi/audit-markers.md`.
- Resolution this run:
  - Updated stamp lines to their last content commits.
  - Verified locally with:
    - `bash build/scripts/test.sh validate_abi_stamps`
    - `bash build/scripts/test.sh manifestgen_negative`
    - `bash build/scripts/test.sh manifestgen_audit_marker_format`
  - Pushed commit to PR branch, marked PR ready, updated PR body, enabled merge flow; PR merged and closed issue #592.
- `gh pr edit` hit Projects classic GraphQL deprecation; used REST fallback (`gh api -X PATCH repos/rwrife/SecureOS/pulls/731`) per workflow guidance.
- End-of-run snapshot: open PRs **0**, open issues **19**.
