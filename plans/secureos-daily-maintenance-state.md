# SecureOS Daily Maintenance State

## Run timestamp (UTC)
- 2026-09-09T21:23:24Z (post-merge refresh appended at 2026-09-09T21:35Z)

## Open PR snapshot
- Snapshot moment: post-sync, pre-implementation merge sweep.
- Open PR count at snapshot: **6** — **all drafts** (draft gate: not merge candidates in unattended runs).

- #755 — test(audit): add launcher owner-kind marker host gate (refs #554)  \
  https://github.com/rwrife/SecureOS/pull/755
  - Draft: `true`; Mergeable: `CONFLICTING`; Merge state: `DIRTY`
  - Checks: `build-and-validate: FAILURE`, `build-iso-vm-smoke: SUCCESS`, `lint: SUCCESS`
  - Blocked: draft + conflicting + failing build-and-validate.

- #750 — test(mem): add mem_brk arena-cap deny marker host gate (refs #558)  \
  https://github.com/rwrife/SecureOS/pull/750
  - Draft: `true`; Mergeable: `CONFLICTING`; Merge state: `DIRTY`
  - Checks: all SUCCESS
  - Blocked: draft + conflicting.

- #749 — docs(abi): align /apps/dev/include manifest header path with 8.3 staging (refs #613)  \
  https://github.com/rwrife/SecureOS/pull/749
  - Draft: `true`; Mergeable: `CONFLICTING`; Merge state: `DIRTY`
  - Checks: all SUCCESS
  - Blocked: draft + conflicting.

- #748 — feat(m6): add hello-from-sdk host gate starter (refs #584)  \
  https://github.com/rwrife/SecureOS/pull/748
  - Draft: `true`; Mergeable: `MERGEABLE`; Merge state: `CLEAN`
  - Checks: none reported yet
  - Blocked: draft (only non-conflicting draft; checks still absent).

- #746 — feat(m5): enforce ownership_role broker edges at runtime (refs #585)  \
  https://github.com/rwrife/SecureOS/pull/746
  - Draft: `true`; Mergeable: `CONFLICTING`; Merge state: `DIRTY`
  - Checks: `build-and-validate: FAILURE`, `build-iso-vm-smoke: SUCCESS`, `lint: SUCCESS`
  - Blocked: draft + conflicting + failing build-and-validate.

- #736 — test(process): add process_exit_qemu starter bridge gate (refs #551)  \
  https://github.com/rwrife/SecureOS/pull/736
  - Draft: `true`; Mergeable: `CONFLICTING`; Merge state: `DIRTY`
  - Checks: all SUCCESS
  - Blocked: draft + conflicting.

## PRs merged this run
- **PR #761 — feat(clib): reserve fds 0/1/2 as console descriptors (refs #538)**  \
  https://github.com/rwrife/SecureOS/pull/761
  - Created and squash-merged this run at 2026-09-09T21:28:54Z after all
    checks green (`lint`, `build-and-validate`, `build-iso-vm-smoke` all
    SUCCESS, `mergeStateStatus: CLEAN`). Squash commit `3bf5617f84`.
    Remote + local branch deleted and worktree removed.
- No pre-existing open PRs were merged (every open PR is a draft; per policy
  drafts are not merged or auto-merged in unattended runs).

## Open issue snapshot
- Open issue count at snapshot: **17**

- #396 [documentation,enhancement] — M6-SDK-003: os-cc / os-pack / os-run tool wrappers  \
  https://github.com/rwrife/SecureOS/issues/396
- #403 [enhancement] — M7-TOOLCHAIN umbrella  \
  https://github.com/rwrife/SecureOS/issues/403
- #408 [enhancement] — M7-TOOLCHAIN-005: TinyCC freestanding port  \
  https://github.com/rwrife/SecureOS/issues/408
