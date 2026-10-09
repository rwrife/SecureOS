/**
 * @file user/apps/cc/main.c
 * @brief In-OS `cc` driver entrypoint — real libtcc source-to-SOF compilation.
 *
 * Purpose:
 *   Implements the in-OS compiler driver for issue #767 (DEMO-03). Replaces
 *   the #540 scaffold that rejected compile requests. The driver:
 *     1. parses argv via `os_get_args` per the pinned CLI grammar v0
 *        (`docs/in-os-toolchain/cc-cli.md`),
 *     2. initialises the freestanding clib heap on `os_mem_brk` and the
 *        clib stdio backend on `os_fs_read_file_bytes`/`os_fs_write_file_bytes`,
 *     3. drives TinyCC through its public libtcc API (tcc_new,
 *        tcc_set_error_func, tcc_set_output_type, tcc_add_include_path,
 *        tcc_add_file, tcc_output_file),
 *     4. reads the raw ELF back, wraps it into an unsigned SOF container via
 *        `sofpack_wrap` (issue #521),
 *     5. resolves the output manifest through the pinned precedence helper
 *        in `manifest_resolution.c` (cli > sidecar > synth, issue #634),
 *     6. persists the SOF bytes and emits the `cc.compile.*` audit markers
 *        pinned in `docs/abi/audit-markers.md` §3.1 plus `manifest.synth.*`
 *        on the synth path (§3.2).
 *
 *   Exit codes follow the v0 contract pin in
 *   `docs/in-os-toolchain/building-apps.md` (#589):
 *     0 success, 1 usage, 2 compile error, 3 link error, 4 io error,
 *     5 arena exhaustion, 64 internal.
 *
 * Known v0 bounds (documented on PR #787):
 *   - `<sid>` in emitted markers is fixed `0`: the freestanding runtime does
 *     not yet expose the launcher/session id to apps (gap tracked under
 *     #771's acceptance-gate work).
 *   - `arena_bytes` reported in `cc.compile.start` is the pinned cc manifest
 *     budget (8 MiB); TinyCC-internal OOM currently exits through libtcc's
 *     abort path rather than the v0 slot 5.
 *   - crt1.o / libtcc1.a staging into `/apps/dev/tcc/` for booted guests is
 *     issue #768's developer-image scope.
 *
 * Interactions:
 *   - secureos_api.h: os_get_args, os_console_write, os_fs_read_file_bytes,
 *     os_fs_write_file_bytes, os_get_abi_version.
 *   - vendor/tinycc/tinycc/libtcc.h: public compiler API (linked from
 *     artifacts/user/libs/libtcc.a via build_user_app.sh's cc link slice).
 *   - user/libs/clib (freestanding libc): heap (clib_malloc_init + clib_os_brk),
 *     stdio/posix_fd backends, string/stdlib.
 *   - user/libs/sofpack: sofpack_wrap_size / sofpack_wrap.
 *   - user/apps/cc/manifest_resolution.c: manifest precedence + synthesis.
 *   - user/apps/cc/cc_sha256.c: SOF-hash helper for the success marker.
 *
 * Launched by:
 *   The kernel launcher as `/apps/dev/cc` (manifest apps/dev/cc, subject 6).
 *   Invoked from the console/shell as `cc <input.c> -o <output.bin> [-I dir]…`
 *   `[--manifest path]` / `cc --help` / `cc --version`. The raw-args tail
 *   excludes the program name, so argv[0] here is the first user argument
 *   (matching the #540 scaffold's parsing assumption).
 */

#include "secureos_api.h"

#include <libtcc.h>

#include <malloc.h>
#include <os_brk.h>
#include <posix_fd.h>
#include <sofpack/sofpack.h>
#include <stdio.h>
#include <string.h>

#include "cc_sha256.h"
#include "manifest_resolution.h"

enum {
  CC_ARGS_MAX = 512,
  CC_ARGV_MAX = 16,
  CC_PATH_MAX = 128,
  CC_INCLUDES_MAX = 8,
  /* Raw-ELF upper bound the driver reads back for SOF wrapping. Hello-class
   * output is a few KiB; the bound keeps a pathological compile from
   * exhausting the 8 MiB manifest arena silently. */
  CC_ELF_MAX = 1u << 20,
  /* Must match runtime.arena_bytes in user/apps/cc/manifest.json (#573 pin). */
  CC_ARENA_BYTES = 8388608,
  /* Canonical `sof_sha_prefix` width for manifest.synth markers. */
  CC_SHA_PREFIX_HEX = 12
};

