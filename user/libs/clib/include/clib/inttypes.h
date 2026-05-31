/**
 * @file include/clib/inttypes.h
 * @brief Freestanding <inttypes.h> format-string nucleus for
 *        user/libs/clib (M7-TOOLCHAIN-004 slice 11, issue #407).
 *
 * Purpose:
 *   Slice 11 of M7-TOOLCHAIN-004 (`user/libs/clib` freestanding libc
 *   subset, plan `plans/2026-05-28-in-os-toolchain-self-hosting.md`
 *   P3). C11 §7.8 specifies `<inttypes.h>` as a layer on top of
 *   `<stdint.h>` containing:
 *
 *     1. `PRI*` / `SCN*` printf/scanf format-string macros for every
 *        exact-width / least-width / fast-width / pointer-width /
 *        max-width integer typedef.
 *     2. `imaxabs` / `imaxdiv` / `imaxdiv_t`.
 *     3. `strtoimax` / `strtoumax` (and wide-char counterparts).
 *
 *   This slice ships piece (1) only — the format-string macros — for
 *   three reasons:
 *
 *   - The macros are pure preprocessor (constant string literals); they
 *     have no `<stdio.h>` dependency at the language level. A consumer
 *     writes `printf("x = %" PRId32 "\n", x);` and the resulting format
 *     literal is whatever `<stdio.h>` slice (#447, PR #453) ships.
 *   - TinyCC (#408) and any non-trivial C source TinyCC compiles parses
 *     these macros pervasively (clang, glibc, musl, and the Linux
 *     kernel sources all rely on them in their own headers). Pinning
 *     them now removes a known TinyCC-port headache without taking on
 *     any link-surface risk.
 *   - `imaxabs` / `imaxdiv` / `strtoimax` / `strtoumax` are intentionally
 *     deferred to a follow-on slice. They have non-trivial bodies that
 *     belong with their `<stdlib.h>` peers (`abs` / `div` / `strtol`),
 *     and `strtoimax` in particular wants to share the overflow-clamp
 *     plumbing #458 (PR #458) just landed for `strtol{,l}`. Filing them
 *     as a separate slice keeps each PR reviewable and avoids touching
 *     `src/stdlib.c` in a header-only slice.
 *
 *   Reservation: the unit test's `symbol_set_pinned` sub-marker
 *   explicitly enumerates the macros shipped here so a future slice
 *   that adds the function family must add the matching test pins
 *   (no silent drift).
 *
 * Containment:
 *   - Freestanding. No libc, no kernel includes, no syscalls.
 *   - The format-string macros are derived from `__*_FMT*__` compiler
 *     builtins where available (Clang exposes `__INT64_FMTd__` and
 *     friends since 3.0; GCC since 4.4). When the builtin is absent
 *     we fall back to a target-shape mapping driven by the matching
 *     `__INTn_TYPE__` / `__UINTn_TYPE__` builtins from <stdint.h>
 *     (slice 10, PR #437) so the header stays target-correct on
 *     both ILP32 and LP64 — matching the discipline the existing
 *     <stdint.h> nucleus uses for typedefs.
 *
 * Symbol coverage (slice 11):
 *   - PRI*: PRId{8,16,32,64,MAX,PTR}, PRIi{...}, PRIu{...}, PRIo{...},
 *           PRIx{...}, PRIX{...}, plus the LEAST / FAST family rows
 *           that ride on top of slice 10b's `int_least*_t` /
 *           `int_fast*_t` typedefs (PR #457).
 *   - SCN*: SCNd{...}, SCNi{...}, SCNu{...}, SCNo{...}, SCNx{...}
 *           (uppercase X is intentionally omitted — C11 §7.8.1¶3
 *           does not define `SCNX*`).
 *
 * Out of scope for this slice:
 *   - `imaxabs` / `imaxdiv` / `imaxdiv_t` (function family, lives
 *     next to `abs`/`div` in `src/stdlib.c`).
 *   - `strtoimax` / `strtoumax` (function family, wants to share
 *     `strtol{,l}`'s overflow-clamp + ERANGE plumbing landed by #458).
 *   - `wcstoimax` / `wcstoumax` and any other wide-character surface
 *     (no `<wchar.h>` slice today).
 *
 * Naming:
 *   Canonical C11 names so TinyCC and other consumers parse for free.
 *
 * ABI status:
 *   Userland-only. Does **not** bump `OS_ABI_VERSION` (parity with
 *   the prior #407 slices: `<limits.h>`, `<stddef.h>`, `<stdarg.h>`,
 *   `<stdbool.h>`, `<stdint.h>`, `<stdalign.h>`, `<float.h>`,
 *   `<errno.h>`, and the `stdlib` slice).
 *
 * Issue: #407. Refs umbrella #403. Plan: P3 in
 * `plans/2026-05-28-in-os-toolchain-self-hosting.md`.
 */

