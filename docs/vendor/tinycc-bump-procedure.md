# TinyCC vendor-bump procedure

This procedure is the canonical checklist for changing the TinyCC pin in
`vendor/tinycc/VERSION`.

> Scope: docs/process only. This file does **not** authorize ad-hoc vendor bumps.

## 1) When to bump

Only bump TinyCC when at least one of the following is true:

1. A security fix requires an upstream TinyCC update.
2. An upstream TinyCC change is required to unblock an open SecureOS M7 issue.
3. A reproducibility or correctness defect cannot be solved at the SecureOS
   integration layer without changing the TinyCC pin.

If none apply, do not bump.

## 2) PR-body template (copy verbatim)

```md
## TinyCC vendor bump

- Old TinyCC commit: `<old_sha>`
- New TinyCC commit: `<new_sha>`
- Upstream changelog / compare link: `<url>`

## Drift summary

- Object-count delta (tinycc artifacts): `<+/-N or none>`
- Total-size delta (tinycc artifacts): `<+/-bytes or none>`
- `vendor/tinycc/libc-deps.json` delta summary: `<none | short summary>`
- `config.h.secureos` / `tinycc_config_secureos` delta summary: `<none | short summary>`
- LGPL source-corresponding bundle delta: `<none | short summary>`

## Required gate run log

- [ ] `./build/scripts/test.sh tinycc_vendor_gate`
- [ ] `./build/scripts/test.sh tinycc_config_secureos`
- [ ] `./build/scripts/test.sh tinycc_libc_deps`
- [ ] `./build/scripts/test.sh release_compliance_bundle`
- [ ] `./build/scripts/test.sh sof_format`
- [ ] `./build/scripts/test.sh sofpack_wrap`

## Notes

- If `sof_format` / `sofpack_wrap` are not affected by this bump,
  state explicitly why (for example: no ELF emission shape change).
- Record any stamp updates under `docs/abi/*` in this PR.
```

## 3) Gate matrix (required run order)

Run in this order so failures isolate quickly and generated artifacts are
refreshed before downstream checks:

1. `tinycc_vendor_gate` ([#516])
   - Command: `./build/scripts/test.sh tinycc_vendor_gate`
   - Purpose: pin submodule SHA + in-scope source-set discipline.
2. `tinycc_config_secureos` ([#519])
   - Command: `./build/scripts/test.sh tinycc_config_secureos`
   - Purpose: pin expected TinyCC config header contract.
3. `tinycc_libc_deps` ([#536])
   - Command: `./build/scripts/test.sh tinycc_libc_deps`
   - Purpose: pin libc symbol/dependency surface.
   - If the bump legitimately changes symbol/dependency shape, regenerate:
     - `python3 tools/scan_tinycc_libc_deps.py --root . --write-json vendor/tinycc/libc-deps.json`
4. `release_compliance_bundle` ([#523], [#526], [#553])
   - Command: `./build/scripts/test.sh release_compliance_bundle`
   - Purpose: keep LGPL-2.1 source-corresponding evidence coherent.
5. `sof_format` / `sofpack_wrap` parity checks ([#511], [#555])
   - Commands:
     - `./build/scripts/test.sh sof_format`
     - `./build/scripts/test.sh sofpack_wrap`
   - Required when the bump changes emitted ELF/object shape.

## 4) Stamp matrix and strict-stamp implications

A TinyCC bump can require `Last verified against commit:` updates in docs that
pin toolchain-facing ABI/state.

At minimum review and update (when content changed or drift gate demands it):

- `docs/abi/clib-symbols.md`
- `docs/abi/manifest.md`
- Any other touched `docs/abi/*` page with `Last verified against commit:`

Validation guard: `validate_abi_stamps` strict-mode freshness gate ([#470]).

## 5) Manifest matrix (`cc` app)

After a TinyCC bump, re-validate the `/apps/dev/cc` manifest contract when
any of these change: compiler invocation semantics, sidecar emission, runtime
resource profile, or declared capability expectations.

Primary gate/contract anchor: issue [#573] (`apps_dev_cc_manifest`).

## 6) Rollback procedure

Rollback is a normal vendor-bump PR that reverts to the prior known-good pin:

1. Revert the TinyCC submodule pointer (`vendor/tinycc/tinycc`).
2. Revert `vendor/tinycc/VERSION` commit line and accompanying bump notes.
3. Re-run the gate matrix above and confirm green state.
4. Include rollback rationale and restored old/new SHA pair in PR body.

A rollback should be a minimal, auditable change: one pointer revert + one
VERSION revert + any required generated artifact/stamp refresh to re-green the
same gates.

[#470]: https://github.com/rwrife/SecureOS/issues/470
[#511]: https://github.com/rwrife/SecureOS/issues/511
[#516]: https://github.com/rwrife/SecureOS/issues/516
[#519]: https://github.com/rwrife/SecureOS/issues/519
[#523]: https://github.com/rwrife/SecureOS/issues/523
[#526]: https://github.com/rwrife/SecureOS/issues/526
[#536]: https://github.com/rwrife/SecureOS/issues/536
[#553]: https://github.com/rwrife/SecureOS/issues/553
[#555]: https://github.com/rwrife/SecureOS/issues/555
[#573]: https://github.com/rwrife/SecureOS/issues/573
