/**
 * @file include/sofpack/sofpack.h
 * @brief Userland SOF container packer (M7-TOOLCHAIN-006 sub-slice of #409).
 *
 * Purpose:
 *   Slice 6a of the in-OS toolchain (plan
 *   `plans/2026-05-28-in-os-toolchain-self-hosting.md` Phase 5). The eventual
 *   in-OS `cc` driver (#409) takes a raw x86_64 ELF emitted by libtcc
 *   (#408) and must wrap it in a `SEOS` container before writing the result
 *   to `/apps/...`. The wrap logic already lives in `kernel/format/sof.c`
 *   (`sof_build`) and `tools/sof_wrap/main.c` (host CLI shape) — both pull
 *   in kernel + crypto headers that the userland `cc` driver has no
 *   business depending on.
 *
 *   `libsofpack` is the freestanding userland-callable factoring of that
 *   logic: same on-disk bytes as `sof_build()` for the same parameters, no
 *   dependency on `kernel/...` or `kernel/crypto/...`, no host libc — just
 *   the freestanding `user/libs/clib` types we already ship on-target.
 *
 *   This is deliberately the *unsigned* path only: SOF signing is owned by
 *   `tools/sof_wrap` for build-time signed artifacts and stays out of
 *   userland (no Ed25519 / root-key surface inside an app). In-OS `cc`
 *   output is unsigned by construction; the launcher's `AUTH_TYPE_UNSIGNED_BIN`
 *   prompt (M7-TOOLCHAIN-007 / #410) is the trust gate for those binaries.
 *
 * Wire compatibility:
 *   The bytes emitted here MUST parse cleanly through `sof_parse()` in
 *   `kernel/format/sof.c` and present the same TLV ordering as
 *   `sof_build()` for any given input. The host unit test
 *   (`tests/sofpack_wrap_test.c`) pins this byte-for-byte.
 *
 * Interactions:
 *   - `user/apps/cc/main.c` (#409 driver) will call `sofpack_wrap()` after
 *     `tcc_compile()` returns an in-memory ELF.
 *   - `tests/sofpack_wrap_test.c` exercises the wrap on a synthetic ELF
 *     payload, parses the resulting bytes back, and verifies field-for-
 *     field equality with the build params plus `sof_parse()` acceptance.
 *
 * Launched by:
 *   Header-only contract; implementation in `src/sofpack.c` is linked into
 *   any userland app that needs to emit SOF containers (today: the host
 *   test; tomorrow: the `cc` driver).
 */

#ifndef SECUREOS_SOFPACK_H
#define SECUREOS_SOFPACK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Public file-type enum -------------------------------------------- */
/* Mirrors `sof_file_type_t` (kernel/format/sof.h); only BIN / LIB are
 * legal sofpack inputs. APP bundles are reserved for the future
 * `sof_parse_app_bundle` path and are intentionally not exposed here. */
typedef enum {
  SOFPACK_TYPE_BIN = 0x01,
  SOFPACK_TYPE_LIB = 0x02,
} sofpack_file_type_t;

/* ---- Result codes ------------------------------------------------------ */
/* Numbering follows the underlying `sof_result_t` arms verified by
 * `tests/sofpack_wrap_test.c`: anything sofpack rejects must also be
 * rejected by `sof_build`/`sof_parse` for the same reason, so that drift
 * in either side shows up as a test failure rather than as a silent
 * shape mismatch on disk. */
typedef enum {
  SOFPACK_OK                   = 0,
  SOFPACK_ERR_INVALID_ARG      = 1,
  SOFPACK_ERR_NO_PAYLOAD       = 2,
  SOFPACK_ERR_INVALID_TYPE     = 3,
  SOFPACK_ERR_BUFFER_TOO_SMALL = 4,
  SOFPACK_ERR_META_TOO_LONG    = 5,
} sofpack_result_t;

/* ---- Build parameters -------------------------------------------------- */
/* Optional string fields may be NULL — they are simply omitted from the
 * TLV stream, matching `sof_build`. Non-NULL strings must be NUL-
 * terminated; each is clamped to 255 bytes on wire (TLV length is a
 * single byte, identical to the kernel-side encoder). */
typedef struct {
  sofpack_file_type_t file_type;
  const char *name;
  const char *description;
  const char *author;
  const char *version;
  const char *date;
  const char *icon;       /* may be NULL */
  const char *syscall_id; /* may be NULL */
  const uint8_t *elf_payload;
  size_t elf_payload_size;
} sofpack_build_params_t;

/* ---- Public API -------------------------------------------------------- */

/**
 * Compute the exact number of bytes `sofpack_wrap()` will write for the
 * given parameters, without touching an output buffer. Returns
 * `SOFPACK_OK` and writes the size to `*out_size` on success. Returns the
 * same shape of error codes `sofpack_wrap()` would on invalid input.
 *
 * Drivers (the `cc` app) use this to size the on-disk write buffer before
 * calling `sofpack_wrap()`.
 */
sofpack_result_t sofpack_wrap_size(const sofpack_build_params_t *params,
                                   size_t *out_size);

/**
 * Wrap a raw ELF payload into an unsigned SOF container. Writes exactly
 * `*out_size` bytes into `out_buffer` (also set on success). The bytes
 * produced are byte-identical to `sof_build()` (kernel/format/sof.c) for
 * the same parameters and parse cleanly through `sof_parse()`.
 *
 * Returns:
 *   - SOFPACK_OK                   on success
 *   - SOFPACK_ERR_INVALID_ARG      params/out_buffer/out_size NULL
 *   - SOFPACK_ERR_NO_PAYLOAD       elf_payload NULL or elf_payload_size 0
 *   - SOFPACK_ERR_INVALID_TYPE     file_type not BIN/LIB
 *   - SOFPACK_ERR_BUFFER_TOO_SMALL out_buffer_size < required wrap size
 */
sofpack_result_t sofpack_wrap(const sofpack_build_params_t *params,
                              uint8_t *out_buffer,
                              size_t out_buffer_size,
                              size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_SOFPACK_H */
