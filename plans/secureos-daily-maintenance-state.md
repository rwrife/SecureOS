# SecureOS Daily Maintenance State

## Run timestamp (UTC)
- 2026-09-08T21:32:19Z

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
- None (every open PR is a draft; per policy drafts are not merged or auto-merged in unattended runs).

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
- **#538 — M7-TOOLCHAIN-005 sub-slice: clib POSIX-fd nucleus**  \
  https://github.com/rwrife/SecureOS/issues/538
- Rationale: the slice-1 nucleus landed via #751/#754 but its acceptance
  criteria remain open: write/create fd modes returned `ENOTSUP`, `write()`
  was absent, and the fd 0/1/2 story was unstarted. TinyCC's ELF writer
  (`tccelf.c`) needs `open(O_WRONLY|O_CREAT|O_TRUNC)` + `write()` before any
  in-OS compile flow can emit binaries, so this is the critical-path slice
  for the M7 toolchain (and thus the consent-gated compile flow). It also
  directly closes the drift between the ABI docs (`clib-symbols.md` claimed
  `unlink` returns ENOSYS, which was already stale) and the shipped code.
- Scope landed this run (slice 2):
  - `open()` accepts `O_WRONLY`/`O_RDWR`; honors `O_CREAT` (lazy create via
    write-back), `O_TRUNC`, `O_APPEND`; mode vararg consumed for shape
    compatibility.
  - New `write()` symbol with `EBADF`/`EFAULT`/`ENOSPC` mapping and
    append-seek semantics; dirty snapshots flush via `os_fs_write_file`
    on `close()`.
  - Documented text-payload limitation (v0 fs bridge marshals C strings;
    embedded NULs truncate on flush).
  - Host gate `clib_posix_fd` extended (14 new markers); fixture models
    create-if-absent.
  - `tests/data/clib_symbols.expected` + `docs/abi/clib-symbols.md` pin
    `write`; `vendor/tinycc/libc-deps.json` `open` note refreshed; ABI
    stamp bumped.

## Issues newly created this run
- None. All identified gaps already have open issues (#538 selected covers
  the freshest gap; #586's remaining byte-framing scope is blocked on a
  future wire-format change and is tracked in the issue itself).

## Branch / PR created for active work
- Branch: `feature/clib-posix-fd-write-modes-538`
- Worktree: `.worktrees/feature/clib-posix-fd-write-modes-538`
- PR: (created by this run — see run report)

## Local verification (ad-hoc evidence for this run)
- `./build/scripts/test.sh clib_posix_fd` → PASS (incl. 14 new write-mode markers)
- `./build/scripts/test.sh clib_symbol_drift` → PASS (pin/doc/libclib agree with `write` added)
- `./build/scripts/test.sh tinycc_libc_deps` → PASS (submodule-deep checks SKIP: submodule not initialized on this runner — same behavior as CI scaffold)
- `./build/scripts/test.sh clib_tinycc_link_surface` → PASS (link surface unaffected)
- `./build/scripts/test.sh validate_abi_stamps` → PASS (stamp points at content commit)
- CI `build-and-validate` is the authoritative full-suite result.

## Blockers / notes
- All 6 open PRs are drafts, and 5 of them are additionally CONFLICTING vs
  main (#755, #750, #749, #746, #736). They need rebase + ready-for-review
  transitions by a human before any merge action is possible; cron will not
  force-merge.
- #755 and #746 additionally show `build-and-validate: FAILURE` — likely
  stale branch bases given the conflicts; re-test after rebase.
- #748 is the only clean-base draft but reports no checks yet; still draft-gated.
- Env note: `GH_TOKEN`/`GITHUB_TOKEN` in `~/.hermes/.env` returned 401 this
  run; the stored `gh` keychain token works (env overrides unset).
- #586 note: merged gate #735 pins the v0 malformed-envelope cases; the
  issue's byte-stream framing subcases (truncated header/unknown opcode) are
  not representable on today's typed-envelope ABI — do not re-implement
  until the wire format gains byte framing.
- Remaining #538 follow-up candidates (tracked in the issue): fd 0/1/2
  console forwarding, byte-length write ABI for binary payloads (needed for
  ELF emission), dedicated delete syscall to replace the unlink shim.
