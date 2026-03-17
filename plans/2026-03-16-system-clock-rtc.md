# Implementation Plan: System Clock & RTC

[Overview]
Add a real-time clock subsystem to SecureOS with CMOS RTC hardware driver, HAL abstraction, kernel clock service, user-space commands, and QEMU host-time passthrough.

SecureOS currently has no concept of time. This plan introduces a complete clock subsystem spanning all layers of the OS:

1. **Hardware layer** — An x86 CMOS RTC driver that reads the MC146818-compatible real-time clock present in all PC-compatible systems (and emulated by QEMU). This chip provides BCD-encoded date/time registers at I/O ports 0x70/0x71. The driver also reads the CMOS century register (0x32) for Y2K correctness.

2. **HAL layer** — A `clock_hal` abstraction following the existing `storage_hal` function-pointer vtable pattern, allowing future architectures (ARM, RISC-V) to register their own clock backends without changing kernel code.

3. **Kernel clock service** — A `clock_service` module that sits above the HAL and provides formatted time output, time setting, and a software-maintained offset for manual time adjustments. Reading time always goes through the HAL to the hardware; setting time applies a signed offset stored in memory (no writes to CMOS, which avoids complexity and is safer for virtualized environments).

4. **Capability gating** — Reading time is ungated (any process can read the clock). Setting time requires a new `CAP_CLOCK_SET` capability, following the existing gate pattern.

5. **Syscall API** — Two new syscall stubs: `os_clock_get` and `os_clock_set`, exposed through `secureos_api.h` and implemented in `secureos_api_stubs.c`.

6. **User-space commands** — A `date` command for reading/setting the date and time, and a `time` command for displaying just the current time. Both follow the existing `user/apps/os/<cmd>/main.c` pattern.

7. **QEMU passthrough** — Adding `-rtc base=utc,clock=host` to both QEMU args files ensures the emulated CMOS RTC reflects the host machine's wall clock.

8. **Console built-in integration** — The `date` and `time` commands are implemented as user-space apps (not console built-ins), following the existing pattern where `console_handle_command` falls through to `console_command_run()` for app dispatch. However, the kernel clock service is called from `process.c` to service the syscalls.

9. **Tests** — A dedicated test for the clock service verifying RTC read, time formatting, offset application, and capability gating of set operations.

[Types]
New type definitions for time representation, clock HAL backend, and clock service.

### `kernel/drivers/clock/cmos_rtc.h` — CMOS RTC raw time structure

```c
/* Raw BCD-decoded time from CMOS RTC registers */
typedef struct {
    uint8_t second;   /* 0-59 */
    uint8_t minute;   /* 0-59 */
    uint8_t hour;     /* 0-23 (24-hour mode) */
    uint8_t day;      /* 1-31 */
    uint8_t month;    /* 1-12 */
    uint16_t year;    /* 4-digit year (century-aware) */
    uint8_t weekday;  /* 1-7 (Sunday=1, per CMOS convention) */
} cmos_time_t;
```

### `kernel/hal/clock_hal.h` — HAL types

```c
/* Unified time structure used across the HAL boundary */
typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  weekday;  /* 1=Sun, 2=Mon, ..., 7=Sat */
} hal_time_t;

typedef enum {
    CLOCK_OK = 0,
    CLOCK_ERR_NOT_READY,
    CLOCK_ERR_INVALID,
    CLOCK_ERR_BUSY
} clock_result_t;

/* Clock backend identifier */
typedef enum {
    CLOCK_BACKEND_NONE = 0,
    CLOCK_BACKEND_CMOS_RTC
} clock_backend_t;

/* Function pointer types for HAL vtable */
typedef clock_result_t (*clock_read_fn_t)(hal_time_t *out);

/* Clock device descriptor (vtable pattern matching storage_hal) */
typedef struct {
    clock_backend_t backend;
    const char     *backend_name;
    clock_read_fn_t read_time;
} clock_device_t;
```

### `kernel/clock/clock_service.h` — Clock service types

```c
/* Formatted time output buffer sizes */
#define CLOCK_FMT_DATETIME_SIZE  20  /* "YYYY-MM-DD HH:MM:SS\0" */
#define CLOCK_FMT_TIME_SIZE       9  /* "HH:MM:SS\0" */
#define CLOCK_FMT_DATE_SIZE      11  /* "YYYY-MM-DD\0" */
#define CLOCK_FMT_DAY_SIZE       10  /* "Wednesday\0" */

/* Time offset for software-based set (seconds from epoch-like base) */
typedef struct {
    int32_t offset_seconds;  /* Signed offset applied to hardware time */
} clock_offset_t;
```

### `kernel/cap/capability.h` — New capability ID

