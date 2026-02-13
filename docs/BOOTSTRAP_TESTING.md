# SecureOS Bootstrap Testing

## Boot Sector Smoke Test

This repository includes a minimal x86 boot sector validation to verify local build tooling and QEMU execution.

### Run

```bash
./build/scripts/test_boot_sector.sh
```

### What it checks

- `nasm` can assemble a boot sector binary
- output image is exactly 512 bytes with boot signature
- `qemu-system-x86_64` boots the image headlessly
- boot code exits through `isa-debug-exit` on I/O port `0xF4`
- serial output contains `SecureOS boot sector OK`

### Debug-exit code mapping

Boot test binaries should write one byte to debug-exit (`out 0xF4, al`).
`run_qemu.sh` maps those semantic codes to QEMU process return codes using:

- `EXIT_PASS = 0x10` → expected QEMU return code `0x21` (`33`)
- `EXIT_FAIL = 0x11` → expected QEMU return code `0x23` (`35`)

The harness treats pass/fail based on debug-exit return mapping, not timeout behavior.

### Expected result

The script prints QEMU serial output and ends with:

```text
PASS: boot sector smoke test
```
