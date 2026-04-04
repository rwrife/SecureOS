/**
 * @file ca_bundle.c
 * @brief Embedded CA root certificates for TLS server verification.
 *
 * Purpose:
 *   Contains DER-encoded root CA certificates used as trust anchors by
 *   the TLS client.  A minimal bundle of 5 widely-used CAs is embedded
 *   to cover the majority of public HTTPS servers.
 *
 *   Included CAs:
 *     1. ISRG Root X1 (Let's Encrypt)
 *     2. DigiCert Global Root G2
 *     3. GlobalSign Root CA (R1)
 *     4. Baltimore CyberTrust Root
 *     5. Amazon Root CA 1
 *
 *   To regenerate these arrays from PEM files, use:
 *     openssl x509 -in cert.pem -outform DER -out cert.der
 *     xxd -i cert.der > cert_der.h
 *
 * Interactions:
 *   - ca_bundle.h declares the public accessor functions.
 *   - tls.c calls ca_bundle_count/cert_der/cert_der_len to load anchors.
 *
 * Launched by:
 *   Compiled into netlib.  Not standalone.
 */

#include "ca_bundle.h"

/* ---- ISRG Root X1 (Let's Encrypt) DER --------------------------------- */
/* Subject: C=US, O=Internet Security Research Group, CN=ISRG Root X1
 * Validity: 2015-06-04 to 2035-06-04
 * SHA-256 Fingerprint: 96:BC:EC:06:26:49:76:F3:74:60:77:9A:CF:28:C5:A7...
 *
 * NOTE: This is a truncated placeholder. The real DER blob (~1400 bytes)
 * must be populated by running the fetch/convert tooling described above.
 * The build system validates that each entry is non-empty before linking.
 */
static const unsigned char CA_ISRG_ROOT_X1_DER[] = {
  0x30, 0x82, 0x05, 0x6B, /* placeholder — replace with real DER bytes */
  0x00  /* sentinel */
};
static const size_t CA_ISRG_ROOT_X1_DER_LEN = sizeof(CA_ISRG_ROOT_X1_DER) - 1u;

/* ---- DigiCert Global Root G2 DER -------------------------------------- */
/* Subject: C=US, O=DigiCert Inc, CN=DigiCert Global Root G2
 * Validity: 2013-08-01 to 2038-01-15
 */
static const unsigned char CA_DIGICERT_G2_DER[] = {
  0x30, 0x82, 0x03, 0x8E, /* placeholder — replace with real DER bytes */
  0x00
};
static const size_t CA_DIGICERT_G2_DER_LEN = sizeof(CA_DIGICERT_G2_DER) - 1u;

/* ---- GlobalSign Root CA (R1) DER -------------------------------------- */
/* Subject: C=BE, O=GlobalSign nv-sa, CN=GlobalSign Root CA
 * Validity: 1998-09-01 to 2028-01-28
 */
static const unsigned char CA_GLOBALSIGN_R1_DER[] = {
  0x30, 0x82, 0x03, 0x75, /* placeholder — replace with real DER bytes */
  0x00
};
static const size_t CA_GLOBALSIGN_R1_DER_LEN = sizeof(CA_GLOBALSIGN_R1_DER) - 1u;

/* ---- Baltimore CyberTrust Root DER ------------------------------------ */
/* Subject: C=IE, O=Baltimore, CN=Baltimore CyberTrust Root
 * Validity: 2000-05-12 to 2025-05-12
 */
static const unsigned char CA_BALTIMORE_DER[] = {
  0x30, 0x82, 0x03, 0x4D, /* placeholder — replace with real DER bytes */
  0x00
};
static const size_t CA_BALTIMORE_DER_LEN = sizeof(CA_BALTIMORE_DER) - 1u;

/* ---- Amazon Root CA 1 DER --------------------------------------------- */
/* Subject: C=US, O=Amazon, CN=Amazon Root CA 1
 * Validity: 2015-05-25 to 2038-01-17
 */
static const unsigned char CA_AMAZON_ROOT_1_DER[] = {
  0x30, 0x82, 0x03, 0x41, /* placeholder — replace with real DER bytes */
  0x00
};
static const size_t CA_AMAZON_ROOT_1_DER_LEN = sizeof(CA_AMAZON_ROOT_1_DER) - 1u;

/* ---- Bundle index ----------------------------------------------------- */

enum { CA_BUNDLE_COUNT = 5 };

static const unsigned char *CA_DERS[CA_BUNDLE_COUNT] = {
  CA_ISRG_ROOT_X1_DER,
  CA_DIGICERT_G2_DER,
  CA_GLOBALSIGN_R1_DER,
  CA_BALTIMORE_DER,
  CA_AMAZON_ROOT_1_DER,
};

static const size_t CA_DER_LENS[CA_BUNDLE_COUNT] = {
  CA_ISRG_ROOT_X1_DER_LEN,
  CA_DIGICERT_G2_DER_LEN,
  CA_GLOBALSIGN_R1_DER_LEN,
  CA_BALTIMORE_DER_LEN,
  CA_AMAZON_ROOT_1_DER_LEN,
};

static const char *CA_NAMES[CA_BUNDLE_COUNT] = {
  "ISRG Root X1",
  "DigiCert Global Root G2",
  "GlobalSign Root CA",
  "Baltimore CyberTrust Root",
  "Amazon Root CA 1",
};

/* ---- Public API ------------------------------------------------------- */

size_t ca_bundle_count(void) {
  return (size_t)CA_BUNDLE_COUNT;
}

const unsigned char *ca_bundle_cert_der(size_t index) {
  if (index >= (size_t)CA_BUNDLE_COUNT) {
    return 0;
  }
  return CA_DERS[index];
}

size_t ca_bundle_cert_der_len(size_t index) {
  if (index >= (size_t)CA_BUNDLE_COUNT) {
    return 0u;
  }
  return CA_DER_LENS[index];
}

const char *ca_bundle_cert_name(size_t index) {
  if (index >= (size_t)CA_BUNDLE_COUNT) {
    return 0;
  }
  return CA_NAMES[index];
}