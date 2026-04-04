/**
 * @file tls_test.c
 * @brief Tests for TLS support components: entropy and CA bundle.
 *
 * Purpose:
 *   Validates the entropy seed generation and CA bundle accessor
 *   functions that support the TLS implementation in netlib.
 *   These components can be tested independently of BearSSL.
 *
 * Interactions:
 *   - entropy.c: RDTSC-based entropy pool
 *   - ca_bundle.c: embedded root CA certificate accessors
 *
 * Launched by:
 *   Compiled and run by the native test harness via test_tls.sh.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Pull in the entropy and ca_bundle sources directly for host testing */
#include "../user/libs/netlib/entropy.h"
#include "../user/libs/netlib/ca_bundle.h"

static void fail(const char *reason) {
  printf("TEST:FAIL:tls:%s\n", reason);
  exit(1);
}

static void test_entropy_init(void) {
  /* Should not crash */
  entropy_init();
}

static void test_entropy_seed_nonzero(void) {
  unsigned char seed[32];
  int all_zero = 1;
  unsigned int i;

  entropy_init();
  entropy_get_seed(seed, sizeof(seed));

  for (i = 0u; i < sizeof(seed); ++i) {
    if (seed[i] != 0u) {
      all_zero = 0;
      break;
    }
  }

  if (all_zero) {
    fail("entropy_seed_all_zeros");
  }
}

static void test_entropy_seed_varies(void) {
  unsigned char seed_a[32];
  unsigned char seed_b[32];

  entropy_init();
  entropy_get_seed(seed_a, sizeof(seed_a));
  entropy_get_seed(seed_b, sizeof(seed_b));

  /* Two successive calls should produce different output because the
   * internal state is mixed after each call. */
  if (memcmp(seed_a, seed_b, sizeof(seed_a)) == 0) {
    fail("entropy_seed_not_varying");
  }
}

static void test_entropy_partial_fill(void) {
  unsigned char seed[4];
  int all_zero = 1;
  unsigned int i;

  entropy_init();
  entropy_get_seed(seed, sizeof(seed));

  for (i = 0u; i < sizeof(seed); ++i) {
    if (seed[i] != 0u) {
      all_zero = 0;
      break;
    }
  }

  if (all_zero) {
    fail("entropy_partial_fill_all_zeros");
  }
}

static void test_ca_bundle_count(void) {
  unsigned int count = ca_bundle_count();

  if (count == 0u) {
    fail("ca_bundle_count_zero");
  }

  /* Plan specifies 5 root CAs */
  if (count != 5u) {
    fail("ca_bundle_count_not_five");
  }
}

static void test_ca_bundle_accessors(void) {
  unsigned int count = ca_bundle_count();
  unsigned int i;

  for (i = 0u; i < count; ++i) {
    const unsigned char *der = ca_bundle_cert_der(i);
    unsigned int der_len = ca_bundle_cert_der_len(i);
    const char *name = ca_bundle_cert_name(i);

    if (der == NULL) {
      fail("ca_bundle_der_null");
    }
    if (der_len == 0u) {
      fail("ca_bundle_der_len_zero");
    }
    if (name == NULL || name[0] == '\0') {
      fail("ca_bundle_name_empty");
    }
  }
}

static void test_ca_bundle_out_of_bounds(void) {
  unsigned int count = ca_bundle_count();
  const unsigned char *der = ca_bundle_cert_der(count);
  unsigned int der_len = ca_bundle_cert_der_len(count);
  const char *name = ca_bundle_cert_name(count);

  if (der != NULL) {
    fail("ca_bundle_oob_der_not_null");
  }
  if (der_len != 0u) {
    fail("ca_bundle_oob_der_len_not_zero");
  }
  if (name != NULL) {
    fail("ca_bundle_oob_name_not_null");
  }
}

static void test_ca_bundle_known_names(void) {
  /* Verify expected CA names are present */
  unsigned int count = ca_bundle_count();
  int found_isrg = 0;
  int found_digicert = 0;
  unsigned int i;

  for (i = 0u; i < count; ++i) {
    const char *name = ca_bundle_cert_name(i);
    if (name != NULL) {
      if (strstr(name, "ISRG") != NULL) {
        found_isrg = 1;
      }
      if (strstr(name, "DigiCert") != NULL) {
        found_digicert = 1;
      }
    }
  }

  if (!found_isrg) {
    fail("ca_bundle_missing_isrg");
  }
  if (!found_digicert) {
    fail("ca_bundle_missing_digicert");
  }
}

int main(void) {
  printf("TEST:START:tls\n");

  test_entropy_init();
  test_entropy_seed_nonzero();
  test_entropy_seed_varies();
  test_entropy_partial_fill();

  test_ca_bundle_count();
  test_ca_bundle_accessors();
  test_ca_bundle_out_of_bounds();
  test_ca_bundle_known_names();

  printf("TEST:PASS:tls_components\n");
  return 0;
}