#ifndef SECUREOS_USER_LIBS_CLIB_INTTYPES_H
#define SECUREOS_USER_LIBS_CLIB_INTTYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stdint.h"

/* ---- length-modifier resolution -------------------------------------- *
 *
 * The PRI* / SCN* macros are concatenations of:
 *   "<length-modifier><conversion>"
 *
 * For each width N we need the modifier that prints / scans an
 * `intN_t` correctly under both ILP32 and LP64. C11 §7.8.1 leaves the
 * exact spelling implementation-defined (because `int32_t` may be
 * `int` on ILP32 but stays `int` on LP64, while `int64_t` is `long`
 * on LP64 and `long long` on ILP32). The compiler exposes the
 * canonical modifier via `__*_FMT*__` predefines on Clang and GCC.
 *
 * Defining wrappers locally (rather than dumping the builtins straight
 * into the macro bodies) gives one hop of indirection that lets the
 * fallback path stay readable on a freestanding cross toolchain that
 * opts out of those predefines.
 */

#if defined(__INT8_FMTd__)
#  define CLIB_INTTYPES_FMTd8   __INT8_FMTd__
#  define CLIB_INTTYPES_FMTi8   __INT8_FMTi__
#  define CLIB_INTTYPES_FMTu8   __UINT8_FMTu__
#  define CLIB_INTTYPES_FMTo8   __UINT8_FMTo__
#  define CLIB_INTTYPES_FMTx8   __UINT8_FMTx__
#  define CLIB_INTTYPES_FMTX8   __UINT8_FMTX__
#else
/* int8_t is `signed char` on every target the project ships against;
 * promotes to int through default argument promotion, so plain "d"
 * is correct. Same shape as glibc's <inttypes.h> on ILP32 and LP64. */
#  define CLIB_INTTYPES_FMTd8   "d"
#  define CLIB_INTTYPES_FMTi8   "i"
#  define CLIB_INTTYPES_FMTu8   "u"
#  define CLIB_INTTYPES_FMTo8   "o"
#  define CLIB_INTTYPES_FMTx8   "x"
#  define CLIB_INTTYPES_FMTX8   "X"
#endif

#if defined(__INT16_FMTd__)
#  define CLIB_INTTYPES_FMTd16  __INT16_FMTd__
#  define CLIB_INTTYPES_FMTi16  __INT16_FMTi__
#  define CLIB_INTTYPES_FMTu16  __UINT16_FMTu__
#  define CLIB_INTTYPES_FMTo16  __UINT16_FMTo__
#  define CLIB_INTTYPES_FMTx16  __UINT16_FMTx__
#  define CLIB_INTTYPES_FMTX16  __UINT16_FMTX__
#else
/* int16_t is `short`; promotes to int through default argument
 * promotion, so plain "d" is correct. */
