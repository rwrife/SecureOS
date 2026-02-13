# SecureOS Coding Conventions

These conventions optimize for deterministic builds, low-level correctness, and auditability in a zero-trust OS codebase.

## 1. Security and trust defaults

1. **Deny by default:** privileged operations must fail closed when capability/context is missing.
2. **No implicit authority:** pass capability context explicitly; do not read global mutable grant state in hot paths.
3. **Negative-path parity:** every allow-path test should have a deny-path counterpart.

## 2. Layout and naming

- Keep architecture-specific code under `kernel/arch/<arch>/...`.
- Capability code goes under `kernel/cap/`.
- Tests and fixtures should encode purpose in names (`*_negative`, `*_smoke`, `*_gate`).

## 3. C/ASM style baseline

- Prefer fixed-width integer types (`uint32_t`, `uint64_t`) for ABI-facing and hardware-touching code.
- Avoid hidden side effects in macros.
- Keep inline assembly minimal and wrapped in documented helpers.
- Use `static` for internal linkage by default.

## 4. Logging and test markers

- Serial output is the source of truth for validation.
- Use structured markers for machine parsing:
  - `TEST:START:<name>`
  - `TEST:PASS:<name>`
  - `TEST:FAIL:<name>:<reason>`
- Marker format is a compatibility contract; changes require parser updates in same PR.

## 5. PR hygiene

- One issue per PR.
- Keep PRs small enough to review in one pass.
- Include exact validation commands and results in PR body.
- Do not merge code paths that are untestable in CI/local deterministic flow.