/* v0 exit-code classes (docs/in-os-toolchain/building-apps.md, #589). */
#define CC_EXIT_OK 0
#define CC_EXIT_USAGE 1
#define CC_EXIT_COMPILE 2
#define CC_EXIT_LINK 3
#define CC_EXIT_IO 4
#define CC_EXIT_ARENA 5
#define CC_EXIT_INTERNAL 64

/* Stable reason tags for cc.compile.fail (#571 grammar). */
#define CC_REASON_USAGE "usage_error"
#define CC_REASON_COMPILE "compile_error"
#define CC_REASON_LINK "link_error"
#define CC_REASON_IO "io_error"
#define CC_REASON_ARENA "arena_exhausted"
#define CC_REASON_INTERNAL "internal_error"

/* cc app subject from its pinned manifest (apps/dev/cc, #573). The synth
 * path stamps this identity until the launcher assigns per-output
 * subjects (plumbing tracked by #771). */
#define CC_SUBJECT_ID 6u
#define CC_APP_VERSION "0.1.0"

/* Diagnostics collected through tcc_set_error_func. TinyCC formats the
 * `file:line:col` prefix itself; the driver mirrors each message to the
 * console (stderr role) and tracks the failure class for the exit code. */
typedef struct cc_diag {
  int saw_error;
  int saw_file_open_error; /* distinguishes io class from compile class */
} cc_diag_t;

static void cc_console(const char *message) {
  (void)os_console_write(message);
}

static void cc_console_line(const char *message) {
  /* TinyCC error/warning strings may or may not carry a trailing newline
   * depending on the emitting path; normalize to exactly one. */
  size_t len;

  if (message == 0) {
    return;
  }
  cc_console(message);
  len = strlen(message);
  if (len == 0u || message[len - 1u] != '\n') {
    (void)os_console_write("\n");
  }
}

static void cc_error_cb(void *opaque, const char *msg) {
  cc_diag_t *diag = (cc_diag_t *)opaque;

  if (diag != 0 && msg != 0) {
    /* tccelf/tccgen surface unreadable inputs as "could not open ..." /
     * "No such file" messages; classify those as the io-error slot. */
    if (strstr(msg, "could not open") != 0 ||
        strstr(msg, "No such file") != 0) {
      diag->saw_file_open_error = 1;
    } else {
      diag->saw_error = 1;
    }
  }
  cc_console_line(msg);
}

/* ---- clib stdio backend over the native syscall surface ---------------- */

static clib_stdio_status_t cc_backend_read(const char *path, char *buf,
                                           size_t *io_size, void *ctx) {
  size_t want;
  unsigned int out_len = 0u;
  os_status_t st;
  (void)ctx;

  if (path == 0 || buf == 0 || io_size == 0 || *io_size == 0u) {
    return CLIB_STDIO_DENIED;
  }
  want = *io_size - 1u; /* keep room for the NUL snapshot consumers expect */
  st = os_fs_read_file_bytes(path, buf, (unsigned int)want, &out_len);
  buf[out_len <= want ? out_len : want] = '\0';
  *io_size = out_len;
  return (st == OS_STATUS_OK) ? CLIB_STDIO_OK : CLIB_STDIO_ERROR;
}

static clib_stdio_status_t cc_backend_write(const char *path,
                                            const char *content, size_t size,
                                            int append, void *ctx) {
  os_status_t st;
  (void)ctx;

  if (path == 0 || content == 0) {
    return CLIB_STDIO_DENIED;
  }
  st = os_fs_write_file_bytes(path, content, (unsigned int)size, append);
  return (st == OS_STATUS_OK) ? CLIB_STDIO_OK : CLIB_STDIO_ERROR;
}

static clib_stdio_status_t cc_backend_console(const char *message, void *ctx) {
  (void)ctx;
  if (message == 0) {
    return CLIB_STDIO_DENIED;
  }
  (void)os_console_write(message);
  return CLIB_STDIO_OK;
}

