# SecureOS Daily Maintenance State

## Run timestamp (UTC)
- 2026-07-22T21:04:46Z

## Open PR snapshot
- Open PR count at snapshot: **1**
- #731 (draft) — `test(manifestgen): add negative-input contract gate (refs #592)`  
  https://github.com/rwrife/SecureOS/pull/731
  - Head: `feature/manifestgen-negative-contract-592` → Base: `main`
  - Mergeable: `MERGEABLE`
  - Merge state: `UNSTABLE`
  - Checks:
    - `build-and-validate`: **fail** (failed step: `Full validation bundle (machine-readable)`)
    - `build-iso-vm-smoke`: pass
    - `lint`: pass

## Open issue snapshot
- Open issue count: **20**
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
- _none_

## Issue selected for implementation
- https://github.com/rwrife/SecureOS/issues/613

## Issues newly created this run
- _none_

## Branch / PR created for active work
- Branch: `feature/apps-dev-include-stage-613`
- Local commit: `e180e7b` (`feat(disk-image): stage namespaced /apps/dev/include headers`)
- PR: _not created (push permission denied)_

## Blockers / notes
- Open PR #731 was not mergeable this run due failing required check (`build-and-validate`).
- Attempted implementation for #613 completed locally in worktree:
  - `build/scripts/build_disk_image.sh`: stages
    - `user/libs/sofpack/include/sofpack/sofpack.h` → `/apps/dev/include/sofpack/sofpack.h`
    - `user/libs/manifestgen/include/manifestgen/manifest_default.h` → `/apps/dev/include/manifestgen/manifest_default.h`
  - `tests/disk_image/apps_dev_include_set.json`: #613 headers promoted to strict requirements; skip marker reduced to `SKIP:#531`.
  - `tools/disk_image_apps_dev_sha.json`: SHA pins added for the two new header mappings.
- Validation evidence:
  - `bash build/scripts/test.sh apps_dev_staging` → PASS (SKIP pending #531)
  - `bash build/scripts/test.sh apps_dev_include_set` → PASS (SKIP pending #531)
  - `bash build/scripts/test.sh apps_dev_sha` → FAIL due existing #548-gated pending source expectations (`artifacts/user/libs/*.a` absent in this local run context).
- GitHub write blocker:
  - `git push -u origin feature/apps-dev-include-stage-613` returned HTTP 403 (`Permission to rwrife/SecureOS.git denied to rwrife`), so branch push and PR creation could not be completed in this cron run.
