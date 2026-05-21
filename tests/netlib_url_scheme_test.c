/**
 * @file netlib_url_scheme_test.c
 * @brief Tests for the deny-by-default netlib URL scheme classifier.
 *
 * Purpose:
 *   Validates that netlib_url_classify_scheme() routes "http://" and
 *   "https://" URLs to their explicit transports while denying NULL,
 *   empty, scheme-less, and arbitrary non-network URLs. This is the
 *   deny-by-default gate in front of the http/https dispatch path that
 *   keeps non-supported schemes from triggering DNS or TCP work.
 *
 * Interactions:
 *   - user/libs/netlib/url_scheme.c: implementation under test.
 *   - user/libs/netlib/url_scheme.h: public contract.
 *
 * Launched by:
 *   Compiled and run by the native test harness via test_netlib_url_scheme.sh
 *   and dispatched by build/scripts/test.sh netlib_url_scheme.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../user/libs/netlib/url_scheme.h"

static int g_failures = 0;

static void expect(int condition, const char *reason) {
  if (!condition) {
    printf("TEST:FAIL:netlib_url_scheme:%s\n", reason);
    ++g_failures;
  }
}

static void test_http_scheme_allowed(void) {
  expect(netlib_url_classify_scheme("http://example.com/") ==
             NETLIB_URL_SCHEME_HTTP,
         "http_scheme_not_classified_as_http");
  expect(netlib_url_classify_scheme("http://example.com:8080/path") ==
             NETLIB_URL_SCHEME_HTTP,
         "http_scheme_with_port_not_http");
  /* Bare prefix without host is still classified by scheme; URL parser
   * downstream is responsible for rejecting empty hosts. */
  expect(netlib_url_classify_scheme("http://") == NETLIB_URL_SCHEME_HTTP,
         "http_bare_prefix_not_http");
}

static void test_https_scheme_allowed(void) {
  expect(netlib_url_classify_scheme("https://example.com/") ==
             NETLIB_URL_SCHEME_HTTPS,
         "https_scheme_not_classified_as_https");
  expect(netlib_url_classify_scheme("https://api.example.com:8443/v1") ==
             NETLIB_URL_SCHEME_HTTPS,
         "https_scheme_with_port_not_https");
}

static void test_unknown_schemes_denied(void) {
  /* Each of these MUST be denied by the gate so the dispatch layer never
   * performs DNS or TCP work for non-network schemes. */
  expect(netlib_url_classify_scheme("file:///etc/passwd") ==
             NETLIB_URL_SCHEME_UNKNOWN,
         "file_scheme_not_denied");
  expect(netlib_url_classify_scheme("ftp://example.com/") ==
             NETLIB_URL_SCHEME_UNKNOWN,
         "ftp_scheme_not_denied");
  expect(netlib_url_classify_scheme("javascript:alert(1)") ==
             NETLIB_URL_SCHEME_UNKNOWN,
         "javascript_scheme_not_denied");
  expect(netlib_url_classify_scheme("data:text/plain,hi") ==
             NETLIB_URL_SCHEME_UNKNOWN,
         "data_scheme_not_denied");
  expect(netlib_url_classify_scheme("gopher://example.com") ==
             NETLIB_URL_SCHEME_UNKNOWN,
         "gopher_scheme_not_denied");
  /* Protocol-relative URLs must not silently default to HTTP. */
  expect(netlib_url_classify_scheme("//example.com/path") ==
             NETLIB_URL_SCHEME_UNKNOWN,
         "protocol_relative_not_denied");
  /* Plain host with no scheme must be denied. */
  expect(netlib_url_classify_scheme("example.com/path") ==
             NETLIB_URL_SCHEME_UNKNOWN,
         "schemeless_not_denied");
  /* Relative paths must be denied. */
  expect(netlib_url_classify_scheme("/relative/path") ==
             NETLIB_URL_SCHEME_UNKNOWN,
         "relative_path_not_denied");
}

static void test_empty_and_null_denied(void) {
  expect(netlib_url_classify_scheme(NULL) == NETLIB_URL_SCHEME_UNKNOWN,
         "null_not_denied");
  expect(netlib_url_classify_scheme("") == NETLIB_URL_SCHEME_UNKNOWN,
         "empty_not_denied");
}

static void test_case_sensitive_only_lowercase_allowed(void) {
  /* The classifier is intentionally strict (lowercase only) so that the
   * dispatch path matches exactly what the URL parsers downstream expect.
   * Mixed-case schemes must be denied rather than silently normalized. */
  expect(netlib_url_classify_scheme("HTTP://example.com/") ==
             NETLIB_URL_SCHEME_UNKNOWN,
         "uppercase_http_not_denied");
  expect(netlib_url_classify_scheme("HTTPS://example.com/") ==
             NETLIB_URL_SCHEME_UNKNOWN,
         "uppercase_https_not_denied");
  expect(netlib_url_classify_scheme("Http://example.com/") ==
             NETLIB_URL_SCHEME_UNKNOWN,
         "mixed_case_http_not_denied");
}

static void test_http_prefix_does_not_match_https(void) {
  /* Sanity check: classifier must distinguish http:// from https:// even
   * though one is a prefix of the other up to the colon. */
  expect(netlib_url_classify_scheme("https://example.com/") !=
             NETLIB_URL_SCHEME_HTTP,
         "https_misclassified_as_http");
  expect(netlib_url_classify_scheme("http://example.com/") !=
             NETLIB_URL_SCHEME_HTTPS,
         "http_misclassified_as_https");
}

int main(void) {
  printf("TEST:START:netlib_url_scheme\n");

  test_http_scheme_allowed();
  test_https_scheme_allowed();
  test_unknown_schemes_denied();
  test_empty_and_null_denied();
  test_case_sensitive_only_lowercase_allowed();
  test_http_prefix_does_not_match_https();

  if (g_failures != 0) {
    printf("TEST:FAIL:netlib_url_scheme:%d_failures\n", g_failures);
    return 1;
  }

  printf("TEST:PASS:netlib_url_scheme_allow_deny\n");
  return 0;
}