static void cc_runtime_init(void) {
  clib_stdio_backend_t backend;

  (void)clib_malloc_init(0, 0u, clib_os_brk, 0);

  memset(&backend, 0, sizeof(backend));
  backend.read_file = cc_backend_read;
  backend.write_file = cc_backend_write;
  backend.console_write = cc_backend_console;
  clib_stdio_init(&backend);
}

/* ---- argument handling -------------------------------------------------- */

static int cc_is_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* Splits the raw `os_get_args` line into argv-style slots (in-place).
 * Quoting is not part of the v0 grammar; whitespace-delimited tokens only. */
static int cc_tokenize(char *args_line, char **argv, int argv_max) {
  int count = 0;
  char *cursor = args_line;

  while (*cursor != '\0') {
    while (*cursor != '\0' && cc_is_space(*cursor)) {
      ++cursor;
    }
    if (*cursor == '\0') {
      break;
    }
    if (count >= argv_max) {
      return -1;
    }
    argv[count++] = cursor;
    while (*cursor != '\0' && !cc_is_space(*cursor)) {
      ++cursor;
    }
    if (*cursor != '\0') {
      *cursor++ = '\0';
    }
  }
  return count;
}

static void cc_print_help(void) {
  cc_console("usage: cc <input.c> -o <output.bin> [-I <dir>]... "
             "[--manifest <path>]\n");
  cc_console("       cc --help\n");
  cc_console("       cc --version\n");
  cc_console("\n");
  cc_console("compiles <input.c> inside SecureOS with TinyCC and writes an\n");
  cc_console("unsigned SOF container to <output.bin>; run the result once\n");
  cc_console("after granting the unsigned-execution prompt.\n");
}

/* Byte-length of the path prefix up to (and excluding) the last '.'. */
static size_t cc_stem_len(const char *path) {
  size_t stem = strlen(path);
  size_t dot = stem;
  size_t i;

  for (i = 0; i < stem; ++i) {
    if (path[i] == '.') {
      dot = i;
    }
  }
  return dot;
}

/* Replace the output path's extension with `ext` (derives the raw-ELF
 * staging path from the SOF output path; 8.3-safe when the input is). */
static int cc_swap_ext(char *out, size_t cap, const char *path,
                       const char *ext) {
  size_t stem;

  stem = cc_stem_len(path);
  if (stem + strlen(ext) + 1u >= cap) {
    return 0;
  }
  memcpy(out, path, stem);
  out[stem] = '.';
  strcpy(out + stem + 1u, ext);
  return 1;
}

/* Derive a v0-schema-safe app id from the output path stem: lowercase,
 * sanitised, guaranteed to start with an ASCII letter. */
static void cc_derive_app_id(char *out, size_t cap, const char *output_path) {
  size_t stem = cc_stem_len(output_path);
  size_t i;
  size_t o = 0;
  char c;

  if (cap == 0u) {
    return;
  }
  for (i = 0; i < stem && o + 1u < cap; ++i) {
    c = output_path[i];
    if (c >= 'A' && c <= 'Z') {
      c = (char)(c - 'A' + 'a');
    }
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
          c == '-' || c == '.')) {
      continue;
    }
    if (o == 0u && !(c >= 'a' && c <= 'z')) {
      continue; /* schema pattern requires an alphabetic first char */
    }
    out[o++] = c;
  }
  if (o == 0u) {
    out[0] = 'c';
    out[1] = 'c';
    out[2] = 'o';
    out[3] = 'u';
    out[4] = 't';
    o = 5;
  }
  out[o] = '\0';
}

/* ---- audit markers (docs/abi/audit-markers.md §3.1/§3.2) ---------------- */

static void cc_emit_start(const char *input) {
  char line[CC_PATH_MAX + 64];
  snprintf(line, sizeof(line), "cc.compile.start:%d:%s:%u\n", 0, input,
           (unsigned int)CC_ARENA_BYTES);
  cc_console(line);
}

static void cc_emit_success(const char *input, const char *sha_hex,
                            unsigned int bytes) {
  char line[CC_PATH_MAX + 128];
  snprintf(line, sizeof(line), "cc.compile.success:%d:%s:%s:%u\n", 0, input,
           sha_hex, bytes);
  cc_console(line);
}

