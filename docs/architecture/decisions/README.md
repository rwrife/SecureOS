# Architecture Decision Records (ADRs)

This directory holds Architecture Decision Records for SecureOS.

## Purpose

ADRs are the durable record of *why* SecureOS looks the way it does. They
complement, but do not replace:

- `BUILD_ROADMAP.md` — forward-looking plan and milestones.
- `docs/BOOT_ENTRY_X86.md`, `docs/CODING_CONVENTIONS.md`, etc. — current
  state and conventions.
- `docs/architecture/CAPABILITIES.md` — narrative architecture docs.

Where roadmap and architecture docs describe *what is* and *what's next*,
ADRs describe *what was decided, when, and why*, so future agents can
understand the constraints behind current code without rediscovering them.

## Cadence

- One MADR-style file per accepted decision.
- Filenames use a 4-digit ordinal and a kebab-case slug:
  `NNNN-short-title.md` (e.g. `0001-boot-protocol-multiboot-long-mode.md`).
- Numbers are assigned at merge time, in order, and never reused.
- Each ADR is short — ideally ≤ 1 page. The point is a stable record,
  not a redesign.

## Template

Each ADR should contain at least the following sections:

```
# NNNN. <Title>

- Status: Proposed | Accepted | Superseded by ADR-XXXX | Deprecated
- Date: YYYY-MM-DD

## Context
What problem are we solving? What constraints exist
(roadmap section, prior commits, dependent subsystems)?

## Decision
The choice we made, stated clearly and unambiguously.

## Consequences
What this implies for code, tests, and future work.
What alternatives were rejected and why.

## References
Links to roadmap sections, related ADRs, code paths, and commits.
```

## Lifecycle

- New ADRs land as `Status: Proposed` in a PR for discussion.
- On merge, status is flipped to `Accepted` with the merge date.
- A later ADR may `Supersede` an earlier one; the older file stays in
  place and gets `Status: Superseded by ADR-NNNN`.

## Index

- [0001 — Boot protocol: Multiboot v1 + 64-bit long mode](./0001-boot-protocol-multiboot-long-mode.md)
