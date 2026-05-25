/**
 * @file bearssl_hw_stubs.c
 * @brief Stub implementations for BearSSL hardware-accelerated functions.
 *
 * Purpose:
 *   BearSSL's default engine selection references hardware acceleration
 *   functions (AES-NI, PCLMUL, SSE2, etc.) that need x86 intrinsics
 *   unavailable in our freestanding build. These stubs return NULL/0 to
 *   indicate hardware acceleration is not available, causing BearSSL to
 *   fall back to portable C implementations.
 *
 * Interactions:
 *   - Linked into the netlib shared library alongside BearSSL objects.
 *   - Called by BearSSL ssl_engine_default_*.c during cipher setup.
 *
 * Launched by:
 *   Not standalone; compiled and linked with netlib.
 */

#include <stddef.h>

/* AES-NI stubs */
void *br_aes_x86ni_cbcenc_get_vtable(void) { return NULL; }
void *br_aes_x86ni_cbcdec_get_vtable(void) { return NULL; }
void *br_aes_x86ni_ctr_get_vtable(void)    { return NULL; }
void *br_aes_x86ni_ctrcbc_get_vtable(void) { return NULL; }

/* PCLMUL (GCM) stub */
void *br_ghash_pclmul_get(void) { return NULL; }

/* SSE2 ChaCha20 stub */
void *br_chacha20_sse2_get(void) { return NULL; }

/* 64-bit Poly1305 stub */
void *br_poly1305_ctmulq_get(void) { return NULL; }
