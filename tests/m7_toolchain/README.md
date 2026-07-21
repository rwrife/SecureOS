# tests/m7_toolchain/

Acceptance suite scaffolding for the in-OS toolchain milestone.

Plan: [`plans/2026-05-28-in-os-toolchain-self-hosting.md`](../../plans/2026-05-28-in-os-toolchain-self-hosting.md)
(§"Acceptance tests").

Umbrella: [#403](https://github.com/rwrife/SecureOS/issues/403).
Execute slice that flips these markers to `TEST:PASS`:
[#410 (M7-TOOLCHAIN-007)](https://github.com/rwrife/SecureOS/issues/410).
Scaffold (this directory): [#423](https://github.com/rwrife/SecureOS/issues/423).

## Status

All thirteen markers are SKIP-pinned. Each test script in this directory is a
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
| `toolchain_cc_manifest_sidecar_written_on_link` | [#634] | qemu harness pin for sidecar synthesis write-on-link when no `--manifest` or sidecar exists |
| `toolchain_cc_manifest_override_precedence` | [#409] + [#410] | precedence pin: `cc --manifest <path>` must override co-located sidecar values (`caps_required`, `runtime.arena_bytes`) with explicit `reason=cli_override` evidence |
| `toolchain_cc_version_and_help_text_pinned` | [#409] | `cc --version` / `cc --help` stdout goldens are byte-stable and deterministic (no host paths/timestamps) |
| `toolchain_cc_exit_codes_match_v0_table` | [#410] | runtime `cc` exit codes match the six-slot v0 table pinned in docs ([#589]); harness stays SKIP until toolchain execute slices land |
| `toolchain_heap_isolation`          | [#410]       | two sequential `cc` runs in one boot don't see each other's arena state (kernel `os_mem_brk` + per-process arena reset shipped in #421 via PR #432/#455 and per-spawn arena clamp via PR #454; remaining gate is `cc` driver #409 + acceptance-suite wiring #410) |
| `toolchain_launch_audit_owner_kind_field_emitted` | [#410] | launch audit contract pin: both `launch.granted` and `launch.denied` records include `owner_kind=<internal|external|local>` (normative contract in `docs/abi/audit-markers.md`, row tracked by #554) |
| `toolchain_launcher_owner_kind_cache_isolation` | [#410] + [#522] | unsigned-run cache isolation pin: cached `AUTH_TYPE_UNSIGNED_BIN` decisions must not cross `owner.kind` boundaries (`external` vs `local`), with deny-path owner_kind evidence per #542/#554 |
| `toolchain_libc_deps_phase3_complete` | [#538] + [#539] | TinyCC Phase-3 libc-deps completion marker; stays SKIP until both sub-slices close, then must flip off `awaiting_*` |

[#409]: https://github.com/rwrife/SecureOS/issues/409
[#410]: https://github.com/rwrife/SecureOS/issues/410
[#421]: https://github.com/rwrife/SecureOS/issues/421
[#422]: https://github.com/rwrife/SecureOS/issues/422
[#522]: https://github.com/rwrife/SecureOS/issues/522
[#538]: https://github.com/rwrife/SecureOS/issues/538
[#539]: https://github.com/rwrife/SecureOS/issues/539
[#554]: https://github.com/rwrife/SecureOS/issues/554
[#589]: https://github.com/rwrife/SecureOS/issues/589
[#634]: https://github.com/rwrife/SecureOS/issues/634

## Running

Each marker is wired as a `build/scripts/test.sh` target. For example:

```
bash build/scripts/test.sh toolchain_compiles_hello_in_os
```

The full set runs as part of `build/scripts/validate_bundle.sh` and is
also reflected in the bundle JSON under the top-level `m7_toolchain`
section (status `"SKIP"` for every marker until #410 flips them).

## Scope guard

This directory is **harness-only**: no compiler invocation, no QEMU boot,
no kernel-side changes, no `OS_ABI_VERSION` bump. Removing a SKIP and
asserting a real `TEST:PASS:<marker>` is the gating execute issue's job.

## Drift gate

[`tools/validate_m7_markers.py`](../../tools/validate_m7_markers.py) (issue
[#494]) treats `markers.json` as a contract instead of a comment block.
Wired as the `validate_m7_markers` target in
[`build/scripts/test.sh`](../../build/scripts/test.sh) and present in the
`TEST_TARGETS` block of
[`build/scripts/validate_bundle.sh`](../../build/scripts/validate_bundle.sh).
It asserts:

1. Every `markers[].name` appears as a `toolchain_*` case arm in
   `build/scripts/test.sh` AND in `TEST_TARGETS` of
   `build/scripts/validate_bundle.sh`.
2. Every `toolchain_*` case arm / `TEST_TARGETS` entry exists in
   `markers.json` (orphan-from-`TEST_TARGETS` shape, mirroring
   [#129] / [#366] / [#401] / [#414] / [#469] / [#482] / [#487]).
3. Each `tests/m7_toolchain/<marker>.sh` stub exists and contains the
   literal `TEST:PASS:<marker>` string (so a rename in the stub that
   breaks the bundle rollup cannot land silently).
4. For each `gatingIssue` (or `gatingIssues` multi-issue override), when `gh`
   is reachable, issues are fetched and the validator FAILs once all resolved
   gate issues are `CLOSED` while `reason` is still `awaiting_<n>` (or
   `awaiting_<n>_<m>` for dual-gate markers).
   Use `--allow-offline` in air-gapped CI to skip this check with an
   explicit `M7_MARKER:SKIP:<marker>:gating_issue_check_offline` line.

The `m7_markers_drift` target is the negative canary that mutates a
marker name in a sandbox copy and asserts the validator FAILs with a
deterministic message — mirroring [#213] /
`capability_registry_drift` ([#234]) /
`sosh_contract_registry_drift` ([#351]).

Issue [#619] adds a sibling host drift gate
[`tools/validate_hello_golden.py`](../../tools/validate_hello_golden.py)
(`hello_sof_golden` in `build/scripts/test.sh`, wired in
`build/scripts/validate_bundle.sh`) that pins the canonical `dev/hello.c`
reference output (`/apps/hello.bin`) and the key input SHAs
(`dev/hello.c`, `secureos_api.h`, `crt0.c`, `secureos_api_stubs.c`, plus
`build/toolchain.lock`) via
[`tests/m7_toolchain/hello_golden.json`](./hello_golden.json). This closes
"does it still build to the same bytes?" drift that a compile-only canary
cannot catch.

Issue [#604] adds a sibling gate
[`tools/validate_m7_marker_harnesses.py`](../../tools/validate_m7_marker_harnesses.py)
(`validate_m7_marker_harnesses` in `build/scripts/test.sh`, wired in bundle
`TEST_TARGETS`) that enforces harness presence for every marker row:
`tests/m7_toolchain/<marker>.sh` or `<marker>.c` must exist unless the marker
is explicitly listed in `tests/m7_toolchain/marker_harness_allowlist.json`
with a non-empty justification. The `m7_marker_harnesses_drift` target is the
negative canary for this gate.

Issue [#611] adds a schema drift gate
[`tools/validate_m7_markers_schema.py`](../../tools/validate_m7_markers_schema.py)
(`validate_m7_markers_schema` in `build/scripts/test.sh`, wired in bundle
`TEST_TARGETS`) that enforces row-level metadata shape in `markers.json`
(`id`, `harnessPath`, `gatingIssue`/`gatingIssues`, `skipReason`, `addedIn`,
and legacy compatibility fields). `m7_markers_schema_drift` is the negative
canary proving schema violations fail deterministically.

For canonical SKIP-pinned harness authoring workflow, see
[`docs/contributing/skip-pinned-harness-guide.md`](../../docs/contributing/skip-pinned-harness-guide.md)
(issue [#608]).
For the pinned schema contract itself, see
[`docs/contrib/m7-markers-schema.md`](../../docs/contrib/m7-markers-schema.md).

[#494]: https://github.com/rwrife/SecureOS/issues/494
[#604]: https://github.com/rwrife/SecureOS/issues/604
[#611]: https://github.com/rwrife/SecureOS/issues/611
[#129]: https://github.com/rwrife/SecureOS/issues/129
[#213]: https://github.com/rwrife/SecureOS/issues/213
[#234]: https://github.com/rwrife/SecureOS/issues/234
[#351]: https://github.com/rwrife/SecureOS/issues/351
[#366]: https://github.com/rwrife/SecureOS/issues/366
[#401]: https://github.com/rwrife/SecureOS/issues/401
[#414]: https://github.com/rwrife/SecureOS/issues/414
[#469]: https://github.com/rwrife/SecureOS/issues/469
[#482]: https://github.com/rwrife/SecureOS/issues/482
[#487]: https://github.com/rwrife/SecureOS/issues/487
