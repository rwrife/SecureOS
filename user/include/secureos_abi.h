/**
 * @file secureos_abi.h
 * @brief SecureOS ABI version constant — single source of truth.
 *
 * Purpose:
 *   Defines the OS_ABI_VERSION constant family as prescribed by
 *   BUILD_ROADMAP.md §7 ("ABI and Interface Freeze Plan").
 *
 *   During the rapid-iteration phase (pre-SDK-beta) OS_ABI_VERSION_MAJOR
 *   remains 0. It will be frozen to 1 when the public SDK beta is
 *   announced (see plan: docs/abi/versioning.md and issue #136).
 *
 * Interactions:
 *   - secureos_api.h re-exports the version accessor
 *     os_get_abi_version() so user apps can query the runtime ABI
 *     without taking any capability (information-only).
 *   - user/runtime/secureos_api_stubs.c provides the stub
 *     implementation that returns OS_ABI_VERSION.
 *   - tests/abi_version_test.c asserts that the compile-time constant
 *     matches the runtime accessor — catches stale stubs or header drift.
 *
 * Wire layout (u32, packed):
 *     bits 16..31  major  (currently 0)
 *     bits  0..15  minor  (currently 0)
 *
 * Compatibility policy (per BUILD_ROADMAP §7):
 *   - Same major: backward-compatible additions only.
 *   - Major bump: breaking change; one previous major must keep
 *     compatibility shims for at least one release cycle.
 */

#ifndef SECUREOS_ABI_H
#define SECUREOS_ABI_H

#ifdef __cplusplus
extern "C" {
#endif

#define OS_ABI_VERSION_MAJOR 0u
#define OS_ABI_VERSION_MINOR 0u

#define OS_ABI_VERSION                                                   \
  (((unsigned int)OS_ABI_VERSION_MAJOR << 16) |                          \
   ((unsigned int)OS_ABI_VERSION_MINOR & 0xFFFFu))

#define OS_ABI_VERSION_MAJOR_OF(v) (((unsigned int)(v) >> 16) & 0xFFFFu)
#define OS_ABI_VERSION_MINOR_OF(v) ((unsigned int)(v) & 0xFFFFu)

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_ABI_H */
