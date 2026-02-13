# ADR 0000: Use Limine/Multiboot-first boot protocol for initial kernel milestones

- **Status:** Accepted
- **Date:** 2026-02-12
- **Decision makers:** SecureOS maintainers

## Context

SecureOS needs rapid, deterministic progress from tooling validation toward enforceable kernel capability boundaries. A custom stage-1/stage-2 bootloader provides maximal control but slows delivery and increases early-stage failure surface.

## Decision

Adopt a **Limine/Multiboot-compatible first** boot protocol strategy for M0→M1 milestones.

This means:

- prioritize reaching `kmain` and capability enforcement checkpoints quickly,
- keep early boot complexity constrained,
- defer custom-loader work to a later milestone once core zero-trust boundaries are testable.

## Consequences

### Positive

- Faster path to first kernel boundary tests.
- Less early-stage assembly/debug burden.
- Better alignment with deterministic CI and agent workflows.

### Negative

- Less ownership of earliest boot path in first milestone.
- Future migration work required if/when adopting a custom loader.

## Follow-up

- Revisit this ADR after M1 capability gate is complete.
- If custom bootloader becomes a strategic requirement, file a superseding ADR with migration plan and acceptance tests.
