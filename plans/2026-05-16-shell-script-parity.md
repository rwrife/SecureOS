# 2026-05-16 — build/scripts shell parity (.sh ↔ .ps1)

Tracked by issue #156.

## Context

`AGENTS.md` (root, "Guidlines") requires:

> Keep ps1 and sh scripts in sync, specifically the build commands so we can
> build on windows, linux and macos

A mechanical diff of `build/scripts/` at the time of writing showed **18
`.sh`-only** and **3 `.ps1`-only** scripts. The full list is captured in the
parity allowlist at `build/scripts/.shell_parity_allowlist`; this plan
proposes how to retire each entry.

## Initial scaffolding (this PR)

- `build/scripts/check_shell_parity.sh` (and `.ps1` peer) — parity check that
  walks `build/scripts/` and fails on unallowlisted drift.
- `build/scripts/.shell_parity_allowlist` — temporary allowlist seeded with
  every current asymmetry, with comments grouping them by root cause.
- `build/scripts/test_shell_parity.sh` (+ `.ps1`) and `parity` target in
  `test.sh` / `test.ps1` so the check runs as part of the validator suite.
- `CONTRIBUTING.md` note restating the AGENTS.md rule and pointing at the
  parity check.

No existing `.sh` or `.ps1` script is touched. The validator-suite hookup is
opt-in (a new target) and does not modify `validate_bundle.sh` TEST_TARGETS,
so it cannot regress the open unblock chain (#104 → #107 → #125 → #134).

## Phased retirement of allowlist entries

Each phase below is intended as one small PR.

### Phase 1 — build hot path (1 entry)

- [ ] Port `build_os_command.sh` → `build_os_command.ps1`. Composes with
  #101 (the `sof_wrap` caller chain). After landing, drop `build_os_command`
  from the allowlist.

### Phase 2 — boot + scheduler validators (3 entries)

- [ ] Port `test_boot_sector.sh`, `test_boot_sector_fail.sh`,
  `test_scheduler.sh`. These are small, no QEMU container assumptions
  beyond what `run_qemu.sh` already abstracts.

### Phase 3 — capability validators (4 entries)

- [ ] Port `test_cap_api_contract.sh`, `test_capability_table.sh`,
  `test_capability_gate.sh`, `test_capability_audit.sh`. Should land
  together so the Windows side gets the full capability surface at once;
  composes with M2/M3/M4 acceptance work (#92, #108, #115).

### Phase 4 — crypto validators (3 entries)

- [ ] Port `test_ed25519.sh`, `test_cert_chain.sh`, `test_codesign.sh`.
  Gated on #133 / PR #134 landing so the validators have a green baseline
  to mirror.

### Phase 5 — fs / event-bus / runtime validators (4 entries)

- [ ] Port `test_event_bus.sh`, `test_fs_service.sh`, `test_app_runtime.sh`,
  `test_kernel_persistence.sh`. Gated on #124 / PR #125 and #140 / PR #141
  to avoid mirroring stale assertions.

### Phase 6 — net validators (2 entries)

- [ ] Port `test_tls.sh`, `test_https.sh`. Gated on #117 / PR #122
  (BearSSL vendor + validator) so the Windows side has a working compile
  step to drive.

### Phase 7 — .ps1-only triage (3 entries)

- [ ] Decide whether `build_bearssl_clean.ps1` / `build_bearssl_fixed.ps1`
  should be deleted as dead Windows-only debugging scaffolding or whether
  bash equivalents are required. Default recommendation: delete unless a
  Windows contributor explicitly claims them.
- [ ] Decide whether a `common.sh` shared module is worth introducing to
  match `common.ps1`. Default recommendation: keep allowlisted; current
  bash callers do not share enough surface to justify it.

## Acceptance

- `bash build/scripts/check_shell_parity.sh` exits `0` with an empty
  allowlist.
- CI gates on the `parity` test target on every PR.
- `CONTRIBUTING.md` documents the rule and the allowlist process.