#  define CLIB_INTTYPES_FMTd16  "d"
#  define CLIB_INTTYPES_FMTi16  "i"
#  define CLIB_INTTYPES_FMTu16  "u"
#  define CLIB_INTTYPES_FMTo16  "o"
#  define CLIB_INTTYPES_FMTx16  "x"
#  define CLIB_INTTYPES_FMTX16  "X"
#endif

#if defined(__INT32_FMTd__)
#  define CLIB_INTTYPES_FMTd32  __INT32_FMTd__
#  define CLIB_INTTYPES_FMTi32  __INT32_FMTi__
#  define CLIB_INTTYPES_FMTu32  __UINT32_FMTu__
#  define CLIB_INTTYPES_FMTo32  __UINT32_FMTo__
#  define CLIB_INTTYPES_FMTx32  __UINT32_FMTx__
#  define CLIB_INTTYPES_FMTX32  __UINT32_FMTX__
#else
/* int32_t is `int` on every target the project ships against. */
#  define CLIB_INTTYPES_FMTd32  "d"
#  define CLIB_INTTYPES_FMTi32  "i"
#  define CLIB_INTTYPES_FMTu32  "u"
#  define CLIB_INTTYPES_FMTo32  "o"
#  define CLIB_INTTYPES_FMTx32  "x"
#  define CLIB_INTTYPES_FMTX32  "X"
#endif

/* For 64-bit / max / ptr widths the spelling depends on the target
 * data model (ILP32 vs LP64). When the compiler exposes the
 * `__INT64_FMTd__` style builtins (Clang) we use them verbatim; when
 * it does not (host GCC), we pick the modifier from the matching
 * `__SIZEOF_LONG__` predefine, which both Clang and GCC define on
 * every supported target. This keeps the macros target-correct
 * without inventing target-detection logic. */
#if defined(__SIZEOF_LONG__) && (__SIZEOF_LONG__ == 8)
#  define CLIB_INTTYPES__L64 "l"
#else
#  define CLIB_INTTYPES__L64 "ll"
#endif

#if defined(__INT64_FMTd__)
#  define CLIB_INTTYPES_FMTd64  __INT64_FMTd__
#  define CLIB_INTTYPES_FMTi64  __INT64_FMTi__
#  define CLIB_INTTYPES_FMTu64  __UINT64_FMTu__
#  define CLIB_INTTYPES_FMTo64  __UINT64_FMTo__
#  define CLIB_INTTYPES_FMTx64  __UINT64_FMTx__
#  define CLIB_INTTYPES_FMTX64  __UINT64_FMTX__
#else
/* int64_t is `long` on LP64 and `long long` on ILP32. */
#  define CLIB_INTTYPES_FMTd64  CLIB_INTTYPES__L64 "d"
#  define CLIB_INTTYPES_FMTi64  CLIB_INTTYPES__L64 "i"
#  define CLIB_INTTYPES_FMTu64  CLIB_INTTYPES__L64 "u"
#  define CLIB_INTTYPES_FMTo64  CLIB_INTTYPES__L64 "o"
#  define CLIB_INTTYPES_FMTx64  CLIB_INTTYPES__L64 "x"
#  define CLIB_INTTYPES_FMTX64  CLIB_INTTYPES__L64 "X"
#endif

#if defined(__INTMAX_FMTd__)
#  define CLIB_INTTYPES_FMTdMAX __INTMAX_FMTd__
#  define CLIB_INTTYPES_FMTiMAX __INTMAX_FMTi__
#  define CLIB_INTTYPES_FMTuMAX __UINTMAX_FMTu__
#  define CLIB_INTTYPES_FMToMAX __UINTMAX_FMTo__
#  define CLIB_INTTYPES_FMTxMAX __UINTMAX_FMTx__
#  define CLIB_INTTYPES_FMTXMAX __UINTMAX_FMTX__
#else
/* C11 requires intmax_t to be ≥ every other signed integer width.
 * In practice it matches int64_t on every supported target. */
