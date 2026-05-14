# 2026-05-14 M3 Filesystem Service — Deny-Path + Ephemeral-Reset Acceptance Tests

Tracking issue: #108. Sibling pattern: #92 (M2 HelloApp deny-path), #115 / PR
#127 (M4 broker allow/deny/revoke). Primary slice it asserts against: #83
(PR #88, `plans/2026-04-16-filesystem-service-faux-fs.md`).

## Goal

Carve the three deterministic acceptance tests `BUILD_ROADMAP.md` §5.3 calls
out for M3 into a small, scoped validator slice that can be wired into
`build/scripts/test.sh` + `build/scripts/validate_bundle.sh` independently of
the filesystem-service implementation PR. Once #88 lands, this slice is the
contract that proves M3's exit criteria — not the unit-shaped tests inside
#88 itself, which exercise `fs_service_*` directly and live in
`tests/fs_service_test.c`.

The roadmap text being asserted:

> - editor with denied persistent FS still saves in ephemeral scope
> - data disappears after app exit/relaunch

## Scope

In:

- Three new validator targets with structured `TEST:PASS:<name>` /
  `TEST:FAIL:<name>:<reason>` markers consumable by the validator JSON
  report (#110 / PR #112):
  - `fs_service_persist_allow`
  - `fs_service_persist_deny`
  - `fs_service_ephemeral_reset`
- A minimal "launcher + app" actor pair simulated through the existing
  `fs_service_*` API plus the `cap_table_*` gates (no new app binary
  required beyond what #88 introduces, no kernel changes).
- Audit-event assertion on the deny path, gated on whether #84's audit ring +
  #98's serial line are merged at execution time (see "Layering" below).

Out:

- New filesystem semantics (quotas, sub-mounts, cross-app shares).
- Real on-disk persistence beyond the ramfs/faux backends #88 lands.
- Launcher manifest loader changes (Phase 3 of the FS plan, deferred per
  #88's follow-ups). The slice consumes whatever grant surface #88 exposes;
  if that surface is just "set the cap directly in the harness" at merge
  time, that is fine — the contract under test is the deny/ephemeral
  behavior, not the manifest loader.
- Mirroring into `test.ps1`; do that opportunistically when the Windows
  harness is next touched.

## Layering against in-flight work

`fs_service` from PR #88 is expected to expose:

- a persistent ramfs-backed namespace gated on `CAP_FS_PERSIST` (or the
  exact cap id #88 settles on — this plan does not pin the spelling)
- a faux ephemeral namespace whose lifecycle is "reset on app exit /
  relaunch"
- explicit fail-closed behavior when the persist cap is missing

This plan does not extend that API. If execution finds a real gap (e.g. a
required `fs_service_session_reset(app_id)` helper so the test can simulate
"app exit/relaunch" without spawning a real process), file a follow-up issue
on the FS service rather than widening this slice. Same discipline as #92 /
#115.

## Test design

Each test runs against a freshly initialized fs_service + cap-table
(`fs_service_reset` if exposed by #88, otherwise re-`fs_service_init` per the
existing `tests/fs_service_test.c` pattern; `cap_table_reset`). Two subject
ids: `APP_ALLOWED` (granted `CAP_FS_PERSIST`) and `APP_DENIED` (no persist
cap, but holds whatever ambient "may invoke fs_service" cap the launcher
slice settled on, if any).

### `fs_service_persist_allow`

`TEST:PASS:` markers, in order:

1. `fs_service_persist_allow:cap_present` — `cap_table_check(APP_ALLOWED,
   CAP_FS_PERSIST, "/persist")` returns `CAP_OK`.
2. `fs_service_persist_allow:write_succeeds` — open + write of a known blob
   under `/persist/<app>/note.txt` returns success and reports the byte
   count actually written.
3. `fs_service_persist_allow:read_back_after_close` — close, reopen, read the
   same path; bytes match exactly.
4. `fs_service_persist_allow:relaunch_round_trip` — simulate app exit /
   relaunch (per the helper #88 lands or, failing that, by tearing down only
   the per-app session state and leaving the persistent backend up); the
   bytes still read back exactly. This is what makes the "persist" label
   meaningful.

### `fs_service_persist_deny`

`TEST:PASS:` markers, in order:

1. `fs_service_persist_deny:cap_absent` —
   `cap_table_check(APP_DENIED, CAP_FS_PERSIST, "/persist")` returns
   `CAP_ERR_MISSING`.
2. `fs_service_persist_deny:persistent_write_denied_or_redirected` — the
   write either:
   - fails closed with a deny-shaped error code (no bytes claimed written
     to `/persist`), **or**
   - lands in the faux/ephemeral scope (the editor-saves-in-ephemeral
     branch the roadmap explicitly allows).
   The test asserts exactly one of these two outcomes and records which one
   in the marker payload (`TEST:PASS:fs_service_persist_deny:fail_closed`
   vs `TEST:PASS:fs_service_persist_deny:redirected_to_ephemeral`). It is a
   `TEST:FAIL:` if a write is reported as having reached `/persist`.
3. `fs_service_persist_deny:no_persist_visibility` — `APP_ALLOWED` opens
   `/persist/<app>/note.txt` (the path `APP_DENIED` "tried" to write); it
   does not exist (or holds only data `APP_ALLOWED` itself wrote earlier in
   the test, never `APP_DENIED`'s blob).
4. `fs_service_persist_deny:audit_deny_recorded` (gated on #98 landing): an
   audit-ring tail entry shows `op=FS_PERSIST_WRITE actor=APP_DENIED
   resource=/persist/... outcome=DENIED`. If #98 is not yet on `main` at
   execution time, emit
   `TEST:SKIP:fs_service_persist_deny:audit_deny_recorded:audit_log_unwired`
   and continue — the rest of the test still gates the slice.

### `fs_service_ephemeral_reset`

`TEST:PASS:` markers, in order:

1. `fs_service_ephemeral_reset:write_to_faux_succeeds` — `APP_DENIED` (no
   persist cap) writes a known blob to the faux/ephemeral scope; write
   reports success, immediate read-back returns the same bytes.
2. `fs_service_ephemeral_reset:visible_in_same_session` — second open of
   the same path within the same simulated app session reads the blob.
3. `fs_service_ephemeral_reset:gone_after_relaunch` — simulate app exit /
   relaunch (same helper as the allow test); the blob is gone — open
   returns `not found` (or read returns zero bytes, depending on the API
   shape #88 settles on; the assertion is "no surviving data", not a
   specific error code).
4. `fs_service_ephemeral_reset:no_persist_leak` — even after the relaunch,
   `APP_ALLOWED` (with `CAP_FS_PERSIST`) cannot see the ephemeral blob in
   `/persist`; ephemeral scope never spills upward.

## Wire-up

- `tests/fs_service_persist_allow_test.c`,
  `tests/fs_service_persist_deny_test.c`,
  `tests/fs_service_ephemeral_reset_test.c`. One C translation unit per
  validator so failures attribute cleanly in the JSON report.
- `build/scripts/test_fs_service_persist_allow.sh`,
  `build/scripts/test_fs_service_persist_deny.sh`,
  `build/scripts/test_fs_service_ephemeral_reset.sh`. Committed `+x` per
  CONTRIBUTING.md post-#90.
- `build/scripts/test.sh` — three new dispatch arms
  (`fs_service_persist_allow`, `fs_service_persist_deny`,
  `fs_service_ephemeral_reset`) and updated usage line.
- `build/scripts/validate_bundle.sh` `TEST_TARGETS` — append the same three
  names so the full bundle exercises them and the JSON report (#110)
  carries per-target `pass | fail | harness_error` rows.

No `kernel/` or `tools/` files are modified by this slice. The existing
`tests/fs_service_test.c` (which is unit-shaped against `fs_service_*`) is
left untouched; #124 is independently fixing its disk-image-staged
assertions.

## Exit criteria (mirrors #108 done-when)

- [ ] Three validator entries wired into `test.sh` + `validate_bundle.sh`.
- [ ] Each emits structured `TEST:PASS:<name>` / `TEST:FAIL:<name>:<reason>`
      so the validator JSON report distinguishes them.
- [ ] Deny test asserts the "fail-closed or ephemeral-redirect" rule
      exactly (records which branch fired) and never accepts a write that
      reached the persistent backend without `CAP_FS_PERSIST`.
- [ ] Deny test asserts an audit deny event — gated on #84 / #98 with
      explicit `TEST:SKIP:` markers until that path is on `main`.
- [ ] Ephemeral-reset test proves data does not survive an app exit /
      relaunch and does not spill into the persistent scope.
- [ ] No ambient FS access path remains: every passing persistent
      read/write requires an explicit `CAP_FS_PERSIST` grant.

## Dependencies

- #83 / PR #88 — filesystem service + faux FS slice. Land first; this
  slice consumes the surface unchanged.
- #84 / PR #98 — audit-event ring + serial line. Audit assertion gates on
  this; SKIP marker keeps the slice mergeable beforehand.
- #110 / PR #112 — validator JSON report. Already shapes the per-target
  enum this slice keys into.
- #91 / #106 / #124 — CI must be green (covered by the unblock chain in
  the daily reviews #119, #126) before new validator targets can be
  trusted.

## Out of scope

- Real on-disk persistence (M3 explicitly stays in ramfs).
- Cross-app filesystem shares (those go through the broker — covered by
  #115's plan, not this one).
- Quotas, lifetime caps, time-bounded ephemeral scopes.
- Launcher-mediated grant prompts and on-disk manifest plumbing — Phase 3
  of the FS plan; will get its own slice after the launcher manifest
  loader lands.
- App-binary level end-to-end tests (a real editor app exercising the deny
  + ephemeral path through the ABI) — useful but waits on the launcher
  Phase 3 slice and on the disk-image staging perms (#106) being green.

## Follow-ups (not in this slice)

- File the *Execute* tracking issue ("Add fs_service_persist_{allow,deny}
  + fs_service_ephemeral_reset validators") once #88 merges, so the FS
  service contract this plan assumes is frozen. Same handoff discipline as
  #115 / PR #127 for the broker and #118 / PR #121 for the M5 ownership
  graph.
- When the launcher manifest loader lands, extend the deny test with a
  manifest that omits `CAP_FS_PERSIST` and assert the request fails before
  it reaches `fs_service` — the launcher-side complement to the
  service-side deny path covered here.
- Mirror the three new dispatch arms into `build/scripts/test.ps1` next
  time the Windows harness is touched.
