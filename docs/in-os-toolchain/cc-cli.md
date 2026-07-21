# `cc` CLI Grammar v0 (Contract Pin)

This document freezes the user-facing command-line contract for the planned
in-OS compiler driver app (`/apps/dev/cc`). It is the normative grammar
reference for issue [#552](https://github.com/rwrife/SecureOS/issues/552)
and companion slices under umbrella
[#403](https://github.com/rwrife/SecureOS/issues/403).

> Runtime implementation is still gated on
> [#409](https://github.com/rwrife/SecureOS/issues/409) and
> [#410](https://github.com/rwrife/SecureOS/issues/410). This file pins the
> expected interface before those execute slices land.

## Canonical invocation

```text
cc <input.c> -o <output.bin>
```

Example:

```text
cc /apps/dev/hello.c -o /apps/hello.bin
```

## Flag table (v0)

| Flag | Shape | Meaning | Notes |
|---|---|---|---|
| `-o` | `-o <path>` | Output binary path (SOF container on disk) | Required for compile mode |
| `-I` | `-I <dir>` | Extra include search path | Repeatable; resolved in-image |
| `--manifest` | `--manifest <path>` | Use author-supplied manifest instead of synthesis | Long form only in v0 |
| `-h`, `--help` | flag only | Print usage text and exit success | No output binary written |

### Manifest override precedence

When manifest inputs are available, precedence is:

1. `--manifest <path>` explicit override
2. Co-located `<output>.manifest.json` sidecar
3. Synthesised default via `libmanifestgen`

This precedence is pinned by
[#607](https://github.com/rwrife/SecureOS/issues/607) and scaffolded in
`tests/m7_toolchain/markers.json` by
`toolchain_cc_manifest_override_precedence`.

## Exit-code contract

The canonical numeric table is pinned in
[`building-apps.md`](./building-apps.md#cc-exit-codes-v0-contract-pin)
(issue [#589](https://github.com/rwrife/SecureOS/issues/589)).

For CLI grammar purposes, the class contract is:

- `0` success
- non-zero failure classes are deterministic and machine-assertable
- bad/missing CLI arguments map to the usage-error class from the v0 table
- manifest read/parse/validation failures map to a non-zero `cc.compile.fail`
  class and produce no output binary

## Diagnostic format (v0)

For compile-class diagnostics, stderr is line-oriented and deterministic:

```text
cc: <file>:<line>:<col>: <message>
```

Rules:

- Prefix is exactly `cc:`.
- One diagnostic per line.
- No trailing summary count line in v0.
- CLI-usage errors keep the `cc: <reason>` shape and return a non-zero
  usage-error class.

## Capability surface

| Action | Capability |
|---|---|
| Read source / include headers / runtime objects | `CAP_FS_READ` |
| Write output binary and sidecar artifacts | `CAP_FS_WRITE` |

No network capability is required by the v0 compile path.

## Cross-references

- Plan: [`plans/2026-05-28-in-os-toolchain-self-hosting.md`](../../plans/2026-05-28-in-os-toolchain-self-hosting.md)
- Companion guide: [`building-apps.md`](./building-apps.md)
- Manifest schema: [`docs/abi/manifest.md`](../abi/manifest.md)
- Marker scaffold: [`tests/m7_toolchain/markers.json`](../../tests/m7_toolchain/markers.json)

Last verified against commit: `c65d593`