#  define CLIB_INTTYPES_FMTdMAX CLIB_INTTYPES__L64 "d"
#  define CLIB_INTTYPES_FMTiMAX CLIB_INTTYPES__L64 "i"
#  define CLIB_INTTYPES_FMTuMAX CLIB_INTTYPES__L64 "u"
#  define CLIB_INTTYPES_FMToMAX CLIB_INTTYPES__L64 "o"
#  define CLIB_INTTYPES_FMTxMAX CLIB_INTTYPES__L64 "x"
#  define CLIB_INTTYPES_FMTXMAX CLIB_INTTYPES__L64 "X"
#endif

#if defined(__INTPTR_FMTd__)
#  define CLIB_INTTYPES_FMTdPTR __INTPTR_FMTd__
#  define CLIB_INTTYPES_FMTiPTR __INTPTR_FMTi__
#  define CLIB_INTTYPES_FMTuPTR __UINTPTR_FMTu__
#  define CLIB_INTTYPES_FMToPTR __UINTPTR_FMTo__
#  define CLIB_INTTYPES_FMTxPTR __UINTPTR_FMTx__
#  define CLIB_INTTYPES_FMTXPTR __UINTPTR_FMTX__
#else
/* uintptr_t is `unsigned long` on LP64 and `unsigned int` on ILP32. */
#  if defined(__SIZEOF_POINTER__) && (__SIZEOF_POINTER__ == 8)
#    define CLIB_INTTYPES_FMTdPTR "ld"
#    define CLIB_INTTYPES_FMTiPTR "li"
#    define CLIB_INTTYPES_FMTuPTR "lu"
#    define CLIB_INTTYPES_FMToPTR "lo"
#    define CLIB_INTTYPES_FMTxPTR "lx"
#    define CLIB_INTTYPES_FMTXPTR "lX"
#  else
#    define CLIB_INTTYPES_FMTdPTR "d"
#    define CLIB_INTTYPES_FMTiPTR "i"
#    define CLIB_INTTYPES_FMTuPTR "u"
#    define CLIB_INTTYPES_FMToPTR "o"
#    define CLIB_INTTYPES_FMTxPTR "x"
#    define CLIB_INTTYPES_FMTXPTR "X"
#  endif
#endif

/* ---- exact-width PRI* / SCN* (C11 §7.8.1) ---------------------------- *
 *
 * One row per width × conversion. The SCN* row drops "X" by spec.
 */

#define PRId8   CLIB_INTTYPES_FMTd8
#define PRId16  CLIB_INTTYPES_FMTd16
#define PRId32  CLIB_INTTYPES_FMTd32
#define PRId64  CLIB_INTTYPES_FMTd64

#define PRIi8   CLIB_INTTYPES_FMTi8
#define PRIi16  CLIB_INTTYPES_FMTi16
#define PRIi32  CLIB_INTTYPES_FMTi32
#define PRIi64  CLIB_INTTYPES_FMTi64

#define PRIu8   CLIB_INTTYPES_FMTu8
#define PRIu16  CLIB_INTTYPES_FMTu16
#define PRIu32  CLIB_INTTYPES_FMTu32
#define PRIu64  CLIB_INTTYPES_FMTu64

#define PRIo8   CLIB_INTTYPES_FMTo8
#define PRIo16  CLIB_INTTYPES_FMTo16
#define PRIo32  CLIB_INTTYPES_FMTo32
#define PRIo64  CLIB_INTTYPES_FMTo64

#define PRIx8   CLIB_INTTYPES_FMTx8
#define PRIx16  CLIB_INTTYPES_FMTx16
#define PRIx32  CLIB_INTTYPES_FMTx32
#define PRIx64  CLIB_INTTYPES_FMTx64

#define PRIX8   CLIB_INTTYPES_FMTX8
#define PRIX16  CLIB_INTTYPES_FMTX16
#define PRIX32  CLIB_INTTYPES_FMTX32
#define PRIX64  CLIB_INTTYPES_FMTX64

