/* tests/m7_toolchain/cc_manifest_override_precedence.c
 *
 * Issue #609 companion C-harness placeholder.
 *
 * The acceptance marker is intentionally SKIP-pinned while #409/#410 are OPEN.
 * Runtime marker emission continues through the shell harness entrypoint
 * `toolchain_cc_manifest_override_precedence.sh` to preserve existing
 * validator expectations and dispatch wiring.
 */

int cc_manifest_override_precedence_marker_placeholder(void) {
    return 0;
}
