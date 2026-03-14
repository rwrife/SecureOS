#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ART_DIR="$ROOT_DIR/artifacts/qemu"
QEMU_ARGS_FILE="$ROOT_DIR/build/qemu/x86_64-headless.args"
TEST_NAME=""
TIMEOUT_SECONDS="${SECUREOS_QEMU_TIMEOUT_SECONDS:-10}"

usage() {
  cat <<EOF
Usage: $(basename "$0") --test <name>

Supported tests:
  hello_boot       Uses experiments/bootloader/boot.bin as pass fixture
  hello_boot_fail  Uses experiments/bootloader/boot_fail.bin as intentional fail fixture
  kernel_console   Boots the kernel ISO and checks console markers
  kernel_filedemo  Boots the kernel ISO, runs filedemo, and checks app markers
  kernel_persistence  Boots the kernel ISO with the seeded disk and checks persisted file contents

Outputs:
  artifacts/qemu/<test>.log
  artifacts/qemu/<test>.meta.json
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --test)
      TEST_NAME="${2:-}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1"
      usage
      exit 1
      ;;
  esac
done

if [[ -z "$TEST_NAME" ]]; then
  usage
  exit 1
fi

if [[ ! -f "$QEMU_ARGS_FILE" ]]; then
  echo "Missing QEMU args file: $QEMU_ARGS_FILE"
  exit 1
fi

mkdir -p "$ART_DIR"
LOG_FILE="$ART_DIR/${TEST_NAME}.log"
META_FILE="$ART_DIR/${TEST_NAME}.meta.json"

case "$TEST_NAME" in
  hello_boot|hello_boot_fail)
    if [[ "$TEST_NAME" == "hello_boot" ]]; then
      BOOT_BIN="$ROOT_DIR/experiments/bootloader/boot.bin"
      EXPECTED_STATUS="pass"
    else
      BOOT_BIN="$ROOT_DIR/experiments/bootloader/boot_fail.bin"
      EXPECTED_STATUS="fail"
    fi

    if [[ ! -f "$BOOT_BIN" ]]; then
      echo "Missing boot binary: $BOOT_BIN"
      echo "Run build/scripts/test.sh $TEST_NAME first."
      exit 1
    fi

    set +e
    python3 - <<PY
import json, pathlib, subprocess, shlex

root = pathlib.Path(r'''$ROOT_DIR''')
args_file = pathlib.Path(r'''$QEMU_ARGS_FILE''')
log_file = pathlib.Path(r'''$LOG_FILE''')
meta_file = pathlib.Path(r'''$META_FILE''')
boot_bin = pathlib.Path(r'''$BOOT_BIN''')
expected_status = r'''$EXPECTED_STATUS'''

timeout_s = int(r'''$TIMEOUT_SECONDS''')

raw_args = [line.strip() for line in args_file.read_text().splitlines() if line.strip() and not line.strip().startswith('#')]
exit_code_map = {
    "pass": 0x10,
    "fail": 0x11,
}
expected_qemu_return = {
    name: (code << 1) | 1 for name, code in exit_code_map.items()
}

cmd = [
    "qemu-system-x86_64",
    "-drive", f"format=raw,file={boot_bin},if=floppy",
    "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
    *raw_args,
]

result = {
    "test": r'''$TEST_NAME''',
    "expectedStatus": expected_status,
    "timeoutSeconds": timeout_s,
    "command": cmd,
    "logFile": str(log_file),
    "debugExitCodeMap": exit_code_map,
    "expectedQemuReturn": expected_qemu_return,
}

try:
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout_s)
    output = (p.stdout or "") + (p.stderr or "")
    log_file.write_text(output)
    result["qemuExitCode"] = p.returncode
    result["timedOut"] = False
except subprocess.TimeoutExpired as e:
    out = (e.stdout or b"") + (e.stderr or b"")
    if isinstance(out, bytes):
        out = out.decode("utf-8", errors="replace")
    log_file.write_text(out)
    result["qemuExitCode"] = 124
    result["timedOut"] = True