#define SCNd8   CLIB_INTTYPES_FMTd8
#define SCNd16  CLIB_INTTYPES_FMTd16
#define SCNd32  CLIB_INTTYPES_FMTd32
#define SCNd64  CLIB_INTTYPES_FMTd64

#define SCNi8   CLIB_INTTYPES_FMTi8
#define SCNi16  CLIB_INTTYPES_FMTi16
#define SCNi32  CLIB_INTTYPES_FMTi32
#define SCNi64  CLIB_INTTYPES_FMTi64

#define SCNu8   CLIB_INTTYPES_FMTu8
#define SCNu16  CLIB_INTTYPES_FMTu16
#define SCNu32  CLIB_INTTYPES_FMTu32
#define SCNu64  CLIB_INTTYPES_FMTu64

#define SCNo8   CLIB_INTTYPES_FMTo8
#define SCNo16  CLIB_INTTYPES_FMTo16
#define SCNo32  CLIB_INTTYPES_FMTo32
#define SCNo64  CLIB_INTTYPES_FMTo64

#define SCNx8   CLIB_INTTYPES_FMTx8
#define SCNx16  CLIB_INTTYPES_FMTx16
#define SCNx32  CLIB_INTTYPES_FMTx32
#define SCNx64  CLIB_INTTYPES_FMTx64

/* ---- least-width / fast-width PRI* / SCN* (C11 §7.8.1¶2) ------------- *
 *
 * Slice 10b (PR #457) defined the `int_least*_t` / `int_fast*_t`
 * typedefs; on every target the project ships against they alias the
 * matching exact-width type, so we forward to the exact-width macros
 * here. If a future port introduces a target where (e.g.)
 * `int_least32_t` is wider than `int32_t`, the unit test's
 * `least_fast_format_pinned` sub-marker will catch the size mismatch
 * at the point of consumption.
 */

#define PRIdLEAST8   PRId8
#define PRIdLEAST16  PRId16
#define PRIdLEAST32  PRId32
#define PRIdLEAST64  PRId64
#define PRIiLEAST8   PRIi8
#define PRIiLEAST16  PRIi16
#define PRIiLEAST32  PRIi32
#define PRIiLEAST64  PRIi64
#define PRIuLEAST8   PRIu8
#define PRIuLEAST16  PRIu16
#define PRIuLEAST32  PRIu32
#define PRIuLEAST64  PRIu64
#define PRIoLEAST8   PRIo8
#define PRIoLEAST16  PRIo16
#define PRIoLEAST32  PRIo32
#define PRIoLEAST64  PRIo64
#define PRIxLEAST8   PRIx8
#define PRIxLEAST16  PRIx16
#define PRIxLEAST32  PRIx32
#define PRIxLEAST64  PRIx64
#define PRIXLEAST8   PRIX8
#define PRIXLEAST16  PRIX16
#define PRIXLEAST32  PRIX32
#define PRIXLEAST64  PRIX64

#define SCNdLEAST8   SCNd8
#define SCNdLEAST16  SCNd16
#define SCNdLEAST32  SCNd32
#define SCNdLEAST64  SCNd64
#define SCNiLEAST8   SCNi8
#define SCNiLEAST16  SCNi16
#define SCNiLEAST32  SCNi32
#define SCNiLEAST64  SCNi64
#define SCNuLEAST8   SCNu8
#define SCNuLEAST16  SCNu16
#define SCNuLEAST32  SCNu32
#define SCNuLEAST64  SCNu64
#define SCNoLEAST8   SCNo8
#define SCNoLEAST16  SCNo16
#define SCNoLEAST32  SCNo32
#define SCNoLEAST64  SCNo64
#define SCNxLEAST8   SCNx8
#define SCNxLEAST16  SCNx16
#define SCNxLEAST32  SCNx32
#define SCNxLEAST64  SCNx64