static void cc_emit_fail(const char *input, int exit_code,
                         const char *reason) {
  char line[CC_PATH_MAX + 64];
  snprintf(line, sizeof(line), "cc.compile.fail:%d:%s:%d:%s\n", 0, input,
           exit_code, reason);
  cc_console(line);
}

static void cc_emit_synth_marker(const char *sha_hex_64) {
  char prefix[CC_SHA_PREFIX_HEX + 1];
  char line[160];
  unsigned int i;

  for (i = 0u; i < (unsigned int)CC_SHA_PREFIX_HEX; ++i) {
    prefix[i] = sha_hex_64[i];
  }
  prefix[CC_SHA_PREFIX_HEX] = '\0';
  snprintf(line, sizeof(line), "manifest.synth.ok:%d:%s:%s:%u\n", 0, prefix,
           manifest_default_owner_kind_tag(MANIFEST_OWNER_KIND_LOCAL),
           (unsigned int)CC_ARENA_BYTES);
  cc_console(line);
}

/* ---- main driver --------------------------------------------------------- */

static int cc_compile(const char *input_path, const char *output_path,
                      const char *manifest_override, char **include_paths,
                      int include_count, const char *app_id) {
  cc_diag_t diag;
  TCCState *state;
  char elf_path[CC_PATH_MAX];
  unsigned char *elf_buf = 0;
  unsigned char *sof_buf = 0;
  size_t sof_size = 0;
  unsigned int read_len = 0u;
  char sha_hex[CC_SHA256_HEX_CHARS + 1];
  sofpack_result_t wrap_rc;
  os_status_t wr_st;
  cc_manifest_resolve_status_t mres;
  cc_manifest_resolution_t *mres_out;
  sofpack_build_params_t sparams;
  cc_manifest_resolve_params_t mparams;
  int rc;
  int exit_code = CC_EXIT_OK;

  cc_emit_start(input_path);

  memset(&diag, 0, sizeof(diag));

  state = tcc_new();
  if (state == 0) {
    cc_console("cc: compiler initialization failed (arena exhausted)\n");
    cc_emit_fail(input_path, CC_EXIT_ARENA, CC_REASON_ARENA);
    return CC_EXIT_ARENA;
  }
  tcc_set_error_func(state, &diag, cc_error_cb);
  rc = tcc_set_output_type(state, TCC_OUTPUT_EXE);
  if (rc != 0) {
    tcc_delete(state);
    cc_emit_fail(input_path, CC_EXIT_INTERNAL, CC_REASON_INTERNAL);
    return CC_EXIT_INTERNAL;
  }
  /* SecureOS native launch convention: the loader executes PT_LOAD segments
   * inside the fixed app window at 0x800000 and jumps to ELF entry as
   * `int (*)(void)` (kernel/user/launcher_exec.c app_native_entry_fn).
   * Match build_user_app.sh's own link flags for the driver itself. */
  rc = tcc_set_options(state, "-Wl,-e=main,-image-base=800000");
  if (rc != 0) {
    tcc_delete(state);
    cc_emit_fail(input_path, CC_EXIT_INTERNAL, CC_REASON_INTERNAL);
    return CC_EXIT_INTERNAL;
  }

  tcc_add_sysinclude_path(state, "/apps/dev/include");
  tcc_add_library_path(state, "/apps/dev/lib");
  tcc_add_library_path(state, "/apps/dev/tcc");
  for (rc = 0; rc < include_count; ++rc) {
    tcc_add_include_path(state, include_paths[rc]);
  }

  rc = tcc_add_file(state, input_path);
  if (rc != 0) {
    exit_code = (diag.saw_file_open_error && !diag.saw_error)
                    ? CC_EXIT_IO
                    : CC_EXIT_COMPILE;
    cc_console("cc: compilation failed\n");
    tcc_delete(state);
    cc_emit_fail(input_path, exit_code,
                 exit_code == CC_EXIT_IO ? CC_REASON_IO : CC_REASON_COMPILE);
    return exit_code;
  }

  /* TinyCC's public API emits the linked ELF through tcc_output_file only;
   * write it to a sibling `.elf` path, then wrap it into the SOF the
   * launcher consumes. The staging file is unlinked afterwards (clib unlink
   * is the v0 truncate shim; the path becomes non-material for reads). */
  if (!cc_swap_ext(elf_path, sizeof(elf_path), output_path, "elf")) {
    cc_console("cc: output path too long\n");
    tcc_delete(state);
    cc_emit_fail(input_path, CC_EXIT_USAGE, CC_REASON_USAGE);
    return CC_EXIT_USAGE;
  }

  rc = tcc_output_file(state, elf_path);
  tcc_delete(state);
  if (rc != 0) {
    exit_code = diag.saw_error ? CC_EXIT_COMPILE : CC_EXIT_LINK;
    cc_console("cc: output generation failed\n");
    cc_emit_fail(input_path, exit_code,
                 exit_code == CC_EXIT_COMPILE ? CC_REASON_COMPILE
                                              : CC_REASON_LINK);
    return exit_code;
  }

  elf_buf = (unsigned char *)clib_malloc(CC_ELF_MAX);
  if (elf_buf == 0) {
    cc_emit_fail(input_path, CC_EXIT_ARENA, CC_REASON_ARENA);
    return CC_EXIT_ARENA;
  }
  wr_st = os_fs_read_file_bytes(elf_path, elf_buf, CC_ELF_MAX - 1u, &read_len);
  if (wr_st != OS_STATUS_OK || read_len == 0u) {
    cc_console("cc: could not read staged ELF output\n");
    clib_free(elf_buf);
    cc_emit_fail(input_path, CC_EXIT_IO, CC_REASON_IO);
    return CC_EXIT_IO;
  }

  /* Manifest precedence (#634): resolve BEFORE writing the SOF so a bad
   * --manifest/sidecar produces no output binary (cc-cli grammar contract).
   * The synth branch of the resolver persists the sidecar itself. */
  mres_out = (cc_manifest_resolution_t *)clib_malloc(
      sizeof(cc_manifest_resolution_t));
  if (mres_out == 0) {
    clib_free(elf_buf);
    cc_emit_fail(input_path, CC_EXIT_ARENA, CC_REASON_ARENA);
    return CC_EXIT_ARENA;
  }
  memset(&mparams, 0, sizeof(mparams));
  mparams.output_binary_path = output_path;
  mparams.manifest_override_path = manifest_override;
  mparams.abi_version = os_get_abi_version();
  mparams.owner_kind = MANIFEST_OWNER_KIND_LOCAL;
  mparams.app_id = app_id;
  mparams.app_version = CC_APP_VERSION;
  mparams.subject_id = CC_SUBJECT_ID;

  mres = cc_manifest_resolve(&mparams, mres_out);
  if (mres != CC_MANIFEST_RESOLVE_OK) {
    cc_console("cc: manifest resolution failed\n");
    clib_free(mres_out);
    clib_free(elf_buf);
    cc_emit_fail(input_path, CC_EXIT_IO, CC_REASON_IO);
    return CC_EXIT_IO;
  }

  memset(&sparams, 0, sizeof(sparams));
  sparams.file_type = SOFPACK_TYPE_BIN;
  sparams.name = app_id;
  sparams.author = "SecureOS";
  sparams.version = CC_APP_VERSION;
  sparams.elf_payload = elf_buf;
  sparams.elf_payload_size = (size_t)read_len;

  if (sofpack_wrap_size(&sparams, &sof_size) != SOFPACK_OK) {
    clib_free(mres_out);
    clib_free(elf_buf);
    cc_emit_fail(input_path, CC_EXIT_INTERNAL, CC_REASON_INTERNAL);
    return CC_EXIT_INTERNAL;
  }
  sof_buf = (unsigned char *)clib_malloc(sof_size);
  if (sof_buf == 0) {
    clib_free(mres_out);
    clib_free(elf_buf);
    cc_emit_fail(input_path, CC_EXIT_ARENA, CC_REASON_ARENA);
    return CC_EXIT_ARENA;
  }
  wrap_rc = sofpack_wrap(&sparams, sof_buf, sof_size, &sof_size);
  if (wrap_rc != SOFPACK_OK) {
    cc_console("cc: SOF packaging failed\n");
    clib_free(sof_buf);
    clib_free(mres_out);
    clib_free(elf_buf);
    cc_emit_fail(input_path, CC_EXIT_INTERNAL, CC_REASON_INTERNAL);
    return CC_EXIT_INTERNAL;
  }

  wr_st = os_fs_write_file_bytes(output_path, sof_buf, (unsigned int)sof_size,
                                 0);
  if (wr_st != OS_STATUS_OK) {
    cc_console("cc: could not write output SOF\n");
    clib_free(sof_buf);
    clib_free(mres_out);
    clib_free(elf_buf);
    cc_emit_fail(input_path, CC_EXIT_IO, CC_REASON_IO);
    return CC_EXIT_IO;
  }
  (void)unlink(elf_path); /* best-effort staging cleanup (v0 truncate shim) */

  cc_sha256_hex(sof_buf, (size_t)sof_size, sha_hex);
  cc_emit_success(input_path, sha_hex, (unsigned int)sof_size);
  if (mres_out->audit_marker != 0) {
    cc_emit_synth_marker(sha_hex);
  }

  clib_free(sof_buf);
  clib_free(mres_out);
  clib_free(elf_buf);
  return CC_EXIT_OK;
}

