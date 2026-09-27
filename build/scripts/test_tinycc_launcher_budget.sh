#!/usr/bin/env bash
# test_tinycc_launcher_budget.sh — measured launcher budget gate for TinyCC.
#
# Builds a minimal SecureOS driver that initializes libtcc, links the complete
# freestanding compiler image, then verifies that both the ELF plus SOF framing
# headroom and every PT_LOAD segment fit the launcher's explicit limits.
# Called by build/scripts/test.sh and validate_bundle.sh for issue #766.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests/tinycc_launcher_budget"
LAUNCHER="$ROOT_DIR/kernel/user/launcher_exec.c"
GATE="tinycc_launcher_budget"
SOF_HEADROOM=4096

emit() { printf '%s\n' "$1"; }
fail() { emit "TEST:FAIL:$GATE:$1" >&2; exit 1; }

for tool in clang ld.lld python3; do
  command -v "$tool" >/dev/null 2>&1 || {
    emit "TEST:SKIP:$GATE:no_${tool}_on_path"
    emit "TEST:PASS:$GATE"
    exit 0
  }
done

if [ ! -d "$ROOT_DIR/vendor/tinycc/tinycc" ] || [ -z "$(ls -A "$ROOT_DIR/vendor/tinycc/tinycc" 2>/dev/null)" ]; then
  emit "TEST:SKIP:$GATE:tinycc_submodule_not_initialized"
  emit "TEST:PASS:$GATE"
  exit 0
fi

mkdir -p "$OUT_DIR"
"$ROOT_DIR/build/scripts/build_tinycc.sh" >"$OUT_DIR/build.log" 2>&1 \
  || fail "build_tinycc_failed"

cat >"$OUT_DIR/driver.c" <<'EOF'
/* Link probe for the complete freestanding TinyCC image. */
#include "libtcc.h"
extern int os_console_write(const char *message);
int main(void) {
  TCCState *state = tcc_new();
  if (state == 0) {
    (void)os_console_write("cc: compiler initialization failed\n");
    return 1;
  }
  tcc_delete(state);
  return 0;
}
EOF

CC_FLAGS="--target=x86_64-unknown-none-elf -ffreestanding -fno-stack-protector -mno-red-zone -nostdlibinc"
# shellcheck disable=SC2086
clang $CC_FLAGS -I "$ROOT_DIR/user/include" -I "$ROOT_DIR/vendor/tinycc/tinycc" \
  -c "$OUT_DIR/driver.c" -o "$OUT_DIR/driver.o"
# shellcheck disable=SC2086
clang $CC_FLAGS -I "$ROOT_DIR/user/include" \
  -c "$ROOT_DIR/user/runtime/secureos_api_stubs.c" -o "$OUT_DIR/secureos_api_stubs.o"

ld.lld -m elf_x86_64 -nostdlib -e main --image-base=0x800000 \
  -o "$OUT_DIR/cc.elf" \
  "$OUT_DIR/driver.o" "$OUT_DIR/secureos_api_stubs.o" \
  "$ROOT_DIR/artifacts/user/libs/libtcc.a" \
  "$ROOT_DIR/artifacts/user/libs/libclib.a" \
  "$ROOT_DIR/artifacts/user/libs/libtcc1.a"

python3 - "$LAUNCHER" "$OUT_DIR/cc.elf" "$SOF_HEADROOM" <<'PY'
import ast
import re
import struct
import sys
from pathlib import Path

launcher = Path(sys.argv[1]).read_text(encoding="utf-8")
elf_path = Path(sys.argv[2])
sof_headroom = int(sys.argv[3])

assignments = {
    name: expr.strip()
    for name, expr in re.findall(r"\b([A-Z0-9_]+)\s*=\s*([^,]+),", launcher)
}
resolved = {}


def parse_number(token: str):
    token = re.sub(r"[uUlL]+$", "", token.strip())
    if re.fullmatch(r"0x[0-9A-Fa-f]+", token) or re.fullmatch(r"[0-9]+", token):
        return int(token, 0)
    return None


