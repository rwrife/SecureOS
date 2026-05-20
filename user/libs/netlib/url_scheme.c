/**
 * @file url_scheme.c
 * @brief Implementation of the deny-by-default URL scheme classifier.
 *
 * Purpose:
 *   Performs a constant-time prefix match against the small, explicitly
 *   allowed transport set ("http://" and "https://"). Anything else is
 *   classified as NETLIB_URL_SCHEME_UNKNOWN so the netlib dispatch layer
 *   can fail closed without performing DNS or TCP/TLS work for unsupported
 *   schemes.
 *
 * Interactions:
 *   - url_scheme.h defines the public contract.
 *   - api.c and http.c call netlib_url_classify_scheme() at the entry of
 *     the shared http/https dispatch path.
 *
 * Launched by:
 *   Compiled into the shared netlib library and the kernel-side netlib
 *   build. Has no platform dependencies.
 */

#include "url_scheme.h"

static int netlib_url_prefix_match(const char *url, const char *prefix) {
  unsigned int i = 0u;

  if (url == 0 || prefix == 0) {
    return 0;
  }

  while (prefix[i] != '\0') {
    if (url[i] != prefix[i]) {
      return 0;
    }
    ++i;
  }
  return 1;
}

netlib_url_scheme_t netlib_url_classify_scheme(const char *url) {
  if (url == 0 || url[0] == '\0') {
    return NETLIB_URL_SCHEME_UNKNOWN;
  }

  if (netlib_url_prefix_match(url, "https://")) {
    return NETLIB_URL_SCHEME_HTTPS;
  }
  if (netlib_url_prefix_match(url, "http://")) {
    return NETLIB_URL_SCHEME_HTTP;
  }

  return NETLIB_URL_SCHEME_UNKNOWN;
}
