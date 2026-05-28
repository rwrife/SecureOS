/**
 * @file crt0.c
 * @brief M6-SDK-002 (#388) — userland C runtime entry shim for SDK apps.
 *
 * Purpose:
 *   Slice 2 of the M6 SDK scaffold (plan #136 / BUILD_ROADMAP §5.6).
 *   External apps built against the public SDK link against `libos.a`,
 *   which provides the `_start` symbol defined here. `_start` is the
 *   entry-point the loader jumps to; it must marshal whatever argv
 *   convention the runtime exposes into the standard
 *   `int main(int argc, char **argv)` C signature, invoke `main`,
 *   and then surrender control back to the OS.
 *
 *   In-tree user apps today are linked with `-e main --image-base=...`
 *   (see `build/scripts/build_user_app.sh`) — i.e. they have no real
 *   crt0 and `main` is the loader entry. This slice does NOT change
 *   that wiring for existing apps; it provides the SDK-side `_start`
 *   that external apps link in via `libos.a` so the third-party
 *   surface gets a stable, documented entry point ahead of the
 *   SDK-tool wrappers (`os-cc`, etc.) in slice `M6-SDK-003`.
 *
 *   Argument marshalling:
 *     The kernel-side ABI (see `docs/abi/syscalls.md` and
 *     `user/include/secureos_api.h`) exposes process args via
 *     `os_get_args(out_buffer, out_buffer_size)`, which today returns
 *     a single flat string. Slice 2 honours that exact contract: we
 *     fetch the raw arg string, split it on whitespace in-place into
 *     `argv[]`, and pass `(argc, argv)` to `main`. Adding a richer
 *     argv ABI (e.g. an `os_get_argv` syscall) is explicitly OUT of
 *     scope for this slice and would be a separate ABI proposal.
 *
 *   Exit handling:
 *     After `main` returns, slice 2 traps the process in a tight
 *     `hlt`/loop fallback. The kernel-side user-exit syscall is not
 *     yet wired (tracked alongside the SDK tool work in
 *     `M6-SDK-003`); when it lands, this file's `_os_exit()` helper
 *     will become a one-line forward to it without changing the
 *     public crt0 contract.
 *
 * Containment:
 *   Freestanding (`-ffreestanding -nostdlib`), no libc, no `kernel/`
 *   includes. Pulls only `os/abi.h` (SDK public re-export) and the
 *   in-tree `secureos_api.h` via the SDK include path — the same
 *   pattern slice 1's `sdk_abi_pin_test.c` uses, and the same surface
 *   that `validate_sdk_no_kernel_includes` enforces.
 *
 * Launched by:
 *   Linker entry symbol for SDK-built user binaries; never invoked
 *   directly.
 */

/*
 * The SDK header is the documented spelling for external consumers.
 * Including it (rather than `secureos_abi.h` directly) keeps slice 2
 * honest about the "external-app-only" SDK surface and exercises the
 * slice 1 re-export wiring at link time.
 */
#include "os/abi.h"

/*
 * `secureos_api.h` lives under `user/include/`; the SDK build wires
 * `-Iuser/include` for crt0 so this resolves the in-tree syscall
 * prototypes without copying them. The header is included via the
 * bare `"..."` spelling so external SDK consumers (who vendor the
 * headers into their own tree) keep working unchanged.
 */
#include "secureos_api.h"

/*
 * Forward declaration of the user-supplied entry. We deliberately use
 * the standard hosted signature `int main(int, char **)`; if a future
 * SDK app wants the no-arg form `int main(void)`, the link-time
 * weakening should happen in the SDK tool wrappers (slice
 * M6-SDK-003), not here.
 */
extern int main(int argc, char **argv);

/* Slice-2 placeholder; max argv we will marshal from os_get_args. */
#ifndef OS_CRT0_MAX_ARGV
#define OS_CRT0_MAX_ARGV 32
#endif
#ifndef OS_CRT0_ARGS_BUF_BYTES
#define OS_CRT0_ARGS_BUF_BYTES 1024
#endif

/*
 * Surrender control to the OS after `main` returns. The user-exit
 * syscall is tracked for a later slice; until it lands we trap the
 * process in `hlt` so it does not run off the end of memory. The
 * symbol is `static` to avoid leaking a new ABI name from the SDK
 * surface.
 */
static void _os_exit(int status) {
  (void)status; /* reserved for the future os_process_exit() syscall */
  for (;;) {
    /*
     * `hlt` requires CPL 0 on x86; in CPL 3 it faults and the kernel
     * will tear the process down — which is exactly the behaviour we
     * want as a safety net for a returned-from-main app today. The
     * compiler-fence form keeps the loop from being optimised away
     * and remains valid on a freestanding toolchain.
     */
    __asm__ __volatile__("hlt" : : : "memory");
  }
}

/*
 * In-place whitespace tokeniser. Mirrors the convention `os_get_args`
 * documents — a single flat string — and avoids any libc dependency.
 * Returns argc; populates argv[0..argc] (argv[argc] is set to NULL,
 * matching the C standard).
 */
static int _os_tokenise(char *buf, char **argv, int max_argv) {
  int argc = 0;
  char *p = buf;
  while (*p && argc < max_argv - 1) {
    /* Skip leading whitespace. */
    while (*p == ' ' || *p == '\t') {
      *p = '\0';
      ++p;
    }
    if (!*p) {
      break;
    }
    argv[argc++] = p;
    /* Walk to end of token. */
    while (*p && *p != ' ' && *p != '\t') {
      ++p;
    }
  }
  argv[argc] = (char *)0;
  return argc;
}

/*
 * The loader entry. Marked `__attribute__((used))` so a future LTO
 * pass cannot drop it as "unreferenced" before the linker resolves
 * the entry symbol.
 */
__attribute__((used))
void _start(void) {
  static char args_buf[OS_CRT0_ARGS_BUF_BYTES];
  static char *argv[OS_CRT0_MAX_ARGV];
  int argc = 0;
  int rc = 0;

  /*
   * Sanity-check the ABI version we were built against. This is a
   * compile-time constant from `os/abi.h`; reading it here pulls the
   * symbol into the binary so a link-time test
   * (`tests/sdk_libos_link_test.c`) can confirm the crt0 actually
   * exercises the slice-1 re-export. The value is intentionally
   * unused at runtime — the kernel-side ABI handshake is a separate
   * concern tracked in the slice-3 SDK tool work.
   */
  (void)os_get_abi_version();

  /*
   * Fetch the raw argv string. `OS_STATUS_NOT_FOUND` / `OK` with an
   * empty buffer both mean "no args" — treat them the same.
   */
  args_buf[0] = '\0';
  (void)os_get_args(args_buf, (unsigned int)sizeof(args_buf));
  /* Force NUL-termination defensively. */
  args_buf[sizeof(args_buf) - 1] = '\0';

  argc = _os_tokenise(args_buf, argv, OS_CRT0_MAX_ARGV);

  rc = main(argc, argv);
  _os_exit(rc);
}
