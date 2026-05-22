/**
 * @file cap_deny_marker.h
 * @brief Single source of truth for the capability-denied serial log marker.
 *
 * Implements the marker grammar fixed by `docs/abi/capability-deny-contract.md`
 * §4 (issue #164 / PR #167):
 *
 *   CAP:DENY:<actor_subject_id>:<capability_id>:<resource>\n
 *
 *   - <actor_subject_id> : decimal cap_subject_id_t (the launched subject's id,
 *                         not the launcher's).
 *   - <capability_id>    : symbolic name from capability_id_t with the "CAP_"
 *                         prefix stripped and lowercased (e.g. "console_write",
 *                         "fs_read", "capability_admin").
 *   - <resource>         : smallest meaningful identifier for the denied
 *                         operation; literal "-" when the operation has no
 *                         resource handle; never empty; ASCII only; any colon
 *                         in the source identifier MUST be replaced with '_'
 *                         BEFORE being passed in.
 *
 * Why this lives in one place:
 *   issue #211 — conformance test for the marker shape across all deny paths.
 *   Without a shared formatter + validator, each future deny-path PR (#108,
 *   #115, #210) would invent its own grep and the shapes would silently drift.
 *
 * Used by:
 *   - tests/cap_deny_marker_shape_test.c (this issue)
 *   - future M3 fs_service / M4 broker / M1 IPC deny paths (#108, #115, #210)
 *
 * Pure host-side; no kernel-only headers. Safe to compile under both the
 * cross toolchain and the host unit-test compile lines.
 */

#ifndef SECUREOS_CAP_DENY_MARKER_H
#define SECUREOS_CAP_DENY_MARKER_H

#include <stddef.h>

#include "capability.h"

/*
 * Maximum bytes a formatted marker can require (excluding NUL).
 * 64 actor digits is overkill for cap_subject_id_t (<= 10 decimal chars),
 * but a fixed upper bound makes static buffers trivial.
 */
#define CAP_DENY_MARKER_MAX 256u

/*
 * Maximum length of a resource string (caller-supplied). Longer values are
 * rejected by cap_deny_marker_format() so a runaway path/URL cannot silently
 * truncate a marker line.
 */
#define CAP_DENY_RESOURCE_MAX 192u

/*
 * Lookup the canonical lowercase, CAP_-prefix-stripped name for a
 * capability_id_t. Returns NULL when the id is not recognized; callers MUST
 * treat NULL as a contract violation (a deny for an unknown cap is a bug).
 */
const char *cap_deny_marker_name(capability_id_t cap_id);

/*
 * Format a single capability-denied marker into `buf`. On success returns
 * the number of bytes written (excluding the trailing NUL); the output
 * INCLUDES the trailing newline required by §4 ("newline-terminated").
 *
 * Returns -1 on:
 *   - NULL buf, NULL resource;
 *   - buf_size too small for the formatted line + NUL;
 *   - resource length 0 or > CAP_DENY_RESOURCE_MAX;
 *   - resource containing ':' (caller must pre-escape per §4);
 *   - resource containing '\n' or any non-printable byte;
 *   - capability_id_t not recognized by cap_deny_marker_name().
 *
 * Pure function: no I/O, no globals.
 */
int cap_deny_marker_format(cap_subject_id_t actor_subject_id,
                           capability_id_t cap_id,
                           const char *resource,
                           char *buf,
                           size_t buf_size);

/*
 * Validate that a candidate line matches §4 grammar exactly. The line MUST
 * include the trailing '\n' (a missing newline is a contract violation).
 *
 * Returns 0 on conformant input; on rejection returns a negative error code
 * and, when `reason` is non-NULL, writes a short human-readable reason
 * (ASCII, NUL-terminated, no colons) into `reason` of size `reason_size`.
 *
 * The reason strings are stable identifiers — they may be grepped by tests
 * to assert which negative case fired:
 *
 *   "missing_prefix"        — line does not start with "CAP:DENY:"
 *   "missing_newline"       — line is not terminated by exactly one '\n'
 *   "field_count"           — wrong number of ':'-delimited fields
 *   "actor_not_decimal"     — <actor_subject_id> contains non-digits or empty
 *   "cap_unknown"           — <capability_id> is not a known cap name
 *   "resource_empty"        — <resource> is empty
 *   "resource_bad_byte"     — <resource> contains '\n' or non-printable byte
 *   "trailing_garbage"      — bytes after the terminating newline
 */
int cap_deny_marker_validate(const char *line,
                             char *reason,
                             size_t reason_size);

#endif /* SECUREOS_CAP_DENY_MARKER_H */