- #409 [enhancement] — M7-TOOLCHAIN-006: sofpack lib + cc driver app  \
  https://github.com/rwrife/SecureOS/issues/409
- #410 [enhancement] — M7-TOOLCHAIN-007: unsigned-run wiring + acceptance suite  \
  https://github.com/rwrife/SecureOS/issues/410
- #531 [documentation,enhancement] — stage secureos_api.h under /apps/dev/include  \
  https://github.com/rwrife/SecureOS/issues/531
- #538 [enhancement] — clib POSIX-fd nucleus (open/close/read/lseek/unlink)  \
  https://github.com/rwrife/SecureOS/issues/538
- #540 [enhancement] — cc driver-app skeleton + disk staging  \
  https://github.com/rwrife/SecureOS/issues/540
- #551 [enhancement] — os_process_exit status round-trip (draft PR #736)  \
  https://github.com/rwrife/SecureOS/issues/551
- #554 [documentation,enhancement] — owner_kind audit markers (draft PR #755)  \
  https://github.com/rwrife/SecureOS/issues/554
- #558 [documentation,enhancement] — mem_brk arena-cap CAP:DENY marker (draft PR #750)  \
  https://github.com/rwrife/SecureOS/issues/558
- #572 [enhancement] — cc determinism gate (starter merged in #744)  \
  https://github.com/rwrife/SecureOS/issues/572
- #584 [documentation,enhancement] — hello-from-sdk sample app (draft PR #748)  \
  https://github.com/rwrife/SecureOS/issues/584
- #585 [enhancement] — ownership_role runtime enforcement (draft PR #746)  \
  https://github.com/rwrife/SecureOS/issues/585
- #586 [documentation,enhancement] — malformed IPC frame harness (gate merged in #735; issue left open intentionally — v0 wire has no byte-stream framing/opcode surface; see issue body scope notes)  \
  https://github.com/rwrife/SecureOS/issues/586
- #613 [documentation,enhancement] — stage sofpack.h + manifestgen.h headers (draft PR #749)  \
  https://github.com/rwrife/SecureOS/issues/613
- #724 [] — length-prefixed argv wire format evaluation (explicitly deferred until #410 runtime signal)  \
  https://github.com/rwrife/SecureOS/issues/724

## Issue selected for implementation
- **#538 — M7-TOOLCHAIN-005 sub-slice: clib POSIX-fd nucleus (slice 3: fd 0/1/2 console descriptors)**  \
  https://github.com/rwrife/SecureOS/issues/538
- Rationale: slice 2 (write modes + `write()`) merged via #759 yesterday, and
  the issue's remaining tracked follow-ups list fd 0/1/2 console forwarding as
  the next open acceptance item. TinyCC's error/diagnostic paths and any
  toolchain app that links libclib expect `write(1, ...)` / `write(2, ...)` to
  reach the console — the kernel owns the console per AGENTS.md, so routing
  these reserved fds through the existing `os_console_write` syscall (no new
  ABI opcode, no new capability) is the consent-preserving way to complete the
  fd nucleus. Small, self-contained, gate-backed slice.
