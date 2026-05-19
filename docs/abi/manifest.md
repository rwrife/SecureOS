# Module Manifest Schema and Compatibility Policy

> **Owner:** unassigned
> **Status:** stub
> **Last reviewed:** 2026-05-19
> **Applies to:** `OS_ABI_VERSION = 0`

To be filled by the work tracked in **#183** (Manifest schema v0: capability
declarations consumed by launcher — BUILD_ROADMAP §5.2, §5.6, §7).

This document will specify, at minimum:

- manifest file format and required fields
- capability declarations consumed by the launcher / broker
- signing / integrity expectations (cross-reference the codesign + ed25519
  work tracked in #133 / #137 / #138)
- compatibility policy: which manifest fields are additive vs. breaking
- versioning rules tied to `OS_ABI_VERSION`

Until then, manifest layout is **unstable** and modules MUST NOT rely on any
field beyond what the in-tree launcher currently parses.
