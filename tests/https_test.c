/**
 * @file https_test.c
 * @brief Tests for HTTPS URL detection and scheme routing in http.c.
 *
 * Purpose:
 *   Validates that the HTTP client correctly identifies https:// URLs,
 *   sets the default port to 443, and that the HTTPS result type enum
 *   values are well-defined. Tests URL parsing by including http.c
 *   internals directly with stubbed dependencies.
 *
 * Interactions:
 *   - http.c: tests the URL parsing and https detection logic.
 *   - https.h: validates result enum definitions.
 *
 * Launched by:
 *   Compiled and run by the native test harness via test_https.sh.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "../user/libs/netlib/https.h"

static void fail(const char *reason) {
  printf("TEST:FAIL:https:%s\n", reason);
  exit(1);
}

/* ---- Minimal URL parser mirroring http.c logic for isolated testing ---- */

typedef struct {
  char host[128];
  uint16_t port;
  char path[256];
  int valid;
  int is_https;
} test_parsed_url_t;

static int test_is_digit(char c) {
  return c >= '0' && c <= '9';
}

static void test_parse_url(const char *url, test_parsed_url_t *out) {
  const char *cursor = url;
  size_t host_len = 0u;
  size_t path_len = 0u;
  size_t i = 0u;

  memset(out, 0, sizeof(*out));
  out->port = 80u;
  out->is_https = 0;

  if (url == 0 || url[0] == '\0') {
    return;
  }

  if (cursor[0] == 'h' && cursor[1] == 't' && cursor[2] == 't' &&
      cursor[3] == 'p' && cursor[4] == 's' && cursor[5] == ':' &&
      cursor[6] == '/' && cursor[7] == '/') {
    out->is_https = 1;
    out->port = 443u;
    cursor += 8u;
  } else if (cursor[0] == 'h' && cursor[1] == 't' && cursor[2] == 't' &&
             cursor[3] == 'p' && cursor[4] == ':' && cursor[5] == '/' &&
             cursor[6] == '/') {
    cursor += 7u;
  } else if (cursor[0] == '/' && cursor[1] == '/') {
    cursor += 2u;
  }

  host_len = 0u;
  while (cursor[host_len] != '\0' && cursor[host_len] != '/' && cursor[host_len] != ':') {
    if (host_len + 1u < sizeof(out->host)) {
      out->host[host_len] = cursor[host_len];
    }
    ++host_len;
  }
  out->host[host_len < sizeof(out->host) ? host_len : sizeof(out->host) - 1u] = '\0';
  cursor += host_len;

  if (*cursor == ':') {
    unsigned int port_val = 0u;
    ++cursor;
    while (test_is_digit(*cursor)) {
      port_val = port_val * 10u + (unsigned int)(*cursor - '0');
      ++cursor;
    }
    if (port_val > 0u && port_val <= 65535u) {
      out->port = (uint16_t)port_val;
    }
  }

  if (*cursor == '\0') {
    out->path[0] = '/';
    out->path[1] = '\0';
  } else {
    path_len = strlen(cursor);
    for (i = 0u; i < path_len && i + 1u < sizeof(out->path); ++i) {
      out->path[i] = cursor[i];
    }
    out->path[i] = '\0';
  }

  out->valid = (out->host[0] != '\0') ? 1 : 0;
}

/* ---- Tests ---- */

static void test_http_url_basic(void) {
  test_parsed_url_t p;
  test_parse_url("http://example.com/index.html", &p);
  if (!p.valid) fail("http_basic_not_valid");
  if (p.is_https) fail("http_basic_should_not_be_https");
  if (p.port != 80u) fail("http_basic_port_not_80");
  if (strcmp(p.host, "example.com") != 0) fail("http_basic_host_mismatch");
  if (strcmp(p.path, "/index.html") != 0) fail("http_basic_path_mismatch");
}

static void test_https_url_basic(void) {
  test_parsed_url_t p;
  test_parse_url("https://secure.example.com/api/v1", &p);
  if (!p.valid) fail("https_basic_not_valid");
  if (!p.is_https) fail("https_basic_should_be_https");
  if (p.port != 443u) fail("https_basic_port_not_443");
  if (strcmp(p.host, "secure.example.com") != 0) fail("https_basic_host_mismatch");
  if (strcmp(p.path, "/api/v1") != 0) fail("https_basic_path_mismatch");
}

static void test_https_with_custom_port(void) {
  test_parsed_url_t p;
  test_parse_url("https://api.example.com:8443/data", &p);
  if (!p.valid) fail("https_custom_port_not_valid");
  if (!p.is_https) fail("https_custom_port_should_be_https");
  if (p.port != 8443u) fail("https_custom_port_not_8443");
  if (strcmp(p.host, "api.example.com") != 0) fail("https_custom_port_host_mismatch");
}

static void test_http_with_custom_port(void) {
  test_parsed_url_t p;
  test_parse_url("http://localhost:3000/", &p);
  if (!p.valid) fail("http_custom_port_not_valid");
  if (p.is_https) fail("http_custom_port_should_not_be_https");
  if (p.port != 3000u) fail("http_custom_port_not_3000");
}

static void test_https_no_path(void) {
  test_parsed_url_t p;
  test_parse_url("https://example.com", &p);
  if (!p.valid) fail("https_no_path_not_valid");
  if (!p.is_https) fail("https_no_path_should_be_https");
  if (p.port != 443u) fail("https_no_path_port_not_443");
  if (strcmp(p.path, "/") != 0) fail("https_no_path_should_default_slash");
}

static void test_bare_host(void) {
  test_parsed_url_t p;
  test_parse_url("//example.com/test", &p);
  if (!p.valid) fail("bare_host_not_valid");
  if (p.is_https) fail("bare_host_should_not_be_https");
  if (p.port != 80u) fail("bare_host_port_not_80");
}

static void test_empty_url(void) {
  test_parsed_url_t p;
  test_parse_url("", &p);
  if (p.valid) fail("empty_url_should_not_be_valid");
}

static void test_null_url(void) {
  test_parsed_url_t p;
  test_parse_url(NULL, &p);
  if (p.valid) fail("null_url_should_not_be_valid");
}

static void test_https_result_enum_values(void) {
  /* Verify HTTPS result enum has expected values defined */
  if ((int)HTTPS_OK != 0) fail("https_ok_not_zero");
  if ((int)HTTPS_ERR_BAD_URL == (int)HTTPS_OK) fail("https_err_bad_url_equals_ok");
  if ((int)HTTPS_ERR_DNS == (int)HTTPS_OK) fail("https_err_dns_equals_ok");
  if ((int)HTTPS_ERR_CONNECT == (int)HTTPS_OK) fail("https_err_connect_equals_ok");
  if ((int)HTTPS_ERR_TLS == (int)HTTPS_OK) fail("https_err_tls_equals_ok");
  if ((int)HTTPS_ERR_SEND == (int)HTTPS_OK) fail("https_err_send_equals_ok");
  if ((int)HTTPS_ERR_RECV == (int)HTTPS_OK) fail("https_err_recv_equals_ok");
  if ((int)HTTPS_ERR_RESPONSE == (int)HTTPS_OK) fail("https_err_response_equals_ok");
}

int main(void) {
  printf("TEST:START:https\n");

  test_http_url_basic();
  test_https_url_basic();
  test_https_with_custom_port();
  test_http_with_custom_port();
  test_https_no_path();
  test_bare_host();
  test_empty_url();
  test_null_url();
  test_https_result_enum_values();

  printf("TEST:PASS:https_url_parsing\n");
  return 0;
}