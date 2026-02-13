#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EXPERIMENT_DIR="$ROOT_DIR/experiments/bootloader"
BOOT_ASM="$EXPERIMENT_DIR/boot.asm"
BOOT_BIN="$EXPERIMENT_DIR/boot.bin"

mkdir -p "$EXPERIMENT_DIR"

if [[ ! -f "$BOOT_ASM" ]]; then
  cat > "$BOOT_ASM" <<'EOF'
[org 0x7C00]
[bits 16]

%define COM1 0x3F8

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    call serial_init

    mov si, msg
.print:
    lodsb
    test al, al
    jz .done
    call serial_out
    jmp .print

.done:
    cli
.hang:
    hlt
    jmp .hang

serial_init:
    mov dx, COM1 + 1
    mov al, 0x00
    out dx, al

    mov dx, COM1 + 3
    mov al, 0x80
    out dx, al

    mov dx, COM1 + 0
    mov al, 0x03
    out dx, al
    mov dx, COM1 + 1
    mov al, 0x00
    out dx, al

    mov dx, COM1 + 3
    mov al, 0x03
    out dx, al

    mov dx, COM1 + 2
    mov al, 0xC7
    out dx, al

    mov dx, COM1 + 4
    mov al, 0x0B
    out dx, al
    ret

serial_out:
    push ax
.wait:
    mov dx, COM1 + 5
    in al, dx
    test al, 0x20
    jz .wait

    pop ax
    mov dx, COM1
    out dx, al
    ret

msg db 'SecureOS boot sector OK', 0x0D, 0x0A, 0

times 510 - ($ - $$) db 0
dw 0xAA55
EOF
fi

command -v nasm >/dev/null 2>&1 || { echo "nasm is required"; exit 1; }
command -v qemu-system-x86_64 >/dev/null 2>&1 || { echo "qemu-system-x86_64 is required"; exit 1; }

nasm -f bin "$BOOT_ASM" -o "$BOOT_BIN"

if [[ ! -f "$BOOT_BIN" ]]; then
  echo "Failed to build boot sector"
  exit 1
fi

BOOT_SIZE=$(wc -c < "$BOOT_BIN" | tr -d '[:space:]')
if [[ "$BOOT_SIZE" -ne 512 ]]; then
  echo "Boot sector size is not 512 bytes (got $BOOT_SIZE)"
  exit 1
fi

set +e
QEMU_OUTPUT=$(python3 - <<PY
import subprocess
cmd = [
    "qemu-system-x86_64",
    "-drive", f"format=raw,file={r'''$BOOT_BIN'''},if=floppy",
    "-nographic", "-serial", "stdio", "-monitor", "none",
]
try:
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=8)
    print(p.stdout + p.stderr, end="")
    raise SystemExit(p.returncode)
except subprocess.TimeoutExpired as e:
    out = (e.stdout or b"") + (e.stderr or b"")
    if isinstance(out, bytes):
        out = out.decode("utf-8", errors="replace")
    print(out, end="")
    raise SystemExit(124)
PY
)
QEMU_EXIT=$?
set -e

echo "$QEMU_OUTPUT"

if [[ $QEMU_EXIT -ne 0 && $QEMU_EXIT -ne 124 ]]; then
  echo "QEMU exited unexpectedly with code $QEMU_EXIT"
  exit 1
fi

if ! grep -q "SecureOS boot sector OK" <<<"$QEMU_OUTPUT"; then
  echo "Expected boot message not found"
  exit 1
fi

echo "PASS: boot sector smoke test"
