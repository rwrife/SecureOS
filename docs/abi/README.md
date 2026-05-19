# SecureOS ABI Reference (Index)

Canonical home for SecureOS ABI surfaces, per
[`BUILD_ROADMAP.md` §2.2 (repo structure)](../../BUILD_ROADMAP.md) and
[§7 (ABI and Interface Freeze Plan)](../../BUILD_ROADMAP.md).

This directory is the single source of truth for the four ABI surfaces that
must be defined and versioned early to prevent churn:

| Surface                              | Document                                       | Tracking issue |
| ------------------------------------ | ---------------------------------------------- | -------------- |
| Syscall ABI                          | [`syscall.md`](./syscall.md)                   | #93            |
| IPC wire format + error model        | [`ipc-wire.md`](./ipc-wire.md)                 | #180           |
| Capability handle repr + revocation  | [`capability-handle.md`](./capability-handle.md) | #163           |
| Module manifest schema + compat      | [`manifest.md`](./manifest.md)                 | #183           |

## Version anchor

All ABI surfaces are versioned against a single constant: **`OS_ABI_VERSION`**.

Policy (from BUILD_ROADMAP §7):

- Start at `OS_ABI_VERSION = 0` during rapid iteration.
- Freeze to `1` once SDK beta is announced.
- Maintain compatibility shims for at least one major version.

The header that owns this constant is tracked by **#150** ("Establish
`OS_ABI_VERSION=0` constant + single header source of truth"). Until #150
lands, refer to the constant by name only — do not duplicate the literal
value in surface docs.

## Conventions

Every surface document in this directory MUST carry a header of the form:

```markdown
> **Owner:** <name or `unassigned`>
> **Status:** stub | draft | review | accepted | frozen
> **Last reviewed:** YYYY-MM-DD
> **Applies to:** `OS_ABI_VERSION = <n>`
```

Changes to any surface document require:

1. Bumping the `Last reviewed` date.
2. Updating `Status` if the surface advances a stage.
3. Bumping `OS_ABI_VERSION` (post-freeze) per the policy above.

## Out of scope

- Plan-directory consolidation (see #149).
- Code changes — this directory is documentation only.
