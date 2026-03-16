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
  kernel_prompt    Boots the kernel ISO interactively at secureos>
  kernel_console   Boots the kernel ISO and checks console markers
  kernel_filedemo  Boots the kernel ISO, runs filedemo, and checks app markers
  kernel_persistence  Boots the kernel ISO with the seeded disk and checks persisted file contents
  kernel_sessions  Boots the kernel ISO and verifies session switching and per-session env/cwd isolation

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
  kernel_prompt|kernel_console|kernel_filedemo|kernel_persistence|kernel_sessions)
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

    if [[ "$TEST_NAME" == "kernel_prompt" ]]; then
      mapfile -t RAW_ARGS < <(sed -e 's/\r$//' -e '/^\s*#/d' -e '/^\s*$/d' "$QEMU_ARGS_FILE")
      echo "Interactive kernel console started."
      echo "Type commands at secureos>. Use 'exit pass' to stop cleanly."
      qemu-system-x86_64 \
        -cdrom "$ISO_PATH" \
        -boot d \
        -drive "format=raw,file=$DISK_PATH,if=ide,index=0,media=disk" \
        -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
        "${RAW_ARGS[@]}"
      QEMU_EXIT=$?
      if [[ "$QEMU_EXIT" -eq 33 ]]; then
        echo "QEMU_PASS:kernel_prompt"
        exit 0
      fi
      if [[ "$QEMU_EXIT" -eq 35 ]]; then
        echo "QEMU_FAIL:kernel_prompt:debug_exit=fail"
        exit 1
      fi
      echo "QEMU_FAIL:kernel_prompt:qemu_rc=$QEMU_EXIT"
      exit "$QEMU_EXIT"
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
  ('secureos[s0]> ', 'help\nlibs\nloadlib envlib\ny\nlibs use 1\nunload 1\nlibs release 1\nlibs loaded\nunload 1\nlibs loaded\nsession new\nsession list\nsession switch 1\nstorage\nexit pass\n'),
    ],
    'kernel_filedemo': [
      ('secureos[s0]> ', 'apps\nrun /apps/filedemo\ny\ny\ny\ny\nexit pass\n'),
    ],
    'kernel_persistence': [
      ('secureos[s0]> ', 'cat appdemo.txt\ny\nexit pass\n'),
    ],
    'kernel_sessions': [
      ('secureos[s0]> ', 'env PROJECT=alpha\nenv PROJECT\nloadlib envlib\ny\nlibs use 1\nunload 1\nlibs release 1\nlibs loaded\nunload 1\nlibs loaded\nmkdir s0dir\ny\ncd s0dir\nsession new\nsession switch 1\nlibs loaded\nenv PROJECT=beta\nenv PROJECT\nloadlib envlib\ny\nlibs loaded\nmkdir s1dir\ny\ncd s1dir\nsession switch 0\nenv PROJECT\nlibs loaded\nls /\ny\nexit pass\n'),
    ],
}

expected_qemu_return = {'pass': 0x21, 'fail': 0x23}
expected_markers = {
    'kernel_console': [
        'TEST:START:boot_entry',
        'TEST:PASS:boot_entry',
      'TEST:START:session_manager',
      'TEST:PASS:session_manager',
        'TEST:START:console',
        'TEST:PASS:console',
        'SecureOS console ready',
        'commands: help, ping, echo <text>',
        'envlib.elf',
        '[lib] loaded /lib/envlib.elf',
      '[lib] use handle=1 refs=1',
      'app failed: library in use',
      '[lib] release handle=1 refs=0',
        'handle=1 path=/lib/envlib.elf',
      'owner_session=0 owner_subject=0 owner_actor=loadlib',
      '[lib] unloaded handle=1 path=/lib/envlib.elf',
      '(no loaded libraries)',
      'session created: 1',
      'session 0 (active)',
      'session 1',
      'switched to session 1',
      'secureos[s1]>',
      'storage backend=',
    ],
    'kernel_filedemo': [
        'TEST:START:boot_entry',
      'TEST:PASS:session_manager',
        'TEST:PASS:console',
        '[filedemo] start',
        '[filedemo] wrote appdemo.txt',
        '[filedemo] done',
        '[auth-session] decision=allow',
    ],
    'kernel_persistence': [
      'TEST:START:boot_entry',
      'TEST:PASS:session_manager',
      'TEST:PASS:console',
      '[auth-session] operation=cat path=/appdemo.txt',
      'filedemo-updated',
    ],
    'kernel_sessions': [
      'TEST:START:boot_entry',
      'TEST:PASS:session_manager',
      'TEST:PASS:console',
      'session created: 1',
      'switched to session 1',
      'switched to session 0',
      'secureos[s1]>',
      'secureos[s0]>',
      'alpha',
      'beta',
      '(no loaded libraries)',
      '[lib] loaded /lib/envlib.elf handle=1',
      '[lib] use handle=1 refs=1',
      'app failed: library in use',
      '[lib] release handle=1 refs=0',
      '[lib] unloaded handle=1 path=/lib/envlib.elf',
      'handle=1 path=/lib/envlib.elf',
      'owner_session=1 owner_subject=0 owner_actor=loadlib',
      's0dir/',
      's1dir/',
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
