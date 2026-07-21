/* tests/m7_toolchain/sofpack_manifestgen_roundtrip_test.c
 *
 * Issue #600 companion C-harness placeholder.
 *
 * This file reserves the C harness name requested by issue #600 while the
 * acceptance marker remains SKIP-pinned behind gating issue #409.
 *
 * Runtime marker emission currently flows through
 * tests/m7_toolchain/toolchain_sofpack_plus_manifestgen_roundtrip.sh (which
 * delegates to the qemu-scoped shell harness) so existing marker validators
 * and dispatch contracts remain stable.
 */

int sofpack_manifestgen_roundtrip_marker_placeholder(void) {
    return 0;
}
