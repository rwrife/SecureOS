/**
 * @file secureos_abi.h
 * @brief Single source of truth for the SecureOS user/kernel ABI version.
 *
 * Purpose:
 *   Defines the packed `OS_ABI_VERSION` constant referenced by
 *   `BUILD_ROADMAP.md` §7. During rapid iteration the version is held at
 *   `0.0` (major=0, minor=0); per the roadmap, this is bumped to `1.0`
 *   only when the SDK beta is announced. All in-tree code that needs to
 *   reason about the ABI version (runtime accessor, tests, future
 *   manifest schema gating) MUST include this header and use the macros
 *   rather than hard-coding integer literals.
 *
 * Layout:
 *   `OS_ABI_VERSION` is a 32-bit packed `(major << 16) | minor` value.
 *   Major-version changes signal an incompatible break; minor-version
 *   changes are reserved for additive, source-compatible extensions.
 *
 * Interactions:
 *   - secureos_api.h: declares `os_get_abi_version()` which returns this
 *     constant via the user-runtime stubs.
 *   - docs/abi/versioning.md: human-readable policy that mirrors this
 *     header.
 *
 * Launched by:
 *   Header-only; not a standalone process.
 */

#ifndef SECUREOS_ABI_H
#define SECUREOS_ABI_H

#ifdef __cplusplus
extern "C" {
#endif

#define OS_ABI_VERSION_MAJOR 0
#define OS_ABI_VERSION_MINOR 0

/*
 * Packed (major << 16) | minor. Kept as a plain integer constant so it can
 * be used in preprocessor comparisons and array sizes without depending on
 * a function call.
 */
#define OS_ABI_VERSION ((OS_ABI_VERSION_MAJOR << 16) | OS_ABI_VERSION_MINOR)

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_ABI_H */
