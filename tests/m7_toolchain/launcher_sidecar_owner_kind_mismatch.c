/* tests/m7_toolchain/launcher_sidecar_owner_kind_mismatch.c
 *
 * Issue #601 companion C-harness placeholder.
 *
 * This file reserves the C harness artifact requested by #601 while runtime
 * marker emission remains SKIP-pinned behind #410.
 *
 * How it is used:
 * - Documents the planned C-side owner.kind mismatch probe for the launcher.
 * - Keeps a stable companion path for the eventual SKIP->PASS flip.
 *
 * What calls it:
 * - No runtime caller today. Marker emission is routed through
 *   tests/m7_toolchain/toolchain_launcher_sidecar_owner_kind_mismatch.sh,
 *   which delegates to tests/m7_toolchain/qemu/launcher_sidecar_owner_kind_mismatch.sh.
 */

int launcher_sidecar_owner_kind_mismatch_marker_placeholder(void) {
    return 0;
}
