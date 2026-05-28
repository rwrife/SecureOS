/**
 * @file sdk_libos_link_test.c
 * @brief M6-SDK-002 (#388) — confirm the slice-2 userland SDK pieces
 *        compile, archive, and link into a tiny C program that defines
 *        `main` and pulls in `_start` + `os_get_abi_version` from
 *        `libos.a`.
 *
 * Purpose:
 *   Slice 2 of the M6 SDK scaffold (plan #136 / BUILD_ROADMAP §5.6).
 *   The slice contract requires that:
 *
 *     1. `sdk/lib/crt0.c` produces a usable `_start` entry symbol.
 *     2. `sdk/lib/libos/version.c` keeps the SDK re-export header
 *        (`os/abi.h`) and the in-tree source-of-truth header
 *        (`secureos_abi.h`) in lockstep.
 *     3. The composed `libos.a` actually references the public
 *        runtime ABI accessor `os_get_abi_version`.
 *
 *   This host-side test enforces all three invariants by:
 *
 *     - compiling the slice-2 sources together with the existing
 *       `user/runtime/secureos_api_stubs.c` into an `libos.a`
 *       sibling archive using the host `cc`;
 *     - compiling a tiny user-program (`tests/sdk_libos_link_test_app.c`)
 *       that defines `main(argc, argv)` against the SDK include path;
 *     - linking the program against the archive with the same
 *       `-e _start` entry contract slice 2 promises external apps;
 *     - inspecting the resulting binary's symbol table for the two
 *       load-bearing symbols (`_start`, `os_get_abi_version`).
 *
 *   The toolchain used here is the host `cc` (not the freestanding
 *   `clang --target=x86_64-unknown-none-elf` the production
 *   `build_sdk_libos.sh` uses). This deliberately mirrors how
 *   `tests/sdk_abi_pin_test.c` validates SDK headers host-side: the
 *   in-container deterministic build is exercised separately by
 *   `build/scripts/build_sdk_libos.sh` in CI, while this test pins
 *   the *semantic* shape of `libos.a` everywhere a developer can run
 *   `bash` and `cc`.
 *
 * Interactions:
 *   - sdk/lib/crt0.c, sdk/lib/libos/version.c — slice-2 sources.
 *   - user/runtime/secureos_api_stubs.c — strict re-export payload.
 *   - sdk/include/os/abi.h, user/include/secureos_api.h — headers.
 *   - tests/sdk_libos_link_test_app.c — tiny `main` fixture.
 *
 * Launched by:
 *   build/scripts/test_sdk_libos_link.sh (wired into
 *   `build/scripts/test.sh sdk_libos_link`).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *reason) {
  /* `TEST:FAIL:<name>:<reason>` matches the bundle harness convention
   * used by every other host-side test in `tests/`. */
  printf("TEST:FAIL:sdk_libos_link:%s\n", reason);
  exit(1);
}

/*
 * Read the entire file at `path` into a heap buffer (size returned via
 * `*out_size`). Returns NULL on failure. Used to scan the `nm` listing
 * for the two required symbols without depending on grep semantics.
 */
static char *slurp(const char *path, size_t *out_size) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
  long n = ftell(f);
  if (n < 0) { fclose(f); return NULL; }
  if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
  char *buf = (char *)malloc((size_t)n + 1);
  if (!buf) { fclose(f); return NULL; }
  size_t got = fread(buf, 1, (size_t)n, f);
  fclose(f);
  buf[got] = '\0';
  if (out_size) *out_size = got;
  return buf;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr,
            "usage: sdk_libos_link_test <nm-dump-path>\n");
    return 2;
  }

  printf("TEST:START:sdk_libos_link\n");

  size_t n = 0;
  char *dump = slurp(argv[1], &n);
  if (!dump) {
    fail("nm_dump_missing");
  }

  /*
   * Required: `_start` must be defined (capital `T`/`t` in nm output
   * for text-segment definitions). We accept either case so the test
   * is portable across `nm` flavours.
   */
  int have_start = 0;
  int have_abi = 0;
  /* Walk lines. */
  char *line = dump;
  while (line && *line) {
    char *nl = strchr(line, '\n');
    if (nl) *nl = '\0';
    /*
     * nm output shape: `<addr-or-blank> <type> <name>`. We only care
     * that the symbol name appears AND that its type letter is not
     * `U` (undefined). A defined `_start` in any text section
     * satisfies the slice-2 contract.
     */
    if (strstr(line, " _start")) {
      /* Reject undefined references (`U _start`). */
      if (!strstr(line, " U ")) {
        have_start = 1;
      }
    }
    if (strstr(line, " os_get_abi_version")) {
      if (!strstr(line, " U ")) {
        have_abi = 1;
      }
    }
    if (!nl) break;
    line = nl + 1;
  }

  free(dump);

  if (!have_start) {
    fail("missing__start_symbol");
  }
  if (!have_abi) {
    fail("missing_os_get_abi_version_symbol");
  }

  printf("TEST:PASS:sdk_libos_link:_start:defined\n");
  printf("TEST:PASS:sdk_libos_link:os_get_abi_version:defined\n");
  printf("TEST:PASS:sdk_libos_link\n");
  return 0;
}
