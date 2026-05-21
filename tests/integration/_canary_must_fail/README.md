# `_canary_must_fail` — DO NOT "FIX" THIS TEST

This is an **intentionally failing** test target. It exists so the
SecureOS validator (`build/scripts/validate_bundle.sh` +
`build/scripts/test.sh` + `validator_report.json` schema, see #110/#112)
proves end-to-end that a real, deliberate failure is **classified as a
failure** — not silently skipped, no-op'd, or mis-counted as a pass.

## Why this exists

BUILD_ROADMAP §3.4 (validation tests) and §8 item 9 explicitly require a
*negative test*: an intentionally failing target that the harness must
catch. We have repeatedly hit cases where missing `+x` bits (#90), stale
`TEST_TARGETS` allowlists (#129), or stale linker inputs (#140) caused a
test to be effectively a no-op while CI stayed green. Without a permanent
canary, those regressions only surface when an unrelated test starts
misbehaving.

This canary closes that gap. It is wired into the validator as an
`EXPECTED_FAIL_TARGETS` entry: the bundle's pass condition is

> every `TEST_TARGETS` target passes **AND** every `EXPECTED_FAIL_TARGETS`
> target fails.

If the canary ever stops failing (e.g. someone "fixes" it, deletes its
`TEST:FAIL:` line, or the dispatcher silently no-ops it), the bundle
exits non-zero with the marker:

```
BUNDLE_FAIL: canary did not fail
```

That marker is the contract — do not change it.

## What it does

`canary_must_fail.sh` emits exactly:

```
TEST:START:_canary_must_fail
TEST:FAIL:_canary_must_fail:intentional
```

…and exits with status `1`. That's the entire program. Anything more
elaborate would risk the canary failing for *the wrong reason* and would
defeat the point.

## How it's wired

- `build/scripts/test.sh canary_must_fail` → runs this script.
- `build/scripts/validate_bundle.sh` lists `canary_must_fail` under
  `EXPECTED_FAIL_TARGETS` (separate from `TEST_TARGETS`). The bundle:
  - runs the canary,
  - records `expectedFail: true`, `observed: <pass|fail|harness_error>`,
    `classification: <ok|anomaly|harness_error>` in
    `validator_report.json`,
  - **fails the bundle** if `observed != "fail"`.
- `docs/test-plans/validator-report.schema.json` documents the new
  `expectedFail` / `observed` / `classification` fields (optional;
  existing consumers still validate).

## If you are tempted to "fix" this

Don't. If the canary fails for an unexpected reason (e.g. the dispatcher
crashes), the validator will surface it as `harness_error` /
`classification: harness_error`, not as a green run. Investigate that
instead.

If the canary contract genuinely needs to change (new marker shape, new
classification value, etc.), update *all of*:

- this README,
- `canary_must_fail.sh`,
- `build/scripts/validate_bundle.sh` `EXPECTED_FAIL_TARGETS` handling,
- `docs/test-plans/validator-report.schema.json`,
- and the `BUNDLE_FAIL: canary did not fail` marker text consumed by
  log scrapers.

Tracks #212.
