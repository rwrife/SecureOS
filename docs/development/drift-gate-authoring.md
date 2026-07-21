# Drift-Gate Authoring Guide

Issue: [#616](https://github.com/rwrife/SecureOS/issues/616)

This guide pins the canonical SecureOS drift-gate pattern used across ABI,
manifest, disk-image, marker, and audit surfaces.

## 1) Canonical 3-step recipe

1. **Pin file**
   - Add/update a JSON pin that captures expected state.
   - Keep the pin deterministic and reviewable (no timestamps, no host-local paths).
2. **Validator**
   - Add a host validator under `tools/validate_<slug>.py` (or `.sh` when shell is simpler).
   - Validator compares source-of-truth vs pin and exits non-zero on drift.
3. **Bundle wiring**
   - Add a named target in `build/scripts/test.sh`.
   - Add that target to `build/scripts/validate_bundle.sh` `TEST_TARGETS` so CI runs it.

If any of the 3 steps is missing, the drift gate is incomplete.

## 2) Pin-file placement and shape

### Placement convention

- Prefer `tests/**` for machine-validated runtime/test fixtures.
- Prefer `docs/abi/**` for normative ABI/index contracts.

### Required shape (when applicable)

Use these fields where relevant:

- `schemaVersion` (required)
- `_comment` (required: explain intent + owning issue)
- Per-entry gating metadata when SKIP-pinned:
  - `gatingIssue` (issue number)
  - `reason` (`awaiting_<issue>` style)
  - Optional `skipReason`, `harnessPath`

Example reference: `tests/m7_toolchain/markers.json`.

## 3) Validator contract

Validator naming:

- Preferred: `tools/validate_<slug>.py`
- Wrapper parity when needed:
  - `build/scripts/validate_<slug>.sh`
  - `build/scripts/validate_<slug>.ps1`

Output/exit-code contract:

- Emit deterministic machine markers (`PASS` / `FAIL` / `SKIP`) with stable prefixes.
- Exit `0` on pass, non-zero on drift or harness error.
- Include actionable remediation text in FAIL output.

## 4) TEST_TARGETS wire-up pattern

Minimum wiring pattern:

1. Add a dispatcher arm in `build/scripts/test.sh`:
   - `run_script "$ROOT_DIR/build/scripts/test_<slug>.sh"`
2. Add `<slug>` to `TEST_TARGETS` in `build/scripts/validate_bundle.sh`.

A validator that is not in `TEST_TARGETS` is treated as ungated.

## 5) SKIP policy and strict-mode interaction

Default posture is **no silent SKIP escape hatches**.

- If SKIP is used, pin it to a concrete open issue (`gatingIssue`) with an explicit
  `awaiting_<issue>` reason.
- When the gating issue closes, the harness must flip from SKIP to PASS/FAIL behavior
  in the same PR or immediate follow-up.

ABI-stamp strictness precedent (issue [#470](https://github.com/rwrife/SecureOS/issues/470)):

- `build/scripts/validate_abi_stamps.sh` supports strict behavior and should be treated
  as the model for SKIP hardening.
- `tools/validate_abi_stamps.py --strict-no-skip`
- `tools/validate_abi_stamps.py --strict-no-placeholder`

Related guide for SKIP-pinned harness authoring:

- [#608](https://github.com/rwrife/SecureOS/issues/608)

## 6) Canonical exemplar gates

Use these as templates before inventing a new style:

- [#494](https://github.com/rwrife/SecureOS/issues/494) — M7 marker drift discipline
- [#547](https://github.com/rwrife/SecureOS/issues/547) — SOF constants drift gate
- [#482](https://github.com/rwrife/SecureOS/issues/482) — capability registry validator
- [#591](https://github.com/rwrife/SecureOS/issues/591) — audit markers drift gate
- [#615](https://github.com/rwrife/SecureOS/issues/615) — staged header-set drift gate

## 7) PR checklist (copy/paste)

- [ ] Pin JSON is deterministic and documented (`schemaVersion`, `_comment`).
- [ ] Validator exists (`tools/validate_<slug>.py` or equivalent) with deterministic markers.
- [ ] Target is dispatched in `build/scripts/test.sh`.
- [ ] Target is listed in `build/scripts/validate_bundle.sh` `TEST_TARGETS`.
- [ ] SKIP, if present, is tied to an open `gatingIssue` and explicit reason.
- [ ] Flip/removal plan is documented for when the gating issue closes.
- [ ] Docs include issue links and rationale for future maintainers.