```c
CAP_CLOCK_SET = 11   /* Permission to modify system time */
```

### `user/include/secureos_api.h` — New syscall signatures

```c
os_status_t os_clock_get(char *buf, unsigned int buf_size);
os_status_t os_clock_set(const char *datetime_str);
```

[Files]
New files to create and existing files to modify for the clock subsystem.

### New Files

| Path | Purpose |
|------|---------|
| `kernel/drivers/clock/cmos_rtc.c` | x86 CMOS RTC hardware driver — reads time from I/O ports 0x70/0x71 |
| `kernel/drivers/clock/cmos_rtc.h` | CMOS RTC driver header — exposes `cmos_rtc_init()` and `cmos_rtc_is_available()` |
| `kernel/hal/clock_hal.c` | Clock HAL implementation — register/read pattern matching `storage_hal.c` |
| `kernel/hal/clock_hal.h` | Clock HAL header — `hal_time_t`, `clock_device_t`, public API |
| `kernel/clock/clock_service.c` | Kernel clock service — formatted read, offset-based set, capability-gated set |
| `kernel/clock/clock_service.h` | Clock service header — formatting functions, init, get, set |
| `user/apps/os/date/main.c` | User-space `date` command — read or set date/time |
| `user/apps/os/time/main.c` | User-space `time` command — display current time |
| `user/apps/os/help/resources/date.txt` | Help resource for `date` command |
| `user/apps/os/help/resources/time.txt` | Help resource for `time` command |
| `tests/clock_service_test.c` | Unit test for clock service |
| `build/scripts/test_clock_service.sh` | Test runner script for clock tests |

### Existing Files to Modify

| Path | Changes |
|------|---------|
| `kernel/core/kmain.c` | Add `#include` for `clock_hal.h`, `clock_service.h`, `cmos_rtc.h`; add `cmos_rtc_init()` and `clock_service_init()` calls in boot sequence after disk init; grant `CAP_CLOCK_SET` to subject 0 (kernel) in capability bootstrap section |
| `kernel/cap/capability.h` | Add `CAP_CLOCK_SET = 11` to `capability_id_t` enum; update `CAP_COUNT` to 12 |
| `user/include/secureos_api.h` | Add `os_clock_get()` and `os_clock_set()` declarations |
| `user/runtime/secureos_api_stubs.c` | Add stub implementations for `os_clock_get()` and `os_clock_set()` |
| `kernel/user/process.c` | Add built-in handler for `clock_get` and `clock_set` syscalls in the script interpreter dispatch |
| `build/scripts/build_kernel_entry.sh` | Add compile lines for `cmos_rtc.c`, `clock_hal.c`, `clock_service.c`; add `.o` files to linker command |
| `build/scripts/build_kernel_entry.ps1` | Mirror the same additions as `build_kernel_entry.sh` |
| `build/qemu/x86_64-graphical.args` | Add `-rtc` and `base=utc,clock=host` lines |
| `build/qemu/x86_64-headless.args` | Add `-rtc` and `base=utc,clock=host` lines |

[Functions]
Function signatures and purposes for all new and modified functions.

### New Functions

**`kernel/drivers/clock/cmos_rtc.c`**

| Function | Signature | Purpose |
|----------|-----------|---------|
| `cmos_rtc_init` | `void cmos_rtc_init(void)` | Read CMOS status registers to detect 24h/BCD mode, register with `clock_hal`, and log init to serial |
| `cmos_rtc_is_available` | `int cmos_rtc_is_available(void)` | Return 1 if CMOS RTC was successfully initialized |
| `cmos_rtc_read_time` (static) | `clock_result_t cmos_rtc_read_time(hal_time_t *out)` | Read all CMOS registers (0x00-0x09, 0x32), wait for update-not-in-progress, BCD-to-binary convert, populate `hal_time_t` |
| `cmos_read_register` (static) | `uint8_t cmos_read_register(uint8_t reg)` | Write register index to port 0x70, read value from port 0x71 |
| `cmos_is_updating` (static) | `int cmos_is_updating(void)` | Check bit 7 of status register A (0x0A) to see if RTC update is in progress |
| `bcd_to_binary` (static) | `uint8_t bcd_to_binary(uint8_t bcd)` | Convert BCD-encoded byte to binary |

**`kernel/hal/clock_hal.c`**

| Function | Signature | Purpose |
|----------|-----------|---------|
| `clock_hal_register` | `clock_result_t clock_hal_register(const clock_device_t *dev)` | Store clock device descriptor; fail if already registered |
| `clock_hal_read_time` | `clock_result_t clock_hal_read_time(hal_time_t *out)` | Delegate to registered backend's `read_time` function pointer |
| `clock_hal_is_ready` | `int clock_hal_is_ready(void)` | Return 1 if a clock backend is registered |
| `clock_hal_get_backend_name` | `const char *clock_hal_get_backend_name(void)` | Return registered backend name string or `"none"` |

