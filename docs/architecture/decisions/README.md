# Architecture Decision Records (ADRs)

This directory contains Architecture Decision Records for SecureOS.

## Purpose

ADRs capture **significant, durable decisions** about the system — the kind of
choices future contributors (human or agent) should be able to discover and
ground new work in without having to reverse-engineer them from commit history.

Each ADR records:
- **Context** — the forces and constraints in play at the time
- **Decision** — what was chosen
- **Consequences** — what it commits us to (and what it rules out)

ADRs are **not** a redesign tool. They are a stable record. Once accepted, an
ADR is not edited in place; instead a new ADR supersedes it and the old one is
marked `Superseded by <NNNN>`.

## Cadence

- One MADR-style file per accepted decision.
- File name: `NNNN-short-kebab-title.md` (zero-padded, monotonically
  increasing).
- Keep each ADR short — ideally ≤ 1 page. Link out to deeper docs
  (`docs/BOOT_ENTRY_X86.md`, `docs/architecture/CAPABILITIES.md`, etc.) rather
  than restating them.
- Status values: `Proposed`, `Accepted`, `Superseded by <NNNN>`,
  `Deprecated`.

## When to write one

Write an ADR when the change:
- pins a wire format, ABI, or boot/loader contract,
- selects one option from a roadmap-level menu,
- locks in an invariant other subsystems will depend on, or
- closes off a previously-open question in `BUILD_ROADMAP.md`.

Routine refactors, bug fixes, and code cleanup do **not** need an ADR.

## Template

```markdown
# NNNN. <Title>

- Status: Accepted
- Date: YYYY-MM-DD

## Context

What problem are we solving? What options were on the table? What constraints
mattered (roadmap section, prior commits, external tooling)?

## Decision

The single choice we made, stated plainly.

## Consequences

What this commits us to. What other subsystems now depend on it. What the
alternative would have changed.

## References

- BUILD_ROADMAP.md §X.Y
- Relevant code paths
- Relevant commits
```

## Index

- [0001 — Boot protocol: Multiboot + 64-bit long mode](0001-boot-protocol-multiboot-long-mode.md)