- Scope landed this run (slice 3, merged via #761):
  - fds 0/1/2 are reserved console descriptors:
    - `write(1|2)` forwards to `os_console_write()` in NUL-bounded chunks
      (embedded NULs split chunks; no empty console calls; `EIO` on syscall
      failure; stdout/stderr share the single console sink).
    - `read(0)` deterministically fails `ENOTSUP` (v0 bridge has no
      console-read syscall; silent EOF would let read loops exit with bogus
      success). `read(1|2)` fails `EBADF`.
    - `lseek` on console fds fails `ESPIPE`.
    - `close(0|1|2)` succeeds as a no-op; descriptors stay usable.
    - `open()` refuses `/dev/stdin|stdout|stderr` aliases with `EBUSY` so no
      file snapshot can alias the console fds.
  - Host gate `clib_posix_fd` extended with an `os_console_write` recorder
    fixture (12 new markers); `build/scripts/test_clib_posix_fd.sh` greps
    updated.
  - `docs/abi/clib-symbols.md` posix_fd section updated + stamp bumped
    (two-commit pattern: content commit, then stamp-only commit at it).
  - No new symbols, no new ABI opcodes, no new capabilities.

## Issues newly created this run
- None. The gap addressed today (fd 0/1/2 forwarding) was already tracked as
  an explicit follow-up inside #538; creating a duplicate issue would violate
  the no-duplicates rule. Remaining open gaps (draft-PR-backed #551/#554/#558/
  #584/#585/#613, blocked #531, deferred #724, umbrella/phase issues
  #396/#403/#408/#409/#410/#540, blocked-on-wire-format #586) all already have
  issues.

## Branch / PR created for active work
- Branch: `feature/clib-fd-console-538` — squash-merged via #761 and cleaned
  up (worktree removed, branches deleted).
- PR: **https://github.com/rwrife/SecureOS/pull/761** — merged this run.
- Post-merge cleanup branch: `chore/abi-stamp-repair-2026-09-09` carrying:
  1. the expected squash-merge stamp repair — `docs/abi/clib-symbols.md`
     `Last verified against commit` repointed from the dangling pre-merge SHA
     `464975ad27` to the on-main squash SHA `3bf5617f84` (known pattern, see
     references/abi-stamp-repair-playbook.md); and
  2. this post-merge state snapshot.

## Local verification (ad-hoc evidence for this run)
- `./build/scripts/test.sh clib_posix_fd` → PASS (incl. 12 new console-fd
  markers; compile with `-Wall -Wextra -Werror -fno-builtin` clean)
- `./build/scripts/test.sh clib_symbol_drift` → PASS (pin/doc/libclib agree;
  no new symbols this slice)
- `./build/scripts/test.sh tinycc_libc_deps` → PASS (submodule-deep checks
  SKIP: submodule not initialized on this runner — same behavior as CI
  scaffold)
- `./build/scripts/test.sh clib_tinycc_link_surface` → PASS (link surface
  unaffected)
- `./build/scripts/test.sh clib_stdio` → PASS (stdio TU coexists with the
  test-local os_console_write recorder fixture)
- `./build/scripts/test.sh validate_abi_stamps` → PASS after stamp repair
  (`clib-symbols.md:3bf5617f84`)
- CI `build-and-validate` on #761: SUCCESS (authoritative full-suite result).

## Blockers / notes
- All 6 pre-existing open PRs are drafts, and 5 are additionally CONFLICTING
  vs main (#755, #750, #749, #746, #736). They need rebase + ready-for-review
  transitions by a human before any merge action is possible; cron will not
  force-merge.
- #755 and #746 additionally show `build-and-validate: FAILURE` — likely stale
  branch bases given the conflicts; re-test after rebase.
- #748 is the only clean-base draft but reports no checks yet; still draft-gated.
- Squash-merge stamp-dangling pattern hit and repaired as expected: #761
  squash rewrote the stamped SHA `464975ad27` (unknown to main), so this
  cleanup PR repoints `docs/abi/clib-symbols.md` to squash SHA `3bf5617f84`.
  If this cleanup PR itself squash-merges without further content edits to
  the doc, the stamp stays valid (stamp-only edits are not content-changing
  per the validator).
- #586 note: merged gate #735 pins the v0 malformed-envelope cases; the
  issue's byte-stream framing subcases (truncated header/unknown opcode) are
  not representable on today's typed-envelope ABI — do not re-implement until
  the wire format gains byte framing.
- Remaining #538 follow-up candidates after this slice: byte-length write ABI
  for binary payloads (needed for ELF emission through the fd layer),
  dedicated delete syscall to replace the truncate-to-empty unlink shim, and
  a console-read syscall to replace the `ENOTSUP` stdin stub.
