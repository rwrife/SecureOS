# tests/m7_toolchain/

Acceptance suite scaffolding for the in-OS toolchain milestone.

Plan: [`plans/2026-05-28-in-os-toolchain-self-hosting.md`](../../plans/2026-05-28-in-os-toolchain-self-hosting.md)
(§"Acceptance tests").

Umbrella: [#403](https://github.com/rwrife/SecureOS/issues/403).
Execute slice that flips these markers to `TEST:PASS`:
[#410 (M7-TOOLCHAIN-007)](https://github.com/rwrife/SecureOS/issues/410).
Scaffold (this directory): [#423](https://github.com/rwrife/SecureOS/issues/423).

## Status

All six markers are SKIP-pinned. Each test script in this directory is a
deterministic stub that emits the canonical `TEST:SKIP:<marker>:awaiting_<n>`
line per the project's SKIP discipline (mirrors #344 / #389 / #392), then
rolls up a `TEST:PASS:<target>` so the bundle gate stays green while the
gating components land.

The marker spellings, the per-marker dispatch script names, the
`build/scripts/test.sh` dispatch entries, and the `validate_bundle.sh`
`TEST_TARGETS` block names all share a single source of truth: a regression
that drops, renames, or silently masks one of these markers flips the
bundle to FAIL (same orphan-from-`TEST_TARGETS` shape #129 / #366 / #384 /
#401 / #414 catch for other host-side targets).

## Markers

| Marker                              | Gating issue | What unblocks PASS                                       |
| ----------------------------------- | ------------ | -------------------------------------------------------- |
| `toolchain_compiles_hello_in_os`    | [#409]       | `cc` driver app produces a SOF/ELF on-target             |
| `toolchain_runs_compiled_binary`    | [#410]       | unsigned-run wiring + `cc` driver output reachable through launcher (os_process_spawn merged via #422 / PR #427) |
| `toolchain_unsigned_prompt_enforced`| [#410]       | unsigned-run wiring through the launcher auth flow       |
| `toolchain_large_output_persisted`  | [#409]       | `cc` emits a >1 KB binary; FS path stays byte-identical  |
| `toolchain_compile_error_reported`  | [#409]       | `cc` exits non-zero on syntax error with no output file  |
| `toolchain_heap_isolation`          | [#421]       | kernel `os_mem_brk` + per-process arena reset on teardown|

[#409]: https://github.com/rwrife/SecureOS/issues/409
[#410]: https://github.com/rwrife/SecureOS/issues/410
[#421]: https://github.com/rwrife/SecureOS/issues/421
[#422]: https://github.com/rwrife/SecureOS/issues/422

## Running

Each marker is wired as a `build/scripts/test.sh` target. For example:

```
bash build/scripts/test.sh toolchain_compiles_hello_in_os
```

The full set runs as part of `build/scripts/validate_bundle.sh` and is
also reflected in the bundle JSON under the top-level `m7_toolchain`
section (status `"SKIP"` for every marker until #410 flips them).

## Drift gate

A static drift gate (`build/scripts/validate_m7_markers.sh`, issue
[#494]) cross-checks `markers.json` against every consumer surface:

- the `toolchain_*` case arms in `build/scripts/test.sh`
- the `TEST_TARGETS` array in `build/scripts/validate_bundle.sh`
- the per-marker stubs in this directory (the `TEST:PASS:<marker>`
  rollup AND the `TEST:SKIP:<marker>:<reason>` line)
- each `gatingIssue` still being `OPEN` on `rwrife/SecureOS`, with the
  paired `reason` field spelled `awaiting_<gatingIssue>`

Deleting or renaming a marker locally surfaces a deterministic
`M7_MARKER:FAIL:<reason>:<marker>` line. The matching negative canary
(`tests/harness/m7_markers_drift_test.sh`, wired as the
`m7_markers_drift` test target) mutates a sandbox copy of
`markers.json` and asserts the validator fails — same canary
discipline as #213 / #234 / #297 / #351.

Run standalone:

```
bash build/scripts/test.sh validate_m7_markers
bash build/scripts/test.sh m7_markers_drift
```

On hosts without `gh`, the validator auto-falls back to
`--allow-offline` and emits `M7_MARKER:SKIP:gh_unavailable:<marker>`
for each gating-issue arm; every other drift class is still enforced.

[#494]: https://github.com/rwrife/SecureOS/issues/494

## Scope guard

This directory is **harness-only**: no compiler invocation, no QEMU boot,
no kernel-side changes, no `OS_ABI_VERSION` bump. Removing a SKIP and
asserting a real `TEST:PASS:<marker>` is the gating execute issue's job.
