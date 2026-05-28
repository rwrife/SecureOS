/**
 * @file sdk_libos_link_test_app.c
 * @brief M6-SDK-002 (#388) — fixture user-program for the libos link test.
 *
 * Purpose:
 *   Tiny external-app stand-in compiled by
 *   `build/scripts/test_sdk_libos_link.sh` against the SDK headers
 *   and linked against the slice-2 `libos.a`. The test does not run
 *   the resulting binary (the freestanding crt0 cannot start under a
 *   hosted host loader); it only checks that the link succeeds and
 *   the binary exposes `_start` plus `os_get_abi_version`.
 *
 *   Keeping `main` here (instead of inline in the test driver) lets
 *   the link step compile with `-nostdlib` while the driver stays a
 *   regular hosted host program.
 *
 * Launched by:
 *   build/scripts/test_sdk_libos_link.sh (link-only).
 */

/* Pulled in via the SDK include path so the host link test exercises
 * the public surface external apps will use, not the in-tree spelling. */
#include "os/abi.h"

extern int os_get_abi_version(void);

/* Minimal signature; the test only needs the symbol to be present. */
int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  /* Force the linker to keep a reference to `os_get_abi_version`
   * so it survives `--gc-sections` and dead-strip passes. */
  return (int)os_get_abi_version() & 0;
}