int main(void) {
  char args[CC_ARGS_MAX];
  char *argv_slots[CC_ARGV_MAX];
  char *include_paths[CC_INCLUDES_MAX];
  char output_path[CC_PATH_MAX];
  char manifest_path[CC_PATH_MAX];
  char app_id[CC_PATH_MAX];
  const char *input_path = 0;
  int include_count = 0;
  int argc;
  int i;

  args[0] = '\0';
  output_path[0] = '\0';
  manifest_path[0] = '\0';

  (void)os_get_args(args, (unsigned int)sizeof(args));
  argc = cc_tokenize(args, argv_slots, CC_ARGV_MAX);
  if (argc < 0) {
    cc_console("cc: too many arguments\n");
    return CC_EXIT_USAGE;
  }

  if (argc == 0) {
    cc_print_help();
    return CC_EXIT_USAGE;
  }

  for (i = 0; i < argc; ++i) {
    const char *tok = argv_slots[i];
    if (strcmp(tok, "--help") == 0 || strcmp(tok, "-h") == 0) {
      cc_print_help();
      return CC_EXIT_OK;
    }
    if (strcmp(tok, "--version") == 0) {
      cc_console("cc 0.1.0\n");
      return CC_EXIT_OK;
    }
  }

  for (i = 0; i < argc; ++i) {
    const char *tok = argv_slots[i];
    if (strcmp(tok, "-o") == 0) {
      if (i + 1 >= argc) {
        cc_console("cc: -o requires a path\n");
        return CC_EXIT_USAGE;
      }
      ++i;
      if (strlen(argv_slots[i]) >= sizeof(output_path)) {
        cc_console("cc: output path too long\n");
        return CC_EXIT_USAGE;
      }
      strcpy(output_path, argv_slots[i]);
    } else if (strcmp(tok, "-I") == 0) {
      if (i + 1 >= argc) {
        cc_console("cc: -I requires a path\n");
        return CC_EXIT_USAGE;
      }
      ++i;
      if (include_count >= CC_INCLUDES_MAX) {
        cc_console("cc: too many -I paths\n");
        return CC_EXIT_USAGE;
      }
      include_paths[include_count++] = argv_slots[i];
    } else if (strcmp(tok, "--manifest") == 0) {
      if (i + 1 >= argc) {
        cc_console("cc: --manifest requires a path\n");
        return CC_EXIT_USAGE;
      }
      ++i;
      if (strlen(argv_slots[i]) >= sizeof(manifest_path)) {
        cc_console("cc: manifest path too long\n");
        return CC_EXIT_USAGE;
      }
      strcpy(manifest_path, argv_slots[i]);
    } else if (tok[0] == '-') {
      cc_console("cc: unknown flag\n");
      return CC_EXIT_USAGE;
    } else if (input_path != 0) {
      cc_console("cc: unexpected extra argument\n");
      return CC_EXIT_USAGE;
    } else {
      if (strlen(tok) >= CC_PATH_MAX) {
        cc_console("cc: input path too long\n");
        return CC_EXIT_USAGE;
      }
      input_path = tok;
    }
  }

  if (input_path == 0 || output_path[0] == '\0') {
    cc_print_help();
    return CC_EXIT_USAGE;
  }

  cc_runtime_init();
  cc_derive_app_id(app_id, sizeof(app_id), output_path);

  return cc_compile(input_path, output_path,
                    manifest_path[0] != '\0' ? manifest_path : 0,
                    include_paths, include_count, app_id);
}