log_text = log_file.read_text(errors="replace")

markers = {
    "start": f"TEST:START:{result['test']}",
    "pass": f"TEST:PASS:{result['test']}",
    "fail_prefix": f"TEST:FAIL:{result['test']}:",
}

result["detectedMessage"] = "SecureOS boot sector OK" in log_text
result["debugExitResult"] = "unknown"
if result.get("qemuExitCode") == expected_qemu_return["pass"]:
    result["debugExitResult"] = "pass"
elif result.get("qemuExitCode") == expected_qemu_return["fail"]:
    result["debugExitResult"] = "fail"

result["markers"] = {
    "start": markers["start"] in log_text,
    "pass": markers["pass"] in log_text,
    "fail": markers["fail_prefix"] in log_text,
}

if expected_status == "pass":
    result["status"] = "pass" if (
        result["debugExitResult"] == "pass"
        and result["detectedMessage"]
        and result["markers"]["start"]
        and result["markers"]["pass"]
        and not result["markers"]["fail"]
    ) else "fail"
else:
    result["status"] = "pass" if (
        result["debugExitResult"] == "fail"
        and result["markers"]["start"]
        and not result["markers"]["pass"]
        and result["markers"]["fail"]
    ) else "fail"

meta_file.write_text(json.dumps(result, indent=2) + "\n")

print(log_text, end="")
print(f"META:{meta_file}")
if result["status"] == "pass":
    print(f"QEMU_PASS:{result['test']}")
    raise SystemExit(0)

print(
    f"QEMU_FAIL:{result['test']}:expected={expected_status}:debug_exit={result['debugExitResult']}:"
    f"qemu_rc={result.get('qemuExitCode')}:message={result['detectedMessage']}:"
    f"start={result['markers']['start']}:pass={result['markers']['pass']}:"
    f"fail_marker={result['markers']['fail']}"
)
raise SystemExit(1)
PY
    EXIT_CODE=$?
    set -e
    exit $EXIT_CODE
    ;;
  kernel_console|kernel_filedemo|kernel_persistence)
    ISO_PATH="$ROOT_DIR/artifacts/kernel/secureos.iso"
    DISK_PATH="$ROOT_DIR/artifacts/disk/secureos-disk.img"

    if [[ ! -f "$ISO_PATH" ]]; then
      echo "Missing kernel ISO: $ISO_PATH"
      echo "Run build/scripts/build.sh image first."
      exit 1
    fi
    if [[ ! -f "$DISK_PATH" ]]; then
      echo "Missing disk image: $DISK_PATH"
      echo "Run build/scripts/build.sh disk first."
      exit 1
    fi

    set +e
    python3 - <<PY
import json, pathlib, subprocess, select, time, os

root = pathlib.Path(r'''$ROOT_DIR''')
args_file = pathlib.Path(r'''$QEMU_ARGS_FILE''')
log_file = pathlib.Path(r'''$LOG_FILE''')
meta_file = pathlib.Path(r'''$META_FILE''')
iso_path = pathlib.Path(r'''$ISO_PATH''')
disk_path = pathlib.Path(r'''$DISK_PATH''')
test_name = r'''$TEST_NAME'''
timeout_s = int(r'''$TIMEOUT_SECONDS''')

raw_args = [line.strip() for line in args_file.read_text().splitlines() if line.strip() and not line.strip().startswith('#')]
cmd = [
    'qemu-system-x86_64',
    '-cdrom', str(iso_path),
    '-boot', 'd',
  '-drive', f'format=raw,file={disk_path},if=ide,index=0,media=disk',
    '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04',
    *raw_args,
]