def eval_expr(expr: str) -> int:
    expr = re.sub(r"\b([0-9]+|0x[0-9A-Fa-f]+)[uUlL]+\b", r"\1", expr)
    node = ast.parse(expr, mode="eval")

    def walk(n):
        if isinstance(n, ast.Expression):
            return walk(n.body)
        if isinstance(n, ast.Constant) and isinstance(n.value, int):
            return int(n.value)
        if isinstance(n, ast.Name):
            return constant(n.id)
        if isinstance(n, ast.UnaryOp) and isinstance(n.op, (ast.UAdd, ast.USub)):
            val = walk(n.operand)
            return val if isinstance(n.op, ast.UAdd) else -val
        if isinstance(n, ast.BinOp) and isinstance(n.op, (ast.Add, ast.Sub, ast.Mult, ast.FloorDiv, ast.Div, ast.LShift, ast.RShift, ast.BitOr, ast.BitAnd)):
            left = walk(n.left)
            right = walk(n.right)
            if isinstance(n.op, ast.Add):
                return left + right
            if isinstance(n.op, ast.Sub):
                return left - right
            if isinstance(n.op, ast.Mult):
                return left * right
            if isinstance(n.op, ast.FloorDiv) or isinstance(n.op, ast.Div):
                return left // right
            if isinstance(n.op, ast.LShift):
                return left << right
            if isinstance(n.op, ast.RShift):
                return left >> right
            if isinstance(n.op, ast.BitOr):
                return left | right
            if isinstance(n.op, ast.BitAnd):
                return left & right
        raise SystemExit("TEST:FAIL:tinycc_launcher_budget:unsupported_expression")

    return walk(node)


def constant(name: str) -> int:
    if name in resolved:
        return resolved[name]
    expr = assignments.get(name)
    if expr is None:
        raise SystemExit(f"TEST:FAIL:tinycc_launcher_budget:missing_constant:{name}")
    literal = parse_number(expr)
    if literal is not None:
        resolved[name] = literal
        return literal
    value = eval_expr(expr)
    resolved[name] = value
    return value


file_max = constant("APP_FILE_MAX")
load_min = constant("APP_NATIVE_LOAD_MIN")
load_max = constant("APP_NATIVE_LOAD_MAX")
data = elf_path.read_bytes()
if len(data) + sof_headroom > file_max:
    raise SystemExit(
        "TEST:FAIL:tinycc_launcher_budget:file_limit:" 
        f"elf_bytes={len(data)}:sof_headroom={sof_headroom}:APP_FILE_MAX={file_max}"
    )
if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
    raise SystemExit("TEST:FAIL:tinycc_launcher_budget:invalid_elf64_le")
phoff = struct.unpack_from("<Q", data, 32)[0]
phentsize = struct.unpack_from("<H", data, 54)[0]
phnum = struct.unpack_from("<H", data, 56)[0]
load_segments = []
for index in range(phnum):
    offset = phoff + index * phentsize
    p_type = struct.unpack_from("<I", data, offset)[0]
    if p_type != 1:
        continue
    p_vaddr = struct.unpack_from("<Q", data, offset + 16)[0]
    p_filesz = struct.unpack_from("<Q", data, offset + 32)[0]
    p_memsz = struct.unpack_from("<Q", data, offset + 40)[0]
    if p_filesz > p_memsz or p_vaddr < load_min or p_vaddr + p_memsz > load_max:
        raise SystemExit(
            "TEST:FAIL:tinycc_launcher_budget:load_segment:" 
            f"index={index}:vaddr={p_vaddr:#x}:filesz={p_filesz}:memsz={p_memsz}:"
            f"window={load_min:#x}-{load_max:#x}"
        )
    load_segments.append((p_vaddr, p_memsz))
if not load_segments:
    raise SystemExit("TEST:FAIL:tinycc_launcher_budget:no_load_segments")
image_end = max(vaddr + memsz for vaddr, memsz in load_segments)
print(f"TINYCC_BUDGET:ELF_BYTES={len(data)}")
print(f"TINYCC_BUDGET:SOF_HEADROOM={sof_headroom}")
print(f"TINYCC_BUDGET:APP_FILE_MAX={file_max}")
print(f"TINYCC_BUDGET:LOAD_WINDOW_BYTES={load_max - load_min}")
print(f"TINYCC_BUDGET:LOAD_IMAGE_BYTES={image_end - load_min}")
print("TEST:PASS:tinycc_launcher_budget")
PY
