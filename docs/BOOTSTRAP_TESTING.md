# SecureOS Bootstrap Testing

## Boot Sector Smoke Test

This repository includes a minimal x86 boot sector validation to verify local build tooling and QEMU execution.

### Run

```bash
./build/scripts/test.sh hello_boot
```

### What it checks

- `nasm` can assemble a boot sector binary
- output image is exactly 512 bytes with boot signature
- `qemu-system-x86_64` boots the image headlessly
- boot code exits through `isa-debug-exit` on I/O port `0xF4`
- serial output contains `SecureOS boot sector OK`
- structured markers are present:
  - `TEST:START:hello_boot`
  - `TEST:PASS:hello_boot`
  - no `TEST:FAIL:hello_boot:<reason>` marker is present

### Debug-exit code mapping

Boot test binaries should write one byte to debug-exit (`out 0xF4, al`).
`run_qemu.sh` maps those semantic codes to QEMU process return codes using:

- `EXIT_PASS = 0x10` → expected QEMU return code `0x21` (`33`)
- `EXIT_FAIL = 0x11` → expected QEMU return code `0x23` (`35`)

The harness treats pass/fail based on debug-exit return mapping, not timeout behavior.

### Negative-path fixture

Run intentional failing fixture validation:

```bash
./build/scripts/test.sh hello_boot_negative
```

Expected markers for this fixture:

- `TEST:START:hello_boot_fail`
- `TEST:FAIL:hello_boot_fail:intentional-fixture`
- no pass marker

Even though the fixture exits with `EXIT_FAIL`, the harness reports success when the failure is correctly detected and prints:

```text
QEMU_PASS:hello_boot_fail
```

### Harness self-test (negative-path falsifier)

`hello_boot_negative` only proves the inverted-expectation path: a failing
fixture marked "expected to fail" results in a green wrapper exit. It does
**not** prove that the wrapper reports failures loudly when a fixture
unexpectedly fails. Without that proof, a silent-skip bug in `run_qemu.sh`
or `test.sh` could make every "green" CI run unfalsifiable
(BUILD_ROADMAP §3.4 / §8 item 9, issue #173).

Run:

```bash
./build/scripts/test.sh harness_negative
```

What it does:

1. Builds the `boot_fail.bin` fixture (same fixture as
   `hello_boot_negative`).
2. Invokes `run_qemu.sh --test hello_boot_fail` with
   `SECUREOS_FORCE_EXPECTED_STATUS=pass`, lying to the wrapper that the
   fixture should pass.
3. Asserts the wrapper exits **non-zero** and that
   `artifacts/qemu/hello_boot_fail.log` contains
   `TEST:FAIL:hello_boot_fail:`.
4. Asserts the wrapper's `hello_boot_fail.meta.json` recorded
   `expectedStatus: "pass"`, `status: "fail"`, and `markers.fail: true`.

`SECUREOS_FORCE_EXPECTED_STATUS` is a harness-internal escape hatch
introduced for this self-test only; it MUST NOT be set in routine CI runs.
The target is wired into `validate_bundle.sh`'s `TEST_TARGETS` so it cannot
be silently dropped (see #129).