**`kernel/clock/clock_service.c`**

| Function | Signature | Purpose |
|----------|-----------|---------|
| `clock_service_init` | `void clock_service_init(void)` | Zero the time offset; log init to serial |
| `clock_service_get_time` | `clock_result_t clock_service_get_time(hal_time_t *out)` | Read time from HAL, apply software offset, return adjusted time |
| `clock_service_set_time` | `clock_result_t clock_service_set_time(uint8_t subject_id, const hal_time_t *new_time)` | Capability-check `CAP_CLOCK_SET` for `subject_id`; compute offset between current hardware time and requested time; store offset |
| `clock_service_format_datetime` | `void clock_service_format_datetime(const hal_time_t *t, char *buf, unsigned int buf_size)` | Format as `"YYYY-MM-DD HH:MM:SS"` |
| `clock_service_format_time` | `void clock_service_format_time(const hal_time_t *t, char *buf, unsigned int buf_size)` | Format as `"HH:MM:SS"` |
| `clock_service_format_date` | `void clock_service_format_date(const hal_time_t *t, char *buf, unsigned int buf_size)` | Format as `"YYYY-MM-DD"` |
| `clock_service_format_day_name` | `void clock_service_format_day_name(const hal_time_t *t, char *buf, unsigned int buf_size)` | Convert weekday number to name string (e.g., `"Monday"`) |
| `time_to_epoch_approx` (static) | `int32_t time_to_epoch_approx(const hal_time_t *t)` | Approximate seconds-since-2000 for offset computation (not a full POSIX epoch — just enough for offset deltas) |
| `epoch_approx_to_time` (static) | `void epoch_approx_to_time(int32_t epoch, hal_time_t *out)` | Reverse of above — convert seconds-since-2000 back to `hal_time_t` fields |

**`kernel/user/process.c` — new built-in syscall handlers**

| Function | Signature | Purpose |
|----------|-----------|---------|
| `builtin_clock_get` (static) | `int builtin_clock_get(process_context_t *ctx)` | Call `clock_service_get_time`, format as datetime string, write to process output buffer |
| `builtin_clock_set` (static) | `int builtin_clock_set(process_context_t *ctx, const char *datetime_str)` | Parse `"YYYY-MM-DD HH:MM:SS"` string into `hal_time_t`, call `clock_service_set_time` with process subject_id |

**`user/apps/os/date/main.c`**

| Function | Signature | Purpose |
|----------|-----------|---------|
| `main` | `int main(void)` | If no args: call `os_clock_get` and print. If args start with `set`: call `os_clock_set` with remaining args. If args are `--help` or `-h`: print usage. |

**`user/apps/os/time/main.c`**

| Function | Signature | Purpose |
|----------|-----------|---------|
| `main` | `int main(void)` | Call `os_clock_get`, extract time portion (chars 11-18 of `"YYYY-MM-DD HH:MM:SS"`), print it |

### Modified Functions

**`kernel/core/kmain.c` → `kmain()`**
- Add calls to `cmos_rtc_init()` and `clock_service_init()` in the initialization sequence, after disk init and before `fs_service_init()`
- Add `cap_grant_for_tests(0, CAP_CLOCK_SET)` in the capability bootstrap block

**`kernel/user/process.c` → script interpreter dispatch**
- Add cases for `os_clock_get` and `os_clock_set` in the syscall dispatch switch/if-chain within the script interpreter function that handles built-in API calls

[Classes]
No classes are used in this C codebase. All abstractions use structs with function pointers (vtable pattern).

The key struct types introduced are `cmos_time_t`, `hal_time_t`, `clock_device_t`, and `clock_offset_t` — all documented in the [Types] section above.

[Dependencies]
No external dependencies are required. All code is freestanding C targeting `i386-unknown-none-elf`.

The implementation uses only:
- x86 I/O port instructions (`inb`/`outb`) for CMOS register access — these will be implemented as inline assembly helpers in `cmos_rtc.c` (matching the pattern used in `serial.c` for COM1 port access)
- Existing kernel headers: `capability.h`, `cap_table.h`, `serial.h` (for debug logging)
- No libc, no external libraries

QEMU dependency: The `-rtc base=utc,clock=host` flag requires QEMU 2.0+ (already satisfied by any modern QEMU installation). By default QEMU already uses `base=utc,clock=host` but making it explicit ensures consistent behavior across all QEMU versions and configurations.

[Testing]
A dedicated test file validates the clock service layer, capability gating, and format output.