#define PRIdFAST8    PRId8
#define PRIdFAST16   PRId16
#define PRIdFAST32   PRId32
#define PRIdFAST64   PRId64
#define PRIiFAST8    PRIi8
#define PRIiFAST16   PRIi16
#define PRIiFAST32   PRIi32
#define PRIiFAST64   PRIi64
#define PRIuFAST8    PRIu8
#define PRIuFAST16   PRIu16
#define PRIuFAST32   PRIu32
#define PRIuFAST64   PRIu64
#define PRIoFAST8    PRIo8
#define PRIoFAST16   PRIo16
#define PRIoFAST32   PRIo32
#define PRIoFAST64   PRIo64
#define PRIxFAST8    PRIx8
#define PRIxFAST16   PRIx16
#define PRIxFAST32   PRIx32
#define PRIxFAST64   PRIx64
#define PRIXFAST8    PRIX8
#define PRIXFAST16   PRIX16
#define PRIXFAST32   PRIX32
#define PRIXFAST64   PRIX64

#define SCNdFAST8    SCNd8
#define SCNdFAST16   SCNd16
#define SCNdFAST32   SCNd32
#define SCNdFAST64   SCNd64
#define SCNiFAST8    SCNi8
#define SCNiFAST16   SCNi16
#define SCNiFAST32   SCNi32
#define SCNiFAST64   SCNi64
#define SCNuFAST8    SCNu8
#define SCNuFAST16   SCNu16
#define SCNuFAST32   SCNu32
#define SCNuFAST64   SCNu64
#define SCNoFAST8    SCNo8
#define SCNoFAST16   SCNo16
#define SCNoFAST32   SCNo32
#define SCNoFAST64   SCNo64
#define SCNxFAST8    SCNx8
#define SCNxFAST16   SCNx16
#define SCNxFAST32   SCNx32
#define SCNxFAST64   SCNx64

/* ---- intmax_t / intptr_t PRI* / SCN* (C11 §7.8.1¶4-5) --------------- */

#define PRIdMAX  CLIB_INTTYPES_FMTdMAX
#define PRIiMAX  CLIB_INTTYPES_FMTiMAX
#define PRIuMAX  CLIB_INTTYPES_FMTuMAX
#define PRIoMAX  CLIB_INTTYPES_FMToMAX
#define PRIxMAX  CLIB_INTTYPES_FMTxMAX
#define PRIXMAX  CLIB_INTTYPES_FMTXMAX
#define SCNdMAX  CLIB_INTTYPES_FMTdMAX
#define SCNiMAX  CLIB_INTTYPES_FMTiMAX
#define SCNuMAX  CLIB_INTTYPES_FMTuMAX
#define SCNoMAX  CLIB_INTTYPES_FMToMAX
#define SCNxMAX  CLIB_INTTYPES_FMTxMAX

#define PRIdPTR  CLIB_INTTYPES_FMTdPTR
#define PRIiPTR  CLIB_INTTYPES_FMTiPTR
#define PRIuPTR  CLIB_INTTYPES_FMTuPTR
#define PRIoPTR  CLIB_INTTYPES_FMToPTR
#define PRIxPTR  CLIB_INTTYPES_FMTxPTR
#define PRIXPTR  CLIB_INTTYPES_FMTXPTR
#define SCNdPTR  CLIB_INTTYPES_FMTdPTR
#define SCNiPTR  CLIB_INTTYPES_FMTiPTR
#define SCNuPTR  CLIB_INTTYPES_FMTuPTR
#define SCNoPTR  CLIB_INTTYPES_FMToPTR
#define SCNxPTR  CLIB_INTTYPES_FMTxPTR

