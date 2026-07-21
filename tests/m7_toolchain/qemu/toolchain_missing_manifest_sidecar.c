/* tests/m7_toolchain/qemu/toolchain_missing_manifest_sidecar.c
 *
 * Issue #596 companion C-harness placeholder.
 *
 * This file reserves the C-harness artifact requested by issue #596 while
 * runtime marker emission remains SKIP-pinned behind #410.
 *
 * How it is used:
 * - Documents the intended qemu-side implementation surface for the
 *   missing-manifest-sidecar deny-path contract.
 * - Keeps a concrete C harness path available for the future flip from
 *   SKIP to PASS without changing marker naming.
 *
 * What calls it:
 * - No runtime caller today. The active dispatcher entrypoint is
 *   tests/m7_toolchain/toolchain_missing_manifest_sidecar.sh, which delegates
 *   to tests/m7_toolchain/qemu/toolchain_missing_manifest_sidecar.sh.
 */

int toolchain_missing_manifest_sidecar_marker_placeholder(void) {
    return 0;
}