### New Test File: `tests/clock_service_test.c`

**Test cases:**

1. **`test_clock_read_returns_valid_time`** — Call `clock_service_get_time()`, verify all fields are in valid ranges (month 1-12, day 1-31, hour 0-23, minute 0-59, second 0-59, year >= 2024)

2. **`test_clock_format_datetime`** — Construct a known `hal_time_t`, call `clock_service_format_datetime()`, verify output matches expected `"YYYY-MM-DD HH:MM:SS"` string

3. **`test_clock_format_time`** — Same as above but for `clock_service_format_time()` → `"HH:MM:SS"`

4. **`test_clock_format_date`** — Same for `clock_service_format_date()` → `"YYYY-MM-DD"`

5. **`test_clock_format_day_name`** — Verify each weekday number maps to correct name string

6. **`test_clock_set_with_capability`** — Grant `CAP_CLOCK_SET` to test subject, call `clock_service_set_time()`, verify it returns `CLOCK_OK`, then verify `clock_service_get_time()` returns adjusted time

7. **`test_clock_set_without_capability`** — Do NOT grant `CAP_CLOCK_SET`, call `clock_service_set_time()`, verify it returns an error (denied)

8. **`test_clock_hal_not_ready`** — Before registering any backend, call `clock_hal_read_time()`, verify it returns `CLOCK_ERR_NOT_READY`

### Test Runner: `build/scripts/test_clock_service.sh`

Follows the existing pattern (e.g., `test_fs_service.sh`):
- Compile `tests/clock_service_test.c` along with `clock_service.c`, `clock_hal.c`, capability modules, and a mock CMOS RTC backend
- For host-side testing, the mock backend returns fixed known values (since we can't access real CMOS ports in a user-space test binary)
- The test itself compiles as a freestanding kernel test binary run in QEMU, following the same `TEST:START`/`TEST:PASS`/`TEST:FAIL` serial marker protocol
- Run the test binary, check for `TEST:PASS` markers on serial output

### Existing Test Modifications

None — existing tests are unaffected since we're only adding new capability IDs (existing IDs don't change) and new modules.

[Implementation Order]
Sequential implementation steps to minimize integration risk and ensure each layer is testable before building upon it.

1. **QEMU args update** — Add `-rtc base=utc,clock=host` to both `build/qemu/x86_64-graphical.args` and `build/qemu/x86_64-headless.args`. This is a zero-risk change that ensures the emulated RTC reflects host time.

2. **Capability ID** — Add `CAP_CLOCK_SET = 11` to `kernel/cap/capability.h` and update `CAP_COUNT`. This unblocks all capability-dependent code.

3. **CMOS RTC driver** — Create `kernel/drivers/clock/cmos_rtc.c` and `cmos_rtc.h`. Implement I/O port access, BCD conversion, update-in-progress waiting, and the `clock_read_fn_t`-compatible read function. This is self-contained and testable in QEMU.

4. **Clock HAL** — Create `kernel/hal/clock_hal.c` and `clock_hal.h`. Implement register/read/query functions following the `storage_hal` pattern. The CMOS driver registers itself here.

5. **Clock service** — Create `kernel/clock/clock_service.c` and `clock_service.h`. Implement `init`, `get_time`, `set_time` (with capability check), and all format functions. The offset math (`time_to_epoch_approx` / `epoch_approx_to_time`) lives here.

6. **Build system** — Update `build/scripts/build_kernel_entry.sh` and `build_kernel_entry.ps1` to compile the three new `.c` files and link the `.o` files into `kernel.elf`.

7. **Kernel init** — Update `kernel/core/kmain.c` to call `cmos_rtc_init()`, `clock_service_init()`, and grant `CAP_CLOCK_SET` to subject 0.

8. **Syscall API** — Add `os_clock_get` and `os_clock_set` to `user/include/secureos_api.h` and stub implementations in `user/runtime/secureos_api_stubs.c`.

9. **Process syscall dispatch** — Add built-in handlers in `kernel/user/process.c` for `clock_get` and `clock_set` syscalls, calling through to `clock_service`.

10. **User-space `date` command** — Create `user/apps/os/date/main.c` with read/set functionality and help resource `user/apps/os/help/resources/date.txt`.

11. **User-space `time` command** — Create `user/apps/os/time/main.c` with time-only display and help resource `user/apps/os/help/resources/time.txt`.

12. **Test infrastructure** — Create `tests/clock_service_test.c` and `build/scripts/test_clock_service.sh`. Verify all test cases pass.

13. **Integration verification** — Build the full kernel image and boot in QEMU. Verify `date` and `time` commands produce correct output reflecting the host's clock. Verify `date set` works with proper capability and is denied without it.