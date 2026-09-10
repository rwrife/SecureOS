# SecureOS Daily Maintenance State

## Run timestamp (UTC)
- 2026-09-10T23:55Z (closeout run; post-merge snapshot)

## Open PR snapshot
- Snapshot moment: post-sync, pre-merge sweep.
- Open PR count at snapshot: **6** — **all drafts**, 5 additionally
  CONFLICTING vs main. All six were automation-authored draft gate/PR
  slices owned by rwrife (single-owner repo). Per the closeout mission
  (PR-first drain), they were rebased via merge commits onto live main,
  conflicts resolved (mechanical: `plans/secureos-daily-maintenance-state.md`
  snapshots + `build/scripts/test.sh` usage-line/dispatch-case hunks),
  local gates re-run green, marked ready-for-review, and squash-merged
  as CI went green.

## PRs merged this run
- **#749 — docs(abi): align /apps/dev/include manifest header path with
  8.3 staging (refs #613)** — squash `a0e0567b89`. All checks green.
- **#736 — test(process): add process_exit_qemu starter bridge gate
  (refs #551)** — squash `2631b53b8c`. All checks green.
- **#750 — test(mem): add mem_brk arena-cap deny marker host gate
  (refs #558)** — squash `cae74389b0`. All checks green.
- **#746 — feat(m5): enforce ownership_role broker edges at runtime
  (refs #585)** — squash `d551e8bc2d`. All checks green (incl. ISO
  VM smoke with the new launcher runtime semantics).
- **#748 — feat(m6): add hello-from-sdk host gate starter (refs #584)** —
  squash `07edbedcef`. Required an in-scope fix during this run: the
  PR's `include_line_has` helper used an unbounded `strstr` from each
  line start, so a prose comment in `samples/hello-from-sdk/main.c`
  mentioning `user/include/secureos_api.h` falsely tripped the
  `non_sdk_header_reference_found` deny check (the gate had never
  seen a green CI run — no checks were reported at creation time).
  Fixed by copying each line into a bounded buffer before probing.
- **#755 — test(audit): add launcher owner-kind marker host gate
  (refs #554)** — squash `e026af0be6`. Required an in-scope fix during
  this run: the PR added `docs/abi/launch-audit-contract.md` without a
  link in `docs/abi/README.md`, tripping `validate_abi_index`; added
  the index entry (+ stamp bump).
- **#763 — fix(abi): repoint apps-dev-layout stamp to on-main squash
  SHA (#749)** — squash `bbecf009f0`. Created and merged this run to
  unblock #750/#755/#746 CI, all of which failed ONLY on the known
  post-squash dangling-stamp pattern in `docs/abi/apps-dev-layout.md`.

## Post-merge stamp repair (this branch)
- #755's squash (`e026af0be6`) rewrote pre-merge SHAs stamped in four
  ABI docs, leaving `validate_abi_stamps` red on main:
  `README.md`, `audit-markers.md`, `launch-audit-contract.md`,
  `manifest.md`. This branch repoints all four `Last verified against
  commit:` lines to the on-main squash SHA `e026af0be6` (known pattern,
  same repair as #758/#762/#763; stamp-only edits are not
  content-changing per the #297 validator).
- `validate_abi_index` PASS (14 docs linked) on this branch.

## Open issue snapshot
- Open issue count after this run: **12** (was 17; 5 closed).

## Issues closed this run
- **#554** — audit: owner_kind launch markers — completed by #755
  (contract doc + 4-sub-marker host gate + status table + index link).
- **#558** — test(cap): mem_brk arena-cap CAP:DENY marker — completed
  by #750 (deny hook + canonical marker + host gate, green + wired).
- **#584** — M6-SDK-004 hello-from-sdk sample gate — completed by #748
  (sample build gate + SKIP-pinned QEMU peer per the issue's own
  SKIP-discipline allowance while #396 wrappers are pending).
- **#585** — M5-SUBSTRATE ownership_role runtime enforcement — completed
  by #746 (launcher edge registration for owner/delegate/none,
  host + QEMU cascade gates green, bundle-wired, no ABI bump).
- **#613** — disk-image: stage sofpack.h + manifestgen.h headers —
  completed by #749 + earlier #733/#737 (staging live in
  `build_disk_image.sh`, ABI doc aligned, canonical set pinned by the
  `apps_dev_include_set` gate in `TEST_TARGETS`).

## Issues intentionally left open despite merged progress
- **#551** — #736 merged an explicitly-starter bridge-level gate; the
  issue's remaining scope is the full deterministic-QEMU boot round-trip
  asserting `0x42` through `g_native_exit_status` on a live kernel.
  Noted on the issue.

## Remaining open issues (12)
- #724 — argv wire-format evaluation (deferred until #410 runtime signal)
- #586 — malformed IPC frame harness (framing subcases unrepresentable
  on v0 typed-envelope ABI; left open intentionally)
- #572 — cc determinism gate (starter merged in #744; full cross-boot
  pinning pending)
- #551 — os_process_exit round-trip (starter gate merged in #736;
  full QEMU boot round-trip pending)
- #540 / #538 / #531 / #410 / #409 / #408 / #403 / #396 — M6/M7
  toolchain + SDK dependency chain (umbrella #403 → #396 wrappers →
  #408 TinyCC port → #409 sofpack/cc → #540 driver app → #410
  unsigned-run wiring + acceptance suite)

## Notes
- No issues/labels/milestones created this run.
- Branches + worktrees for all merged PRs deleted (remote and local).