scripts = {
    'kernel_console': [
    ('secureos> ', 'help\nstorage\nexit pass\n'),
    ],
    'kernel_filedemo': [
        ('secureos> ', 'apps\nrun filedemo\ny\ny\ny\ny\nexit pass\n'),
    ],
    'kernel_persistence': [
      ('secureos> ', 'cat appdemo.txt\ny\nexit pass\n'),
    ],
}

expected_qemu_return = {'pass': 0x21, 'fail': 0x23}
expected_markers = {
    'kernel_console': [
        'TEST:START:boot_entry',
        'TEST:PASS:boot_entry',
        'TEST:START:console',
        'TEST:PASS:console',
        'SecureOS console ready',
      'commands: help, ping, echo <text>, ls, cat <file>, write <file> <text>, append <file> <text>, storage, apps, run <app>, exit <pass|fail>',
      'storage backend=',
    ],
    'kernel_filedemo': [
        'TEST:START:boot_entry',
        'TEST:PASS:console',
        '[filedemo] start',
        '[filedemo] wrote appdemo.txt',
        '[filedemo] done',
        '[auth-session] decision=allow',
    ],
    'kernel_persistence': [
      'TEST:START:boot_entry',
      'TEST:PASS:console',
      '[auth-session] operation=cat path=appdemo.txt',
      'filedemo-updated',
    ],
}

result = {
    'test': test_name,
    'timeoutSeconds': timeout_s,
    'command': cmd,
    'logFile': str(log_file),
    'isoPath': str(iso_path),
  'diskPath': str(disk_path),
}

script_steps = scripts[test_name]
next_step = 0
output = bytearray()
p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
deadline = time.time() + timeout_s
timed_out = False

while True:
    if p.poll() is not None:
      break

    remaining = deadline - time.time()
    if remaining <= 0:
      timed_out = True
      p.kill()
      break

    ready, _, _ = select.select([p.stdout], [], [], min(0.1, remaining))
    if ready:
      chunk = os.read(p.stdout.fileno(), 4096)
      if chunk:
        output.extend(chunk)
        decoded = output.decode('utf-8', errors='replace')
        if next_step < len(script_steps) and script_steps[next_step][0] in decoded:
          p.stdin.write(script_steps[next_step][1].encode('utf-8'))
          p.stdin.flush()
          next_step += 1
      continue

if p.stdout is not None:
    while True:
      chunk = os.read(p.stdout.fileno(), 4096)
      if not chunk:
        break
      output.extend(chunk)

if p.stdin is not None:
    p.stdin.close()

log_text = output.decode('utf-8', errors='replace')
log_file.write_text(log_text)

qemu_exit_code = p.returncode if p.returncode is not None else 124
if timed_out and qemu_exit_code is None:
    qemu_exit_code = 124

result['qemuExitCode'] = qemu_exit_code
result['timedOut'] = timed_out
result['markers'] = {marker: (marker in log_text) for marker in expected_markers[test_name]}
result['debugExitResult'] = 'unknown'
if qemu_exit_code == expected_qemu_return['pass']:
    result['debugExitResult'] = 'pass'
elif qemu_exit_code == expected_qemu_return['fail']:
    result['debugExitResult'] = 'fail'

result['status'] = 'pass' if result['debugExitResult'] == 'pass' and all(result['markers'].values()) else 'fail'
meta_file.write_text(json.dumps(result, indent=2) + '\n')

print(log_text, end='')
print(f'META:{meta_file}')
if result['status'] == 'pass':
    print(f'QEMU_PASS:{test_name}')
    raise SystemExit(0)

missing = [marker for marker, present in result['markers'].items() if not present]
print(f'QEMU_FAIL:{test_name}:debug_exit={result["debugExitResult"]}:qemu_rc={qemu_exit_code}:timed_out={timed_out}:missing={missing}')
raise SystemExit(1)
PY
    EXIT_CODE=$?
    set -e
    exit $EXIT_CODE
    ;;
  *)
    echo "Unknown test: $TEST_NAME"
    exit 1
    ;;
esac
