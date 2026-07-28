# user/libs/manifestgen

In-OS minimal-manifest synthesiser (M7-TOOLCHAIN-006 sub-slice, issue #533).

Freestanding userland archive used by the in-OS `cc` driver (#409) to emit
a deterministic v0 manifest JSON document next to the SOF binary it just
produced on-target. The synthesiser is the third extraction out of #409,
after #521 (libsofpack) and PR #519 (TinyCC config-secureos.h).

See `include/manifestgen/manifest_default.h` for the public contract.
Pinning host test: `tests/manifest_default_synthesise_test.c`.

Issue #577 adds a byte-identical golden-corpus gate under
`tests/manifestgen_golden/` (`build/scripts/test_manifestgen_golden.sh`).
When intentionally changing manifest synthesis bytes, update the corresponding
`*.expected.json` fixture(s) in that directory in the same PR and include the
behavioral rationale in the PR body.
