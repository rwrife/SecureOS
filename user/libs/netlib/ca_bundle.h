#ifndef SECUREOS_NET_CA_BUNDLE_H
#define SECUREOS_NET_CA_BUNDLE_H

/**
 * @file ca_bundle.h
 * @brief Embedded CA root certificate trust anchors for TLS verification.
 *
 * Purpose:
 *   Provides access to a minimal set of trusted Certificate Authority
 *   root certificates used by the TLS client to verify server identity.
 *   Certificates are stored as DER-encoded byte arrays and converted
 *   to BearSSL trust anchor format.
 *
 * Interactions:
 *   - tls.c calls ca_bundle_get() to configure the BearSSL X.509
 *     validator with trusted root CAs during TLS handshake setup.
 *
 * Launched by:
 *   Called on-demand from tls.c.  Not standalone.
 */

#include <stddef.h>

/**
 * Opaque trust anchor type.  When BearSSL headers are available this
 * resolves to br_x509_trust_anchor; otherwise it is an opaque struct
 * pointer used only by tls.c.
 */
struct secureos_trust_anchor;

/**
 * Return the number of embedded CA trust anchors.
 */
size_t ca_bundle_count(void);

/**
 * Return a pointer to the static array of DER-encoded CA certificates.
 * Each entry is a contiguous DER blob.  Use ca_bundle_cert_der() and
 * ca_bundle_cert_der_len() to access individual certificates.
 */
const unsigned char *ca_bundle_cert_der(size_t index);

/**
 * Return the DER byte length of the certificate at the given index.
 * Returns 0 if index is out of range.
 */
size_t ca_bundle_cert_der_len(size_t index);

/**
 * Return the human-readable name of the CA at the given index.
 * Returns NULL if index is out of range.
 */
const char *ca_bundle_cert_name(size_t index);

#endif