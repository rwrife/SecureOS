# SKIP-pinned M7 harness authoring guide

Issue: [#608](https://github.com/rwrife/SecureOS/issues/608)  
Related: [#494](https://github.com/rwrife/SecureOS/issues/494), [#590](https://github.com/rwrife/SecureOS/issues/590), [#604](https://github.com/rwrife/SecureOS/issues/604), [#587](https://github.com/rwrife/SecureOS/issues/587)

This guide standardizes how to add **pre-gate SKIP-pinned harnesses** for M7 toolchain slices so we avoid drift in marker names, wiring, and review expectations.

## 1) When to add a SKIP-pinned harness vs wait

Author a SKIP-pinned harness now when **all** are true:

1. There is a concrete open gating issue (for example #408 / #409 / #410 / #585).
2. You can pin a deterministic contract today (expected marker names, file paths, output shape, deny-path behavior).
3. The harness can run in CI without the gated feature (emits canonical `TEST:SKIP:<marker>:awaiting_<issue>` and a deterministic `TEST:PASS:<target>` rollup).

Wait for the gating issue to land first when:

- The behavior is still exploratory and cannot be pinned yet.
- The harness would mostly duplicate an existing marker with no new drift-catching value.
- You cannot define a stable marker contract tied to an ABI/process doc.

## 2) File naming and location convention

Primary convention for new harness logic:

- `tests/m7_toolchain/<slug>_test.c` or `tests/m7_toolchain/<slug>_test.py`

Current in-repo dispatch compatibility requirement (until dispatcher shape changes):

- Keep a marker entrypoint at `tests/m7_toolchain/<marker>.sh`
- If needed, delegate that entrypoint to scoped implementation files (for example `tests/m7_toolchain/qemu/<slug>.sh`).

This keeps `build/scripts/test.sh` dispatch stable while allowing harness internals to evolve.

## 3) Marker entry shape to pin in review

For authoring/review, capture this canonical checklist shape per harness row:

```json
{
  "slug": "toolchain_cc_version_and_help_text_pinned",
  "gatingIssue": [409],
  "status": "SKIP",
  "audit_marker": "cc.compile.success",
  "last_updated": "<git sha or yyyy-mm-dd>"
}
```

Current `tests/m7_toolchain/markers.json` schema uses equivalent fields with repo-specific names (`name`, `gatingIssue`/`gatingIssues`, `reason`, optional `skipReason`, optional `harnessPath`). Keep both in sync during review:

- `slug` -> `name`
- `status=SKIP` -> `reason=awaiting_<issue>`
- `gatingIssue[]` -> `gatingIssue` (+ optional `gatingIssues` for multi-gate)
- `audit_marker` -> mention in `description` and corresponding ABI doc link
- `last_updated` -> reflected via commit history / PR context

## 4) Required audit-marker contract reference

Every SKIP harness must cite its normative marker contract from
`docs/abi/audit-markers.md` (issue [#587](https://github.com/rwrife/SecureOS/issues/587))
and/or the family-specific ABI page it links to.

Rule: if a harness asserts a marker, reviewer should be able to answer
"where is this marker shape documented?" with a direct doc link.

## 5) Required bundle wiring (TEST_TARGETS)

Wire each harness in both places:

1. `build/scripts/test.sh` dispatcher arm
2. `build/scripts/validate_bundle.sh` `TEST_TARGETS`

Snippet pattern (same wiring discipline used by prior bundle-wire issues like #503/#509/#513/#515):

```bash
# build/scripts/test.sh
<marker_name>)
  run_script "$ROOT_DIR/tests/m7_toolchain/<marker_name>.sh"
  ;;

# build/scripts/validate_bundle.sh (inside TEST_TARGETS)
<marker_name>
```

If either leg is missing, the harness is considered ungated.

## 6) Worked example (minimal end-to-end)

Example marker: `toolchain_cc_version_and_help_text_pinned`

1. Open scoped issue with clear done-when + gate number.
2. Add/adjust `tests/m7_toolchain/markers.json` row:
   - `name=toolchain_cc_version_and_help_text_pinned`
   - `gatingIssue=409`
   - `reason=awaiting_409`
   - `harnessPath=tests/m7_toolchain/qemu/cc_version_and_help_text_pinned.sh`
3. Add entrypoint `tests/m7_toolchain/toolchain_cc_version_and_help_text_pinned.sh`
   that delegates to the qemu-scoped harness.
4. Add qemu-scoped harness skeleton that emits canonical SKIP/PASS markers.
5. Wire target in `build/scripts/test.sh` and `build/scripts/validate_bundle.sh`.
6. Run drift gates locally:
   - `bash build/scripts/test.sh validate_m7_markers`
   - `bash build/scripts/test.sh validate_m7_marker_harnesses`
7. Open PR with explicit `Closes #<issue>` and include gate rationale.

## 7) SKIP -> PASS flip checklist (when gating issue closes)

When the execute/gating issue lands:

- [ ] Replace `TEST:SKIP:...:awaiting_<n>` with real assertions.
- [ ] Keep deterministic `TEST:PASS:<marker>` success marker.
- [ ] Update `markers.json` row so `reason` no longer points at a closed gate.
- [ ] If `harnessPath` changed, update it in `markers.json`.
- [ ] Re-run:
  - [ ] `bash build/scripts/test.sh validate_m7_markers`
  - [ ] `bash build/scripts/test.sh validate_m7_marker_harnesses`
  - [ ] marker target itself (`bash build/scripts/test.sh <marker>`)
- [ ] Confirm bundle still includes the marker target in `TEST_TARGETS`.

## 8) PR checklist (copy/paste)

- [ ] Marker row added/updated in `tests/m7_toolchain/markers.json`
- [ ] Harness files present under `tests/m7_toolchain/`
- [ ] `test.sh` dispatcher wired
- [ ] `validate_bundle.sh` `TEST_TARGETS` wired
- [ ] Audit marker contract linked (`docs/abi/audit-markers.md` or sibling)
- [ ] Local validator runs pasted in PR body
