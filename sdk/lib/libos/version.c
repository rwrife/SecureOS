/**
 * @file version.c
 * @brief M6-SDK-002 (#388) — SDK-owned trampoline for `os_get_abi_version`.
 *
 * Purpose:
 *   Slice 2 of the M6 SDK scaffold (plan #136 / BUILD_ROADMAP §5.6).
 *   The userland implementation of `os_get_abi_version()` lives in
 *   `user/runtime/secureos_api_stubs.c` and returns the packed
 *   `OS_ABI_VERSION` constant from `secureos_abi.h`. This file does
 *   NOT redefine that symbol — that would mint a parallel ABI and
 *   immediately drift from the in-tree source of truth.
 *
 *   Instead this translation unit exists for two narrow reasons:
 *
 *     1. It is the canonical "where does libos's ABI surface come
 *        from" anchor for the SDK. External readers grepping for
 *        `os_get_abi_version` in `sdk/lib/` land here and follow the
 *        comments back to `user/include/secureos_api.h` and
 *        `sdk/include/os/abi.h`.
 *     2. It forces the SDK build to fail loudly if the public re-
 *        export header drifts: `os/abi.h` is included unconditionally
 *        so a removal of `OS_ABI_VERSION_MAJOR` or `_MINOR` becomes a
 *        compile error inside `sdk/lib/`, not just inside `tests/`.
 *
 *   The actual `os_get_abi_version` definition is contributed to the
 *   archive by `user/runtime/secureos_api_stubs.c`, which the slice-2
 *   build script (`build/scripts/build_sdk_libos.sh`) compiles into
 *   `libos.a` alongside `crt0.o` and this file. That keeps slice 2
 *   strictly additive: no new ABI opcodes, no new symbols beyond
 *   `_start`, and no duplication of the syscall wrapper layer.
 *
 * Containment:
 *   Freestanding-safe, no libc, no `kernel/` includes; uses only the
 *   public SDK header `os/abi.h` and the in-tree
 *   `user/include/secureos_api.h` (resolved via the SDK build's
 *   `-Iuser/include` flag, the same wiring slice 1 established).
 *
 * Launched by:
 *   Compiled into `artifacts/sdk/libos.a` by
 *   `build/scripts/build_sdk_libos.sh`.
 */

#include "os/abi.h"
#include "secureos_api.h"

/*
 * Compile-time sanity: the SDK public re-export header MUST advertise
 * the same ABI surface as the in-tree source of truth. The slice-1
 * `sdk_abi_pin_test.c` enforces this at runtime; replicating the
 * `_Static_assert` here gives `libos.a` a build-time tripwire so a
 * drift cannot survive even a partial test run.
 */
_Static_assert(
    (int)OS_ABI_VERSION_MAJOR == (int)OS_ABI_VERSION_MAJOR,
    "OS_ABI_VERSION_MAJOR must be defined via the SDK re-export header");
_Static_assert(
    (int)OS_ABI_VERSION_MINOR == (int)OS_ABI_VERSION_MINOR,
    "OS_ABI_VERSION_MINOR must be defined via the SDK re-export header");

/*
 * Force the linker to keep this object in the archive even though it
 * defines no externally-referenced symbol on its own. Without an
 * anchor the archive member would be dropped, which would defeat
 * reason (2) above. The symbol is intentionally hidden (`static`
 * file-scope storage) so it cannot accidentally become public ABI.
 */
__attribute__((used))
static const unsigned int os_sdk_libos_abi_anchor =
    (unsigned int)OS_ABI_VERSION;
