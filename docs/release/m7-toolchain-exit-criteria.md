# M7-TOOLCHAIN Exit Criteria Checklist

Issue: [#603](https://github.com/rwrife/SecureOS/issues/603)

This is the single canonical "are we done yet?" checklist for
[M7-TOOLCHAIN umbrella #403](https://github.com/rwrife/SecureOS/issues/403).
Daily review automation can grep unchecked boxes (`- [ ]`) in this file to
report remaining release work.

## 1) Phase status table

| Phase | Scope | Umbrella / gates | Required acceptance markers | Current status |
|---|---|---|---|---|
| 1 | Heap/brk substrate (`os_mem_brk`, clib brk wiring) | [#421](https://github.com/rwrife/SecureOS/issues/421), [#424](https://github.com/rwrife/SecureOS/issues/424) | `toolchain_heap_isolation` | 🔄 In progress (schema landed, execute umbrella still open) |
| 2 | FS round-trip for larger compiler outputs | [#405](https://github.com/rwrife/SecureOS/issues/405) lineage, `os_fs_*` large-output path | `toolchain_large_output_persisted` | 🔄 In progress (acceptance marker still gated on #409) |
| 3 | TinyCC freestanding port (`libtcc`) | [#408](https://github.com/rwrife/SecureOS/issues/408), [#538](https://github.com/rwrife/SecureOS/issues/538), [#539](https://github.com/rwrife/SecureOS/issues/539) | `toolchain_compile_error_reported` | 🔄 In progress |
| 4 | `cc` driver + `sofpack` + manifest sidecar generation | [#409](https://github.com/rwrife/SecureOS/issues/409), [#540](https://github.com/rwrife/SecureOS/issues/540), [#533](https://github.com/rwrife/SecureOS/issues/533), [#634](https://github.com/rwrife/SecureOS/issues/634) | `toolchain_compiles_hello_in_os`, `toolchain_cc_manifest_sidecar_written_on_link`, `toolchain_cc_version_and_help_text_pinned` | 🔄 In progress |
| 5 | Unsigned-run enforcement + launcher auth flow | [#410](https://github.com/rwrife/SecureOS/issues/410) | `toolchain_runs_compiled_binary`, `toolchain_unsigned_prompt_enforced` | 🔄 In progress |
| 6 | Release packaging/compliance and ship gate | [#523](https://github.com/rwrife/SecureOS/issues/523), [#550](https://github.com/rwrife/SecureOS/issues/550), [#583](https://github.com/rwrife/SecureOS/issues/583) | All markers listed below at `PASS` | 🔄 In progress |

## 2) Acceptance markers that must be `PASS` (not `SKIP`) before ship

Source of truth: [`tests/m7_toolchain/markers.json`](../../tests/m7_toolchain/markers.json)

- [ ] `toolchain_compiles_hello_in_os` (gating issue: [#409](https://github.com/rwrife/SecureOS/issues/409))
- [ ] `toolchain_runs_compiled_binary` (gating issue: [#410](https://github.com/rwrife/SecureOS/issues/410))
- [ ] `toolchain_unsigned_prompt_enforced` (gating issue: [#410](https://github.com/rwrife/SecureOS/issues/410))
- [ ] `toolchain_large_output_persisted` (gating issue: [#409](https://github.com/rwrife/SecureOS/issues/409))
- [ ] `toolchain_compile_error_reported` (gating issue: [#409](https://github.com/rwrife/SecureOS/issues/409))
- [ ] `toolchain_cc_manifest_sidecar_written_on_link` (gating issue: [#409](https://github.com/rwrife/SecureOS/issues/409))
- [ ] `toolchain_cc_version_and_help_text_pinned` (gating issue: [#409](https://github.com/rwrife/SecureOS/issues/409))
- [ ] `toolchain_heap_isolation` (gating issue: [#410](https://github.com/rwrife/SecureOS/issues/410))

## 3) ABI freeze checklist (must have non-placeholder verification stamps)

- [ ] Manifest fields for M7 path (`owner.kind`, `runtime.arena_bytes`, sidecar rules) are pinned and stamped in [`docs/abi/manifest.md`](../abi/manifest.md).
- [ ] Syscall numbers/contract for compiler runtime path (`os_mem_brk`, `os_process_exit`, `os_process_spawn`) are pinned and stamped in [`docs/abi/syscalls.md`](../abi/syscalls.md).
- [ ] Audit marker formats used by compile/run flow are pinned and stamped in [`docs/abi/audit-markers.md`](../abi/audit-markers.md).

## 4) Release artefacts

- [x] LGPL compliance bundle landed ([#523](https://github.com/rwrife/SecureOS/issues/523)).
- [ ] `libtcc1.a` staged under `/apps/dev/tcc/` ([#550](https://github.com/rwrife/SecureOS/issues/550)).
- [ ] `cc` driver staged under `/apps/dev/cc` ([#540](https://github.com/rwrife/SecureOS/issues/540)).
- [x] Determinism known-sources baseline landed ([#583](https://github.com/rwrife/SecureOS/issues/583)).

## 5) Exit decision rule

M7 is ready to ship when all of the following are true:

1. Every marker in section 2 is `PASS` (no `SKIP`/`FAIL`).
2. ABI freeze checklist in section 3 is all checked with non-placeholder stamp evidence.
3. Release artefacts in section 4 are complete.
4. Umbrella [#403](https://github.com/rwrife/SecureOS/issues/403) has no remaining open execute slices blocking compile→run inside SecureOS.
