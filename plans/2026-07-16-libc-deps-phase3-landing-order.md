# libc-deps Phase 3 landing order for #408

Issue: [#642](https://github.com/rwrife/SecureOS/issues/642)  
Umbrella: [#408](https://github.com/rwrife/SecureOS/issues/408)  
Inputs: `vendor/tinycc/libc-deps.json` (drift pin from [#536](https://github.com/rwrife/SecureOS/pull/536)), decomposition rollup [#640](https://github.com/rwrife/SecureOS/issues/640)

## Goal

Turn the remaining `clib_provider: null` entries in `vendor/tinycc/libc-deps.json` into an explicit, ordered queue of landable sub-slices for Phase 3.

## Snapshot (current null-provider symbols)

Current `clib_provider: null` count: **16** symbols.

### Existing issue coverage audit

All 16 symbols already have open tracking issues. No net-new symbol issue was needed in this pass.

| Symbol | TinyCC TUs | Tracking issue | SecureOS forwarding surface | Drift-gate follow-up |
|---|---|---|---|---|
| `open` | `libtcc.c`, `tccelf.c` | [#538](https://github.com/rwrife/SecureOS/issues/538) | POSIX-fd shim over `os_fs_*` | set non-null `clib_provider`; update summary counts |
| `close` | `libtcc.c` | [#538](https://github.com/rwrife/SecureOS/issues/538) | POSIX-fd shim over `os_fs_*` | same |
| `read` | `tccelf.c` | [#538](https://github.com/rwrife/SecureOS/issues/538) | POSIX-fd shim over `os_fs_*` | same |
| `lseek` | `libtcc.c`, `tccelf.c` | [#538](https://github.com/rwrife/SecureOS/issues/538) | POSIX-fd shim over `os_fs_*` | same |
| `unlink` | `tccelf.c` | [#538](https://github.com/rwrife/SecureOS/issues/538) | POSIX-fd shim over `os_fs_*` | same |
| `sprintf` | `tccasm.c`, `tccgen.c` | [#564](https://github.com/rwrife/SecureOS/issues/564) | `sprintf` shim backed by `vsnprintf`/`snprintf` | set non-null `clib_provider`; update summary counts |
| `exit` | `libtcc.c`, `tccpp.c` | [#563](https://github.com/rwrife/SecureOS/issues/563) | `exit()` shim to `os_process_exit` ([#406](https://github.com/rwrife/SecureOS/issues/406)) | set non-null `clib_provider`; update summary counts |
| `getcwd` | `tccdbg.c`, `tccelf.c` | [#539](https://github.com/rwrife/SecureOS/issues/539) | deterministic shim (`/apps/dev` launcher cwd) | set non-null `clib_provider`; update summary counts |
| `getenv` | `tccelf.c` | [#539](https://github.com/rwrife/SecureOS/issues/539) | deterministic stub (`NULL`) | same |
| `time` | `tccpp.c` | [#539](https://github.com/rwrife/SecureOS/issues/539) | deterministic constant | same |
| `localtime` | `tccpp.c` | [#539](https://github.com/rwrife/SecureOS/issues/539) | deterministic constant companion to `time` | same |
| `realloc` | `libtcc.c` | [#539](https://github.com/rwrife/SecureOS/issues/539) | plain-name alias to `clib_realloc` | same |
| `free` | `libtcc.c` | [#539](https://github.com/rwrife/SecureOS/issues/539) | plain-name alias to `clib_free` | same |
| `realpath` | `libtcc.c` | [#539](https://github.com/rwrife/SecureOS/issues/539) | canonicalization stub / passthrough | same |
| `dlopen` | `libtcc.c` | [#539](https://github.com/rwrife/SecureOS/issues/539) | JIT-path stub (`NULL`) or `#ifdef` exclusion | same |
| `dlsym` | `tccelf.c` | [#539](https://github.com/rwrife/SecureOS/issues/539) | JIT-path stub (`NULL`/`0`) or `#ifdef` exclusion | same |

## Ordered landing sequence

1. [ ] **[#564](https://github.com/rwrife/SecureOS/issues/564) — `sprintf` shim**  
   **Next-to-land:** yes (single symbol, no cross-dependencies).
2. [ ] [#563](https://github.com/rwrife/SecureOS/issues/563) — `exit()` shim to `os_process_exit`  
   Blocked on/paired with [#406](https://github.com/rwrife/SecureOS/issues/406) semantics.
3. [ ] [#538](https://github.com/rwrife/SecureOS/issues/538) — POSIX-fd nucleus (`open/close/read/lseek/unlink`)  
   Independent of #563/#564; larger diff but cleanly bounded.
4. [ ] [#539](https://github.com/rwrife/SecureOS/issues/539) — residual deterministic stubs + alias/JIT surface

## Phase-3 completion check

After each sub-slice merge, update `vendor/tinycc/libc-deps.json` and keep `build/scripts/test_tinycc_libc_deps.sh` green until:

- `summary.notYetProvided = 0`
- every currently-null symbol has a concrete `clib_provider`

At that point #408 can flip its libc-deps prerequisite from pending to satisfied.

Last verified against commit: 399b717d75ff2fa6f70e8db6865e89ec76056c1a
