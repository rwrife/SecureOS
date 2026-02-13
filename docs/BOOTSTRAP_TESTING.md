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
- serial output contains `SecureOS boot sector OK`

### Expected result

The script prints QEMU serial output and ends with:

```text
PASS: boot sector smoke test
```