/* ---- drift-anchor enum (mirrors src/stdint.c pattern) ---------------- *
 *
 * The header is pure preprocessor, but src/inttypes.c surfaces a tiny
 * helper that returns the format-string macro a given selector
 * resolves to. The unit test compares the LINKED helper's view to
 * the test TU's view so a future drift in either the compiler
 * builtins or the fallback table cannot silently regress one side
 * without the other.
 */
enum {
  CLIB_INTTYPES_SEL_PRId8 = 0,
  CLIB_INTTYPES_SEL_PRId16,
  CLIB_INTTYPES_SEL_PRId32,
  CLIB_INTTYPES_SEL_PRId64,
  CLIB_INTTYPES_SEL_PRIu32,
  CLIB_INTTYPES_SEL_PRIu64,
  CLIB_INTTYPES_SEL_PRIx32,
  CLIB_INTTYPES_SEL_PRIx64,
  CLIB_INTTYPES_SEL_PRIdMAX,
  CLIB_INTTYPES_SEL_PRIuMAX,
  CLIB_INTTYPES_SEL_PRIdPTR,
  CLIB_INTTYPES_SEL_PRIuPTR,
  CLIB_INTTYPES_SEL_SCNd32,
  CLIB_INTTYPES_SEL_SCNu64,
  CLIB_INTTYPES_SEL_COUNT
};

const char *clib_inttypes_fmt(int which);

/* ---- C11 §7.8.2 imaxabs / imaxdiv ----------------------------------- *
 *
 * Slice 11b (issue #407 follow-on, M7-TOOLCHAIN-004) — the function
 * family <inttypes.h> §7.8.2 mandates on top of the format-string
 * macros shipped in slice 11. Symbols are pinned by the existing
 * clib_symbol_drift gate; behavior is exercised by the per-symbol
 * sub-checks in tests/clib_inttypes_test.c.
 *
 * imaxabs(j)
 *   Behavior on INTMAX_MIN is undefined per C11; this implementation
 *   returns the input unchanged in that case (consistent with the
 *   two's-complement carve-out we use for abs/labs).
 *
 * imaxdiv_t / imaxdiv(numer, denom)
 *   Truncated-toward-zero division (C11 §7.8.2.2) — quot * denom + rem
 *   == numer, sign(rem) == sign(numer) when rem != 0. Division by zero
 *   is undefined per the standard; this implementation returns
 *   {INTMAX_MAX, 0} for numer >= 0 and {INTMAX_MIN, 0} for numer < 0
 *   rather than trapping, mirroring the deny-clean discipline the
 *   other clib slices use for UB-adjacent inputs.
 *
 * Layout note: imaxdiv_t members are exposed as `intmax_t quot` then
 * `intmax_t rem` per the C standard. Order is fixed; no other members.
 */
typedef struct imaxdiv_t {
  intmax_t quot;
  intmax_t rem;
} imaxdiv_t;

intmax_t  imaxabs(intmax_t j);
imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom);

/* ---- C11 §7.8.2 strtoimax / strtoumax ------------------------------- *
 *
 * Same parse / overflow / *endptr rules as strtoll / strtoull (slice 9
 * extension, PR #444 / #458), but accumulating into intmax_t /
 * uintmax_t. On overflow errno is set to ERANGE and the call returns
 * INTMAX_MAX / INTMAX_MIN / UINTMAX_MAX as appropriate (same clamp
 * shape as strtol{l}).
 *
 * Implementation forwards to strtoll / strtoull on targets where
 * intmax_t / uintmax_t are layout-compatible with long long /
 * unsigned long long (the only configuration libclib supports today —
 * SecureOS x86_64 + the libclib host test bench). A static assertion
 * in src/inttypes.c pins the width contract so a future target with
 * a wider intmax_t fails the build loudly rather than silently
 * truncating.
 */
intmax_t  strtoimax(const char *nptr, char **endptr, int base);
uintmax_t strtoumax(const char *nptr, char **endptr, int base);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_USER_LIBS_CLIB_INTTYPES_H */
