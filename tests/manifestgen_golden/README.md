# Manifestgen Golden Corpus

Issue: [#577](https://github.com/rwrife/SecureOS/issues/577)

This directory pins byte-identical `libmanifestgen` synthesis output for a
small set of canonical inputs.

Each `*.fixture` file declares one driver invocation for
`tests/manifest_default_synthesise_test.c` (driver mode), including expected
exit code and optional expected output JSON.

- Positive fixtures (`EXPECT_RC=0`) must match their `*.expected.json` file
  byte-for-byte and pass `build/scripts/validate_manifests.sh`.
- Negative fixtures (`EXPECT_RC!=0`) must emit the pinned failure substring and
  must not produce a non-empty JSON output file.

Primary gate: `build/scripts/test_manifestgen_golden.sh`.
