# 2026-05-14 M4 Capability Broker — Allow / Deny / Revoke Acceptance Tests

Tracking issue: #115. Sibling pattern: #92 (M2 HelloApp deny-path), #108 (M3 FS
deny + ephemeral-reset). Primary slice it asserts against: #85 (PR #99,
`plans/2026-04-21-capability-broker-share-workflow.md`).

## Goal

Carve the three deterministic acceptance tests `BUILD_ROADMAP.md` §5.4 calls
out for M4 into a small, scoped validator slice that can be wired into
`build/scripts/test.sh` + `build/scripts/validate_bundle.sh` independently of
the broker implementation PR. Once #99 lands, this slice is the contract that
proves M4's exit criteria — not the broker tests inside #99 itself, which are
unit-shaped and live in `tests/cap_broker_test.c`.

## Scope

In:

- Three new validator targets (`broker_share_allow`, `broker_share_deny`,
  `broker_share_revoke`) with structured `TEST:PASS:` / `TEST:FAIL:` markers
  consumable by the validator JSON report (#110).
- A minimal "provider + consumer" actor pair, simulated through the existing
  `cap_broker_*` API and `cap_table_*` gates (no new app binaries, no kernel
  changes).
- Audit-event assertions gated on whether #84's audit ring + #98's serial line
  are merged at execution time (see "Layering" below).

Out:

- New broker semantics, expiration, time-bounded shares, cross-machine.
- Launcher-mediated share prompts (Phase 3 of the broker plan; explicitly
  deferred per #99's follow-ups).
- Mirroring into `test.ps1`; do that opportunistically when the Windows
  harness is next touched.

## Layering against in-flight work

`cap_broker` from PR #99 already exposes the surface this slice needs:

- `cap_broker_request(owner, recipient, cap_id, resource_id) -> share_id`
- `cap_broker_approve(owner, share_id)` / `cap_broker_deny(owner, share_id)`
- `cap_broker_revoke(actor, share_id)` (owner or recipient)
- `cap_broker_recipient_check(recipient, share_id, cap_id, resource_id)`

This plan does not extend that API. If execution finds a real gap (e.g. a
required `cap_broker_state(share_id)` introspection helper for the test to
assert "no usable handle on the consumer side"), file a follow-up issue on
the broker rather than widening this slice. Same discipline as #92 / #108.

## Test design

Each test runs against a freshly reset broker + cap-table (`cap_broker_reset`,
`cap_table_reset`) and uses three subject ids — `OWNER` (provider, holds the
shared cap directly), `RECIP` (consumer), `BYSTANDER` (regression).

### `broker_share_allow`

`TEST:PASS:` markers, in order:

1. `broker_share_allow:owner_holds_cap` — `cap_table_check(OWNER, CAP_FS_READ,
   "doc-alpha") == CAP_OK`. Sets the precondition the broker requires.
2. `broker_share_allow:request_returns_pending_share_id` — request succeeds,
   share id is non-zero, recipient cannot yet read.
3. `broker_share_allow:approve_grants_recipient` —
   `cap_broker_approve(OWNER, share_id) == CAP_OK` and
   `cap_broker_recipient_check(RECIP, share_id, CAP_FS_READ, "doc-alpha") ==
   CAP_OK`.
4. `broker_share_allow:scope_is_resource_bound` —
   `cap_broker_recipient_check(RECIP, share_id, CAP_FS_READ, "doc-beta") ==
   CAP_ERR_MISSING`.
5. `broker_share_allow:scope_is_capability_bound` —
   `cap_broker_recipient_check(RECIP, share_id, CAP_FS_WRITE, "doc-alpha") ==
   CAP_ERR_MISSING`.
6. `broker_share_allow:audit_grant_recorded` (gated on #98 landing): an
   audit-ring tail entry shows `op=GRANT actor=OWNER subject=RECIP
   cap=CAP_FS_READ outcome=GRANTED`. If #98 is not yet on `main` at execution
   time, emit `TEST:SKIP:broker_share_allow:audit_grant_recorded:audit_log_unwired`
   and continue — the rest of the test still gates the slice.

### `broker_share_deny`

`TEST:PASS:` markers, in order:

1. `broker_share_deny:owner_holds_cap` — same precondition as the allow test.
2. `broker_share_deny:request_returns_pending_share_id` — request succeeds.
3. `broker_share_deny:deny_path` — `cap_broker_deny(OWNER, share_id) == CAP_OK`
   (deny is a successful workflow outcome, not an error).
4. `broker_share_deny:no_recipient_grant` —
   `cap_broker_recipient_check(RECIP, share_id, CAP_FS_READ, "doc-alpha") ==
   CAP_ERR_MISSING` *and* `cap_table_check(RECIP, CAP_FS_READ, "doc-alpha") ==
   CAP_ERR_MISSING` (no leak through the underlying table).
5. `broker_share_deny:cannot_be_re_approved` — calling
   `cap_broker_approve(OWNER, share_id)` after a deny returns
   `CAP_ERR_INVALID_STATE` (or whatever the broker uses for terminal-state
   transitions; assert non-`CAP_OK` and assert the recipient still cannot
   read). Denials remain denials.
6. `broker_share_deny:bystander_cannot_mutate` —
   `cap_broker_approve(BYSTANDER, share_id)` rejected; recipient still cannot
   read.
7. `broker_share_deny:audit_deny_recorded` (gated on #98, same SKIP rule as
   above): audit tail shows
   `op=GRANT actor=OWNER subject=RECIP cap=CAP_FS_READ outcome=GRANT_DENIED`.

### `broker_share_revoke`

`TEST:PASS:` markers, in order:

1. `broker_share_revoke:setup_grants_recipient` — runs the allow path through
   `cap_broker_approve`; recipient read succeeds.
2. `broker_share_revoke:owner_revoke_takes_effect` —
   `cap_broker_revoke(OWNER, share_id) == CAP_OK`; the next
   `cap_broker_recipient_check(RECIP, ...)` returns `CAP_ERR_MISSING`.
3. `broker_share_revoke:underlying_table_revoked` —
   `cap_table_check(RECIP, CAP_FS_READ, "doc-alpha") == CAP_ERR_MISSING`
   (defense in depth; no stale grant).
4. `broker_share_revoke:double_revoke_is_idempotent` — second revoke returns
   `CAP_OK` (or a stable terminal-state code); recipient still cannot read.
5. `broker_share_revoke:recipient_self_revoke` — re-run setup with a new share
   id, then `cap_broker_revoke(RECIP, share_id) == CAP_OK`; recipient cannot
   read; bystander still cannot.
6. `broker_share_revoke:audit_revoke_recorded` (gated on #98, same SKIP rule):
   audit tail shows
   `op=REVOKE actor=OWNER subject=RECIP cap=CAP_FS_READ outcome=REVOKED` for
   the first revoke and an additional `REVOKED`/idempotent marker for the
   second per the broker's chosen semantics.

## Wire-up

- `tests/broker_share_allow_test.c`, `tests/broker_share_deny_test.c`,
  `tests/broker_share_revoke_test.c`. One C translation unit per validator so
  failures attribute cleanly in the JSON report.
- `build/scripts/test_broker_share_allow.sh`,
  `build/scripts/test_broker_share_deny.sh`,
  `build/scripts/test_broker_share_revoke.sh`. Committed `+x` per
  CONTRIBUTING.md post-#90.
- `build/scripts/test.sh` — three new dispatch arms (`broker_share_allow`,
  `broker_share_deny`, `broker_share_revoke`) and updated usage line.
- `build/scripts/validate_bundle.sh` `TEST_TARGETS` — append the same three
  names so the full bundle exercises them and the JSON report (#110) carries
  per-target `pass | fail | harness_error` rows.

No `kernel/` or `tools/` files are modified by this slice.

## Exit criteria (mirrors #115 done-when)

- [ ] Three validator entries wired into `test.sh` + `validate_bundle.sh`.
- [ ] Each emits structured `TEST:PASS:<name>` / `TEST:FAIL:<name>:<reason>`
      so the validator JSON report distinguishes them.
- [ ] Allow + revoke assert an audit transfer event; deny asserts an audit
      deny event — gated on #84 / #98 with explicit `TEST:SKIP:` markers
      until that path is on `main`.
- [ ] No ambient broker bypass: every passing `recipient_check` requires an
      explicit `cap_broker_approve`; the deny test produces no usable handle
      on the consumer side.

## Dependencies

- #85 / PR #99 — broker API. Land first; this slice consumes the surface
  unchanged.
- #84 / PR #98 — audit-event ring + serial line. Audit assertions gate on
  this; SKIP markers keep the slice mergeable beforehand.
- #110 / PR #112 — validator JSON report. Already shapes the per-target enum
  this slice keys into.
- #91 / #106 — CI must be green (covered by the unblock chain in the daily
  reviews #119, #126) before new validator targets can be trusted.

## Out of scope

- Cross-machine / multi-host share workflows.
- Time-bounded or count-bounded shares (broker plan calls these out as
  optional follow-ups).
- Launcher-mediated share prompts and on-disk manifest plumbing — Phase 3 of
  the broker plan; will get its own slice after the launcher manifest loader
  lands.
- App-binary level end-to-end tests (a real provider + consumer app pair
  exchanging a share through the ABI) — useful but waits on the launcher
  Phase 3 slice and on the disk-image staging perms (#106) being green.

## Follow-ups (not in this slice)

- File the *Execute* tracking issue ("Add broker_share_{allow,deny,revoke}
  validators") once #99 merges, so the broker contract this plan assumes is
  frozen. Same handoff discipline as the M5 ownership-graph plan (#118 →
  #121).
- When the launcher manifest loader lands, extend the deny test with a
  manifest that omits the offered cap and assert the request fails before it
  reaches the broker — that is the launcher-side complement to the
  broker-side deny path covered here.
- Mirror the three new dispatch arms into `build/scripts/test.ps1` next time
  the Windows harness is touched.
