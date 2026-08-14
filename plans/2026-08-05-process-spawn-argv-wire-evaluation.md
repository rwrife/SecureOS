# Plan: `os_process_spawn` argv wire-format evaluation runway (issue #724)

- Date: 2026-08-05
- Issue: [#724](https://github.com/rwrife/SecureOS/issues/724)
- Related: [#410](https://github.com/rwrife/SecureOS/issues/410), [#546](https://github.com/rwrife/SecureOS/issues/546), [#422](https://github.com/rwrife/SecureOS/issues/422)
- Status: starter slice (instrumentation + evidence runway)

## Problem

`os_process_spawn` currently joins `argv[1..]` into a single space-delimited
`raw_args` string before handing control to the launcher bridge. This is a
known v0 contract and is intentionally documented, but it can lose vector
boundaries when an argument itself contains spaces.

For SecureOS, this matters because the in-OS toolchain path (`cc` in #409/#410)
will rely on deterministic, auditable process-launch semantics. Ambiguous argv
framing can make failures harder to diagnose and can obscure exactly what a
child process received.

## Goal

Create a low-risk runway for #724 by pinning explicit host evidence of argv
boundary loss now, then using #410 runtime coverage to decide whether an
additive length-prefixed wire format is required.

## This slice (land now)

1. Extend the existing host contract test (`process_spawn_argv_roundtrip`) with
   explicit collision evidence where distinct argv vectors collapse to the same
   joined `raw_args` payload (single-space payload and multi-argument payload
   variants).
2. Strengthen the harness script to assert all expected sub-markers (instead of
   only relying on process exit code), so CI catches silent marker drift.
3. Keep behavior unchanged (no ABI or runtime semantics changes in this slice).

## Follow-up slice (after #410 has runtime signal)

1. Capture real `cc` invocation corpora from QEMU acceptance runs and classify
   whether any toolchain-relevant vectors are ambiguous under space-join.
2. If ambiguity is observed in real flows, propose an additive framing update:
   - length-prefixed argv transport (or equivalent unambiguous framing),
   - backward-compatible handling for legacy v0 callers,
   - docs + host + QEMU tests updated together.
3. If ambiguity is not observed, retain v0 and record the bounded risk with the
   measured invocation corpus.

## Acceptance for this starter plan

- Host gate emits and checks:
  - `TEST:PASS:process_spawn_argv_roundtrip:space_join_limitation_pinned`
  - `TEST:PASS:process_spawn_argv_roundtrip:space_join_collision_pinned`
  - `TEST:PASS:process_spawn_argv_roundtrip:space_join_multiarg_payload_pinned`
  - `TEST:PASS:process_spawn_argv_roundtrip:space_join_multiarg_collision_pinned`
- No runtime behavior or ABI change in this PR.
- Plan links #724 to concrete next-step evidence collection once #410 lands.
